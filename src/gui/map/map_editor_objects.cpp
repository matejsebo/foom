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

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QChar>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QRectF>
#include <QString>
#include <QVBoxLayout>

#include "core/extents.h"
#include "core/map.h"
#include "core/map_color.h"
#include "core/map_view.h"
#include "core/map_printer.h"
#include "core/map_part.h"
#include "core/objects/object.h"
#include "core/ops/apply_op.h"
#include "core/ops/crop.h"
#include "core/ops/wkt.h"
#include "core/symbols/symbol.h"
#include "fileformats/file_format.h"
#include "fileformats/file_format_registry.h"
#include "fileformats/file_import_export.h"  // foom
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

namespace {

/// Asks for the path of a map file in one of the formats which can be saved; empty when cancelled.
QString askMapFilePath(QWidget* parent, const QString& title)
{
	QString filters;
	for (auto const* format : FileFormats.formats())
	{
		if (format->supportsFileSaveAs())
		{
			if (!filters.isEmpty())
				filters += QLatin1String(";;");
			filters += format->filter();
		}
	}
	QString filter;
	auto path = FileDialog::getSaveFileName(parent, title, {}, filters, &filter);
	auto const quirk = path.indexOf(QLatin1String(" *."));
	if (quirk >= 0)
		path.truncate(quirk);
	if (path.isEmpty())
		return path;
	if (auto const* format = FileFormats.findFormatByFilter(filter, &FileFormat::supportsFileSaveAs))
		path = format->fixupExtension(path);
	else if (QFileInfo(path).suffix().isEmpty())
		path += QLatin1String(".foom");
	return path;
}

}  // namespace


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

void MapEditorController::exportPartClicked()
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

	QDialog dialog(window);
	dialog.setWindowTitle(tr("Export part of the map"));
	auto* shapes_button = new QRadioButton(tr("The selected lines and areas"));
	auto* extent_button = new QRadioButton(tr("A named extent"));
	auto* rect_button = new QRadioButton(tr("A rectangle"));
	auto* extent_combo = new QComboBox();
	for (auto const& named : map->extents())
		extent_combo->addItem(named.name);
	shapes_button->setEnabled(have_shapes);
	extent_button->setEnabled(extent_combo->count() > 0);
	extent_combo->setEnabled(extent_combo->count() > 0);
	(have_shapes ? shapes_button : rect_button)->setChecked(true);
	auto edit = [](double value) {
		auto* e = new QDoubleSpinBox();
		e->setRange(-1000000.0, 1000000.0);
		e->setDecimals(2);
		e->setSuffix(QStringLiteral(" mm"));
		e->setValue(value);
		return e;
	};
	auto* left = edit(extent.left());
	auto* top = edit(extent.top());
	auto* right = edit(extent.right());
	auto* bottom = edit(extent.bottom());
	auto* form = new QFormLayout();
	form->addRow(shapes_button);
	form->addRow(extent_button, extent_combo);
	form->addRow(rect_button);
	form->addRow(tr("Left:"), left);
	form->addRow(tr("Top:"), top);
	form->addRow(tr("Right:"), right);
	form->addRow(tr("Bottom:"), bottom);
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	auto* layout = new QVBoxLayout(&dialog);
	layout->addLayout(form);
	layout->addWidget(buttons);
	if (dialog.exec() != QDialog::Accepted)
		return;

	QRectF rect = QRectF(QPointF(left->value(), top->value()), QPointF(right->value(), bottom->value())).normalized();
	if (extent_button->isChecked())
		rect = map->extents().at(std::size_t(extent_combo->currentIndex())).rect_mm;
	auto const path = askMapFilePath(window, tr("Export part of the map"));
	if (path.isEmpty())
		return;
	if (exportPart(path, !shapes_button->isChecked(), rect))
		window->showStatusBarMessage(tr("Exported successfully to %1").arg(path), 4000);
}

bool MapEditorController::exportPart(const QString& path, bool use_rectangle, const QRectF& rect)
{
	// the design notes, "Part of the map, and objects by symbol or selection".
	ops::CropParams crop;
	if (use_rectangle)
	{
		crop.rect = rect;
	}
	else
	{
		for (auto const* object : map->selectedObjects())
		{
			if (object->getType() == Object::Path)
				crop.shapes.push_back(object->asPath());
		}
	}
	if (crop.shapes.empty() && !(crop.rect.width() > 0 && crop.rect.height() > 0))
	{
		QMessageBox::warning(window, tr("Error"), tr("Select a line or area as the boundary, or give a rectangle."));
		return false;
	}
	std::vector<const Object*> objects;
	auto const* part = map->getCurrentPart();
	for (int i = 0; i < part->getNumObjects(); ++i)
	{
		auto const* object = part->getObject(i);
		if (std::find(crop.shapes.begin(), crop.shapes.end(), object) == crop.shapes.end())
			objects.push_back(object);
	}
	auto extracted = extractMap(*map, objects, &crop);
	QString error;
	if (!saveMapFile(*extracted, path, &error))
	{
		QMessageBox::warning(window, tr("Error"), error);
		return false;
	}
	return true;
}

void MapEditorController::exportObjectsClicked()
{
	QMessageBox box(QMessageBox::Question, tr("Export objects"), tr("Which objects are to be exported?"), QMessageBox::Cancel, window);
	auto* selection_button = box.addButton(tr("The selection"), QMessageBox::AcceptRole);
	auto* symbols_button = box.addButton(tr("The objects of the selected symbols"), QMessageBox::AcceptRole);
	selection_button->setEnabled(map->getNumSelectedObjects() > 0);
	symbols_button->setEnabled(symbol_widget && symbol_widget->selectedSymbolsCount() > 0);
	box.exec();
	if (box.clickedButton() != selection_button && box.clickedButton() != symbols_button)
		return;
	auto const path = askMapFilePath(window, tr("Export objects"));
	if (path.isEmpty())
		return;
	if (exportObjects(path, box.clickedButton() == symbols_button))
		window->showStatusBarMessage(tr("Exported successfully to %1").arg(path), 4000);
}

bool MapEditorController::exportObjects(const QString& path, bool selected_symbols)
{
	// the design notes, "Part of the map, and objects by symbol or selection".
	std::vector<const Object*> objects;
	if (selected_symbols)
	{
		auto const* part = map->getCurrentPart();
		for (int i = 0; i < part->getNumObjects(); ++i)
		{
			auto const* object = part->getObject(i);
			if (symbol_widget && symbol_widget->isSymbolSelected(object->getSymbol()))
				objects.push_back(object);
		}
	}
	else
	{
		objects.assign(map->selectedObjects().begin(), map->selectedObjects().end());
	}
	if (objects.empty())
	{
		QMessageBox::warning(window, tr("Error"), tr("There are no objects to export."));
		return false;
	}
	auto extracted = extractMap(*map, objects);
	QString error;
	if (!saveMapFile(*extracted, path, &error))
	{
		QMessageBox::warning(window, tr("Error"), error);
		return false;
	}
	return true;
}

// foom: the design notes, "DXF export through a cross-reference table"
void MapEditorController::exportDxfCrtClicked()
{
	auto const crt = FileDialog::getOpenFileName(window, tr("Cross-reference table"), {},
	                                             tr("Cross-reference tables") + QLatin1String(" (*.crt *.txt);;") + tr("All files") + QLatin1String(" (*.*)"));
	if (crt.isEmpty())
		return;
	auto path = FileDialog::getSaveFileName(window, tr("Export DXF"), {}, tr("DXF") + QLatin1String(" (*.dxf)"));
	if (path.isEmpty())
		return;
	if (!path.endsWith(QLatin1String(".dxf"), Qt::CaseInsensitive))
		path += QLatin1String(".dxf");
	auto const answer = QMessageBox::question(window, tr("Export DXF"), tr("Export only the symbols which the table lists?"),
	                                          QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel, QMessageBox::No);
	if (answer == QMessageBox::Cancel)
		return;
	if (exportDxfWithCrt(path, crt, answer == QMessageBox::Yes))
		window->showStatusBarMessage(tr("Exported successfully to %1").arg(path), 4000);
}

bool MapEditorController::exportDxfWithCrt(const QString& dxf, const QString& crt, bool listed_only)
{
	auto const* format = FileFormats.findFormat("OGR-export-DXF");
	if (!format || !format->supportsFileExport())
	{
		QMessageBox::warning(window, tr("Error"), tr("DXF cannot be written: GDAL has no DXF driver."));
		return false;
	}
	auto exporter = format->makeExporter(dxf, map, main_view);
	if (!exporter)
		return false;
	exporter->setOption(QStringLiteral("foom dxf crt"), crt);
	exporter->setOption(QStringLiteral("foom dxf crt only"), listed_only);
	if (!exporter->doExport())
	{
		QMessageBox::warning(window, tr("Error"), exporter->warnings().empty() ? tr("Cannot write %1.").arg(dxf) : exporter->warnings().back());
		return false;
	}
	if (!exporter->warnings().empty())
		MainWindow::showMessageBox(window, tr("Warning"), tr("The map export generated warnings."), exporter->warnings());
	return true;
}


// foom: the design notes, "Spot colour view"
void MapEditorController::addSpotColourViewMenu(QMenu* view_menu)
{
	spot_colour_view_menu = view_menu->addMenu(tr("Spot colour view"));
	spot_colour_view_menu->setObjectName(QStringLiteral("spot-colour-view"));
	spot_colour_view_menu->menuAction()->setObjectName(QStringLiteral("spot-colour-view"));
	connect(spot_colour_view_menu, &QMenu::aboutToShow, this, &MapEditorController::updateSpotColourViewMenu);
	spot_colour_view_menu->setEnabled(map && map->hasSpotColors());
}

void MapEditorController::updateSpotColourViewMenu()
{
	if (!spot_colour_view_menu)
		return;
	spot_colour_view_menu->clear();
	auto* group = new QActionGroup(spot_colour_view_menu);
	auto const* current = main_view ? main_view->spotColourView() : nullptr;
	auto add = [this, group](const QString& text, int index, bool checked) {
		auto* action = spot_colour_view_menu->addAction(text);
		action->setCheckable(true);
		action->setChecked(checked);
		group->addAction(action);
		connect(action, &QAction::triggered, this, [this, index]() { showSpotColourView(index); });
	};
	add(tr("Off"), -1, !current);
	if (!map)
		return;
	for (int i = 0; i < map->getNumColors(); ++i)
	{
		auto const* color = map->getColor(i);
		if (color->getSpotColorMethod() == MapColor::SpotColor)
			add(color->getName(), i, color == current);
	}
}

void MapEditorController::showSpotColourView(int colour_index)
{
	if (!main_view || !map)
		return;
	const MapColor* color = nullptr;
	if (colour_index >= 0 && colour_index < map->getNumColors()
	    && map->getColor(colour_index)->getSpotColorMethod() == MapColor::SpotColor)
		color = map->getColor(colour_index);
	main_view->setSpotColourView(color);
	if (overprinting_simulation_act)
		overprinting_simulation_act->setChecked(main_view->isOverprintingSimulationEnabled());
}


}  // namespace OpenOrienteering
