/*
 *    Copyright 2026 Matej Šebo
 *
 *    This file is part of FOOM, the Fantastic Open Orienteering Mapper,
 *    a fork of OpenOrienteering Mapper.
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


#include "generator.h"
#include "generator_p.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <set>
#include <utility>

#include <Qt>
#include <QtGlobal>
#include <QtMath>
#include <QChar>
#include <QCollator>
#include <QJsonValue>
#include <QLatin1Char>
#include <QLatin1String>
#include <QRect>
#include <QStringList>

#include "core/extents.h"
#include "core/georeferencing.h"
#include "core/latlon.h"
#include "core/map.h"
#include "core/map_part.h"
#include "core/map_printer.h"
#include "core/symbols/area_symbol.h"
#include "core/symbols/line_symbol.h"
#include "core/symbols/point_symbol.h"
#include "core/symbols/text_symbol.h"
#include "layout/layout_part.h"
#include "layout/qr_code.h"


namespace OpenOrienteering {

namespace generator_impl {

namespace {

double number(const QJsonObject& params, const char* key, double fallback = 0)
{
	return params.value(QLatin1String(key)).toDouble(fallback);
}

bool flag(const QJsonObject& params, const char* key, bool fallback)
{
	return params.value(QLatin1String(key)).toBool(fallback);
}

QString string(const QJsonObject& params, const char* key)
{
	return params.value(QLatin1String(key)).toString();
}

double cross(const QPointF& a, const QPointF& b)
{
	return a.x() * b.y() - a.y() * b.x();
}

QPointF unit(const QPointF& vector)
{
	auto const length = std::hypot(vector.x(), vector.y());
	return length > 0 ? vector / length : QPointF(0, -1);
}

/** The outline to clip to: the stored polygon, or the extent of the map objects. */
bool resolveOutline(const Context& c, const QJsonObject& params, QPolygonF& outline, QString* error)
{
	outline = outlineFromParams(params);
	if (outline.size() < 3)
	{
		auto const extent = mapContentExtent(c);
		if (!extent.isValid() || extent.isEmpty())
		{
			setError(error, tr("The map has no objects to take the extent from."));
			return false;
		}
		outline = QPolygonF(extent);
	}
	if (outline.isClosed())
		outline.removeLast();
	return true;
}

/**
 * Where the straight line with the given offset across lies inside the
 * polygon, as pairs of distances along the line.
 */
std::vector<std::pair<double, double>> lineIntervals(const QPolygonF& outline, const QPointF& across, const QPointF& along, double offset)
{
	std::vector<double> crossings;
	for (int i = 0; i < outline.size(); ++i)
	{
		auto const& a = outline.at(i);
		auto const& b = outline.at((i + 1) % outline.size());
		auto const side_a = QPointF::dotProduct(across, a) - offset;
		auto const side_b = QPointF::dotProduct(across, b) - offset;
		if ((side_a < 0) == (side_b < 0))
			continue;
		auto const point = a + (b - a) * (side_a / (side_a - side_b));
		crossings.push_back(QPointF::dotProduct(along, point));
	}
	std::sort(begin(crossings), end(crossings));
	std::vector<std::pair<double, double>> intervals;
	for (std::size_t i = 0; i + 1 < crossings.size(); i += 2)
		intervals.emplace_back(crossings[i], crossings[i + 1]);
	return intervals;
}

/** A label 1 mm beyond the end of a line, aligned away from it. */
TextObject* frameLabel(const Symbol* symbol, const QPointF& end, const QPointF& outward, const QString& text)
{
	auto const position = end + outward;
	if (qAbs(outward.x()) > qAbs(outward.y()))
		return makeText(symbol, position, text, outward.x() < 0 ? TextObject::AlignRight : TextObject::AlignLeft, TextObject::AlignVCenter);
	return makeText(symbol, position, text, TextObject::AlignHCenter, outward.y() < 0 ? TextObject::AlignBottom : TextObject::AlignTop);
}

/** An angle given in seconds of arc as 47 12'N, with seconds when asked for. */
QString formatAngle(double seconds, bool with_seconds, QChar positive, QChar negative)
{
	auto const hemisphere = seconds < 0 ? negative : positive;
	const QChar degree_sign(0x00B0);
	const QChar minute_sign(0x2032);
	const QChar second_sign(0x2033);
	auto const two_digits = [](qint64 value) {
		return QString::number(value).rightJustified(2, QLatin1Char('0'));
	};
	if (!with_seconds)
	{
		auto const minutes = qRound64(qAbs(seconds) / 60);
		QString result = QString::number(minutes / 60) + degree_sign + two_digits(minutes % 60) + minute_sign;
		result += hemisphere;
		return result;
	}
	auto const hundredths = qRound64(qAbs(seconds) * 100);
	QString result = QString::number(hundredths / 360000) + degree_sign + two_digits(hundredths / 6000 % 60) + minute_sign + two_digits(hundredths / 100 % 60);
	if (hundredths % 100 != 0)
	{
		result += QLatin1Char('.');
		result += two_digits(hundredths % 100);
	}
	result += second_sign;
	result += hemisphere;
	return result;
}

/** The points which Douglas and Peucker keep at the tolerance. */
std::vector<QPointF> simplified(const std::vector<QPointF>& line, double tolerance)
{
	if (line.size() < 3)
		return line;
	std::vector<char> keep(line.size(), 0);
	keep.front() = 1;
	keep.back() = 1;
	std::vector<std::pair<std::size_t, std::size_t>> stack { { 0, line.size() - 1 } };
	while (!stack.empty())
	{
		auto const range = stack.back();
		stack.pop_back();
		auto const& a = line[range.first];
		auto const direction = line[range.second] - a;
		auto const length = std::hypot(direction.x(), direction.y());
		auto farthest = range.first;
		auto distance = 0.0;
		for (auto i = range.first + 1; i < range.second; ++i)
		{
			auto const offset = line[i] - a;
			auto const d = length > 0 ? qAbs(cross(direction, offset)) / length : std::hypot(offset.x(), offset.y());
			if (d > distance)
			{
				distance = d;
				farthest = i;
			}
		}
		if (distance > tolerance)
		{
			keep[farthest] = 1;
			stack.emplace_back(range.first, farthest);
			stack.emplace_back(farthest, range.second);
		}
	}
	std::vector<QPointF> result;
	for (std::size_t i = 0; i < line.size(); ++i)
	{
		if (keep[i])
			result.push_back(line[i]);
	}
	return result;
}

/** A part of a line inside the outline. */
struct Piece
{
	std::vector<QPointF> points;
	bool start_on_frame = false;
	bool end_on_frame = false;
};

/** The parts of the line which lie inside the outline. */
std::vector<Piece> clipToOutline(const std::vector<QPointF>& line, const QPolygonF& outline)
{
	std::vector<Piece> pieces;
	auto open = false;
	for (std::size_t s = 0; s + 1 < line.size(); ++s)
	{
		auto const& p = line[s];
		auto const step = line[s + 1] - p;
		std::vector<double> stops { 0.0, 1.0 };
		for (int i = 0; i < outline.size(); ++i)
		{
			auto const& a = outline.at(i);
			auto const edge = outline.at((i + 1) % outline.size()) - a;
			auto const denominator = cross(step, edge);
			if (qAbs(denominator) < 1e-12)
				continue;
			auto const t = cross(a - p, edge) / denominator;
			auto const u = cross(a - p, step) / denominator;
			if (t > 0 && t < 1 && u >= 0 && u <= 1)
				stops.push_back(t);
		}
		std::sort(begin(stops), end(stops));
		for (std::size_t k = 0; k + 1 < stops.size(); ++k)
		{
			auto const from = stops[k];
			auto const to = stops[k + 1];
			if (to - from < 1e-9)
				continue;
			if (!outline.containsPoint(p + step * ((from + to) / 2), Qt::OddEvenFill))
			{
				if (open)
					pieces.back().end_on_frame = true;
				open = false;
				continue;
			}
			if (!open)
			{
				pieces.emplace_back();
				pieces.back().points.push_back(p + step * from);
				pieces.back().start_on_frame = !(s == 0 && from == 0.0);
				open = true;
			}
			pieces.back().points.push_back(p + step * to);
		}
	}
	return pieces;
}

PathObject* makePath(const Symbol* symbol, const std::vector<QPointF>& points)
{
	MapCoordVector coords;
	coords.reserve(points.size());
	for (auto const& point : points)
		coords.emplace_back(point);
	return new PathObject(symbol, coords);
}

/** The column of a cell as A to Z, AA and so on. */
QString columnLetters(int column)
{
	QString letters;
	do
	{
		letters.prepend(QChar('A' + column % 26));
		column = column / 26 - 1;
	}
	while (column >= 0);
	return letters;
}

}  // namespace


// ### North arrow ###

bool buildNorthArrow(const Context& c, const QJsonObject& params, const MapCoordF& anchor, Build& build, QString* /*error*/)
{
	auto const size = qBound(2.0, number(params, "size", 15), 500.0);
	auto const design = string(params, "design");
	auto const* colour = darkestColor(c.map);
	auto const line_width = qMax(0.15, size / 60);
	
	auto* symbol = new PointSymbol();
	symbol->setName(tr("North arrow"));
	symbol->setRotatable(true);
	symbol->setPrivate(true);
	build.symbols.push_back(symbol);
	
	// The elements, in mm from the centre of the figure, y down.
	auto const add_line = [symbol, colour, line_width](const std::vector<QPointF>& points, bool closed) {
		auto* line = new LineSymbol();
		line->setLineWidth(line_width);
		line->setColor(colour);
		line->setJoinStyle(LineSymbol::RoundJoin);
		MapCoordVector coords;
		for (auto const& point : points)
			coords.emplace_back(point);
		if (closed)
		{
			coords.push_back(coords.front());
			coords.back().setClosePoint(true);
		}
		symbol->addElement(symbol->getNumElements(), new PathObject(line, coords), line);
	};
	auto const add_area = [symbol, colour](const std::vector<QPointF>& points) {
		auto* area = new AreaSymbol();
		area->setColor(colour);
		MapCoordVector coords;
		for (auto const& point : points)
			coords.emplace_back(point);
		coords.push_back(coords.front());
		coords.back().setClosePoint(true);
		symbol->addElement(symbol->getNumElements(), new PathObject(area, coords), area);
	};
	
	auto top = -size / 2;
	auto const bottom = size / 2;
	if (flag(params, "letter", true))
	{
		auto const height = 0.2 * size;
		auto const half_width = 0.07 * size;
		add_line({ { -half_width, top + height }, { -half_width, top }, { half_width, top + height }, { half_width, top } }, false);
		top += 0.26 * size;
	}
	auto const height = bottom - top;
	if (design == QLatin1String("needle"))
	{
		auto const half_width = 0.14 * height;
		auto const middle = (top + bottom) / 2;
		add_area({ { 0, top }, { half_width, middle }, { -half_width, middle } });
		add_line({ { 0, top }, { half_width, middle }, { 0, bottom }, { -half_width, middle } }, true);
	}
	else if (design == QLatin1String("barb"))
	{
		add_line({ { 0, bottom }, { 0, top }, { 0.15 * height, top + 0.3 * height } }, false);
	}
	else
	{
		auto const head = 0.3 * height;
		add_line({ { 0, bottom }, { 0, top + head } }, false);
		add_area({ { 0, top }, { 0.12 * height, top + head }, { -0.12 * height, top + head } });
	}
	
	// A point object turns anticlockwise by its rotation. Up is magnetic north;
	// true north lies the declination, grid north the grivation anticlockwise of it.
	auto rotation = 0.0;
	auto const reference = string(params, "reference");
	if (reference == QLatin1String("true"))
		rotation = qDegreesToRadians(c.map.getGeoreferencing().getDeclination());
	else if (reference == QLatin1String("grid"))
		rotation = qDegreesToRadians(c.map.getGeoreferencing().getGrivation());
	
	auto* point = new PointObject(symbol);
	point->setPosition(anchor);
	point->setRotation(rotation);
	build.objects.push_back(point);
	return true;
}


// ### Grid ###

bool buildGrid(const Context& c, const QJsonObject& params, const MapCoordF& /*anchor*/, Build& build, QString* error)
{
	auto const scale = c.map.getScaleDenominator();
	auto spacing_e = number(params, "spacing_e");
	if (spacing_e <= 0)
		spacing_e = gridAutoSpacing(scale);
	auto spacing_n = number(params, "spacing_n");
	if (spacing_n <= 0)
		spacing_n = spacing_e;
	auto const paper = [scale](double metres) { return metres * 1000.0 / scale; };
	if (paper(qMin(spacing_e, spacing_n)) < 0.5)
	{
		setError(error, tr("Grid lines %1 m apart would be %2 mm apart on the map.").arg(qMin(spacing_e, spacing_n)).arg(paper(qMin(spacing_e, spacing_n)), 0, 'f', 2));
		return false;
	}
	
	QPolygonF outline;
	if (!resolveOutline(c, params, outline, error))
		return false;
	// The transformation to projected coordinates is affine, so the lines are
	// straight on the map and can be clipped in projected coordinates.
	auto const& georef = c.map.getGeoreferencing();
	QPolygonF projected;
	for (auto const& vertex : qAsConst(outline))
		projected.append(georef.toProjectedCoords(MapCoordF(vertex)));
	
	struct Family
	{
		bool on;
		QPointF across;
		QPointF along;
		double spacing;
		double offset;
		double other_spacing;
		double other_offset;
	};
	const Family families[2] = {
		{ flag(params, "vertical", true), { 1, 0 }, { 0, 1 }, spacing_e, number(params, "offset_e"), spacing_n, number(params, "offset_n") },
		{ flag(params, "horizontal", true), { 0, 1 }, { 1, 0 }, spacing_n, number(params, "offset_n"), spacing_e, number(params, "offset_e") },
	};
	auto const with_labels = flag(params, "labels", true);
	auto const with_junctions = flag(params, "junction_vertices", false);
	auto const* line = lineSymbol(c, params, build);
	auto const* text = with_labels ? textSymbol(c, params, build) : nullptr;
	auto const shortest = 0.01 * scale / 1000.0;  // 0.01 mm on paper, in metres
	
	std::vector<Object*> labels;
	for (auto const& family : families)
	{
		if (!family.on)
			continue;
		auto lowest = std::numeric_limits<double>::max();
		auto highest = std::numeric_limits<double>::lowest();
		for (auto const& vertex : qAsConst(projected))
		{
			auto const value = QPointF::dotProduct(family.across, vertex);
			lowest = std::min(lowest, value);
			highest = std::max(highest, value);
		}
		auto const first = std::ceil((lowest - family.offset) / family.spacing);
		auto const last = std::floor((highest - family.offset) / family.spacing);
		if (last - first > 2000)
		{
			for (auto* label : labels)
				delete label;
			setError(error, tr("That would be more than 2000 grid lines."));
			return false;
		}
		for (auto k = first; k <= last; ++k)
		{
			auto const value = family.offset + k * family.spacing;
			auto const intervals = lineIntervals(projected, family.across, family.along, value);
			auto const on_map = [&georef, &family, value](double along) {
				return QPointF(georef.toMapCoordF(family.across * value + family.along * along));
			};
			auto drawn = false;
			for (auto const& interval : intervals)
			{
				if (interval.second - interval.first < shortest)
					continue;
				std::vector<QPointF> points { on_map(interval.first) };
				if (with_junctions)
				{
					auto j = std::floor((interval.first - family.other_offset) / family.other_spacing) + 1;
					for (; family.other_offset + j * family.other_spacing < interval.second - shortest; ++j)
					{
						auto const junction = family.other_offset + j * family.other_spacing;
						if (junction > interval.first + shortest)
							points.push_back(on_map(junction));
					}
				}
				points.push_back(on_map(interval.second));
				build.objects.push_back(makePath(line, points));
				drawn = true;
			}
			if (with_labels && drawn)
			{
				auto const low = on_map(intervals.front().first);
				auto const high = on_map(intervals.back().second);
				auto const direction = unit(high - low);
				auto const label = QString::number(qRound64(value));
				labels.push_back(frameLabel(text, low, -direction, label));
				labels.push_back(frameLabel(text, high, direction, label));
			}
		}
	}
	if (build.objects.empty())
	{
		for (auto* label : labels)
			delete label;
		setError(error, tr("No grid line falls inside the outline."));
		return false;
	}
	build.objects.insert(build.objects.end(), labels.begin(), labels.end());
	return true;
}


// ### Graticule ###

bool buildGraticule(const Context& c, const QJsonObject& params, const MapCoordF& /*anchor*/, Build& build, QString* error)
{
	auto const& georef = c.map.getGeoreferencing();
	if (georef.getState() != Georeferencing::Geospatial)
	{
		setError(error, tr("A graticule needs a map with a georeferencing which gives latitude and longitude."));
		return false;
	}
	
	auto interval = number(params, "interval_min") * 60;  // seconds of arc
	if (interval <= 0)
		interval = graticuleAutoInterval(c.map.getScaleDenominator());
	if (interval < 0.01)
	{
		setError(error, tr("The interval is too small."));
		return false;
	}
	
	QPolygonF outline;
	if (!resolveOutline(c, params, outline, error))
		return false;
	
	// The bounds, in seconds of arc.
	auto lat_min = number(params, "lat_min") * 3600;
	auto lat_max = number(params, "lat_max") * 3600;
	auto lon_min = number(params, "lon_min") * 3600;
	auto lon_max = number(params, "lon_max") * 3600;
	if (lat_min == 0 && lat_max == 0 && lon_min == 0 && lon_max == 0)
	{
		lat_min = lon_min = std::numeric_limits<double>::max();
		lat_max = lon_max = std::numeric_limits<double>::lowest();
		for (int i = 0; i < outline.size(); ++i)
		{
			auto const& a = outline.at(i);
			auto const& b = outline.at((i + 1) % outline.size());
			for (int k = 0; k < 10; ++k)
			{
				auto ok = false;
				auto const position = georef.toGeographicCoords(MapCoordF(a + (b - a) * (k / 10.0)), &ok);
				if (!ok)
					continue;
				lat_min = std::min(lat_min, position.latitude() * 3600);
				lat_max = std::max(lat_max, position.latitude() * 3600);
				lon_min = std::min(lon_min, position.longitude() * 3600);
				lon_max = std::max(lon_max, position.longitude() * 3600);
			}
		}
		lat_min -= interval;
		lat_max += interval;
		lon_min -= interval;
		lon_max += interval;
	}
	lat_min = std::max(lat_min, -89.0 * 3600);
	lat_max = std::min(lat_max, 89.0 * 3600);
	if (lat_min >= lat_max || lon_min >= lon_max)
	{
		setError(error, tr("The bounds of the graticule are empty."));
		return false;
	}
	auto const tolerance = interval * 1e-6;
	auto const first_lon = std::ceil((lon_min - tolerance) / interval);
	auto const last_lon = std::floor((lon_max + tolerance) / interval);
	auto const first_lat = std::ceil((lat_min - tolerance) / interval);
	auto const last_lat = std::floor((lat_max + tolerance) / interval);
	if ((last_lon - first_lon) + (last_lat - first_lat) > 2000)
	{
		setError(error, tr("That would be more than 2000 lines."));
		return false;
	}
	
	auto const with_lines = flag(params, "lines", true);
	auto const with_labels = flag(params, "labels", true);
	auto const with_seconds = std::fmod(interval, 60.0) > 1e-6 && 60.0 - std::fmod(interval, 60.0) > 1e-6;
	auto const* line = with_lines ? lineSymbol(c, params, build) : nullptr;
	auto const* text = with_labels ? textSymbol(c, params, build) : nullptr;
	std::vector<Object*> labels;
	auto const step = interval / 10;
	
	// One line of constant longitude or latitude between the bounds of the other.
	auto const add = [&](bool meridian, double value, double from, double to) {
		std::vector<QPointF> points;
		auto const count = int(std::ceil((to - from) / step - 1e-9));
		for (int i = 0; i <= count; ++i)
		{
			auto const other = std::min(from + i * step, to);
			auto ok = false;
			auto const position = meridian ? LatLon(other / 3600, value / 3600) : LatLon(value / 3600, other / 3600);
			auto const point = georef.toMapCoordF(position, &ok);
			if (ok)
				points.push_back(QPointF(point));
		}
		auto const pieces = clipToOutline(simplified(points, 0.01), outline);
		if (pieces.empty())
			return;
		if (with_lines)
		{
			for (auto const& piece : pieces)
				build.objects.push_back(makePath(line, piece.points));
		}
		if (with_labels)
		{
			auto const label = meridian ? formatAngle(value, with_seconds, QLatin1Char('E'), QLatin1Char('W'))
			                            : formatAngle(value, with_seconds, QLatin1Char('N'), QLatin1Char('S'));
			auto const& start = pieces.front();
			if (start.start_on_frame)
				labels.push_back(frameLabel(text, start.points[0], unit(start.points[0] - start.points[1]), label));
			auto const& end = pieces.back();
			auto const n = end.points.size();
			if (end.end_on_frame)
				labels.push_back(frameLabel(text, end.points[n - 1], unit(end.points[n - 1] - end.points[n - 2]), label));
		}
	};
	for (auto k = first_lon; k <= last_lon; ++k)
		add(true, k * interval, lat_min, lat_max);
	for (auto k = first_lat; k <= last_lat; ++k)
		add(false, k * interval, lon_min, lon_max);
	
	build.objects.insert(build.objects.end(), labels.begin(), labels.end());
	if (build.objects.empty())
	{
		setError(error, tr("No line of the graticule falls inside the outline."));
		return false;
	}
	return true;
}


// ### Name index ###

bool buildNameIndex(const Context& c, const QJsonObject& params, const MapCoordF& anchor, Build& build, QString* error)
{
	QStringList numbers;
	for (auto const& entry : string(params, "symbols").split(QLatin1Char(','), Qt::SkipEmptyParts))
		numbers.append(entry.trimmed());
	auto const by_graticule = string(params, "cells") == QLatin1String("graticule");
	auto const& georef = c.map.getGeoreferencing();
	if (by_graticule && georef.getState() != Georeferencing::Geospatial)
	{
		setError(error, tr("Cells of latitude and longitude need a map with a georeferencing which gives them."));
		return false;
	}
	const QPointF origin(number(params, "origin_x"), number(params, "origin_y"));
	auto const cell_width = qMax(1.0, number(params, "cell_width", 50));
	auto const cell_height = qMax(1.0, number(params, "cell_height", 50));
	auto const cell_seconds = qMax(0.01, number(params, "cell_minutes", 1)) * 60;
	auto const with_seconds = std::fmod(cell_seconds, 60.0) > 1e-6 && 60.0 - std::fmod(cell_seconds, 60.0) > 1e-6;
	auto const rows_per_column = qBound(1, params.value(QLatin1String("rows_per_column")).toInt(40), 10000);
	auto const column_width = qMax(25.0, number(params, "column_width", 60));
	
	// The cells of each name, in the order column, row.
	using Cell = std::pair<std::pair<qint64, qint64>, QString>;
	std::map<QString, std::set<Cell>> names;
	for (int p = 0; p < c.map.getNumParts(); ++p)
	{
		auto const* part = c.map.getPart(std::size_t(p));
		if (!isMapContent(*part))
			continue;
		for (int i = 0; i < part->getNumObjects(); ++i)
		{
			auto const* object = part->getObject(i);
			if (object->getType() != Object::Text || !object->getTag(QLatin1String("generator")).isEmpty())
				continue;
			if (!numbers.isEmpty() && !(object->getSymbol() && numbers.contains(object->getSymbol()->getNumberAsString())))
				continue;
			auto name = object->asText()->getText();
			name.replace(QLatin1Char('\n'), QLatin1Char(' '));
			name = name.simplified();
			if (name.isEmpty())
				continue;
			auto const position = object->asText()->getAnchorCoordF();
			if (by_graticule)
			{
				auto ok = false;
				auto const where = georef.toGeographicCoords(position, &ok);
				if (!ok)
					continue;
				auto const row = qint64(std::floor(where.latitude() * 3600 / cell_seconds));
				auto const column = qint64(std::floor(where.longitude() * 3600 / cell_seconds));
				QString label = formatAngle(row * cell_seconds, with_seconds, QLatin1Char('N'), QLatin1Char('S'));
				label += QLatin1Char(' ');
				label += formatAngle(column * cell_seconds, with_seconds, QLatin1Char('E'), QLatin1Char('W'));
				names[name].insert({ { column, -row }, label });
			}
			else
			{
				auto const column = qint64(std::floor((position.x() - origin.x()) / cell_width));
				auto const row = qint64(std::floor((position.y() - origin.y()) / cell_height));
				if (column < 0 || row < 0)
					continue;
				QString label = columnLetters(int(column));
				label += QString::number(row + 1);
				names[name].insert({ { column, row }, label });
			}
		}
	}
	if (names.empty())
	{
		setError(error, tr("No text object of the chosen symbols lies in a cell."));
		return false;
	}
	
	QStringList sorted;
	for (auto const& entry : names)
		sorted.append(entry.first);
	QCollator collator;
	collator.setNumericMode(true);
	std::sort(sorted.begin(), sorted.end(), [&collator](const QString& a, const QString& b) {
		auto const order = collator.compare(a, b);
		return order != 0 ? order < 0 : a < b;
	});
	
	auto const symbols_before = build.symbols.size();
	auto const* text = textSymbol(c, params, build);
	if (build.symbols.size() > symbols_before)
	{
		// The automatic symbol: one tab stop, where the cells start.
		static_cast<TextSymbol*>(build.symbols.back())->setCustomTabs({ qRound(1000 * (column_width - 20)) });
	}
	QStringList lines;
	auto column = 0;
	auto const flush = [&]() {
		if (lines.isEmpty())
			return;
		build.objects.push_back(makeText(text, { anchor.x() + column * column_width, anchor.y() }, lines.join(QLatin1Char('\n')), TextObject::AlignLeft, TextObject::AlignTop));
		lines.clear();
		++column;
	};
	for (auto const& name : qAsConst(sorted))
	{
		QStringList cells;
		for (auto const& cell : names[name])
			cells.append(cell.second);
		lines.append(name + QLatin1Char('\t') + cells.join(QLatin1String(", ")));
		if (lines.size() == rows_per_column)
			flush();
	}
	flush();
	return true;
}


// ### Trim marks ###

bool buildTrimMarks(const Context& c, const QJsonObject& params, const MapCoordF& /*anchor*/, Build& build, QString* error)
{
	QRectF trim;
	auto const name = string(params, "extent");
	if (name.isEmpty())
	{
		if (!c.map.hasPrinterConfig())
		{
			setError(error, tr("The map has no print area. Choose an extent."));
			return false;
		}
		trim = c.map.printerConfig().print_area;
	}
	else
	{
		auto const& extents = c.map.extents();
		auto const found = std::find_if(begin(extents), end(extents), [&name](const Extent& extent) { return extent.name == name; });
		if (found == end(extents))
		{
			setError(error, tr("There is no extent %1.").arg(name));
			return false;
		}
		trim = found->rect_mm;
	}
	if (!trim.isValid() || trim.isEmpty())
	{
		setError(error, tr("The trim rectangle is empty."));
		return false;
	}
	
	auto const bleed = qBound(0.0, number(params, "bleed", 3), 100.0);
	auto const length = qBound(1.0, number(params, "mark_length", 5), 100.0);
	auto const gap = qMax(2.0, bleed);
	auto const* line = lineSymbol(c, params, build);
	struct Corner
	{
		QPointF point;
		double x_out;
		double y_out;
	};
	const Corner corners[4] = {
		{ trim.topLeft(), -1, -1 },
		{ trim.topRight(), 1, -1 },
		{ trim.bottomRight(), 1, 1 },
		{ trim.bottomLeft(), -1, 1 },
	};
	for (auto const& corner : corners)
	{
		auto const x = corner.point.x();
		auto const y = corner.point.y();
		build.objects.push_back(makeLine(line, { x + corner.x_out * (gap + length), y }, { x + corner.x_out * gap, y }));
		build.objects.push_back(makeLine(line, { x, y + corner.y_out * (gap + length) }, { x, y + corner.y_out * gap }));
	}
	if (bleed > 0 && flag(params, "bleed_rectangle", true))
		build.objects.push_back(makeRectangle(line, trim.adjusted(-bleed, -bleed, bleed, bleed)));
	return true;
}


// ### QR code ###

bool buildQrCode(const Context& c, const QJsonObject& params, const MapCoordF& anchor, Build& build, QString* error)
{
	auto const text = string(params, "text");
	if (text.isEmpty())
	{
		setError(error, tr("Enter the text of the QR code."));
		return false;
	}
	auto level = QrEcLevel::M;
	auto const level_name = string(params, "ec_level");
	if (level_name == QLatin1String("L"))
		level = QrEcLevel::L;
	else if (level_name == QLatin1String("Q"))
		level = QrEcLevel::Q;
	else if (level_name == QLatin1String("H"))
		level = QrEcLevel::H;
	auto const code = encodeQr(text.toUtf8(), level, error);
	if (!code.isValid())
		return false;
	
	auto const module = qBound(0.1, number(params, "module", 0.5), 20.0);
	auto const quiet_zone = qBound(0, params.value(QLatin1String("quiet_zone")).toInt(4), 20);
	const QPointF origin(anchor.x() + quiet_zone * module, anchor.y() + quiet_zone * module);
	
	// Runs of dark modules in a row, joined with the same run in the rows below.
	struct Run
	{
		int from;
		int to;
		int top;
	};
	std::vector<Run> open;
	std::vector<QRect> rectangles;
	for (int y = 0; y <= code.size; ++y)
	{
		std::vector<Run> row;
		if (y < code.size)
		{
			for (int x = 0; x < code.size; ++x)
			{
				if (!code.dark(x, y))
					continue;
				auto const from = x;
				while (x < code.size && code.dark(x, y))
					++x;
				row.push_back({ from, x, y });
			}
		}
		std::vector<Run> still_open;
		for (auto const& run : open)
		{
			auto const same = std::find_if(begin(row), end(row), [&run](const Run& other) { return other.from == run.from && other.to == run.to; });
			if (same == end(row))
			{
				rectangles.emplace_back(run.from, run.top, run.to - run.from, y - run.top);
			}
			else
			{
				still_open.push_back(run);
				row.erase(same);
			}
		}
		still_open.insert(still_open.end(), row.begin(), row.end());
		open = std::move(still_open);
	}
	
	// One object, so that no seams show between the rectangles.
	MapCoordVector coords;
	coords.reserve(rectangles.size() * 5);
	for (auto const& rectangle : rectangles)
	{
		const QRectF box(origin.x() + rectangle.x() * module, origin.y() + rectangle.y() * module, rectangle.width() * module, rectangle.height() * module);
		coords.emplace_back(box.topLeft());
		coords.emplace_back(box.topRight());
		coords.emplace_back(box.bottomRight());
		coords.emplace_back(box.bottomLeft());
		coords.emplace_back(box.topLeft());
		coords.back().setClosePoint(true);
		coords.back().setHolePoint(true);
	}
	build.objects.push_back(new PathObject(areaSymbol(c, params, build), coords));
	return true;
}

}  // namespace generator_impl


double gridAutoSpacing(unsigned int scale_denominator)
{
	auto const needed = 0.04 * qMax(1u, scale_denominator);  // 40 mm on paper, in metres
	for (int exponent = 0; exponent <= 9; ++exponent)
	{
		auto const power = std::pow(10.0, exponent);
		for (auto factor : { 1.0, 2.0, 5.0 })
		{
			if (factor * power >= needed * (1 - 1e-9))
				return factor * power;
		}
	}
	return 1e10;
}

double graticuleAutoInterval(unsigned int scale_denominator)
{
	// 30.87 m to the second of latitude.
	auto const needed = 0.04 * qMax(1u, scale_denominator) / 30.87;
	for (auto seconds : { 1.0, 2.0, 5.0, 10.0, 15.0, 20.0, 30.0, 60.0, 120.0, 300.0, 600.0, 900.0, 1200.0, 1800.0, 3600.0, 7200.0, 18000.0 })
	{
		if (seconds >= needed)
			return seconds;
	}
	return 36000.0;
}


}  // namespace OpenOrienteering
