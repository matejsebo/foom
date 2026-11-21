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

#include "symbol_tools_dialogs.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFontComboBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QSpinBox>
#include <QStackedWidget>
#include <QVBoxLayout>

#include "core/map.h"
#include "core/map_color.h"
#include "core/symbol_tools.h"
#include "core/symbols/symbol.h"


namespace OpenOrienteering {

namespace {

void finish(QDialog* dialog, QFormLayout* form)
{
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	QObject::connect(buttons, &QDialogButtonBox::accepted, dialog, &QDialog::accept);
	QObject::connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
	auto* layout = new QVBoxLayout(dialog);
	layout->addLayout(form);
	layout->addWidget(buttons);
}

QComboBox* colorCombo(const Map& map)
{
	auto* combo = new QComboBox();
	for (int i = 0; i < map.getNumColors(); ++i)
		combo->addItem(map.getColor(i)->getName(), i);
	return combo;
}

}  // namespace



SelectSymbolsDialog::SelectSymbolsDialog(const Map& map, QWidget* parent)
: QDialog(parent)
, map(map)
{
	setWindowTitle(tr("Select symbols"));
	criterion_combo = new QComboBox();
	criterion_combo->addItem(tr("Colour"));
	criterion_combo->addItem(tr("Font"));
	criterion_combo->addItem(tr("Type"));
	criterion_combo->addItem(tr("Status"));
	criterion_combo->addItem(tr("Number pattern"));
	color_combo = colorCombo(map);
	font_combo = new QComboBox();
	font_combo->addItems(fontFamilies(map));
	type_combo = new QComboBox();
	type_combo->addItem(tr("Point"), int(Symbol::Point));
	type_combo->addItem(tr("Line"), int(Symbol::Line));
	type_combo->addItem(tr("Area"), int(Symbol::Area));
	type_combo->addItem(tr("Text"), int(Symbol::Text));
	type_combo->addItem(tr("Combined"), int(Symbol::Combined));
	status_combo = new QComboBox();
	status_combo->addItem(tr("Normal"), int(SymbolStatus::Normal));
	status_combo->addItem(tr("Protected"), int(SymbolStatus::Protected));
	status_combo->addItem(tr("Hidden"), int(SymbolStatus::Hidden));
	pattern_edit = new QLineEdit(QStringLiteral("1*"));
	pattern_edit->setToolTip(tr("* stands for any characters, ? for one."));
	value_stack = new QStackedWidget();
	for (auto* widget : std::initializer_list<QWidget*>{ color_combo, font_combo, type_combo, status_combo, pattern_edit })
		value_stack->addWidget(widget);
	connect(criterion_combo, QOverload<int>::of(&QComboBox::currentIndexChanged), value_stack, &QStackedWidget::setCurrentIndex);
	auto* form = new QFormLayout();
	form->addRow(tr("Select by:"), criterion_combo);
	form->addRow(tr("Value:"), value_stack);
	finish(this, form);
}

SelectSymbolsDialog::~SelectSymbolsDialog() = default;

std::vector<const Symbol*> SelectSymbolsDialog::symbols() const
{
	// the design notes, "Selecting symbols, replacing fonts and colours, renumbering".
	switch (criterion_combo->currentIndex())
	{
	case 0:
		return color_combo->currentIndex() >= 0 ? symbolsUsingColor(map, map.getColor(color_combo->currentData().toInt())) : std::vector<const Symbol*>{};
	case 1:
		return symbolsUsingFont(map, font_combo->currentText());
	case 2:
		return symbolsOfType(map, type_combo->currentData().toInt());
	case 3:
		return symbolsWithStatus(map, SymbolStatus(status_combo->currentData().toInt()));
	default:
		return symbolsMatchingNumber(map, pattern_edit->text().trimmed());
	}
}



SymbolReplaceDialog::SymbolReplaceDialog(Mode mode, const Map& map, QWidget* parent)
: QDialog(parent)
, map(map)
{
	auto* form = new QFormLayout();
	switch (mode)
	{
	case Font:
		setWindowTitle(tr("Replace font"));
		from_combo = new QComboBox();
		from_combo->addItems(fontFamilies(map));
		to_font_combo = new QFontComboBox();
		form->addRow(tr("Replace:"), from_combo);
		form->addRow(tr("With:"), to_font_combo);
		break;
	case Color:
		setWindowTitle(tr("Replace colour"));
		from_combo = colorCombo(map);
		to_color_combo = colorCombo(map);
		form->addRow(tr("Replace:"), from_combo);
		form->addRow(tr("With:"), to_color_combo);
		break;
	case Renumber:
		setWindowTitle(tr("Renumber symbols"));
		start_edit = new QSpinBox();
		start_edit->setRange(0, 99999);
		start_edit->setValue(101);
		step_edit = new QSpinBox();
		step_edit->setRange(1, 1000);
		step_edit->setValue(1);
		form->addRow(tr("First number:"), start_edit);
		form->addRow(tr("Step:"), step_edit);
		break;
	}
	selected_check = new QCheckBox(tr("Only the selected symbols"));
	form->addRow(selected_check);
	finish(this, form);
}

SymbolReplaceDialog::~SymbolReplaceDialog() = default;

QString SymbolReplaceDialog::fromFont() const { return from_combo ? from_combo->currentText() : QString{}; }
QString SymbolReplaceDialog::toFont() const { return to_font_combo ? to_font_combo->currentFont().family() : QString{}; }

const MapColor* SymbolReplaceDialog::fromColor() const
{
	return from_combo && from_combo->currentIndex() >= 0 && !to_font_combo ? map.getColor(from_combo->currentData().toInt()) : nullptr;
}

const MapColor* SymbolReplaceDialog::toColor() const
{
	return to_color_combo && to_color_combo->currentIndex() >= 0 ? map.getColor(to_color_combo->currentData().toInt()) : nullptr;
}

int SymbolReplaceDialog::start() const { return start_edit ? start_edit->value() : 0; }
int SymbolReplaceDialog::step() const { return step_edit ? step_edit->value() : 1; }
bool SymbolReplaceDialog::onlySelected() const { return selected_check->isChecked(); }


}  // namespace OpenOrienteering
