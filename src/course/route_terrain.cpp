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


#include "route_terrain.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iterator>
#include <map>

#include <Qt>
#include <QColor>
#include <QFile>
#include <QImage>
#include <QIODevice>
#include <QLatin1Char>
#include <QLatin1String>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPointF>
#include <QPolygonF>
#include <QTextStream>

#include "core/map.h"
#include "core/map_part.h"
#include "core/objects/object.h"
#include "core/path_coord.h"
#include "core/symbols/combined_symbol.h"
#include "core/symbols/line_symbol.h"
#include "core/symbols/symbol.h"
#include "core/virtual_path.h"
#include "templates/template.h"
#include "templates/template_map.h"

namespace OpenOrienteering {

namespace {

/// The width of a single line in pixels, whatever the width of its symbol.
constexpr double line_pixels = 1.5;

/// The width of an impassable line in pixels: cliffs and impassable fences are
/// three pixels wide, so that nothing slips through them.
constexpr double impassable_line_pixels = 3.0;

/// No raster is larger than this in either direction.
constexpr int max_raster_size = 30000;

struct DefaultEntry
{
	int number;
	/** The second part of the number; -1 for every symbol of the first. */
	int second;
	Runnability::Kind kind;
	int percent;
};

/// ISOM 2017-2, with the percentage and the kind of every symbol;
/// the design notes. An entry with a second number comes before the
/// entry of its first number.
const DefaultEntry default_table[] = {
    { 104, 4, Runnability::None, 100 },        // the tags of an earth bank
    { 104, -1, Runnability::Hindrance, 37 },   // earth bank
    { 114, -1, Runnability::Surface, 65 },     // very broken ground
    { 201, -1, Runnability::Impassable, 0 },   // impassable cliff
    { 202, -1, Runnability::Hindrance, 10 },   // cliff
    { 210, -1, Runnability::Surface, 65 },     // stony ground, slow running
    { 211, -1, Runnability::Surface, 37 },     // stony ground, walk
    { 212, -1, Runnability::Surface, 10 },     // stony ground, fight
    { 301, -1, Runnability::Impassable, 0 },   // uncrossable body of water
    { 304, -1, Runnability::Hindrance, 65 },   // crossable watercourse
    { 305, -1, Runnability::Hindrance, 65 },   // small crossable watercourse
    { 307, -1, Runnability::Impassable, 0 },   // uncrossable marsh
    { 309, -1, Runnability::Hindrance, 75 },   // narrow marsh
    { 401, -1, Runnability::Ground, 86 },      // open land
    { 402, -1, Runnability::Ground, 86 },      // open land with scattered trees
    { 403, -1, Runnability::Ground, 83 },      // rough open land
    { 404, -1, Runnability::Ground, 83 },      // rough open land with scattered trees
    { 405, -1, Runnability::Vegetation, 100 }, // forest, where it is drawn: it covers open land below
    { 406, -1, Runnability::Vegetation, 65 },  // slow running
    { 407, -1, Runnability::Hindrance, 65 },   // slow running, good visibility
    { 408, -1, Runnability::Vegetation, 37 },  // walk
    { 409, -1, Runnability::Hindrance, 37 },   // walk, good visibility
    { 410, -1, Runnability::Vegetation, 10 },  // fight
    { 411, -1, Runnability::Impassable, 0 },   // impassable vegetation
    { 413, -1, Runnability::Ground, 83 },      // orchard
    { 501, 2, Runnability::None, 100 },        // the boundary of a paved area
    { 501, -1, Runnability::Ground, 100 },     // paved area
    { 502, -1, Runnability::Path, 100 },       // wide road
    { 503, -1, Runnability::Path, 100 },       // road
    { 504, -1, Runnability::Path, 100 },       // vehicle track
    { 505, -1, Runnability::Path, 93 },        // footpath
    { 506, -1, Runnability::Path, 93 },        // small footpath
    { 507, -1, Runnability::Path, 83 },        // less distinct small footpath
    { 508, 3, Runnability::Path, 37 },         // narrow ride, difficult to run
    { 508, -1, Runnability::Path, 83 },        // narrow ride
    { 513, -1, Runnability::Hindrance, 37 },   // wall
    { 515, -1, Runnability::Impassable, 0 },   // impassable wall
    { 516, -1, Runnability::Hindrance, 37 },   // fence
    { 518, -1, Runnability::Impassable, 0 },   // impassable fence
    { 520, -1, Runnability::Impassable, 0 },   // area that shall not be entered
    { 521, -1, Runnability::Impassable, 0 },   // building
    { 708, -1, Runnability::Impassable, 0 },   // out-of-bounds boundary of a course project
    { 709, -1, Runnability::Impassable, 0 },   // out-of-bounds area of a course project
};

/// The width with which a symbol is drawn in mm: a single line is one pixel wide,
/// a combined symbol as wide as its widest line; 0 for a symbol without a line.
double strokeWidthOf(const Symbol* symbol)
{
	if (!symbol || symbol->getType() != Symbol::Combined)
		return 0.0;
	auto width = 0.0;
	auto const* combined = symbol->asCombined();
	for (int i = 0; i < combined->getNumParts(); ++i)
	{
		auto const* part = combined->getPart(i);
		if (part && part->getType() == Symbol::Line)
			width = std::max(width, part->asLine()->getLineWidth() / 1000.0);
		else
			width = std::max(width, strokeWidthOf(part));
	}
	return width;
}

/// What is drawn of an object: its outline, filled or stroked.
struct Shape
{
	QPainterPath path;
	bool fill = false;
	/** The width of the stroke in mm; 0 for none. */
	double stroke = 0.0;
};

/// One raster of the size of the terrain, painted in mm of the map.
class Layer
{
public:
	Layer(const RouteTerrain& terrain, int initial)
	: image(terrain.width, terrain.height, QImage::Format_Grayscale8)
	, pixel(terrain.pixel)
	{
		image.fill(QColor(initial, initial, initial));
		painter.begin(&image);
		painter.setRenderHint(QPainter::Antialiasing, false);
		painter.scale(1.0 / terrain.pixel, 1.0 / terrain.pixel);
		painter.translate(-terrain.area.topLeft());
	}
	
	void draw(const Shape& shape, int value)
	{
		auto const colour = QColor(value, value, value);
		if (shape.fill)
			painter.fillPath(shape.path, colour);
		if (shape.stroke > 0.0)
		{
			QPen pen(colour, std::max(shape.stroke, pixel));
			pen.setCapStyle(Qt::FlatCap);
			pen.setJoinStyle(Qt::BevelJoin);
			painter.strokePath(shape.path, pen);
		}
	}
	
	/// Ends the painting; the image can be read then.
	const QImage& done()
	{
		if (painter.isActive())
			painter.end();
		return image;
	}
	
private:
	QImage image;
	QPainter painter;
	double pixel;
};

/// Calls f(pixel index, value) for every pixel of a layer.
template <class F>
void forEachPixel(const QImage& image, F f)
{
	auto const width = image.width();
	for (int y = 0; y < image.height(); ++y)
	{
		auto const* line = image.constScanLine(y);
		auto const row = std::size_t(y) * std::size_t(width);
		for (int x = 0; x < width; ++x)
			f(row + std::size_t(x), line[x]);
	}
}

}  // namespace



QString routeSymbolNumber(const Symbol& symbol)
{
	return QStringLiteral("%1.%2").arg(symbol.getNumberComponent(0)).arg(std::max(symbol.getNumberComponent(1), 0), 3, 10, QLatin1Char('0'));
}


Runnability defaultRunnability(const Symbol& symbol)
{
	Runnability result;
	auto const types = symbol.getContainedTypes();
	if (!(types & (Symbol::Line | Symbol::Area)))
		return result;
	auto const number = symbol.getNumberComponent(0);
	auto const second = std::max(symbol.getNumberComponent(1), 0);
	auto const found = std::find_if(std::begin(default_table), std::end(default_table), [number, second](const DefaultEntry& entry) {
		return entry.number == number && (entry.second < 0 || entry.second == second);
	});
	if (found == std::end(default_table))
		return result;
	result.kind = found->kind;
	result.percent = found->percent;
	return result;
}


Runnability runnabilityOf(const RouteSettings& settings, const Symbol& symbol)
{
	if (!settings.symbols.empty())
	{
		auto const number = routeSymbolNumber(symbol);
		for (auto const& entry : settings.symbols)
		{
			if (entry.first == number)
				return entry.second;
		}
	}
	return defaultRunnability(symbol);
}



int RouteTerrain::runnabilityAt(const MapCoordF& place) const
{
	auto const x = int(std::floor((place.x() - area.left()) / pixel));
	auto const y = int(std::floor((place.y() - area.top()) / pixel));
	if (x < 0 || y < 0 || x >= width || y >= height)
		return forest_runnability;
	return runnability[std::size_t(y) * std::size_t(width) + std::size_t(x)];
}


bool RouteTerrain::isImpassableAt(const MapCoordF& place) const
{
	auto const x = int(std::floor((place.x() - area.left()) / pixel));
	auto const y = int(std::floor((place.y() - area.top()) / pixel));
	if (x < 0 || y < 0 || x >= width || y >= height)
		return false;
	return impassable[std::size_t(y) * std::size_t(width) + std::size_t(x)] != 0;
}



RouteTerrain buildRouteTerrain(const Map& map, const RouteSettings& settings, const QRectF& wanted_area)
{
	RouteTerrain terrain;
	
	// The maps whose objects count: this one, and its background maps which are maps.
	struct Source
	{
		const Map* map;
		const Template* placed;  ///< the template which places the map; nullptr for the map itself
	};
	std::vector<Source> sources = { { &map, nullptr } };
	for (int i = 0; i < map.getNumTemplates(); ++i)
	{
		auto const* temp = map.getTemplate(i);
		if (temp->getTemplateState() != Template::Loaded)
			continue;
		if (auto const* template_map = qobject_cast<const TemplateMap*>(temp))
		{
			if (template_map->templateMap())
				sources.push_back({ template_map->templateMap(), temp });
		}
	}
	
	// What is drawn, by the kind of its runnability.
	struct Drawn
	{
		Runnability runnability;
		const Symbol* symbol;
		Shape shape;
	};
	std::vector<Drawn> drawn;
	QRectF extent;
	for (auto const& source : sources)
	{
		for (int p = 0; p < source.map->getNumParts(); ++p)
		{
			auto const* part = source.map->getPart(std::size_t(p));
			for (int o = 0; o < part->getNumObjects(); ++o)
			{
				auto const* object = part->getObject(o);
				auto const* symbol = object->getSymbol();
				if (!symbol || object->getType() != Object::Path)
					continue;
				auto const runnability = runnabilityOf(settings, *symbol);
				if (runnability.kind == Runnability::None)
					continue;
				
				Drawn entry;
				entry.runnability = runnability;
				entry.symbol = symbol;
				auto const types = symbol->getContainedTypes();
				entry.shape.fill = bool(types & Symbol::Area);
				auto const pixels = runnability.kind == Runnability::Impassable ? impassable_line_pixels : line_pixels;
				entry.shape.stroke = (types & Symbol::Line) ? std::max(strokeWidthOf(symbol), terrain.pixel * pixels) : 0.0;
				entry.shape.path.setFillRule(Qt::OddEvenFill);
				object->update();
				for (auto const& path_part : object->asPath()->parts())
				{
					QPolygonF polygon;
					polygon.reserve(int(path_part.path_coords.size()));
					for (auto const& coord : path_part.path_coords)
						polygon.push_back(source.placed ? QPointF(source.placed->templateToMap(coord.pos)) : QPointF(coord.pos));
					if (polygon.size() < 2)
						continue;
					if (entry.shape.fill && polygon.front() != polygon.back())
						polygon.push_back(polygon.front());
					entry.shape.path.addPolygon(polygon);
				}
				if (entry.shape.path.isEmpty())
					continue;
				extent = extent.united(entry.shape.path.boundingRect());
				drawn.push_back(std::move(entry));
			}
		}
	}
	
	terrain.area = wanted_area.isValid() ? wanted_area : map.calculateExtent(false, true).united(extent);
	if (!terrain.area.isValid() || terrain.area.isEmpty())
		terrain.area = extent;
	if (!terrain.area.isValid() || terrain.area.isEmpty())
		return terrain;
	terrain.width = int(std::ceil(terrain.area.width() / terrain.pixel - 1e-6));
	terrain.height = int(std::ceil(terrain.area.height() / terrain.pixel - 1e-6));
	if (terrain.width <= 0 || terrain.height <= 0 || terrain.width > max_raster_size || terrain.height > max_raster_size)
	{
		terrain.width = 0;
		terrain.height = 0;
		return terrain;
	}
	auto const count = std::size_t(terrain.width) * std::size_t(terrain.height);
	terrain.runnability.assign(count, quint8(settings.use_runnability ? forest_runnability : 100));
	terrain.impassable.assign(count, 0);
	
	// What is impassable.
	{
		Layer layer(terrain, 0);
		for (auto const& entry : drawn)
		{
			if (entry.runnability.kind == Runnability::Impassable)
				layer.draw(entry.shape, 255);
		}
		forEachPixel(layer.done(), [&terrain](std::size_t i, uchar value) { terrain.impassable[i] = value ? 1 : 0; });
	}
	
	if (settings.use_runnability)
	{
		// The slowest of the symbols of a kind which cover a place: they are drawn from the fastest to the slowest.
		constexpr int nothing = 255;
		auto const slowest = [&drawn, &terrain](Runnability::Kind kind) {
			std::vector<const Drawn*> entries;
			for (auto const& entry : drawn)
			{
				if (entry.runnability.kind == kind)
					entries.push_back(&entry);
			}
			std::stable_sort(begin(entries), end(entries), [](const Drawn* lhs, const Drawn* rhs) { return lhs->runnability.percent > rhs->runnability.percent; });
			Layer layer(terrain, nothing);
			for (auto const* entry : entries)
				layer.draw(entry->shape, qBound(0, entry->runnability.percent, 100));
			return layer.done().copy();
		};
		auto const ground = slowest(Runnability::Ground);
		auto const vegetation = slowest(Runnability::Vegetation);
		auto const hindrance = slowest(Runnability::Hindrance);

		// Forest, or under vegetation, counts from the speed in forest; open ground from the full speed.
		std::vector<float> value(count);
		for (int y = 0; y < terrain.height; ++y)
		{
			auto const* ground_line = ground.constScanLine(y);
			auto const* vegetation_line = vegetation.constScanLine(y);
			auto const* hindrance_line = hindrance.constScanLine(y);
			auto const row = std::size_t(y) * std::size_t(terrain.width);
			for (int x = 0; x < terrain.width; ++x)
			{
				auto base = float(forest_runnability);
				auto factor = 100;
				if (vegetation_line[x] != nothing)
				{
					factor = vegetation_line[x];
				}
				else if (ground_line[x] != nothing)
				{
					base = 100.0f;
					factor = ground_line[x];
				}
				if (hindrance_line[x] != nothing)
					factor = std::min(factor, int(hindrance_line[x]));
				value[row + std::size_t(x)] = base * float(factor) / 100.0f;
			}
		}

		// Stony and broken ground multiplies it: every symbol once, however many of its objects overlap.
		std::map<const Symbol*, std::vector<const Drawn*>> by_symbol;
		for (auto const& entry : drawn)
		{
			if (entry.runnability.kind == Runnability::Surface)
				by_symbol[entry.symbol].push_back(&entry);
		}
		for (auto const& group : by_symbol)
		{
			Layer layer(terrain, 0);
			for (auto const* entry : group.second)
				layer.draw(entry->shape, 255);
			auto const factor = float(qBound(0, group.second.front()->runnability.percent, 100)) / 100.0f;
			forEachPixel(layer.done(), [&value, factor](std::size_t i, uchar covered) {
				if (covered)
					value[i] *= factor;
			});
		}
		for (std::size_t i = 0; i < count; ++i)
			terrain.runnability[i] = quint8(std::floor(value[i] + 0.5f));
	}

	// What is impassable is 0.
	for (std::size_t i = 0; i < count; ++i)
	{
		if (terrain.impassable[i])
			terrain.runnability[i] = 0;
	}

	if (settings.use_runnability)
	{
		// Roads, paths and rides put their value in place of what they cross, the faster ones last.
		std::vector<const Drawn*> paths;
		for (auto const& entry : drawn)
		{
			if (entry.runnability.kind == Runnability::Path)
				paths.push_back(&entry);
		}
		if (!paths.empty())
		{
			std::stable_sort(begin(paths), end(paths), [](const Drawn* lhs, const Drawn* rhs) { return lhs->runnability.percent < rhs->runnability.percent; });
			constexpr int nothing = 255;
			Layer layer(terrain, nothing);
			for (auto const* entry : paths)
				layer.draw(entry->shape, qBound(0, entry->runnability.percent, 100));
			forEachPixel(layer.done(), [&terrain](std::size_t i, uchar value) {
				if (value != nothing)
				{
					terrain.runnability[i] = value;
					// A road or a path through what is impassable can be run.
					terrain.impassable[i] = 0;
				}
			});
		}
	}
	return terrain;
}


bool saveRouteTerrain(const RouteTerrain& terrain, const QString& base_path)
{
	if (terrain.isEmpty())
		return false;
	auto const write = [&terrain](const std::vector<quint8>& values, int scale, const QString& path) {
		QImage image(terrain.width, terrain.height, QImage::Format_Grayscale8);
		for (int y = 0; y < terrain.height; ++y)
		{
			auto* line = image.scanLine(y);
			auto const row = std::size_t(y) * std::size_t(terrain.width);
			for (int x = 0; x < terrain.width; ++x)
				line[x] = uchar(std::min(255, values[row + std::size_t(x)] * scale));
		}
		if (!image.save(path + QLatin1String(".png"), "PNG"))
			return false;
		QFile world(path + QLatin1String(".pgw"));
		if (!world.open(QIODevice::WriteOnly | QIODevice::Text))
			return false;
		QTextStream stream(&world);
		stream.setRealNumberNotation(QTextStream::FixedNotation);
		stream.setRealNumberPrecision(6);
		// In mm of the map, whose y axis points down as the rows do.
		stream << terrain.pixel << '\n' << 0.0 << '\n' << 0.0 << '\n' << terrain.pixel << '\n'
		       << terrain.area.left() + terrain.pixel / 2 << '\n' << terrain.area.top() + terrain.pixel / 2 << '\n';
		return true;
	};
	return write(terrain.runnability, 1, base_path + QLatin1String(".~run"))
	       && write(terrain.impassable, 255, base_path + QLatin1String(".~imp"));
}


}  // namespace OpenOrienteering
