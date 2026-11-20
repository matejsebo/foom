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


#include "course_map.h"

#include <algorithm>
#include <cstddef>
#include <functional>
#include <iterator>
#include <map>

#include <QtMath>
#include <QChar>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLatin1Char>
#include <QLatin1String>
#include <QCoreApplication>
#include <QPainter>
#include <QUuid>
#ifdef QT_PRINTSUPPORT_LIB
#  include <QPrinter>
#endif

#include "core/extent_printing.h"
#include "core/extents.h"
#include "core/map.h"
#include "core/map_printer.h"
#include "mapper_config.h"
#include "core/objects/object.h"
#include "core/objects/text_object.h"
#include "core/renderables/renderable.h"
#include "core/symbols/symbol.h"
#include "course/course_description.h"
#include "course/course_parts.h"
#include "course/course_render.h"

namespace OpenOrienteering {

namespace {

QString tr(const char* text)
{
	return QCoreApplication::translate("OpenOrienteering::CourseMap", text);
}

bool isHiddenRole(Symbol::CourseRole role)
{
	switch (role)
	{
	case Symbol::CourseStart:
	case Symbol::CourseControl:
	case Symbol::CourseFinish:
	case Symbol::CourseMarkedRoute:
	case Symbol::CourseDescription:
	case Symbol::CourseTitle:
	case Symbol::CourseStartNumber:
	case Symbol::CourseVariantCode:
		return true;
	default:
		return false;
	}
}

/// The letters of a variant, without its leg and without blanks.
QString variantLetters(const RelayVariant& variant)
{
	return relayVariantCode(variant).mid(QString::number(variant.leg).size()).remove(QLatin1Char(' '));
}

/// The sides of a course without variations: its parts, on sheets.
std::vector<CourseMapSheet> sheetsOfParts(const Course& course)
{
	std::vector<CourseMapSheet> sheets;
	auto const parts = courseParts(course);
	sheets.emplace_back();
	for (std::size_t p = 0; p < parts.size(); ++p)
	{
		CourseMapSide side;
		side.course = course.name;
		side.part = parts.size() > 1 ? int(p) : -1;
		sheets.back().sides.push_back(side);
		// A map exchange begins a new sheet, a map flip a new side.
		if (parts[p].ends_with == CoursePart::MapExchange)
			sheets.emplace_back();
	}
	for (std::size_t s = 0; s < sheets.size(); ++s)
		sheets[s].name = sheets.size() > 1 ? QString(course.name + QLatin1Char('_') + QString::number(s + 1)) : course.name;
	return sheets;
}

/// Copies of the symbols of a map which are shown whatever the map hides.
class ShownSymbols
{
public:
	explicit ShownSymbols(std::vector<std::unique_ptr<Symbol>>& store) : store(store) {}
	
	const Symbol* get(const Symbol* symbol)
	{
		auto& copy = copies[symbol];
		if (!copy)
		{
			store.push_back(duplicate(*symbol));
			copy = store.back().get();
			copy->setHidden(false);
		}
		return copy;
	}
	
private:
	std::vector<std::unique_ptr<Symbol>>& store;
	std::map<const Symbol*, Symbol*> copies;
};

}  // namespace



std::vector<CourseMapSheet> courseMapSheets(const CourseData& data, const QString& course_name, RelayMaps relay_maps)
{
	std::vector<CourseMapSheet> sheets;
	auto const* course = data.findCourse(course_name);
	if (!course)
		return sheets;
	auto const structure = relayStructure(*course);
	if (!structure.isValid())
		return sheets;
	if (course->type == Course::Normal)
		return sheetsOfParts(*course);
	
	auto const add = [&sheets, course](const QString& name, const RelayVariant& variant, const QString& start_number) {
		CourseMapSheet sheet;
		sheet.name = course->name + QLatin1Char('.') + name;
		CourseMapSide side;
		side.course = course->name;
		side.variant = variant;
		side.start_number = start_number;
		sheet.sides.push_back(side);
		sheets.push_back(sheet);
	};
	if (relay_maps == RelayMaps::Runners)
	{
		auto const teams = relayTeams(data, course->name);
		auto const allocation = allocateRelay(structure, teams.count);
		if (!allocation.ok)
			return sheets;
		for (std::size_t t = 0; t < allocation.teams.size(); ++t)
		{
			for (auto const& variant : allocation.teams[t])
			{
				auto const number = QString(QString::number(teams.first_number + int(t)) + QLatin1Char('.') + QString::number(variant.leg));
				add(number, variant, number);
			}
		}
		return sheets;
	}
	
	QStringList loops;
	for (auto const& variant : relayVariants(structure))
	{
		if (course->type == Course::OneManRelay)
		{
			// Every loop once, whichever leg runs it.
			auto const letters = variantLetters(variant);
			if (loops.contains(letters))
				continue;
			loops.push_back(letters);
			add(letters, variant, {});
		}
		else
		{
			add(relayVariantCode(variant).remove(QLatin1Char(' ')), variant, {});
		}
	}
	return sheets;
}


QString courseTitleText(const CourseData& data, const CourseMapSide& side)
{
	auto const* course = data.findCourse(side.course);
	if (!course)
		return {};
	auto name = course->name;
	if (side.part >= 0 && courseParts(*course).size() > 1)
		name += QLatin1String(" (") + QString::number(side.part + 1) + QLatin1Char(')');
	QStringList classes;
	for (auto const& entry : data.classes)
	{
		if (entry.course == course->name)
			classes.push_back(entry.name);
	}
	QStringList lines;
	auto const mode = data.options.titleMode();
	if (mode != 0 || classes.isEmpty())
		lines.push_back(name);
	if (mode != 2)
		lines += classes;
	return lines.join(QLatin1Char('\n'));
}


QString courseVariantText(const CourseData& data, const CourseMapSide& side)
{
	auto const* course = data.findCourse(side.course);
	if (!course || course->type == Course::Normal || side.variant.leg < 1)
		return {};
	return course->type == Course::OneManRelay ? variantLetters(side.variant)
	                                           : relayVariantCode(side.variant).remove(QLatin1Char(' '));
}



CourseMapDrawing::CourseMapDrawing() = default;
CourseMapDrawing::CourseMapDrawing(CourseMapDrawing&&) noexcept = default;
CourseMapDrawing& CourseMapDrawing::operator=(CourseMapDrawing&&) noexcept = default;
CourseMapDrawing::~CourseMapDrawing() = default;


CourseMapDrawing drawCourseMap(const Map& map, const CourseData& original, const CourseMapSide& side)
{
	CourseMapDrawing result;
	if (!original.findCourse(side.course))
		return result;
	// A relay is drawn as the course of its variant.
	auto const data = side.variant.leg > 0 ? withRelayVariant(original, side.course, side.variant) : original;
	auto const* course = data.findCourse(side.course);
	if (!course)
		return result;
	auto course_drawing = drawCourse(map, data, side.course, side.part);
	if (!course_drawing.drawn)
		return result;
	result.drawn = true;
	ShownSymbols shown(result.symbols);
	
	// The objects of the items.
	for (auto const& stop : courseStops(map, data, *course))
	{
		if (side.part >= 0 && coursePartOfItem(*course, stop.item) != side.part)
			continue;
		auto const& item = course->items[std::size_t(stop.item)];
		if (result.items.contains(item.code))
			continue;
		auto const* course_object = data.findObject(item.code);
		auto const* object = course_object ? map.findObject(course_object->object) : nullptr;
		if (!object || !object->getSymbol())
			continue;
		std::unique_ptr<Object> copy(object->duplicate());
		copy->setSymbol(shown.get(object->getSymbol()), true);
		if (item.type == CourseItem::Start && copy->getType() == Object::Point && copy->getSymbol()->isRotatable())
		{
			auto const turned = std::find_if(begin(course_drawing.starts), end(course_drawing.starts), [&item](const CourseStartDrawing& start) {
				return start.code == item.code;
			});
			if (turned != end(course_drawing.starts))
				copy->setRotation(qDegreesToRadians(turned->angle));
		}
		result.objects.push_back(std::move(copy));
		result.items.push_back(item.code);
	}
	
	// The lines and the numbers. Their symbols are those of the map: it has their objects as overrides only.
	for (auto const& leg : course_drawing.legs)
		result.legs += leg.has_line || leg.overridden ? 1 : 0;
	result.numbers = int(course_drawing.numbers.size());
	for (auto& object : course_drawing.objects)
	{
		object->setSymbol(shown.get(object->getSymbol()), true);
		result.objects.push_back(std::move(object));
	}
	
	// The description, which is painted as the editor paints it.
	result.description = drawCourseDescriptions(map, data, side.course, side.part);
	result.has_description = result.description.drawn && !result.description.layouts.empty();
	for (auto& symbol : result.description.symbols)
		symbol->setHidden(false);
	
	// The title, the variant code and the start number.
	auto const title = courseTitleText(original, side);
	auto const variant = courseVariantText(original, side);
	for (auto kind : { CourseObject::CourseTitle, CourseObject::VariantCode, CourseObject::StartNumber })
	{
		auto const text = kind == CourseObject::CourseTitle ? title : kind == CourseObject::VariantCode ? variant : side.start_number;
		if (text.isEmpty())
			continue;
		auto made = false;
		for (auto const& course_object : original.objects)
		{
			if (course_object.kind != kind || course_object.object.isNull())
				continue;
			auto const* object = map.findObject(course_object.object);
			if (!object || object->getType() != Object::Text || !object->getSymbol())
				continue;
			std::unique_ptr<Object> copy(object->duplicate());
			copy->setSymbol(shown.get(object->getSymbol()), true);
			copy->asText()->setText(text);
			result.objects.push_back(std::move(copy));
			made = true;
		}
		if (made)
			result.texts.push_back(text);
	}
	return result;
}


void paintCourseMap(QPainter* painter, Map* map, const CourseMapDrawing& drawing, const QRectF& bounding_box, qreal scaling)
{
	if (!drawing.drawn)
		return;
	// The description first: its white ground lies below the course.
	paintCourseDescriptions(painter, map, drawing.description, bounding_box, scaling);
	if (drawing.objects.empty())
		return;
	MapRenderables renderables(map);
	for (auto const& object : drawing.objects)
	{
		object->update();
		renderables.insertRenderablesOfObject(object.get());
	}
	RenderConfig config = { *map, bounding_box, scaling, RenderConfig::NoOptions, 1.0 };
	renderables.draw(painter, config);
	for (auto const& object : drawing.objects)
		renderables.removeRenderablesOfObject(object.get(), false);
}



CourseSymbolsHider::CourseSymbolsHider(Map& map)
{
	for (int i = 0; i < map.getNumSymbols(); ++i)
	{
		auto* symbol = map.getSymbol(i);
		if (!symbol->isHidden() && isHiddenRole(symbol->courseRole()))
		{
			symbol->setHidden(true);
			hidden.push_back(symbol);
		}
	}
}

CourseSymbolsHider::~CourseSymbolsHider()
{
	for (auto* symbol : hidden)
		symbol->setHidden(false);
}



QString courseMapFileName(const QString& base_name, const QString& sheet)
{
	auto name = QString(base_name + QLatin1Char('.') + sheet + QLatin1String(".pdf"));
	for (auto& character : name)
	{
		if (character < QLatin1Char(' ') || QStringLiteral("\\/:*?\"<>|").contains(character))
			character = QLatin1Char('_');
	}
	return name;
}


CourseMapExportReport exportCourseMaps(Map& map, const CourseMapExportOptions& options)
{
	CourseMapExportReport report;
#ifndef QT_PRINTSUPPORT_LIB
	Q_UNUSED(map)
	Q_UNUSED(options)
	report.error = tr("This build cannot write PDF files.");
	return report;
#else
	auto const& data = map.courseData();
	QDir folder(options.folder);
	if (options.folder.isEmpty() || !folder.mkpath(QStringLiteral(".")))
	{
		report.error = tr("Cannot write into the folder \"%1\".").arg(options.folder);
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
	
	auto courses = options.courses;
	if (courses.isEmpty())
	{
		for (auto const& course : data.courses)
			courses.push_back(course.name);
	}
	
	for (auto const& course : qAsConst(courses))
	{
		auto const sheets = courseMapSheets(data, course, options.relay_maps);
		if (sheets.empty())
		{
			report.skipped.push_back(course);
			continue;
		}
		for (auto const& sheet : sheets)
		{
			// Drawn before the symbols are hidden: the copies are of symbols as the map shows them.
			std::vector<CourseMapDrawing> sides;
			for (auto const& side : sheet.sides)
				sides.push_back(drawCourseMap(map, data, side));
			
			MapPrinter printer(map, options.view);
			printer.setTarget(MapPrinter::pdfTarget());
			printer.setMode(MapPrinterOptions::Vector);
			printer.setPrintTemplates(true);
			if (extent)
			{
				applyExtentToPrinter(printer, map, *extent);
				// The sides of the sheet are those of the course, not the backs of the extent.
				printer.setBackSides({});
			}
			else if (!map.hasPrinterConfig())
			{
				printer.setPrintArea(map.calculateExtent(false, true, options.view));
			}
			if (options.scale > 0)
				printer.setScale(options.scale);
			// One page for a side: without an extent which brings its paper, the page is as large as the area.
			if (!extent || options.scale > 0)
				printer.setCustomPageSize(printer.getPrintAreaPaperSize());
			printer.setOverlay([&map, &sides](QPainter* painter, const RenderConfig& config, int side) {
				if (side >= 0 && std::size_t(side) < sides.size())
					paintCourseMap(painter, &map, sides[std::size_t(side)], config.bounding_box, config.scaling);
			}, int(sides.size()));
			
			auto const path = folder.absoluteFilePath(courseMapFileName(options.base_name, sheet.name));
			auto qprinter = printer.makePrinter();
			auto written = false;
			if (qprinter)
			{
				qprinter->setOutputFormat(QPrinter::PdfFormat);
				qprinter->setOutputFileName(path);
				qprinter->setCreator(QString::fromLatin1("FOOM " APP_VERSION));
				qprinter->setDocName(QFileInfo(path).completeBaseName());
				const CourseSymbolsHider hider(map);
				written = printer.printMap(qprinter.get());
			}
			if (!written)
			{
				QFile::remove(path);
				report.error = tr("Cannot write the file \"%1\".").arg(path);
				return report;
			}
			report.files.push_back(path);
		}
	}
	report.ok = true;
	return report;
#endif
}


}  // namespace OpenOrienteering
