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


#include "spatial_index.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

#include "core/map_coord.h"
#include "core/objects/object.h"


namespace OpenOrienteering {

namespace {

constexpr qint32 cell_coord_min = std::numeric_limits<qint32>::min() / 2;
constexpr qint32 cell_coord_max = std::numeric_limits<qint32>::max() / 2;

/// The cell coordinate of a map coordinate (in mm), clamped to a safe range.
qint32 cellCoord(qreal value)
{
	if (std::isnan(value))
		return 0;
	return qint32(qBound(qreal(cell_coord_min), std::floor(value / SpatialIndex::cellSize()), qreal(cell_coord_max)));
}

/// The rectangle an object is entered for.
///
/// This is the extent, grown to include the first coordinate for point
/// objects: Object::isPointOnObject() tests the distance to that
/// coordinate for points, and nothing else, so the coordinate must be
/// covered even when the symbol draws nothing there.
void indexedRect(const Object* object, qreal& left, qreal& top, qreal& right, qreal& bottom)
{
	auto const& extent = object->getExtent();
	left = extent.left();
	top = extent.top();
	right = extent.right();
	bottom = extent.bottom();
	if (object->getType() == Object::Point)
	{
		auto const& coords = object->getRawCoordinateVector();
		if (!coords.empty())
		{
			auto const pos = MapCoordF(coords.front());
			if (!extent.isValid())
			{
				left = right = pos.x();
				top = bottom = pos.y();
			}
			else
			{
				left = qMin(left, pos.x());
				top = qMin(top, pos.y());
				right = qMax(right, pos.x());
				bottom = qMax(bottom, pos.y());
			}
		}
	}
}

}  // namespace



std::size_t SpatialIndex::CellKeyHash::operator()(const CellKey& key) const
{
	auto v = (quint64(quint32(key.x)) << 32) | quint64(quint32(key.y));
	// Finalizer of MurmurHash3, to spread neighbouring cells over the buckets.
	v ^= v >> 33;
	v *= 0xff51afd7ed558ccdULL;
	v ^= v >> 33;
	v *= 0xc4ceb9fe1a85ec53ULL;
	v ^= v >> 33;
	return std::size_t(v);
}



SpatialIndex::SpatialIndex() = default;

SpatialIndex::~SpatialIndex() = default;



void SpatialIndex::cellRange(const QRectF& rect, qint32& x0, qint32& y0, qint32& x1, qint32& y1)
{
	auto const r = rect.normalized();
	x0 = cellCoord(r.left());
	y0 = cellCoord(r.top());
	x1 = qMax(x0, cellCoord(r.right()));
	y1 = qMax(y0, cellCoord(r.bottom()));
}



void SpatialIndex::add(Object* object)
{
	auto const inserted = entries.emplace(object, Entry{object});
	auto& entry = inserted.first->second;
	if (!entry.stale)
	{
		entry.stale = true;
		stale.push_back(object);
	}
}

void SpatialIndex::enter(Object* object)
{
	auto const inserted = entries.emplace(object, Entry{object});
	enterEntry(inserted.first->second);
}

void SpatialIndex::markStale(const Object* object)
{
	auto const found = entries.find(object);
	if (found != entries.end() && !found->second.stale)
	{
		found->second.stale = true;
		stale.push_back(object);
	}
}

void SpatialIndex::remove(const Object* object)
{
	auto const found = entries.find(object);
	if (found != entries.end())
	{
		if (found->second.entered)
			leaveCells(found->second);
		// A stale entry may stay in the stale list: reenterStale() skips
		// objects it cannot find.
		entries.erase(found);
	}
}

void SpatialIndex::clear()
{
	entries.clear();
	cells.clear();
	large.clear();
	stale.clear();
}



void SpatialIndex::enterEntry(Entry& entry)
{
	if (entry.entered)
		leaveCells(entry);
		
	qreal left, top, right, bottom;
	indexedRect(entry.object, left, top, right, bottom);
	entry.x0 = cellCoord(left);
	entry.y0 = cellCoord(top);
	entry.x1 = qMax(entry.x0, cellCoord(right));
	entry.y1 = qMax(entry.y0, cellCoord(bottom));
	
	auto const num_cells = (qint64(entry.x1) - entry.x0 + 1) * (qint64(entry.y1) - entry.y0 + 1);
	if (num_cells > largeThreshold())
	{
		entry.large = true;
		large.push_back(&entry);
	}
	else
	{
		entry.large = false;
		for (auto y = entry.y0; y <= entry.y1; ++y)
		{
			for (auto x = entry.x0; x <= entry.x1; ++x)
				cells[CellKey{x, y}].push_back(&entry);
		}
	}
	entry.entered = true;
}

void SpatialIndex::leaveCells(Entry& entry)
{
	if (entry.large)
	{
		auto const found = std::find(large.begin(), large.end(), &entry);
		if (found != large.end())
		{
			*found = large.back();
			large.pop_back();
		}
	}
	else
	{
		for (auto y = entry.y0; y <= entry.y1; ++y)
		{
			for (auto x = entry.x0; x <= entry.x1; ++x)
			{
				auto const cell = cells.find(CellKey{x, y});
				if (cell == cells.end())
					continue;
				auto& members = cell->second;
				auto const found = std::find(members.begin(), members.end(), &entry);
				if (found != members.end())
				{
					*found = members.back();
					members.pop_back();
				}
				if (members.empty())
					cells.erase(cell);
			}
		}
	}
	entry.entered = false;
	entry.large = false;
}

void SpatialIndex::reenterStale()
{
	if (stale.empty())
		return;
		
	// An object's update may mark objects stale again; keep those for the next query.
	auto const pending = std::move(stale);
	stale.clear();
	for (auto const* object : pending)
	{
		auto const found = entries.find(object);
		if (found == entries.end() || !found->second.stale)
			continue;
		auto& entry = found->second;
		entry.stale = false;
		entry.object->update();
		enterEntry(entry);
	}
}



void SpatialIndex::query(const QRectF& rect_mm, std::vector<Object*>& out)
{
	reenterStale();
	
	qint32 x0, y0, x1, y1;
	cellRange(rect_mm, x0, y0, x1, y1);
	
	++query_stamp;
	auto const visit_cell = [this, &out](const Cell& cell) {
		for (auto* entry : cell)
		{
			if (entry->stamp != query_stamp)
			{
				entry->stamp = query_stamp;
				out.push_back(entry->object);
			}
		}
	};
	
	auto const num_cells = (qint64(x1) - x0 + 1) * (qint64(y1) - y0 + 1);
	if (num_cells > qint64(cells.size()))
	{
		// A rectangle wider than the occupied area: walk the occupied cells instead.
		for (auto const& item : cells)
		{
			auto const& key = item.first;
			if (key.x >= x0 && key.x <= x1 && key.y >= y0 && key.y <= y1)
				visit_cell(item.second);
		}
	}
	else
	{
		for (auto y = y0; y <= y1; ++y)
		{
			for (auto x = x0; x <= x1; ++x)
			{
				auto const cell = cells.find(CellKey{x, y});
				if (cell != cells.end())
					visit_cell(cell->second);
			}
		}
	}
	
	for (auto* entry : large)
	{
		if (entry->x1 >= x0 && entry->x0 <= x1 && entry->y1 >= y0 && entry->y0 <= y1)
			out.push_back(entry->object);
	}
}


}  // namespace OpenOrienteering
