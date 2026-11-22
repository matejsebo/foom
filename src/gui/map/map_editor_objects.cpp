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

#include <algorithm>
#include <vector>

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QChar>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QRectF>
#include <QString>
#include <QVBoxLayout>

#include "core/extents.h"
#include "core/map.h"
#include "core/map_printer.h"
#include "core/map_part.h"
#include "core/objects/object.h"
#include "core/ops/apply_op.h"
#include "core/ops/crop.h"
#include "core/ops/wkt.h"
#include "core/symbols/symbol.h"
#include "fileformats/file_format.h"
#include "fileformats/file_format_registry.h"
#include "fileformats/map_export.h"
#include "gui/file_dialog.h"
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

void MapEditorController::copyWktClicked()
{
	copyWkt(true);
}

QString MapEditorController::copyWkt(bool projected)
{
	// the design notes, "Well-known text".
	auto const& selection = map->selectedObjects();
	std::vector<const Object*> objects(selection.begin(), selection.end());
	ops::WktParams params;
	params.projected = projected;
	auto const text = ops::toWkt(*map, objects, params);
	if (text.isEmpty())
		return text;
	QApplication::clipboard()->setText(text);
	window->showStatusBarMessage(tr("Copied %n object(s) as WKT", nullptr, int(text.count(QLatin1Char('\n')) + 1)), 2000);
	return text;
}

void MapEditorController::pasteWktClicked()
{
	pasteWkt(QApplication::clipboard()->text(), true);
}

bool MapEditorController::pasteWkt(const QString& text, bool projected)
{
	// the design notes, "Well-known text".
	ops::WktParams params;
	params.projected = projected;
	auto const* symbol = symbol_widget ? symbol_widget->getSingleSelectedSymbol() : nullptr;
	if (symbol && symbol->getType() == Symbol::Point)
		params.point_symbol = symbol;
	if (symbol && (symbol->getType() == Symbol::Line || symbol->getType() == Symbol::Area || symbol->getType() == Symbol::Combined))
		params.line_symbol = params.area_symbol = symbol;
	auto result = ops::fromWkt(*map, text, params);
	if (!result.error.isEmpty())
	{
		QMessageBox::warning(window, tr("Paste WKT"), result.error + QLatin1Char(' ')
		                     + tr("Points need a point symbol, lines and polygons a line or area symbol, selected in the symbol pane."));
		return false;
	}
	auto const count = int(result.out.size());
	if (!ops::applyOp(*map, std::move(result)))
		return false;
	window->showStatusBarMessage(tr("Pasted %n object(s)", nullptr, count), 2000);
	return true;
}

void MapEditorController::exportSvgClicked()
{
	auto path = FileDialog::getSaveFileName(window, tr("Export SVG"), {}, tr("SVG") + QLatin1String(" (*.svg)"));
	if (path.isEmpty())
		return;
	if (!path.endsWith(QLatin1String(".svg"), Qt::CaseInsensitive))
		path += QLatin1String(".svg");
	if (exportSvg(path))
		window->showStatusBarMessage(tr("Exported successfully to %1").arg(path), 4000);
}

bool MapEditorController::exportSvg(const QString& path)
{
	// the design notes, "SVG export".
	QString error;
	if (!OpenOrienteering::exportSvg(*map, path, map->printerConfig().print_area, &error))
	{
		QMessageBox::warning(window, tr("Error"), error);
		return false;
	}
	return true;
}

}  // namespace OpenOrienteering
