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

#ifndef FOOM_OPS_WKT_H
#define FOOM_OPS_WKT_H

#include <vector>

#include <QString>

#include "core/ops/op.h"

namespace OpenOrienteering {

class Map;
class Object;
class Symbol;

namespace ops {

/**
 * Parameters of toWkt() and fromWkt().
 */
struct WktParams : public OpParams
{
	/** Projected coordinates in metres; otherwise mm on paper with y up. */
	bool projected = true;
	
	/** The symbols of pasted points, lines and polygons. */
	const Symbol* point_symbol = nullptr;
	const Symbol* line_symbol = nullptr;
	const Symbol* area_symbol = nullptr;
};


/**
 * Returns the objects as well-known text, one geometry per line ("copy-wkt").
 *
 * See the design notes, "Well-known text".
 */
QString toWkt(const Map& map, const std::vector<const Object*>& objects, const WktParams& params);

/**
 * Makes objects from well-known text; consumes nothing ("paste-wkt").
 *
 * See the design notes, "Well-known text".
 */
OpResult fromWkt(const Map& map, const QString& text, const WktParams& params);


}  // namespace ops

}  // namespace OpenOrienteering

#endif
