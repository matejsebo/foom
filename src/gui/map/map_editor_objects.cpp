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


// foom: the editor's object commands and exports which belong to no phase of
// the implementation order; see the design notes, "Object
// commands", and the design notes, "Export". They are
// kept out of map_editor.cpp so that it changes little.

#include "map_editor.h"

#include <vector>

#include <QRectF>

#include "core/map.h"
#include "core/map_part.h"
#include "core/objects/object.h"
#include "core/ops/crop.h"
#include "core/symbols/symbol.h"
#include "gui/main_window.h"
#include "gui/map/crop_dialog.h"
#include "gui/widgets/symbol_widget.h"


namespace OpenOrienteering {

void MapEditorController::cropClicked()
{
	bool have_shapes = false;
	bool have_extent = false;
	QRectF extent;
	for (auto const* object : map->selectedObjects())
	{
		extent = have_extent ? extent.united(object->getExtent()) : object->getExtent();
		have_extent = true;
		have_shapes |= object->getType() == Object::Path;
	}
	if (!have_extent)
		extent = map->calculateExtent(false, false, nullptr);
	CropDialog dialog(have_shapes, extent, window);
	if (dialog.exec() == QDialog::Accepted)
		crop(dialog.selectedSymbolsOnly(), dialog.useRectangle(), dialog.rectangle(), dialog.hole(), dialog.deleteShapes());
}

void MapEditorController::crop(bool selected_symbols, bool use_rectangle, const QRectF& rect, bool hole, bool delete_shapes)
{
	// the design notes, "Crop".
	ops::CropParams params;
	params.hole = hole;
	if (use_rectangle)
	{
		params.rect = rect;
	}
	else
	{
		for (auto const* object : map->selectedObjects())
		{
			if (object->getType() == Object::Path)
				params.shapes.push_back(object->asPath());
		}
		params.delete_shapes = delete_shapes;
	}
	
	std::vector<const Object*> in;
	auto const* part = map->getCurrentPart();
	for (int i = 0; i < part->getNumObjects(); ++i)
	{
		auto const* object = part->getObject(i);
		auto const* symbol = object->getSymbol();
		if (!use_rectangle && map->isObjectSelected(object))
			continue;
		if (symbol && (symbol->isHidden() || symbol->isProtected()))
			continue;
		if (selected_symbols && !(symbol_widget && symbol_widget->isSymbolSelected(symbol)))
			continue;
		in.push_back(object);
	}
	applyOpResult(ops::crop(*map, in, params));
}

}  // namespace OpenOrienteering
