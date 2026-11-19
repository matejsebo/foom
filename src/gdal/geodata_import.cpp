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


#include "geodata_import.h"

#include <functional>
#include <memory>

#include <QByteArray>
#include <QCoreApplication>
#include <QFileInfo>
#include <QLatin1String>
#include <QRectF>
#include <QTransform>
#include <QVariant>

#include <cpl_conv.h>
#include <gdal.h>
#include <ogr_api.h>
#include <ogr_srs_api.h>

#include "core/crt_rules.h"
#include "core/georeferencing.h"
#include "core/map.h"
#include "core/map_coord.h"
#include "core/map_part.h"
#include "core/objects/object.h"
#include "core/objects/symbol_rule_set.h"
#include "core/objects/text_object.h"
#include "core/symbols/line_symbol.h"
#include "core/symbols/symbol.h"
#include "fileformats/file_import_export.h"
#include "gdal/gdal_manager.h"
#include "gdal/ogr_file_format_p.h"
#include "tools/cutout_operation.h"


namespace OpenOrienteering {

namespace {

QString tr(const char* text)
{
	return QCoreApplication::translate("OpenOrienteering::GeodataImport", text);
}

/// Sets a GDAL configuration option for this thread while in scope.
class ScopedConfigOption
{
public:
	ScopedConfigOption(const char* key, const char* value)
	: key(key)
	{
		if (auto const* old = CPLGetThreadLocalConfigOption(key, nullptr))
		{
			previous = old;
			had_previous = true;
		}
		CPLSetThreadLocalConfigOption(key, value);
	}
	
	ScopedConfigOption(const ScopedConfigOption&) = delete;
	ScopedConfigOption& operator=(const ScopedConfigOption&) = delete;
	
	~ScopedConfigOption()
	{
		CPLSetThreadLocalConfigOption(key, had_previous ? previous.constData() : nullptr);
	}
	
private:
	const char* key;
	QByteArray previous;
	bool had_previous = false;
};

std::unique_ptr<ScopedConfigOption> blocksOption(bool blocks_as_points)
{
	if (!blocks_as_points)
		return {};
	return std::make_unique<ScopedConfigOption>("DXF_INLINE_BLOCKS", "FALSE");
}

ogr::unique_datasource openVector(const QString& path)
{
	GdalManager().configure();
	return ogr::unique_datasource(OGROpen(path.toUtf8().constData(), 0, nullptr));
}

GeodataLayer::Geometry geometryOf(OGRwkbGeometryType type)
{
	switch (wkbFlatten(type))
	{
	case wkbPoint:
	case wkbMultiPoint:
		return GeodataLayer::Point;
	case wkbLineString:
	case wkbMultiLineString:
		return GeodataLayer::Line;
	case wkbPolygon:
	case wkbMultiPolygon:
		return GeodataLayer::Polygon;
	default:
		return GeodataLayer::Mixed;
	}
}

/// The importer's default text symbol, numbered 4 by OgrFileImport.
const Symbol* defaultTextSymbol(const Map& map)
{
	for (int i = 0; i < map.getNumSymbols(); ++i)
	{
		auto const* symbol = map.getSymbol(i);
		if (symbol->getType() == Symbol::Text && symbol->getNumberAsString() == QLatin1String("4"))
			return symbol;
	}
	return nullptr;
}

/// Adds a text object at every point object which has the attribute.
void addTexts(Map& map, const QString& attribute)
{
	auto const* text_symbol = defaultTextSymbol(map);
	if (!text_symbol)
		return;
	std::vector<Object*> texts;
	map.applyOnAllObjects([&](Object* object) {
		if (object->getType() != Object::Point)
			return;
		auto const text = object->getTag(attribute);
		if (text.isEmpty())
			return;
		auto* text_object = new TextObject(text_symbol);
		text_object->setAnchorPosition(object->getRawCoordinateVector().front());
		text_object->setHorizontalAlignment(TextObject::AlignHCenter);
		text_object->setVerticalAlignment(TextObject::AlignVCenter);
		text_object->setText(text);
		text_object->setTags(object->tags());
		texts.push_back(text_object);
	});
	for (auto* text_object : texts)
		map.addObject(text_object);
}

/// Cuts every object of every part to the polygon, keeping what is inside.
void clip(Map& map, const QPolygonF& polygon)
{
	MapCoordVector coords;
	coords.reserve(std::size_t(polygon.size()) + 1);
	for (auto const& point : polygon)
		coords.emplace_back(MapCoordF(point));
	coords.emplace_back(coords.front());
	coords.back().setClosePoint(true);
	PathObject outline(Map::getUndefinedLine(), coords);
	outline.update();
	
	for (int i = 0; i < map.getNumParts(); ++i)
	{
		map.setCurrentPartIndex(std::size_t(i));
		CutoutOperation operation(&map, &outline, false);
		map.getCurrentPart()->applyOnAllObjects(std::ref(operation));
		operation.commit();
	}
	map.setCurrentPartIndex(0);
}

QTransform drawingTransform(const GeodataImportOptions& options, unsigned int scale_denominator)
{
	auto const f = geodataMmPerUnit(options, scale_denominator);
	QTransform transform;
	transform.translate(options.offset_mm.x(), options.offset_mm.y());
	transform.rotate(options.rotation_deg);
	transform.scale(f, f);
	return transform;
}

}  // namespace



std::vector<GeodataLayer> geodataLayers(const QString& path, QString* error, bool blocks_as_points)
{
	std::vector<GeodataLayer> result;
	auto const blocks = blocksOption(blocks_as_points);
	auto data_source = openVector(path);
	if (!data_source)
	{
		if (error)
			*error = tr("Cannot open file\n%1:\n%2").arg(path, QString::fromUtf8(CPLGetLastErrorMsg()));
		return result;
	}
	auto const num_layers = OGR_DS_GetLayerCount(data_source.get());
	for (int i = 0; i < num_layers; ++i)
	{
		auto* layer = OGR_DS_GetLayer(data_source.get(), i);
		if (!layer)
			continue;
		GeodataLayer info;
		info.name = QString::fromUtf8(OGR_L_GetName(layer));
		info.geometry = geometryOf(OGR_L_GetGeomType(layer));
		info.features = OGR_L_GetFeatureCount(layer, TRUE);
		if (info.geometry == GeodataLayer::Mixed && info.features > 0)
		{
			// Some drivers (DXF) declare no geometry type; look at the features.
			auto geometry = GeodataLayer::Mixed;
			auto first = true;
			OGR_L_ResetReading(layer);
			while (auto feature = ogr::unique_feature(OGR_L_GetNextFeature(layer)))
			{
				auto const* g = OGR_F_GetGeometryRef(feature.get());
				if (!g)
					continue;
				auto const this_geometry = geometryOf(OGR_G_GetGeometryType(const_cast<OGRGeometryH>(g)));
				geometry = first || geometry == this_geometry ? this_geometry : GeodataLayer::Mixed;
				first = false;
			}
			info.geometry = geometry;
		}
		result.push_back(info);
	}
	return result;
}


QStringList geodataAttributes(const QString& path)
{
	QStringList result;
	auto data_source = openVector(path);
	if (!data_source)
		return result;
	auto const num_layers = OGR_DS_GetLayerCount(data_source.get());
	for (int i = 0; i < num_layers; ++i)
	{
		auto* layer = OGR_DS_GetLayer(data_source.get(), i);
		auto* definition = layer ? OGR_L_GetLayerDefn(layer) : nullptr;
		if (!definition)
			continue;
		for (int j = 0; j < OGR_FD_GetFieldCount(definition); ++j)
		{
			auto const name = QString::fromUtf8(OGR_Fld_GetNameRef(OGR_FD_GetFieldDefn(definition, j)));
			if (!result.contains(name))
				result.push_back(name);
		}
	}
	return result;
}


QString geodataCrsName(const QString& path)
{
	auto data_source = openVector(path);
	if (!data_source || OGR_DS_GetLayerCount(data_source.get()) == 0)
		return {};
	auto* layer = OGR_DS_GetLayer(data_source.get(), 0);
	auto* srs = layer ? OGR_L_GetSpatialRef(layer) : nullptr;
	if (!srs)
		return {};
	if (auto const* name = OSRGetName(srs))
		return QString::fromUtf8(name);
	return {};
}


QPolygonF geodataClipBox(const Map& map, const QRectF& projected_box)
{
	auto const& georef = map.getGeoreferencing();
	auto const box = projected_box.normalized();
	QPolygonF polygon;
	for (auto const& corner : { box.topLeft(), box.topRight(), box.bottomRight(), box.bottomLeft() })
		polygon << QPointF(georef.toMapCoordF(corner));
	return polygon;
}


double geodataMmPerUnit(const GeodataImportOptions& options, unsigned int scale_denominator)
{
	switch (options.units)
	{
	case GeodataImportOptions::MetresOnGround:
		return scale_denominator > 0 ? 1000.0 / scale_denominator : 1.0;
	case GeodataImportOptions::MillimetresOnPaper:
		return 1.0;
	case GeodataImportOptions::CustomMillimetres:
		return options.custom_mm_per_unit;
	}
	return 1.0;
}


GeodataImportResult importGeodata(Map& map, const QStringList& paths, const GeodataImportOptions& options)
{
	GeodataImportResult result;
	auto const& georef = map.getGeoreferencing();
	auto const unreferenced = options.crs_mode == GeodataImportOptions::CrsUnreferenced;
	if (!unreferenced && georef.getState() != Georeferencing::Geospatial)
	{
		result.error = tr("The map has no coordinate reference system. Import the data as unreferenced coordinates.");
		return result;
	}
	
	Map combined;
	combined.setScaleDenominator(map.getScaleDenominator());
	combined.setGeoreferencing(georef);
	
	for (auto const& path : paths)
	{
		auto const blocks = blocksOption(options.blocks_as_points);
		auto layers = options.layers;
		if (options.blocks_as_points)
		{
			// The block definitions are not features of the drawing.
			if (layers.isEmpty())
			{
				for (auto const& layer : geodataLayers(path, nullptr, true))
					layers.push_back(layer.name);
			}
			layers.removeAll(QStringLiteral("blocks"));
			if (layers.isEmpty())
				continue;
		}
		
		Map file_map;
		file_map.setScaleDenominator(map.getScaleDenominator());
		file_map.setGeoreferencing(georef);
		OgrFileImport importer(path, &file_map, nullptr, unreferenced ? OgrFileImport::UnitOnPaper : OgrFileImport::UnitOnGround);
		importer.setGeoreferencingImportEnabled(false);
		importer.setOption(QStringLiteral("Clip layers"), false);
		importer.setOption(QStringLiteral("foom layers"), layers);
		importer.setOption(QStringLiteral("foom unreferenced"), unreferenced);
		importer.setOption(QStringLiteral("foom layer name tag"), options.layer_tag == GeodataImportOptions::LayerNameTag);
		if (options.crs_mode == GeodataImportOptions::CrsOverride)
			importer.setOption(QStringLiteral("foom srs"), options.crs_spec);
		if (unreferenced)
			importer.setOption(QStringLiteral("foom drawing transform"), drawingTransform(options, map.getScaleDenominator()));
		
		if (!importer.doImport())
		{
			result.error = importer.warnings().empty()
			               ? tr("Cannot open file\n%1:\n%2").arg(path, QString::fromUtf8(CPLGetLastErrorMsg()))
			               : importer.warnings().back();
			return result;
		}
		for (auto const& warning : importer.warnings())
			result.warnings.push_back(warning);
		
		if (options.layer_tag == GeodataImportOptions::AttributeTag && !options.layer_tag_attribute.isEmpty())
		{
			file_map.applyOnAllObjects([&options](Object* object) {
				auto const value = object->getTag(options.layer_tag_attribute);
				if (!value.isEmpty())
					object->setTag(QStringLiteral("layer_name"), value);
			});
		}
		if (!options.text_attribute.isEmpty())
			addTexts(file_map, options.text_attribute);
		
		combined.importMap(file_map, Map::MinimalObjectImport | Map::GeorefImport);
	}
	
	if (options.clip.size() >= 3)
		clip(combined, options.clip);
	
	if (options.simplify_mm > 0)
	{
		combined.applyOnAllObjects([&options](Object* object) {
			if (auto* path = object->asPath())
				path->simplify(nullptr, options.simplify_mm);
		});
	}
	
	if (!options.crt_file.isEmpty())
	{
		SymbolRuleSet rules;
		QString error;
		if (!loadCrtRules(options.crt_file, map, combined, rules, error))
		{
			result.error = error;
			return result;
		}
		std::move(rules).apply(combined, map, {});
	}
	
	auto const objects_before = map.getNumObjects();
	if (combined.getNumObjects() > 0)
		map.importMap(combined, Map::MinimalObjectImport | Map::GeorefImport);
	result.objects = map.getNumObjects() - objects_before;
	result.ok = true;
	return result;
}


}  // namespace OpenOrienteering
