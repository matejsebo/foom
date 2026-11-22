/*
 *    Copyright 2026 Matej Šebo
 *
 *    This file is part of FOOM.
 *
 *    FOOM is free software: you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation, either version 3 of the License, or
 *    (at your option) any later version.
 *
 *    FOOM is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with FOOM.  If not, see <http://www.gnu.org/licenses/>.
 */


#include "route_print.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iterator>
#include <vector>

#include <Qt>
#include <QtMath>
#include <QColor>
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QLatin1Char>
#include <QLatin1String>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QRectF>
#include <QTransform>
#ifdef QT_PRINTSUPPORT_LIB
#  include <QPrinter>
#endif

#include "mapper_config.h"
#include "core/extent_printing.h"
#include "core/extents.h"
#include "core/map.h"
#include "core/map_coord.h"
#include "core/map_printer.h"
#include "core/renderables/renderable.h"
#include "course/course_map.h"
#include "course/ocd_course_records.h"
#include "course/route_course.h"
#include "course/route_store.h"

namespace OpenOrienteering {

namespace {

QString tr(const char* text)
{
	return QCoreApplication::translate("OpenOrienteering::RoutePrint", text);
}

/** The colours of the lines of paintCourseRoutes(). */
const QColor route_colour(225, 0, 50);
const QColor via_colour(0, 90, 225);

/** The size of the captions in mm. */
constexpr double caption_size = 2.3;
/** The size of the title of a page in mm. */
constexpr double title_size = 4.0;
/** The width of the white framing in mm. */
constexpr double framing = 0.4;

/// A text as a path, its base line from (0, 0), em high as the size in mm.
QPainterPath textPath(const QString& text, double size)
{
	QFont font(QStringLiteral("Arial"));
	font.setBold(true);
	font.setPixelSize(100);
	QPainterPath path;
	path.addText(0.0, 0.0, font, text);
	return QTransform::fromScale(size / 100.0, size / 100.0).map(path);
}

/// Paints a text path with a white framing.
void paintFramed(QPainter* painter, const QPainterPath& path, const QColor& colour)
{
	QPen pen(Qt::white, framing);
	pen.setJoinStyle(Qt::RoundJoin);
	painter->strokePath(path, pen);
	painter->fillPath(path, colour);
}

void paintCaption(QPainter* painter, const RouteResult& route, bool with_heights, const QColor& colour)
{
	if (!route.found || route.points.size() < 2 || qRound(route.length_m) < OcdCourse::route_caption_min_m)
		return;
	StoredRoute stored;
	stored.length_m = route.length_m;
	stored.time_s = route.time_s;
	stored.climb_m = route.climb_m;
	stored.points = route.points;
	auto const place = OcdCourse::routeCaptionPlace(route.points);
	if (!place.valid)
		return;
	auto const text = textPath(OcdCourse::routeCaption(stored, with_heights), caption_size);
	// Centred on its base line at the anchor; the angle is counter-clockwise, Qt's rotation clockwise on the map.
	QTransform transform;
	transform.translate(place.anchor.x(), place.anchor.y());
	transform.rotateRadians(-place.rotation);
	transform.translate(-text.boundingRect().center().x(), 0.0);
	paintFramed(painter, transform.map(text), colour);
}

QString titleOf(const CourseRoutes& routes)
{
	auto name = routes.variant.isEmpty() ? routes.course : QString(routes.course + QLatin1Char('-') + routes.variant);
	if (routes.part >= 0)
		name += QLatin1String(" (") + QString::number(routes.part + 1) + QLatin1Char(')');
	return tr("%1: %2 m in %3").arg(name).arg(qRound(routes.length_m)).arg(formatRouteTime(routes.time_s));
}

bool hasRoutes(const CourseRoutes& routes)
{
	return std::any_of(routes.legs.begin(), routes.legs.end(), [](const CourseRouteLeg& leg) { return leg.known && leg.route.found; });
}

}  // namespace



void paintRouteCaptions(QPainter* painter, const CourseRoutes& routes, bool with_heights)
{
	painter->save();
	painter->setRenderHint(QPainter::Antialiasing, true);
	for (auto const& leg : routes.legs)
	{
		if (!leg.marked)
			paintCaption(painter, leg.route, with_heights, route_colour);
		for (auto const& via_route : leg.via_routes)
			paintCaption(painter, via_route, with_heights, via_colour);
	}
	painter->restore();
}



RouteMapReport exportRoutesMap(Map& map, const RouteMapOptions& options)
{
	RouteMapReport report;
#ifndef QT_PRINTSUPPORT_LIB
	Q_UNUSED(map)
	Q_UNUSED(options)
	report.error = tr("This build cannot write PDF files.");
	return report;
#else
	auto const& data = map.courseData();
	if (data.routes.isEmpty())
	{
		report.error = tr("The map keeps no routes. Analyse the routes of a course or of all courses first.");
		return report;
	}
	const Extent* extent = nullptr;
	if (!options.extent.isEmpty())
	{
		auto const& extents = map.extents();
		auto const named = std::find_if(begin(extents), end(extents), [&options](const Extent& entry) { return entry.name == options.extent; });
		if (named == end(extents))
		{
			report.error = tr("The map has no extent \"%1\".").arg(options.extent);
			return report;
		}
		extent = &*named;
	}

	std::vector<CourseRoutes> pages;
	auto all = storedAllRoutes(map, data);
	for (auto& table : all.tables)
	{
		if (!options.courses.isEmpty() && !options.courses.contains(table.course))
			continue;
		if (hasRoutes(table))
			pages.push_back(std::move(table));
	}
	if (pages.empty())
	{
		report.error = tr("No course of these has routes which are kept.");
		return report;
	}
	auto const with_heights = data.routes.with_heights;

	MapPrinter printer(map, options.view);
	printer.setTarget(MapPrinter::pdfTarget());
	printer.setMode(MapPrinterOptions::Vector);
	printer.setPrintTemplates(true);
	if (extent)
	{
		applyExtentToPrinter(printer, map, *extent);
		printer.setBackSides({});
	}
	else
	{
		if (!map.hasPrinterConfig())
			printer.setPrintArea(map.calculateExtent(false, true, options.view));
		// One page for a course: without an extent which brings its paper, the page is as large as the area.
		printer.setCustomPageSize(printer.getPrintAreaPaperSize());
	}
	auto const captions = options.captions;
	printer.setOverlay([&pages, captions, with_heights](QPainter* painter, const RenderConfig& config, int side) {
		if (side < 0 || std::size_t(side) >= pages.size())
			return;
		auto const& routes = pages[std::size_t(side)];
		paintCourseRoutes(painter, routes);
		if (captions)
			paintRouteCaptions(painter, routes, with_heights);
		painter->save();
		painter->setRenderHint(QPainter::Antialiasing, true);
		auto const title = textPath(titleOf(routes), title_size);
		auto const corner = config.bounding_box.topLeft() + QPointF(3.0, 3.0 + title_size);
		paintFramed(painter, QTransform::fromTranslate(corner.x(), corner.y()).map(title), Qt::black);
		painter->restore();
	}, int(pages.size()));

	auto qprinter = printer.makePrinter();
	auto written = false;
	if (qprinter)
	{
		qprinter->setOutputFormat(QPrinter::PdfFormat);
		qprinter->setOutputFileName(options.path);
		qprinter->setCreator(QString::fromLatin1("FOOM " APP_VERSION));
		qprinter->setDocName(QFileInfo(options.path).completeBaseName());
		// The objects of the courses are not printed.
		const CourseSymbolsHider hider(map);
		written = printer.printMap(qprinter.get());
	}
	if (!written)
	{
		QFile::remove(options.path);
		report.error = tr("Cannot write the file \"%1\".").arg(options.path);
		return report;
	}
	report.pages = int(pages.size());
	report.ok = true;
	return report;
#endif
}


}  // namespace OpenOrienteering
