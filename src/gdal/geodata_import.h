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


#ifndef OPENORIENTEERING_GEODATA_IMPORT_H
#define OPENORIENTEERING_GEODATA_IMPORT_H

#include <vector>

#include <QtGlobal>
#include <QPointF>
#include <QPolygonF>
#include <QString>
#include <QStringList>

class QRectF;

namespace OpenOrienteering {

class Map;


/**
 * foom: One layer of a geodata file.
 *
 * See the design notes, "Import options".
 */
struct GeodataLayer
{
	enum Geometry { Point, Line, Polygon, Mixed };
	
	QString name;
	Geometry geometry = Mixed;
	qint64 features = 0;
};


/**
 * foom: The options of the geodata import (`import-geodata`).
 *
 * See the design notes, "Import options".
 */
struct GeodataImportOptions
{
	enum CrsMode
	{
		CrsDetected,      ///< Each layer's own system; none means the map's projected coordinates.
		CrsOverride,      ///< Every layer in crs_spec.
		CrsUnreferenced,  ///< Plain numbers, scaled by the units, then rotated and offset.
	};
	
	enum Units
	{
		MetresOnGround,      ///< A unit is a metre on the ground.
		MillimetresOnPaper,  ///< A unit is a millimetre on paper.
		CustomMillimetres,   ///< A unit is custom_mm_per_unit millimetres on paper.
	};
	
	enum LayerTag
	{
		NoLayerTag,
		LayerNameTag,  ///< Tag `layer_name` holds the OGR layer name.
		AttributeTag,  ///< Tag `layer_name` holds the value of layer_tag_attribute.
	};
	
	QStringList layers;                ///< The layers to read; empty means all.
	CrsMode crs_mode = CrsDetected;
	QString crs_spec;                  ///< For CrsOverride: an EPSG code ("EPSG:32613") or a PROJ string.
	Units units = MetresOnGround;
	double custom_mm_per_unit = 1.0;
	QPointF offset_mm;                 ///< Paper millimetres, y up.
	double rotation_deg = 0.0;         ///< Counter-clockwise about the data origin.
	QPolygonF clip;                    ///< Map coordinates in millimetres; empty means no clipping.
	LayerTag layer_tag = NoLayerTag;
	QString layer_tag_attribute;
	QString text_attribute;            ///< Point features with this attribute also get a text object.
	bool blocks_as_points = false;     ///< DXF block references as point objects.
	double simplify_mm = 0.0;          ///< Tolerance; 0 means off.
	QString crt_file;                  ///< Applied with the target map's symbols; empty means default symbols.
};


/**
 * foom: The outcome of importGeodata().
 */
struct GeodataImportResult
{
	bool ok = false;
	int objects = 0;      ///< The number of objects added to the map.
	QStringList warnings;
	QString error;        ///< Set when ok is false.
};


/**
 * foom: Lists the layers of a geodata file.
 *
 * With blocks_as_points, a DXF file is opened as the import opens it with
 * that option, so that its layer of block definitions appears.
 */
std::vector<GeodataLayer> geodataLayers(const QString& path, QString* error = nullptr, bool blocks_as_points = false);

/**
 * foom: Returns the attribute names of all layers of a geodata file.
 */
QStringList geodataAttributes(const QString& path);

/**
 * foom: Returns the name of the first layer's coordinate system, or an empty string.
 */
QString geodataCrsName(const QString& path);

/**
 * foom: Returns the map polygon of a box given in the map's projected coordinates.
 */
QPolygonF geodataClipBox(const Map& map, const QRectF& projected_box);

/**
 * foom: Returns the paper millimetres per data unit of an unreferenced import.
 */
double geodataMmPerUnit(const GeodataImportOptions& options, unsigned int scale_denominator);

/**
 * foom: Imports geodata files into the map as one undo step.
 *
 * The data is read with OOM's OGR importer and the options, clipped,
 * simplified, given the symbols of the CRT file, and added to the map.
 */
GeodataImportResult importGeodata(Map& map, const QStringList& paths, const GeodataImportOptions& options);


}  // namespace OpenOrienteering

#endif
