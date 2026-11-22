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


#ifndef FOOM_GENERATOR_P_H
#define FOOM_GENERATOR_P_H

#include <utility>
#include <vector>

#include <QJsonObject>
#include <QPointF>
#include <QPolygonF>
#include <QRectF>
#include <QString>

#include "core/map_coord.h"
#include "core/objects/object.h"
#include "core/objects/text_object.h"
#include "core/symbols/symbol.h"

namespace OpenOrienteering {

class Map;
class MapColor;
class TextSymbol;


/**
 * What the generators share. Not part of the interface of generator.h.
 */
namespace generator_impl {

/** What a generator made: objects, and the private symbols they use. */
struct Build
{
	std::vector<Symbol*> symbols;
	std::vector<Object*> objects;
	std::vector<std::pair<MapColor*, int>> colors;  ///< New colours and the index to insert each at.
	
	Build() = default;
	Build(const Build&) = delete;
	Build& operator=(const Build&) = delete;
	
	~Build()
	{
		// Only what was not handed over to the map.
		for (auto* object : objects)
			delete object;
		for (auto* symbol : symbols)
			delete symbol;
		for (auto& color : colors)
			delete color.first;
	}
	
	void handedOver()
	{
		objects.clear();
		symbols.clear();
		colors.clear();
	}
};

struct Context
{
	const Map& map;
	int exclude_group;  ///< The group which is being rebuilt, or 0.
};


QString tr(const char* text);
void setError(QString* error, const QString& message);

const MapColor* darkestColor(const Map& map);
const Symbol* symbolByNumber(const Map& map, const QString& number, Symbol::TypeCombination types);

/** The symbol the parameters name, or a private one made for the run. */
const Symbol* lineSymbol(const Context& c, const QJsonObject& params, Build& build);
const Symbol* areaSymbol(const Context& c, const QJsonObject& params, Build& build);
const TextSymbol* textSymbol(const Context& c, const QJsonObject& params, Build& build);

PathObject* makeLine(const Symbol* symbol, const QPointF& from, const QPointF& to);
PathObject* makeRectangle(const Symbol* symbol, const QRectF& box);
TextObject* makeText(const Symbol* symbol, const QPointF& anchor, const QString& string, TextObject::HorizontalAlignment h_align, TextObject::VerticalAlignment v_align);

/** The polygon stored as the parameter `outline`. */
QPolygonF outlineFromParams(const QJsonObject& params);

/** The bounding box of the objects in the parts with map content, without the group being rebuilt. */
QRectF mapContentExtent(const Context& c);


// The generators. Each fills the build and returns false with a message on failure.

bool buildNorthArrow(const Context& c, const QJsonObject& params, const MapCoordF& anchor, Build& build, QString* error);
bool buildGrid(const Context& c, const QJsonObject& params, const MapCoordF& anchor, Build& build, QString* error);
bool buildGraticule(const Context& c, const QJsonObject& params, const MapCoordF& anchor, Build& build, QString* error);
bool buildNameIndex(const Context& c, const QJsonObject& params, const MapCoordF& anchor, Build& build, QString* error);
bool buildTrimMarks(const Context& c, const QJsonObject& params, const MapCoordF& anchor, Build& build, QString* error);
bool buildQrCode(const Context& c, const QJsonObject& params, const MapCoordF& anchor, Build& build, QString* error);


}  // namespace generator_impl

}  // namespace OpenOrienteering

#endif
