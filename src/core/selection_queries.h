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

#ifndef FOOM_CORE_SELECTION_QUERIES_H
#define FOOM_CORE_SELECTION_QUERIES_H

#include <vector>

#include <QtGlobal>

namespace OpenOrienteering {

class MapPart;
class Object;
class Symbol;


/*
 * The selection queries. Each looks at the objects of one part, leaves
 * out objects of hidden symbols and returns the objects in the part's order.
 *
 * See the design notes, "Selection queries".
 */

/// Objects equal to an earlier object of the same type and symbol (within 0.001 mm; a path also reversed).
std::vector<Object*> findDuplicates(const MapPart& part);

/// Path objects two of whose segments meet, segments which follow one another left out.
std::vector<Object*> findSelfIntersecting(const MapPart& part);

/// Paths with too few distinct coordinates or a broken curve, and texts without text.
std::vector<Object*> findInvalid(const MapPart& part);

/**
 * Line text objects whose text is longer than their path (the layout
 * overflows, as for the map check's text-path-overflow); empty texts and
 * line texts with a single anchor are left out.
 * See the design notes, "Selecting line text whose line is too short".
 */
std::vector<Object*> findLineTextTooShort(const MapPart& part);

enum class DateKind { Created, Modified };

/// Objects created, or last modified, from one time to another (seconds since the epoch), both included.
std::vector<Object*> findByDate(const MapPart& part, DateKind kind, qint64 from, qint64 to);

/**
 * A comparison of one property of the objects with a value.
 */
struct PropertyQuery
{
	enum Property { Vertices, Length, Area, Rotation, TextLength };
	enum Compare { Equal, Less, Greater, LessEqual, GreaterEqual, NotEqual };
	
	Property property = Length;
	Compare compare = Greater;
	double value = 0.0;                   ///< Paper mm, mm², degrees, or a count.
	std::vector<const Symbol*> symbols;   ///< Only objects of these symbols; empty for all.
};

/// Objects whose property compares as asked; objects without the property never match.
std::vector<Object*> findByProperty(const MapPart& part, const PropertyQuery& query);


}  // namespace OpenOrienteering

#endif
