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


#include "rectangle_box_dialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QSpinBox>
#include <QVBoxLayout>

#include "core/map.h"
#include "core/symbols/symbol.h"
#include "core/symbols/text_symbol.h"
#include "gui/widgets/symbol_dropdown.h"


namespace OpenOrienteering {

RectangleBoxDialog::RectangleBoxDialog(const Map* map, QWidget* parent)
: QDialog(parent)
{
	setWindowTitle(tr("Rectangle box"));
	ops::RectangleBoxParams const defaults;
	auto const mm_edit = [](double value) {
		auto* edit = new QDoubleSpinBox();
		edit->setDecimals(2);
		edit->setRange(0.0, 1000.0);
		edit->setSingleStep(0.5);
		edit->setSuffix(QStringLiteral(" mm"));
		edit->setValue(value);
		return edit;
	};
	radius_edit = mm_edit(defaults.corner_radius);
	cell_width_edit = mm_edit(defaults.cell_width);
	cell_width_edit->setSpecialValueText(tr("No grid"));
	cell_height_edit = mm_edit(defaults.cell_height);
	cell_height_edit->setSpecialValueText(tr("No grid"));
	grid_symbol_edit = new SymbolDropDown(map, Symbol::Line | Symbol::Combined, nullptr);
	numbering_check = new QCheckBox(tr("Number the cells"));
	numbering_check->setChecked(defaults.numbering);
	from_bottom_check = new QCheckBox(tr("Count from the bottom"));
	from_bottom_check->setChecked(defaults.number_from_bottom);
	const Symbol* first_text_symbol = nullptr;
	for (int i = 0; i < map->getNumSymbols() && !first_text_symbol; ++i)
	{
		if (map->getSymbol(i)->getType() == Symbol::Text)
			first_text_symbol = map->getSymbol(i);
	}
	text_symbol_edit = new SymbolDropDown(map, Symbol::Text, first_text_symbol);
	unnumbered_edit = new QSpinBox();
	unnumbered_edit->setRange(0, 9999);
	unnumbered_edit->setValue(defaults.unnumbered_cells);
	unnumbered_text_edit = new QLineEdit(defaults.unnumbered_text);
	
	auto const enable_numbering = [this](bool checked) {
		from_bottom_check->setEnabled(checked);
		text_symbol_edit->setEnabled(checked);
		unnumbered_edit->setEnabled(checked);
		unnumbered_text_edit->setEnabled(checked);
	};
	enable_numbering(defaults.numbering);
	connect(numbering_check, &QCheckBox::toggled, this, enable_numbering);
	
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	auto* layout = new QFormLayout();
	layout->addRow(tr("Corner radius:"), radius_edit);
	layout->addRow(tr("Cell width:"), cell_width_edit);
	layout->addRow(tr("Cell height:"), cell_height_edit);
	layout->addRow(tr("Grid line symbol:"), grid_symbol_edit);
	layout->addRow(numbering_check);
	layout->addRow(from_bottom_check);
	layout->addRow(tr("Number text symbol:"), text_symbol_edit);
	layout->addRow(tr("Un-numbered cells:"), unnumbered_edit);
	layout->addRow(tr("Text of un-numbered cells:"), unnumbered_text_edit);
	auto* main_layout = new QVBoxLayout(this);
	main_layout->addLayout(layout);
	main_layout->addWidget(buttons);
}

RectangleBoxDialog::~RectangleBoxDialog() = default;


ops::RectangleBoxParams RectangleBoxDialog::params() const
{
	ops::RectangleBoxParams params;
	params.corner_radius = radius_edit->value();
	params.cell_width = cell_width_edit->value();
	params.cell_height = cell_height_edit->value();
	params.grid = grid_symbol_edit->symbol();
	params.numbering = numbering_check->isChecked();
	params.number_from_bottom = from_bottom_check->isChecked();
	auto const* text_symbol = text_symbol_edit->symbol();
	params.number_text = (text_symbol && text_symbol->getType() == Symbol::Text) ? text_symbol->asText() : nullptr;
	params.unnumbered_cells = unnumbered_edit->value();
	params.unnumbered_text = unnumbered_text_edit->text();
	return params;
}


}  // namespace OpenOrienteering
