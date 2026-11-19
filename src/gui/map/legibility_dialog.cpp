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


#include "legibility_dialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPointF>
#include <QRadioButton>
#include <QRectF>
#include <QVBoxLayout>

#include "core/map.h"
#include "core/symbols/symbol.h"
#include "gui/map/map_editor.h"
#include "gui/map/map_widget.h"
#include "gui/widgets/symbol_widget.h"


namespace OpenOrienteering {

LegibilityDialog::LegibilityDialog(MapEditorController* editor, QWidget* parent)
: QDialog(parent)
, editor(editor)
{
	setWindowTitle(tr("Check legibility"));
	auto const* map = editor->getMap();
	
	length_check = new QCheckBox(tr("Minimum line length"));
	length_check->setChecked(true);
	area_check = new QCheckBox(tr("Minimum area size"));
	area_check->setChecked(true);
	gap_check = new QCheckBox(tr("Gap between objects"));
	gap_check->setChecked(true);
	gap_edit = new QDoubleSpinBox();
	gap_edit->setDecimals(2);
	gap_edit->setRange(0.01, 50.0);
	gap_edit->setSingleStep(0.05);
	gap_edit->setSuffix(QStringLiteral(" mm"));
	gap_edit->setValue(defaultGap(*map));
	same_group_check = new QCheckBox(tr("Same color group only"));
	same_group_check->setChecked(true);
	contours_check = new QCheckBox(tr("Between contours"));
	contours_check->setChecked(false);
	
	all_objects_button = new QRadioButton(tr("All objects"));
	all_objects_button->setChecked(true);
	selected_symbols_button = new QRadioButton(tr("Selected symbols"));
	auto const* symbol_widget = editor->getSymbolWidget();
	selected_symbols_button->setEnabled(symbol_widget && symbol_widget->selectedSymbolsCount() > 0);
	whole_map_button = new QRadioButton(tr("Whole map"));
	whole_map_button->setChecked(true);
	current_view_button = new QRadioButton(tr("Current view"));
	
	auto* gap_row = new QHBoxLayout();
	gap_row->addWidget(gap_check);
	gap_row->addWidget(gap_edit);
	gap_row->addStretch();
	auto* scope_row = new QHBoxLayout();
	scope_row->addWidget(all_objects_button);
	scope_row->addWidget(selected_symbols_button);
	scope_row->addStretch();
	auto* extent_row = new QHBoxLayout();
	extent_row->addWidget(whole_map_button);
	extent_row->addWidget(current_view_button);
	extent_row->addStretch();
	
	auto* layout = new QFormLayout();
	layout->addRow(new QLabel(tr("Checks")));
	layout->addRow(length_check);
	layout->addRow(area_check);
	layout->addRow(gap_row);
	layout->addRow(same_group_check);
	layout->addRow(contours_check);
	layout->addRow(tr("Scope:"), scope_row);
	layout->addRow(tr("Extent:"), extent_row);
	
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	connect(gap_check, &QCheckBox::toggled, gap_edit, &QWidget::setEnabled);
	connect(gap_check, &QCheckBox::toggled, same_group_check, &QWidget::setEnabled);
	connect(gap_check, &QCheckBox::toggled, contours_check, &QWidget::setEnabled);
	
	auto* main_layout = new QVBoxLayout(this);
	main_layout->addLayout(layout);
	main_layout->addWidget(buttons);
}

LegibilityDialog::~LegibilityDialog() = default;

LegibilityParams LegibilityDialog::params() const
{
	LegibilityParams params;
	params.minimum_length = length_check->isChecked();
	params.minimum_area = area_check->isChecked();
	params.gap = gap_check->isChecked();
	params.gap_mm = gap_edit->value();
	params.same_colour_group_only = same_group_check->isChecked();
	params.between_contours = contours_check->isChecked();
	auto const* map = editor->getMap();
	auto const* symbol_widget = editor->getSymbolWidget();
	if (selected_symbols_button->isChecked() && symbol_widget)
	{
		for (int i = 0; i < map->getNumSymbols(); ++i)
		{
			if (symbol_widget->isSymbolSelected(map->getSymbol(i)))
				params.symbols.push_back(map->getSymbol(i));
		}
	}
	if (current_view_button->isChecked())
	{
		auto const* widget = editor->getMainWidget();
		auto const rect = widget->rect();
		params.extent = QRectF(QPointF(widget->viewportToMapF(rect.topLeft())), QPointF(widget->viewportToMapF(rect.bottomRight()))).normalized();
	}
	return params;
}


}  // namespace OpenOrienteering
