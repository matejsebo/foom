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


#ifndef OPENORIENTEERING_ELEVATION_SET_H
#define OPENORIENTEERING_ELEVATION_SET_H

#include <array>
#include <memory>

#include <QString>

namespace OpenOrienteering {

class ElevationModel;


/**
 * The elevation grids of a map: terrain, surface and canopy height.
 *
 * Each grid is a side file named by an absolute path (href); the grid is
 * loaded on first use. The file carries one <elevation href kind/> element
 * per grid. See the design notes, "Core classes".
 */
class ElevationSet
{
public:
	enum Kind
	{
		Terrain = 0,
		Surface = 1,
		Canopy  = 2
	};
	static constexpr int kind_count = 3;

	ElevationSet();
	ElevationSet(const ElevationSet& other);
	ElevationSet& operator=(const ElevationSet& other);
	~ElevationSet();

	/// True when no grid is set.
	bool isEmpty() const;

	/// Forgets every grid.
	void clear();

	/// The absolute path of the grid, empty when none.
	QString href(Kind kind) const { return hrefs[std::size_t(kind)]; }

	/// Sets the path of the grid and drops a loaded model; an empty path removes the grid.
	void setHref(Kind kind, const QString& href);

	/// Sets a loaded grid together with its path.
	void setModel(Kind kind, std::unique_ptr<ElevationModel> model, const QString& href);

	/**
	 * The grid, loaded on first use into the given coordinate reference system.
	 *
	 * Returns nullptr when there is no grid or it cannot be opened.
	 */
	const ElevationModel* model(Kind kind, const QString& target_crs = {});

	/// The grid if it is loaded.
	const ElevationModel* loadedModel(Kind kind) const { return models[std::size_t(kind)].get(); }

	/// The file's name of the kind: terrain, surface, canopy.
	static QString kindName(Kind kind);

	/// Returns false when the name is not a kind.
	static bool kindFromName(const QString& name, Kind& kind);

	/// A path relative to the map file when the grid lies in its folder or below, otherwise absolute.
	static QString relativeHref(const QString& href, const QString& map_path);

	/// The absolute path of an href read from a map file.
	static QString absoluteHref(const QString& href, const QString& map_path);

private:
	std::array<QString, kind_count> hrefs;
	std::array<std::unique_ptr<ElevationModel>, kind_count> models;
};


}  // namespace OpenOrienteering

#endif
