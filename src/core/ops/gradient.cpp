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


#include "gradient.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>

#include <Qt>
#include <QtGlobal>
#include <QtMath>
#include <QBuffer>
#include <QByteArray>
#include <QCoreApplication>
#include <QIODevice>
#include <QImage>
#include <QLineF>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPointF>
#include <QPolygonF>
#include <QRectF>
#include <QSizeF>
#include <QString>

#include "core/map_coord.h"
#include "core/objects/image_object.h"
#include "core/objects/object.h"
#include "core/path_coord.h"
#include "core/symbols/symbol.h"


namespace OpenOrienteering {

namespace ops {

namespace {

QString tr(const char* text)
{
	return QCoreApplication::translate("OpenOrienteering::GradientFill", text);
}

bool hasArea(const Object* object)
{
	if (object->getType() != Object::Path || !object->getSymbol())
		return false;
	if (!(object->getSymbol()->getContainedTypes() & Symbol::Area))
		return false;
	for (auto const& part : object->asPath()->parts())
	{
		if (part.isClosed())
			return true;
	}
	return false;
}

bool isOpenLine(const Object* object)
{
	if (object->getType() != Object::Path)
		return false;
	auto const& parts = object->asPath()->parts();
	return !parts.empty() && !parts.front().isClosed() && object->getRawCoordinateVector().size() >= 2;
}

/** The outline of the closed parts in map coordinates, odd-even. */
QPainterPath outlineOf(const PathObject& path)
{
	QPainterPath outline;
	outline.setFillRule(Qt::OddEvenFill);
	for (auto const& part : path.parts())
	{
		if (!part.isClosed())
			continue;
		QPolygonF ring;
		ring.reserve(int(part.path_coords.size()));
		for (auto const& path_coord : part.path_coords)
			ring.append(QPointF(path_coord.pos));
		outline.addPolygon(ring);
		outline.closeSubpath();
	}
	return outline;
}

}  // namespace


OpResult gradientFill(const Map& /*map*/, const std::vector<const Object*>& in, const GradientParams& params)
{
	OpResult result;

	// The direction, and for a line its ends.
	auto has_ends = false;
	QPointF line_start;
	QPointF line_end;
	if (params.use_line)
	{
		auto const found = std::find_if(begin(in), end(in), isOpenLine);
		if (found == end(in))
		{
			result.error = tr("Select a line for the direction of the gradient.");
			return result;
		}
		auto const& coords = (*found)->getRawCoordinateVector();
		line_start = QPointF(MapCoordF(coords.front()));
		line_end = QPointF(MapCoordF(coords.back()));
		if (QLineF(line_start, line_end).length() <= 0)
		{
			result.error = tr("The line for the direction of the gradient has no length.");
			return result;
		}
		has_ends = true;
	}
	auto const radians = qDegreesToRadians(params.angle);
	auto const direction = QPointF(std::cos(radians), -std::sin(radians));  // the map's y axis points down

	for (auto const* object : in)
	{
		if (!hasArea(object))
			continue;
		auto const outline = outlineOf(*object->asPath());
		auto const box = outline.boundingRect();
		if (box.width() <= 0 || box.height() <= 0)
			continue;

		// The pixels.
		auto pixels_per_mm = params.dpi / 25.4;
		auto const longer = std::max(box.width(), box.height());
		if (longer * pixels_per_mm > params.max_pixels)
			pixels_per_mm = params.max_pixels / longer;
		auto const width = std::max(1, qRound(box.width() * pixels_per_mm));
		auto const height = std::max(1, qRound(box.height() * pixels_per_mm));
		QImage image(width, height, QImage::Format_ARGB32_Premultiplied);
		if (image.isNull())
		{
			result.error = tr("Not enough memory for the gradient image.");
			result.out.clear();
			result.consumed.clear();
			return result;
		}
		image.fill(Qt::transparent);

		// The ends of the gradient in map coordinates.
		QPointF start = line_start;
		QPointF stop = line_end;
		if (!has_ends)
		{
			auto lowest = std::numeric_limits<double>::max();
			auto highest = std::numeric_limits<double>::lowest();
			for (auto const& corner : { box.topLeft(), box.topRight(), box.bottomRight(), box.bottomLeft() })
			{
				auto const t = QPointF::dotProduct(corner, direction);
				lowest = std::min(lowest, t);
				highest = std::max(highest, t);
			}
			auto const centre = box.center();
			auto const middle = QPointF::dotProduct(centre, direction);
			start = centre + (lowest - middle) * direction;
			stop = centre + (highest - middle) * direction;
		}

		{
			QPainter painter(&image);
			painter.setRenderHint(QPainter::Antialiasing);
			painter.scale(width / box.width(), height / box.height());
			painter.translate(-box.topLeft());
			QLinearGradient gradient(start, stop);
			gradient.setSpread(QGradient::PadSpread);
			gradient.setColorAt(0, params.from_color);
			gradient.setColorAt(1, params.to_color);
			painter.setPen(Qt::NoPen);
			painter.setBrush(gradient);
			painter.drawPath(outline);
		}

		QByteArray bytes;
		{
			QBuffer buffer(&bytes);
			buffer.open(QIODevice::WriteOnly);
			image.convertToFormat(QImage::Format_ARGB32).save(&buffer, "PNG");
		}

		auto output = std::make_unique<ImageObject>();
		output->setData(bytes, QStringLiteral("gradient.png"));
		output->setCorners({ MapCoordF(box.topLeft()), MapCoordF(box.topRight()), MapCoordF(box.bottomRight()), MapCoordF(box.bottomLeft()) });
		result.out.push_back(std::move(output));
		if (!params.keep_areas)
			result.consumed.push_back(object);
	}

	if (result.out.empty())
		result.error = tr("Select an area for the gradient.");
	return result;
}


}  // namespace ops

}  // namespace OpenOrienteering
