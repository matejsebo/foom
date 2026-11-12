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


#ifndef OPENORIENTEERING_SPATIAL_INDEX_H
#define OPENORIENTEERING_SPATIAL_INDEX_H

#include <cstddef>
#include <unordered_map>
#include <vector>

#include <QtGlobal>
#include <QRectF>

// IWYU pragma: no_forward_declare QRectF

namespace OpenOrienteering {

class Object;


/**
 * A uniform grid index over the extents of the objects of one map part.
 *
 * The index answers "which objects may overlap this rectangle" from a hash
 * of 5 mm cells to the objects whose extent touches the cell. An object
 * whose extent touches more than 4,096 cells is kept in a separate list of
 * large objects which every query scans. A side table records the cell
 * range of every object so that it can leave the cells again.
 *
 * Maintenance is lazy. An object which is added, or marked stale after its
 * geometry or symbol changed, is (re-)entered at the next query, with the
 * extent it has then (Object::update() is called first). MapPart owns one
 * index per part and drives it from the places where objects enter or
 * leave the part; Object::setOutputDirty() reports changes through
 * Map::markIndexStale().
 *
 * Candidates are a superset: callers refine them with exact geometry tests.
 * See the design notes, section "Spatial index".
 */
class SpatialIndex
{
public:
	/** The side length of a cell in mm. */
	static constexpr qreal cellSize() { return 5.0; }
	
	/** Objects touching more cells than this are kept in the large list. */
	static constexpr qint64 largeThreshold() { return 4096; }
	
	
	SpatialIndex();
	
	SpatialIndex(const SpatialIndex&) = delete;
	SpatialIndex& operator=(const SpatialIndex&) = delete;
	
	~SpatialIndex();
	
	
	/**
	 * Adds an object which is entered lazily at the next query.
	 * 
	 * Does nothing but mark the object stale if it is already in the index.
	 */
	void add(Object* object);
	
	/**
	 * Enters an object right away, with its current extent.
	 * 
	 * The object's output must be up to date (Object::update()).
	 * An object which is already in the index leaves its old cells first.
	 */
	void enter(Object* object);
	
	/**
	 * Marks an object for re-entry at the next query.
	 * 
	 * Does nothing for objects which are not in the index.
	 */
	void markStale(const Object* object);
	
	/**
	 * Removes an object from the index.
	 * 
	 * Does nothing for objects which are not in the index.
	 */
	void remove(const Object* object);
	
	/**
	 * Removes all objects.
	 */
	void clear();
	
	
	/**
	 * Returns the objects whose extent may overlap the rectangle.
	 * 
	 * Re-enters the stale objects first. The result holds no duplicates
	 * and is appended to out. Every object whose extent intersects the
	 * rectangle is returned, and possibly more.
	 */
	void query(const QRectF& rect_mm, std::vector<Object*>& out);
	
	
	/** Tests if the object is in the index (entered or stale). */
	bool contains(const Object* object) const;
	
	/** Returns the number of objects in the index, stale ones included. */
	std::size_t size() const;
	
	/** Returns the number of objects in the large list. */
	std::size_t numLarge() const;
	
	/** Returns the number of objects waiting for re-entry. */
	std::size_t numStale() const;
	
	/** Returns the number of occupied cells. */
	std::size_t numCells() const;
	
	
	/**
	 * Returns the range of cells covered by a rectangle: x0, y0, x1, y1 (inclusive).
	 */
	static void cellRange(const QRectF& rect, qint32& x0, qint32& y0, qint32& x1, qint32& y1);
	
private:
	struct CellKey
	{
		qint32 x;
		qint32 y;
		
		bool operator==(const CellKey& other) const { return x == other.x && y == other.y; }
	};
	
	struct CellKeyHash
	{
		std::size_t operator()(const CellKey& key) const;
	};
	
	/** The side table row of an object. */
	struct Entry
	{
		Object* object;
		qint32 x0 = 0;
		qint32 y0 = 0;
		qint32 x1 = -1;
		qint32 y1 = -1;
		bool entered = false;  ///< In the cells or in the large list
		bool large = false;    ///< In the large list (only when entered)
		bool stale = false;    ///< In the stale list
		quint64 stamp = 0;     ///< The last query which returned the object
	};
	
	using Cell = std::vector<Entry*>;
	
	void enterEntry(Entry& entry);
	void leaveCells(Entry& entry);
	void reenterStale();
	
	/// The side table. The container is node-based, so the addresses of the entries are stable.
	std::unordered_map<const Object*, Entry> entries;
	std::unordered_map<CellKey, Cell, CellKeyHash> cells;
	std::vector<Entry*> large;
	std::vector<const Object*> stale;
	quint64 query_stamp = 0;
};



// ## SpatialIndex inline code ###

inline
bool SpatialIndex::contains(const Object* object) const
{
	return entries.find(object) != entries.end();
}

inline
std::size_t SpatialIndex::size() const
{
	return entries.size();
}

inline
std::size_t SpatialIndex::numLarge() const
{
	return large.size();
}

inline
std::size_t SpatialIndex::numStale() const
{
	return stale.size();
}

inline
std::size_t SpatialIndex::numCells() const
{
	return cells.size();
}


}  // namespace OpenOrienteering

#endif
