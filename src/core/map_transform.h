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

#ifndef OPENORIENTEERING_MAP_TRANSFORM_H
#define OPENORIENTEERING_MAP_TRANSFORM_H

#include <vector>

#include <QString>
#include <QTransform>

#include "core/map_coord.h"

namespace OpenOrienteering {

class Georeferencing;
class Map;
class Symbol;


/**
 * Options of the map transformations.
 */
struct MapTransformOptions
{
	/// Also transform the layout parts' objects, the named extents, the bookmarks
	/// and the templates which are not georeferenced.
	bool carry = true;
	
	/// Add the rotation of the transformation's x axis to the orientation of objects
	/// whose symbol is rotatable or has a rotatable pattern.
	bool rotate_orientations = false;
	
	/// Leave the layout parts' objects in place, even when carrying the rest.
	bool keep_layout = false;
	
	/// Symbols whose objects stay in place.
	std::vector<const Symbol*> fixed_symbols;
	
	/// When set, the map takes this georeferencing in the same undo step.
	const Georeferencing* georeferencing = nullptr;
};


/**
 * A point of the map and the same point on the reference, in map coordinates.
 */
struct PointPair
{
	MapCoordF map;
	MapCoordF reference;
};


/**
 * The transformation fitted to point pairs, and the residual of each pair in mm.
 */
struct PairFit
{
	QTransform transform;
	std::vector<double> residuals;
	bool ok = false;
};


/**
 * Applies an affine transformation of map coordinates (mm, y growing south) to the map,
 * as one undo step. The georeferencing does not change.
 *
 * Objects are transformed by Object::transform(): coordinates only, rotations and the
 * order of coordinates kept, texts by their anchors. The course data's stored routes
 * always follow. Returns false for a transformation which cannot be inverted.
 *
 * See the design notes, "Move, stretch and mirror".
 */
bool transformMap(Map& map, const QTransform& transform, const MapTransformOptions& options);

/// The translation which moves the map by the given metres east and north, through its georeferencing.
QTransform mapMoveTransform(const Map& map, double east_m, double north_m);

/// Scaling by the factors, with the mirrors, about the map's origin.
QTransform mapStretchTransform(double factor_x, double factor_y, bool mirror_horizontally, bool mirror_vertically);

/// Rotation by the angle, counter-clockwise on screen, about the centre.
QTransform mapRotationTransform(const MapCoordF& centre, double degrees);

/// The angle in degrees by which rotateMapToMagneticNorth() turns the map for the declination.
double magneticNorthRotation(const Georeferencing& georef, double declination);

/**
 * Gives the map the declination and turns it by the change of grivation about the
 * georeferencing's map reference point, so that every object keeps its projected
 * coordinates. One undo step.
 *
 * See the design notes, "Rotation to magnetic north and centring".
 */
bool rotateMapToMagneticNorth(Map& map, double declination, MapTransformOptions options);

/// The projected coordinates of the map's origin (the map offset).
QPointF mapOffset(const Map& map);

/// The projected coordinates of the centre of the objects' coordinates' bounding box, rounded to whole kilometres.
QPointF proposedMapOffset(const Map& map);

/**
 * Moves everything so that the projected point comes to the map's origin, and the
 * georeferencing's map reference point with it.
 * One undo step.
 */
bool centerMap(Map& map, const QPointF& offset);

/**
 * Fits a transformation to the point pairs: one pair a translation, two a similarity,
 * three the affine transformation through them, more the least-squares affine one.
 *
 * See the design notes, "Affine by point pairs".
 */
PairFit fitPointPairs(const std::vector<PointPair>& pairs);


/**
 * The settings of rubbersheeting; the options are off by default.
 */
struct RubbersheetParams
{
	std::vector<PointPair> pairs;
	std::vector<MapCoordF> perimeter;      ///< A closed polygon in map coordinates.
	bool decrease_near_perimeter = false;  ///< Adds a zero displacement weighted 4 / e^2, e the distance to the perimeter.
	bool include_protected = false;        ///< Transform objects of protected symbols.
	bool include_layout = false;           ///< Transform the objects of layout parts.
};

/**
 * The displacement at a point: the mean of the pairs' displacements weighted
 * 1 / (|p - old| |p - new|).
 *
 * See the design notes, "Rubbersheeting".
 */
MapCoordF rubbersheetDisplacement(const RubbersheetParams& params, const MapCoordF& point);

/**
 * Moves every coordinate inside the perimeter by the displacement at it, texts as a whole
 * by the displacement at their anchors, as one undo step. Returns false without pairs or perimeter.
 */
bool rubbersheetMap(Map& map, const RubbersheetParams& params);

/// A points file: system name, "m", count, then x-old;y-old;x-new;y-new;used;distance in projected metres.
bool writeRubbersheetPoints(const QString& path, const Map& map, const std::vector<PointPair>& pairs, QString* error);
/// The pairs of a points file which are used, in map coordinates.
std::vector<PointPair> readRubbersheetPoints(const QString& path, const Map& map, QString* error);

/// A perimeter file: system name, "m", the number of polygons (1), then x;y per vertex.
bool writeRubbersheetPerimeter(const QString& path, const Map& map, const std::vector<MapCoordF>& perimeter, QString* error);
/// The first polygon of a perimeter file, in map coordinates.
std::vector<MapCoordF> readRubbersheetPerimeter(const QString& path, const Map& map, QString* error);


/**
 * One drag of the local transformation; the radius in map millimetres.
 */
struct LocalWarpParams
{
	MapCoordF center;
	MapCoordF target;
	double radius = 10.0;            ///< mm on paper.
	bool include_protected = false;  ///< Transform objects of protected symbols.
	bool include_layout = false;     ///< Transform the objects of layout parts.
};

/**
 * (target - center) * max(0, 1 - |p - center| / radius).
 *
 * See the design notes, "Local transformation".
 */
MapCoordF localWarpDisplacement(const LocalWarpParams& params, const MapCoordF& point);

/// Moves what lies within the radius, as one undo step; false when the target lies beyond the radius.
bool localWarpMap(Map& map, const LocalWarpParams& params);

/**
 * Changes the map's projected coordinate reference system, keeping every object
 * at its geographic position.
 *
 * The id, spec and params are those of Georeferencing::setProjectedCRS(). One undo step;
 * false for a map without a projected system or an invalid target.
 *
 * See the design notes, "Change of coordinate system".
 */
bool reprojectMap(Map& map, const QString& id, const QString& spec, const std::vector<QString>& params = {});


}  // namespace OpenOrienteering

#endif
