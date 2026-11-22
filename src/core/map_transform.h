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

#include <QByteArray>
#include <QPointF>
#include <QPolygonF>
#include <QRectF>
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
	bool used = true;  ///< Pairs which are not used are left out of the fit and the field.
};


/**
 * The transformation fitted to the used point pairs, and the residual of each pair
 * (used or not) in mm.
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
 * The map's area and its surroundings in projected coordinates, for a map of the surroundings.
 *
 * See the design notes, "The map of the surroundings".
 */
struct MapSurroundings
{
	QPolygonF outline;  ///< The corners of the objects' bounding box: top left, top right, bottom right, bottom left on paper; empty without objects
	QRectF extent;      ///< The area to show (left/top = min x/y); null without a projected system
};

/// The outline of the map's objects and the extent around it, both projected.
MapSurroundings mapSurroundings(const Map& map);

/**
 * Moves everything so that the projected point comes to the map's origin, and the
 * georeferencing's map reference point with it.
 * One undo step.
 */
bool centerMap(Map& map, const QPointF& offset);

/**
 * Fits a transformation to the point pairs: one pair a translation, two a similarity,
 * three the affine transformation through them, more the least-squares affine one.
 * Only the used pairs count; every pair gets a residual against the fit.
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
	std::vector<std::vector<MapCoordF>> perimeter;  ///< Closed polygons in map coordinates; inside is inside any.
	bool decrease_near_perimeter = false;  ///< Adds a zero displacement weighted 4 / e^2, e the distance to the nearest polygon.
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
 * What a warp moved: the largest and the mean displacement of the moved object
 * coordinates in map millimetres, and their number.
 */
struct WarpReport
{
	double largest = 0.0;
	double mean = 0.0;
	int count = 0;
};

/**
 * Moves every coordinate inside the perimeter by the displacement at it, texts as a whole
 * by the displacement at their anchors, as one undo step. Returns false without used pairs or perimeter.
 */
bool rubbersheetMap(Map& map, const RubbersheetParams& params, WarpReport* report = nullptr);

/// A points file: system name, "m", count, then x-old;y-old;x-new;y-new;used;distance in projected metres.
bool writeRubbersheetPoints(const QString& path, const Map& map, const std::vector<PointPair>& pairs, QString* error);
/// The pairs of a points file in map coordinates; those with used 0 are not used.
std::vector<PointPair> readRubbersheetPoints(const QString& path, const Map& map, QString* error);

/// A perimeter file: system name, "m", the number of polygons, then x;y per vertex, each polygon closed by its first vertex.
bool writeRubbersheetPerimeter(const QString& path, const Map& map, const std::vector<std::vector<MapCoordF>>& perimeter, QString* error);
/// The polygons of a perimeter file, in map coordinates.
std::vector<std::vector<MapCoordF>> readRubbersheetPerimeter(const QString& path, const Map& map, QString* error);


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
 * With an offset, the map's origin comes to that projected point of the target system
 * in the same step, the map reference point moving with the objects.
 *
 * See the design notes, "Change of coordinate system".
 */
bool reprojectMap(Map& map, const QString& id, const QString& spec, const std::vector<QString>& params = {}, const QPointF* offset = nullptr);

/**
 * The offset proposed for the target system: the old origin's position in it,
 * rounded to whole kilometres. The arguments as for reprojectMap().
 */
QPointF proposedReprojectOffset(const Georeferencing& georef, const QString& id, const QString& spec, const std::vector<QString>& params, bool* ok = nullptr);


/**
 * The magnetic field of a lookup at the BGS service of OOM's georeferencing dialog.
 */
struct GeomagneticField
{
	double declination = 0.0;           ///< Degrees east.
	double horizontal_intensity = 0.0;  ///< nT.
	double yearly_change = 0.0;         ///< Degrees per year.
	bool has_declination = false;
	bool has_horizontal_intensity = false;
	bool has_yearly_change = false;
};

/// Reads the service's XML reply.
GeomagneticField parseGeomagneticReply(const QByteArray& xml);

/// The uncertainty of the declination in degrees, by the World Magnetic Model 2025's error model.
double declinationUncertainty(double horizontal_intensity);


}  // namespace OpenOrienteering

#endif
