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


#include "map_export.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include <QtGlobal>
#include <QCoreApplication>
#include <QHash>
#include <QChar>
#include <QPainter>
#include <QSize>
#include <QSvgGenerator>

#include "core/map.h"
#include "core/objects/object.h"
#include "core/ops/crop.h"
#include "core/renderables/renderable.h"
#include "core/symbols/symbol.h"
#include "fileformats/file_format_registry.h"
#include "fileformats/file_import_export.h"


namespace OpenOrienteering {

namespace {

/// SVG user units per mm on paper.
constexpr double svg_units_per_mm = 100.0;

}  // namespace


bool exportSvg(Map& map, const QString& path, const QRectF& area, QString* error)
{
	// the design notes, "SVG export".
	if (!area.isValid() || area.width() <= 0 || area.height() <= 0)
	{
		if (error)
			*error = QCoreApplication::translate("OpenOrienteering::MapExport", "The area to export is empty.");
		return false;
	}
	auto const width = int(std::ceil(area.width() * svg_units_per_mm));
	auto const height = int(std::ceil(area.height() * svg_units_per_mm));

	QSvgGenerator generator;
	generator.setFileName(path);
	generator.setResolution(qRound(svg_units_per_mm * 25.4));
	generator.setSize(QSize(width, height));
	generator.setViewBox(QRectF(0, 0, width, height));

	QPainter painter;
	if (!painter.begin(&generator))
	{
		if (error)
			*error = QCoreApplication::translate("OpenOrienteering::MapExport", "Cannot write the file %1.").arg(path);
		return false;
	}
	painter.setRenderHint(QPainter::Antialiasing);
	painter.scale(svg_units_per_mm, svg_units_per_mm);
	painter.translate(-area.left(), -area.top());
	painter.setClipRect(area);
	RenderConfig config = { map, area, svg_units_per_mm, RenderConfig::NoOptions, 1.0 };
	map.draw(&painter, config);
	if (!painter.end())
	{
		if (error)
			*error = QCoreApplication::translate("OpenOrienteering::MapExport", "Cannot write the file %1.").arg(path);
		return false;
	}
	return true;
}


std::unique_ptr<Map> extractMap(const Map& map, const std::vector<const Object*>& objects, const ops::CropParams* crop)
{
	// the design notes, "Part of the map, and objects by symbol or selection".
	auto result = std::make_unique<Map>();
	result->setScaleDenominator(map.getScaleDenominator());
	result->setGeoreferencing(map.getGeoreferencing());

	std::vector<bool> filter(std::size_t(map.getNumSymbols()), false);
	for (auto const* object : objects)
	{
		auto const index = map.findSymbolIndex(object->getSymbol());
		if (index >= 0)
			filter[std::size_t(index)] = true;
	}
	auto const symbols = result->importMap(map, Map::MinimalSymbolImport, &filter, -1, true);

	std::vector<const Object*> copies;
	for (auto const* object : objects)
	{
		auto* copy = object->duplicate();
		if (symbols.contains(copy->getSymbol()))
			copy->setSymbol(symbols.value(copy->getSymbol()), true);
		result->addObject(copy);
		copies.push_back(copy);
	}

	if (crop)
	{
		auto cropped = ops::crop(*result, copies, *crop);
		if (cropped.error.isEmpty())
		{
			for (auto const* object : cropped.consumed)
			{
				// Shapes from the source map are not in this one.
				if (std::find(copies.begin(), copies.end(), object) != copies.end())
					result->deleteObject(const_cast<Object*>(object));
			}
			for (auto& object : cropped.out)
				result->addObject(object.release());
		}
	}
	return result;
}


bool saveMapFile(const Map& map, const QString& path, QString* error)
{
	auto exporter = FileFormats.makeExporter(path, &map, nullptr);
	if (!exporter)
	{
		if (error)
			*error = QCoreApplication::translate("OpenOrienteering::MapExport", "Cannot export the map as %1 since the file format is unknown.").arg(path);
		return false;
	}
	if (!exporter->doExport())
	{
		if (error)
		{
			*error = QCoreApplication::translate("OpenOrienteering::MapExport", "Cannot write the file %1.").arg(path);
			for (auto const& warning : exporter->warnings())
				*error += QLatin1Char('\n') + warning;
		}
		return false;
	}
	return true;
}


}  // namespace OpenOrienteering
