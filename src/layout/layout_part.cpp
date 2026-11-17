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


#include "layout_part.h"

#include <algorithm>
#include <cstddef>
#include <iterator>

#include <QtGlobal>
#include <QCoreApplication>
#include <QHash>
#include <QString>
#include <QTransform>

#include "core/map.h"
#include "core/map_part.h"
#include "core/objects/object.h"
#include "fileformats/file_format.h"
#include "fileformats/file_format_registry.h"
#include "fileformats/file_import_export.h"
#include "undo/map_part_undo.h"
#include "undo/object_undo.h"
#include "undo/undo.h"
#include "undo/undo_manager.h"


namespace OpenOrienteering {

namespace {

QString tr(const char* text)
{
	return QCoreApplication::translate("OpenOrienteering::LayoutPart", text);
}

void setError(QString* error, const QString& message)
{
	if (error)
		*error = message;
}

/** The symbols the objects of the part use, as a filter on the map's symbols. */
std::vector<bool> symbolsOfPart(const Map& map, const MapPart& part)
{
	std::vector<bool> filter(std::size_t(map.getNumSymbols()), false);
	for (int i = 0; i < part.getNumObjects(); ++i)
	{
		auto const index = map.findSymbolIndex(part.getObject(i)->getSymbol());
		if (index >= 0)
			filter[std::size_t(index)] = true;
	}
	return filter;
}

}  // namespace


QString layoutPartName()
{
	return tr("Layout");
}

int layoutPartIndex(const Map& map)
{
	return map.findPartIndexWithRole(MapPart::Role::Layout);
}

int mapContentPartIndex(const Map& map)
{
	return map.findPartIndexWithRole(MapPart::Role::Map);
}

int addLayoutPart(Map& map)
{
	auto const existing = layoutPartIndex(map);
	if (existing >= 0)
		return existing;
	
	auto* part = new MapPart(layoutPartName(), &map);
	part->setRole(MapPart::Role::Layout);
	map.addPart(part, std::size_t(map.getNumParts()));
	map.setCurrentPart(part);
	map.push(new MapPartUndoStep(&map, MapPartUndoStep::RemoveMapPart, part));
	return map.findPartIndex(part);
}

bool setLayoutVisible(Map& map, bool visible)
{
	auto const index = layoutPartIndex(map);
	if (index < 0)
		return false;
	if (!visible && int(map.getCurrentPartIndex()) == index)
	{
		auto const target = mapContentPartIndex(map);
		if (target >= 0)
			map.setCurrentPartIndex(std::size_t(target));
	}
	map.getPart(std::size_t(index))->setVisible(visible);
	return true;
}

int moveSelectionToPart(Map& map, int target)
{
	// As MapEditorController::reassignObjectsToMapPart()
	auto const current = map.getCurrentPartIndex();
	if (target < 0 || target >= map.getNumParts() || std::size_t(target) == current || map.getNumSelectedObjects() == 0)
		return 0;
	
	auto* current_part = map.getPart(current);
	std::vector<int> objects(map.selectedObjects().size());
	std::transform(map.selectedObjectsBegin(), map.selectedObjectsEnd(), begin(objects), [current_part](auto* object) {
		return current_part->findObjectIndex(object);
	});
	std::sort(objects.rbegin(), objects.rend());
	map.reassignObjectsToMapPart(begin(objects), end(objects), current, std::size_t(target));
	
	auto* undo = new SwitchPartUndoStep(&map, target, int(current));
	for (auto i : objects)
		undo->addObject(i);
	map.push(undo);
	return int(objects.size());
}

bool isMapContent(const MapPart& part)
{
	return part.role() != MapPart::Role::Layout;
}

void applyOnMapContent(const Map& map, const std::function<void (const Object*)>& operation, const std::function<bool (const Object*)>& condition)
{
	for (int i = 0; i < map.getNumParts(); ++i)
	{
		auto const* part = map.getPart(std::size_t(i));
		if (isMapContent(*part))
			part->applyOnMatchingObjects(operation, condition);
	}
}

void determineMapContentSymbolsInUse(const Map& map, std::vector<bool>& out)
{
	out.assign(std::size_t(map.getNumSymbols()), false);
	for (int i = 0; i < map.getNumParts(); ++i)
	{
		auto const* part = map.getPart(std::size_t(i));
		if (!isMapContent(*part))
			continue;
		auto const used = symbolsOfPart(map, *part);
		std::transform(begin(out), end(out), begin(used), begin(out), [](bool a, bool b) { return a || b; });
	}
	map.determineSymbolUseClosure(out);
}

std::unique_ptr<Map> extractLayout(const Map& map)
{
	auto const index = layoutPartIndex(map);
	if (index < 0)
		return {};
	auto const* layout = map.getPart(std::size_t(index));
	
	auto result = std::make_unique<Map>();
	result->setGeoreferencing(map.getGeoreferencing());
	auto filter = symbolsOfPart(map, *layout);
	auto const symbol_map = result->importMap(map, Map::MinimalSymbolImport, &filter, -1, false);
	
	auto* part = result->getPart(0);
	part->setName(layout->getName());
	part->setRole(MapPart::Role::Layout);
	part->importPart(layout, symbol_map, QTransform(), false);
	result->undoManager().clear();
	return result;
}

bool saveLayout(const Map& map, const QString& path, QString* error)
{
	auto const* format = FileFormats.findFormatForFilename(path, &FileFormat::supportsFileSaveAs);
	if (!format || qstrcmp(format->id(), "XML") != 0)
	{
		setError(error, tr("A layout is saved as a .foom, .omap or .xmap file."));
		return false;
	}
	auto layout = extractLayout(map);
	if (!layout)
	{
		setError(error, tr("The map has no layout part."));
		return false;
	}
	auto exporter = format->makeExporter(path, layout.get(), nullptr);
	if (!exporter || !exporter->doExport())
	{
		auto message = tr("Cannot write the file %1.").arg(path);
		if (exporter && !exporter->warnings().empty())
			message = exporter->warnings().back();
		setError(error, message);
		return false;
	}
	return true;
}

int importLayout(Map& map, const Map& source, QString* error)
{
	auto const source_index = layoutPartIndex(source);
	if (source_index < 0)
	{
		setError(error, tr("The file has no layout part."));
		return -1;
	}
	auto const* source_part = source.getPart(std::size_t(source_index));
	if (source_part->getNumObjects() == 0)
		return 0;
	
	auto filter = symbolsOfPart(source, *source_part);
	auto const symbol_map = map.importMap(source, Map::MinimalSymbolImport, &filter, -1, true);
	
	auto* undo = new CombinedUndoStep(&map);
	auto index = layoutPartIndex(map);
	if (index < 0)
	{
		auto* part = new MapPart(layoutPartName(), &map);
		part->setRole(MapPart::Role::Layout);
		map.addPart(part, std::size_t(map.getNumParts()));
		undo->push(new MapPartUndoStep(&map, MapPartUndoStep::RemoveMapPart, part));
		index = map.findPartIndex(part);
	}
	
	// The undo step of the import refers to the current part.
	auto const current = map.getCurrentPartIndex();
	map.setCurrentPartIndex(std::size_t(index));
	auto import_undo = map.getPart(std::size_t(index))->importPart(source_part, symbol_map, QTransform(), false);
	map.setCurrentPartIndex(current);
	if (import_undo)
		undo->push(import_undo.release());
	map.push(undo);
	return source_part->getNumObjects();
}

int importLayoutFile(Map& map, const QString& path, QString* error)
{
	Map source;
	if (!source.loadFrom(path))
	{
		setError(error, tr("Cannot read the file %1.").arg(path));
		return -1;
	}
	return importLayout(map, source, error);
}


}  // namespace OpenOrienteering
