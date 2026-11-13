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


#include "topology_dialogs.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QRadioButton>
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


}  // namespace OpenOrienteering
