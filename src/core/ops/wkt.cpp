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


#include "wkt.h"

#include <cstddef>
#include <memory>
#include <utility>

#include <QChar>
#include <QLatin1String>
#include <QPointF>
#include <QRegularExpression>
#include <QStringList>

#include "core/georeferencing.h"
#include "core/map.h"
#include "core/map_coord.h"
#include "core/objects/object.h"
#include "core/ops/flatten.h"
#include "core/path_coord.h"
#include "core/symbols/symbol.h"


namespace OpenOrienteering {

namespace ops {

namespace {

/// The tolerance of flattening curves (mm), the default of To polyline.
constexpr double flatten_tolerance = 0.01;

QString number(double value)
{
	auto text = QString::number(value, 'f', 3);
	if (text.contains(QLatin1Char('.')))
	{
		while (text.endsWith(QLatin1Char('0')))
			text.chop(1);
		if (text.endsWith(QLatin1Char('.')))
			text.chop(1);
	}
	if (text == QLatin1String("-0"))
		text = QStringLiteral("0");
	return text;
}

QString pointText(const Map& map, const MapCoordF& coord, bool projected)
{
	if (projected)
	{
		auto const p = map.getGeoreferencing().toProjectedCoords(coord);
		return number(p.x()) + QLatin1Char(' ') + number(p.y());
	}
	return number(coord.x()) + QLatin1Char(' ') + number(-coord.y());
}

/// The points of one part, curves flattened; a closed part ends at its first point.
QString partText(const Map& map, const PathObject& path, const PathPart& part, bool projected)
{
	auto const& raw = path.getRawCoordinateVector();
	MapCoordVector coords(raw.begin() + part.first_index, raw.begin() + part.last_index + 1);
	coords = flattenCoords(coords, flatten_tolerance);
	QStringList points;
	for (auto const& coord : coords)
		points.push_back(pointText(map, MapCoordF(coord), projected));
	if (part.isClosed() && !coords.empty() && MapCoordF(coords.front()) != MapCoordF(coords.back()))
		points.push_back(pointText(map, MapCoordF(coords.front()), projected));
	return QLatin1Char('(') + points.join(QStringLiteral(", ")) + QLatin1Char(')');
}

QString objectText(const Map& map, const Object& object, bool projected)
{
	auto const& coords = object.getRawCoordinateVector();
	switch (object.getType())
	{
	case Object::Point:
	case Object::Text:
		if (coords.empty())
			return {};
		return QStringLiteral("POINT (") + pointText(map, MapCoordF(coords.front()), projected) + QLatin1Char(')');
	case Object::Path:
		break;
	default:
		return {};
	}

	auto const* path = object.asPath();
	path->update();
	if (path->parts().empty())
		return {};
	QStringList parts;
	for (auto const& part : path->parts())
		parts.push_back(partText(map, *path, part, projected));
	auto const* symbol = path->getSymbol();
	if (symbol && (symbol->getContainedTypes() & Symbol::Area))
		return QStringLiteral("POLYGON (") + parts.join(QStringLiteral(", ")) + QLatin1Char(')');
	if (parts.size() == 1)
		return QStringLiteral("LINESTRING ") + parts.front();
	return QStringLiteral("MULTILINESTRING (") + parts.join(QStringLiteral(", ")) + QLatin1Char(')');
}


/// A reader of WKT text by recursive descent.
class Reader
{
public:
	Reader(const Map& map, const QString& text, const WktParams& params)
	: map(map)
	, params(params)
	{
		static const QRegularExpression srid(QStringLiteral("SRID\\s*=\\s*\\d+\\s*;"), QRegularExpression::CaseInsensitiveOption);
		static const QRegularExpression token_pattern(QStringLiteral("[A-Za-z]+|[-+]?(?:\\d+\\.?\\d*|\\.\\d+)(?:[eE][-+]?\\d+)?|\\S"));
		auto cleaned = text;
		cleaned.replace(srid, QStringLiteral(" "));
		auto it = token_pattern.globalMatch(cleaned);
		while (it.hasNext())
			tokens.push_back(it.next().captured());
	}

	OpResult read()
	{
		while (pos < tokens.size() && error.isEmpty())
		{
			if (peek() == QLatin1String(",") || peek() == QLatin1String(";"))
			{
				++pos;
				continue;
			}
			geometry();
		}
		if (error.isEmpty() && result.out.empty())
			error = QStringLiteral("The text holds no WKT geometry.");
		if (!error.isEmpty())
		{
			result.out.clear();
			result.error = error;
		}
		return std::move(result);
	}

private:
	using Ring = std::vector<MapCoordF>;

	QString peek() const
	{
		return pos < tokens.size() ? tokens[pos] : QString{};
	}

	/// Skips a comma and returns true when there is one.
	bool comma()
	{
		if (error.isEmpty() && peek() == QLatin1String(","))
		{
			++pos;
			return true;
		}
		return false;
	}

	bool expect(const char* token)
	{
		if (error.isEmpty() && peek() == QLatin1String(token))
		{
			++pos;
			return true;
		}
		fail();
		return false;
	}

	void fail()
	{
		if (error.isEmpty())
			error = QStringLiteral("The text is no WKT geometry.");
	}

	/// Skips Z, M or ZM; returns true at EMPTY, which it skips.
	bool dimensionAndEmpty()
	{
		auto token = peek().toUpper();
		if (token == QLatin1String("Z") || token == QLatin1String("M") || token == QLatin1String("ZM"))
		{
			++pos;
			token = peek().toUpper();
		}
		if (token == QLatin1String("EMPTY"))
		{
			++pos;
			return true;
		}
		return false;
	}

	MapCoordF coordinate()
	{
		bool ok_x = false;
		bool ok_y = false;
		auto const x = peek().toDouble(&ok_x);
		++pos;
		auto const y = peek().toDouble(&ok_y);
		++pos;
		if (!ok_x || !ok_y)
		{
			fail();
			return {};
		}
		// Further ordinates (z, m) are ignored.
		for (bool ok = true; ok && pos < tokens.size(); )
		{
			peek().toDouble(&ok);
			if (ok)
				++pos;
		}
		if (params.projected)
			return map.getGeoreferencing().toMapCoordF(QPointF(x, y));
		return MapCoordF(x, -y);
	}

	Ring coordinateList()
	{
		Ring ring;
		if (!expect("("))
			return ring;
		do
			ring.push_back(coordinate());
		while (comma());
		expect(")");
		return ring;
	}

	std::vector<Ring> ringList()
	{
		std::vector<Ring> rings;
		if (!expect("("))
			return rings;
		do
			rings.push_back(coordinateList());
		while (comma());
		expect(")");
		return rings;
	}

	bool needSymbol(const Symbol* symbol, const char* kind)
	{
		if (symbol)
			return true;
		if (error.isEmpty())
			error = QStringLiteral("There is no symbol for the %1 of the text.").arg(QLatin1String(kind));
		return false;
	}

	void addPoint(const MapCoordF& coord)
	{
		if (!error.isEmpty() || !needSymbol(params.point_symbol, "points"))
			return;
		auto object = std::unique_ptr<PointObject>(new PointObject(params.point_symbol));
		object->setPosition(coord);
		result.out.push_back(std::move(object));
	}

	/// Adds a path of the rings; closed rings drop their repeated end point.
	void addPath(std::vector<Ring> rings, bool area)
	{
		if (!error.isEmpty())
			return;
		auto const* symbol = area ? params.area_symbol : params.line_symbol;
		if (!needSymbol(symbol, area ? "polygons" : "lines"))
			return;
		MapCoordVector coords;
		std::vector<bool> closed;
		for (auto& ring : rings)
		{
			auto const is_closed = ring.size() > 2 && ring.front() == ring.back();
			if (is_closed)
				ring.pop_back();
			if (ring.size() < (area ? 3u : 2u))
			{
				error = area ? QStringLiteral("A ring of a polygon has fewer than three points.")
				             : QStringLiteral("A line has fewer than two points.");
				return;
			}
			if (!coords.empty())
				coords.back().setHolePoint(true);
			for (auto const& c : ring)
				coords.push_back(MapCoord(c));
			closed.push_back(area || is_closed);
		}
		if (coords.empty())
			return;
		auto object = std::unique_ptr<PathObject>(new PathObject(symbol, coords));
		auto& parts = object->parts();
		for (std::size_t i = 0; i < parts.size() && i < closed.size(); ++i)
		{
			if (closed[i])
				parts[i].setClosed(true, true);
		}
		result.out.push_back(std::move(object));
	}

	void geometry()
	{
		auto const type = peek().toUpper();
		if (type.isEmpty() || !type[0].isLetter())
		{
			fail();
			return;
		}
		++pos;
		if (dimensionAndEmpty())
			return;
		if (type == QLatin1String("POINT"))
		{
			if (expect("("))
			{
				auto const c = coordinate();
				if (expect(")"))
					addPoint(c);
			}
		}
		else if (type == QLatin1String("MULTIPOINT"))
		{
			if (!expect("("))
				return;
			do
			{
				if (peek() == QLatin1String("("))
				{
					++pos;
					auto const c = coordinate();
					if (expect(")"))
						addPoint(c);
				}
				else
				{
					addPoint(coordinate());
				}
			}
			while (comma());
			expect(")");
		}
		else if (type == QLatin1String("LINESTRING"))
		{
			addPath({ coordinateList() }, false);
		}
		else if (type == QLatin1String("MULTILINESTRING"))
		{
			addPath(ringList(), false);
		}
		else if (type == QLatin1String("POLYGON"))
		{
			addPath(ringList(), true);
		}
		else if (type == QLatin1String("MULTIPOLYGON"))
		{
			if (!expect("("))
				return;
			do
				addPath(ringList(), true);
			while (comma());
			expect(")");
		}
		else if (type == QLatin1String("GEOMETRYCOLLECTION"))
		{
			if (!expect("("))
				return;
			do
				geometry();
			while (comma());
			expect(")");
		}
		else
		{
			fail();
		}
	}

	const Map& map;
	const WktParams& params;
	QStringList tokens;
	int pos = 0;
	QString error;
	OpResult result;
};

}  // namespace


QString toWkt(const Map& map, const std::vector<const Object*>& objects, const WktParams& params)
{
	// the design notes, "Well-known text".
	QStringList lines;
	for (auto const* object : objects)
	{
		if (!object)
			continue;
		auto const text = objectText(map, *object, params.projected);
		if (!text.isEmpty())
			lines.push_back(text);
	}
	return lines.join(QLatin1Char('\n'));
}


OpResult fromWkt(const Map& map, const QString& text, const WktParams& params)
{
	// the design notes, "Well-known text".
	return Reader(map, text, params).read();
}


}  // namespace ops

}  // namespace OpenOrienteering
