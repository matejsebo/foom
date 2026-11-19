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


#ifndef OPENORIENTEERING_LASSO_QUERY_H
#define OPENORIENTEERING_LASSO_QUERY_H

#include <vector>

#include "core/map_coord.h"

namespace OpenOrienteering {

class MapPart;
class Object;


/**
 * Finds the objects of the part which a lasso drawn as the polygon selects.
 *
 * This is the rectangle-selection rule of MapPart::findObjectsAtBox applied
 * to a polygon: a point inside; a path with a vertex of its flattened outline
 * inside or a segment crossing the polygon, or an area holding the polygon's
 * first vertex; a text whose extent has a corner inside, an edge crossed or
 * the polygon's first vertex within.
 *
 * See the design notes, "Lasso selection".
 */
void findObjectsInPolygon(
        const MapPart& part,
        const std::vector<MapCoordF>& polygon,
        bool include_hidden_objects,
        bool include_protected_objects,
        std::vector<Object*>& out
);


}  // namespace OpenOrienteering

#endif
