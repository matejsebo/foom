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


#include "topology_dialogs.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QRadioButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include "core/map.h"
#include "core/map_part.h"
#include "core/objects/object.h"
#include "core/symbols/symbol.h"
#include "gui/map/map_editor.h"
#include "gui/widgets/symbol_widget.h"


namespace OpenOrienteering {

std::vector<const Object*> topologyScopeObjects(const MapEditorController* editor, TopologyScope scope)
{
	auto const* map = editor->getMap();
	std::vector<const Object*> objects;
	switch (scope)
	{
	case TopologyScope::Selection:
		objects.assign(map->selectedObjects().begin(), map->selectedObjects().end());
		break;
	case TopologyScope::SelectedSymbols:
	case TopologyScope::AllObjects:
		{
			auto const* symbol_widget = editor->getSymbolWidget();
			auto const* part = map->getCurrentPart();
			for (int i = 0; i < part->getNumObjects(); ++i)
			{
				auto const* object = part->getObject(i);
				if (scope == TopologyScope::AllObjects || (symbol_widget && symbol_widget->isSymbolSelected(object->getSymbol())))
					objects.push_back(object);
			}
		}
		break;
	}
	return objects;
}


TopologyScopeDialog::TopologyScopeDialog(const QString& title, MapEditorController* editor, QWidget* parent)
: QDialog(parent)
{
	setWindowTitle(title);
	all_button = new QRadioButton(tr("All objects"));
	symbols_button = new QRadioButton(tr("Objects of the selected symbols"));
	selection_button = new QRadioButton(tr("Selected objects"));
	auto const* symbol_widget = editor->getSymbolWidget();
	symbols_button->setEnabled(symbol_widget && symbol_widget->selectedSymbolsCount() > 0);
	selection_button->setEnabled(editor->getMap()->getNumSelectedObjects() > 0);
	if (selection_button->isEnabled())
		selection_button->setChecked(true);
	else
		all_button->setChecked(true);
	
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	auto* layout = new QVBoxLayout(this);
	layout->addWidget(new QLabel(tr("Apply to:")));
	layout->addWidget(all_button);
	layout->addWidget(symbols_button);
	layout->addWidget(selection_button);
	layout->addWidget(buttons);
}

TopologyScopeDialog::~TopologyScopeDialog() = default;

TopologyScope TopologyScopeDialog::scope() const
{
	if (selection_button->isChecked())
		return TopologyScope::Selection;
	if (symbols_button->isChecked())
		return TopologyScope::SelectedSymbols;
	return TopologyScope::AllObjects;
}


OvershootDialog::OvershootDialog(QWidget* parent)
: QDialog(parent)
{
	setWindowTitle(tr("Remove overshoots and undershoots"));
	overshoots_check = new QCheckBox(tr("Remove overshoots"));
	overshoots_check->setChecked(true);
	undershoots_check = new QCheckBox(tr("Remove undershoots"));
	undershoots_check->setChecked(true);
	tolerance_edit = new QDoubleSpinBox();
	tolerance_edit->setDecimals(2);
	tolerance_edit->setRange(0.0, 10.0);
	tolerance_edit->setSingleStep(0.1);
	tolerance_edit->setSuffix(QStringLiteral(" mm"));
	tolerance_edit->setValue(ops::OvershootParams().tolerance);
	
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	auto* layout = new QFormLayout();
	layout->addRow(overshoots_check);
	layout->addRow(undershoots_check);
	layout->addRow(tr("Tolerance:"), tolerance_edit);
	auto* main_layout = new QVBoxLayout(this);
	main_layout->addLayout(layout);
	main_layout->addWidget(buttons);
}

OvershootDialog::~OvershootDialog() = default;

ops::OvershootParams OvershootDialog::params() const
{
	ops::OvershootParams params;
	params.remove_overshoots = overshoots_check->isChecked();
	params.remove_undershoots = undershoots_check->isChecked();
	params.tolerance = tolerance_edit->value();
	return params;
}


IntersectionDialog::IntersectionDialog(MapEditorController* editor, QWidget* parent)
: QDialog(parent)
, editor(editor)
{
	setWindowTitle(tr("Intersections"));
	insert_button = new QRadioButton(tr("Insert vertex"));
	insert_button->setChecked(true);
	vertex_combo = new QComboBox();
	vertex_combo->addItem(tr("Normal point"), false);
	vertex_combo->addItem(tr("Dash point"), true);
	cut_button = new QRadioButton(tr("Cut lines"));
	auto const* active = editor->activeSymbol();
	auto const have_point_symbol = active && active->getType() == Symbol::Point;
	point_button = new QRadioButton(have_point_symbol ? tr("Add point object of symbol %1").arg(active->getNumberAsString()) : tr("Add point object (select a point symbol first)"));
	point_button->setEnabled(have_point_symbol);
	touching_check = new QCheckBox(tr("Include touching objects"));
	touching_check->setChecked(true);
	connect(insert_button, &QRadioButton::toggled, vertex_combo, &QWidget::setEnabled);
	
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	auto* insert_row = new QHBoxLayout();
	insert_row->addWidget(insert_button);
	insert_row->addWidget(vertex_combo);
	insert_row->addStretch();
	auto* layout = new QVBoxLayout(this);
	layout->addWidget(new QLabel(tr("At each intersection of the selected paths:")));
	layout->addLayout(insert_row);
	layout->addWidget(cut_button);
	layout->addWidget(point_button);
	layout->addWidget(touching_check);
	layout->addWidget(buttons);
}

IntersectionDialog::~IntersectionDialog() = default;

ops::IntersectionParams IntersectionDialog::params() const
{
	ops::IntersectionParams params;
	if (cut_button->isChecked())
		params.action = ops::IntersectionParams::Cut;
	else if (point_button->isChecked())
		params.action = ops::IntersectionParams::AddPoint;
	params.dash = vertex_combo->currentData().toBool();
	params.point_symbol = editor->activeSymbol();
	params.include_touching = touching_check->isChecked();
	return params;
}


SmoothDialog::SmoothDialog(QWidget* parent)
: QDialog(parent)
{
	setWindowTitle(tr("Smooth"));
	ops::SmoothParams const defaults;
	tolerance_edit = new QDoubleSpinBox();
	tolerance_edit->setDecimals(2);
	tolerance_edit->setRange(0.01, 100.0);
	tolerance_edit->setSingleStep(0.05);
	tolerance_edit->setSuffix(QStringLiteral(" mm"));
	tolerance_edit->setValue(defaults.tolerance);
	corners_check = new QCheckBox(tr("Keep corners sharper than:"));
	corners_check->setChecked(defaults.keep_corners);
	angle_edit = new QDoubleSpinBox();
	angle_edit->setDecimals(0);
	angle_edit->setRange(1.0, 179.0);
	angle_edit->setSuffix(QStringLiteral(" \u00b0"));
	angle_edit->setValue(defaults.corner_angle_deg);
	angle_edit->setEnabled(defaults.keep_corners);
	connect(corners_check, &QCheckBox::toggled, angle_edit, &QWidget::setEnabled);
	inflections_check = new QCheckBox(tr("Keep where the curvature changes direction"));
	inflections_check->setChecked(defaults.keep_inflections);

	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	auto* corner_row = new QHBoxLayout();
	corner_row->addWidget(corners_check);
	corner_row->addWidget(angle_edit);
	corner_row->addStretch();
	auto* layout = new QFormLayout();
	layout->addRow(tr("Tolerance:"), tolerance_edit);
	layout->addRow(corner_row);
	layout->addRow(inflections_check);
	auto* main_layout = new QVBoxLayout(this);
	main_layout->addLayout(layout);
	main_layout->addWidget(buttons);
}

SmoothDialog::~SmoothDialog() = default;

ops::SmoothParams SmoothDialog::params() const
{
	ops::SmoothParams params;
	params.tolerance = tolerance_edit->value();
	params.keep_corners = corners_check->isChecked();
	params.corner_angle_deg = angle_edit->value();
	params.keep_inflections = inflections_check->isChecked();
	return params;
}


MakeRectangularDialog::MakeRectangularDialog(MapEditorController* editor, QWidget* parent)
: QDialog(parent)
, editor(editor)
{
	setWindowTitle(tr("Make rectangular"));
	tolerance_edit = new QSpinBox();
	tolerance_edit->setRange(0, 90);
	tolerance_edit->setSingleStep(5);
	tolerance_edit->setSuffix(QStringLiteral(" \u00b0"));
	tolerance_edit->setValue(int(ops::RectangularParams().tolerance_deg));
	auto const* first = editor->getMap()->getFirstSelectedObject();
	auto const have_reference = first && first->getType() == Object::Path && first->getSymbol() && (first->getSymbol()->getContainedTypes() & Symbol::Line) && editor->getMap()->getNumSelectedObjects() >= 2;
	reference_check = new QCheckBox(tr("Align to the first selected line"));
	reference_check->setEnabled(have_reference);
	distance_check = new QCheckBox(tr("Place at distance from it:"));
	distance_edit = new QDoubleSpinBox();
	distance_edit->setDecimals(2);
	distance_edit->setRange(0.0, 100.0);
	distance_edit->setSingleStep(0.1);
	distance_edit->setSuffix(QStringLiteral(" mm"));
	distance_check->setEnabled(false);
	distance_edit->setEnabled(false);
	connect(reference_check, &QCheckBox::toggled, distance_check, &QWidget::setEnabled);
	connect(reference_check, &QCheckBox::toggled, this, [this](bool on) { distance_edit->setEnabled(on && distance_check->isChecked()); });
	connect(distance_check, &QCheckBox::toggled, distance_edit, &QWidget::setEnabled);

	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	auto* distance_row = new QHBoxLayout();
	distance_row->addWidget(distance_check);
	distance_row->addWidget(distance_edit);
	distance_row->addStretch();
	auto* layout = new QFormLayout();
	layout->addRow(tr("Angle tolerance:"), tolerance_edit);
	layout->addRow(reference_check);
	layout->addRow(distance_row);
	auto* main_layout = new QVBoxLayout(this);
	main_layout->addLayout(layout);
	main_layout->addWidget(buttons);
}

MakeRectangularDialog::~MakeRectangularDialog() = default;

ops::RectangularParams MakeRectangularDialog::params() const
{
	ops::RectangularParams params;
	params.tolerance_deg = tolerance_edit->value();
	if (reference_check->isChecked())
	{
		params.reference = editor->getMap()->getFirstSelectedObject();
		params.place_at_distance = distance_check->isChecked();
		params.distance_mm = distance_edit->value();
	}
	return params;
}


GeneralizeBuildingsDialog::GeneralizeBuildingsDialog(QWidget* parent)
: QDialog(parent)
{
	setWindowTitle(tr("Generalize buildings"));
	ops::SimplifyBuildingParams const simplify_defaults;
	simplify_button = new QRadioButton(tr("Simplify outlines"));
	simplify_button->setChecked(true);
	keep_corners_check = new QCheckBox(tr("Keep corner vertices (dash points)"));
	keep_corners_check->setChecked(simplify_defaults.keep_corners);
	angle_edit = new QDoubleSpinBox();
	angle_edit->setDecimals(1);
	angle_edit->setRange(0.0, 45.0);
	angle_edit->setSingleStep(0.5);
	angle_edit->setSuffix(QStringLiteral(" \u00b0"));
	angle_edit->setValue(simplify_defaults.angle_deg);
	offset_edit = new QDoubleSpinBox();
	offset_edit->setDecimals(2);
	offset_edit->setRange(0.0, 10.0);
	offset_edit->setSingleStep(0.05);
	offset_edit->setSuffix(QStringLiteral(" mm"));
	offset_edit->setValue(simplify_defaults.offset_mm);
	compensate_check = new QCheckBox(tr("Compensate the area"));
	compensate_check->setChecked(simplify_defaults.compensate_area);

	rectangle_button = new QRadioButton(tr("Replace by rectangles"));
	mode_combo = new QComboBox();
	mode_combo->addItem(tr("Each object by its own rectangle"), int(ops::RectangleParams::Each));
	mode_combo->addItem(tr("All objects by one rectangle"), int(ops::RectangleParams::All));
	mode_combo->addItem(tr("All objects by a series of rectangles"), int(ops::RectangleParams::Series));
	count_edit = new QSpinBox();
	count_edit->setRange(1, 50);
	count_edit->setValue(2);
	factor_edit = new QSpinBox();
	factor_edit->setRange(20, 500);
	factor_edit->setSingleStep(10);
	factor_edit->setSuffix(QStringLiteral(" %"));
	factor_edit->setValue(100);

	auto const enable_mode = [this](bool simplify) {
		for (auto* widget : std::vector<QWidget*>{ keep_corners_check, angle_edit, offset_edit, compensate_check })
			widget->setEnabled(simplify);
		for (auto* widget : std::vector<QWidget*>{ mode_combo, count_edit, factor_edit })
			widget->setEnabled(!simplify);
	};
	connect(simplify_button, &QRadioButton::toggled, this, enable_mode);
	enable_mode(true);

	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	auto* layout = new QFormLayout();
	layout->addRow(simplify_button);
	layout->addRow(keep_corners_check);
	layout->addRow(tr("Angle limit:"), angle_edit);
	layout->addRow(tr("Offset tolerance:"), offset_edit);
	layout->addRow(compensate_check);
	layout->addRow(rectangle_button);
	layout->addRow(tr("Mode:"), mode_combo);
	layout->addRow(tr("Number in the series:"), count_edit);
	layout->addRow(tr("Area:"), factor_edit);
	auto* main_layout = new QVBoxLayout(this);
	main_layout->addLayout(layout);
	main_layout->addWidget(buttons);
}

GeneralizeBuildingsDialog::~GeneralizeBuildingsDialog() = default;

bool GeneralizeBuildingsDialog::simplify() const
{
	return simplify_button->isChecked();
}

ops::SimplifyBuildingParams GeneralizeBuildingsDialog::simplifyParams() const
{
	ops::SimplifyBuildingParams params;
	params.keep_corners = keep_corners_check->isChecked();
	params.angle_deg = angle_edit->value();
	params.offset_mm = offset_edit->value();
	params.compensate_area = compensate_check->isChecked();
	return params;
}

ops::RectangleParams GeneralizeBuildingsDialog::rectangleParams() const
{
	ops::RectangleParams params;
	params.mode = ops::RectangleParams::Mode(mode_combo->currentData().toInt());
	params.count = count_edit->value();
	params.area_factor = factor_edit->value() / 100.0;
	return params;
}


}  // namespace OpenOrienteering
