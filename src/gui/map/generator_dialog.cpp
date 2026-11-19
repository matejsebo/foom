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


#include "generator_dialog.h"

#include <cstddef>

#include <Qt>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QJsonValue>
#include <QLineEdit>
#include <QSpinBox>
#include <QVBoxLayout>

#include "core/map.h"
#include "core/symbols/symbol.h"
#include "gui/widgets/symbol_dropdown.h"


namespace OpenOrienteering {

namespace {

int symbolFilter(GeneratorField::Kind kind)
{
	switch (kind)
	{
	case GeneratorField::LineSymbol:
		return Symbol::Line | Symbol::Combined;
	case GeneratorField::AreaSymbol:
		return Symbol::Area | Symbol::Combined;
	default:
		return Symbol::Text;
	}
}

const Symbol* symbolByNumber(const Map& map, const QString& number, int filter)
{
	if (number.isEmpty())
		return nullptr;
	for (int i = 0; i < map.getNumPublicSymbols(); ++i)
	{
		auto const* symbol = map.getSymbol(i);
		if ((symbol->getType() & filter) && symbol->getNumberAsString() == number)
			return symbol;
	}
	return nullptr;
}

}  // namespace


GeneratorDialog::GeneratorDialog(const Map& map, const QString& id, const QJsonObject& params, QWidget* parent)
: QDialog(parent)
, map(map)
, initial_params(params)
, fields(generatorFields(id, &map))
{
	setWindowTitle(generatorTitle(id));
	setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
	
	auto* form = new QFormLayout();
	for (auto const& field : fields)
	{
		auto const value = params.value(field.key);
		switch (field.kind)
		{
		case GeneratorField::Bool:
			{
				auto* check = new QCheckBox(field.label);
				check->setChecked(value.toBool());
				form->addRow(check);
				editors.push_back(check);
			}
			break;
		case GeneratorField::Int:
			{
				auto* spin = new QSpinBox();
				spin->setRange(int(field.minimum), int(field.maximum));
				spin->setSuffix(field.suffix);
				spin->setValue(value.toInt());
				form->addRow(field.label, spin);
				editors.push_back(spin);
			}
			break;
		case GeneratorField::Double:
			{
				auto* spin = new QDoubleSpinBox();
				spin->setDecimals(field.decimals);
				spin->setRange(field.minimum, field.maximum);
				spin->setSuffix(field.suffix);
				spin->setValue(value.toDouble());
				form->addRow(field.label, spin);
				editors.push_back(spin);
			}
			break;
		case GeneratorField::Choice:
			{
				auto* combo = new QComboBox();
				for (int i = 0; i < field.choices.size(); ++i)
					combo->addItem(field.choice_labels.value(i, field.choices.at(i)), field.choices.at(i));
				combo->setCurrentIndex(qMax(0, field.choices.indexOf(value.toString())));
				form->addRow(field.label, combo);
				editors.push_back(combo);
			}
			break;
		case GeneratorField::String:
			{
				auto* edit = new QLineEdit(value.toString());
				edit->setMinimumWidth(240);
				form->addRow(field.label, edit);
				editors.push_back(edit);
			}
			break;
		case GeneratorField::LineSymbol:
		case GeneratorField::AreaSymbol:
		case GeneratorField::TextSymbol:
			{
				auto const filter = symbolFilter(field.kind);
				auto* dropdown = new SymbolDropDown(&map, filter, symbolByNumber(map, value.toString(), filter));
				form->addRow(field.label, dropdown);
				editors.push_back(dropdown);
			}
			break;
		}
	}
	
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, Qt::Horizontal);
	connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	
	auto* layout = new QVBoxLayout();
	layout->addLayout(form);
	layout->addWidget(buttons);
	setLayout(layout);
}

GeneratorDialog::~GeneratorDialog() = default;

QJsonObject GeneratorDialog::params() const
{
	auto result = initial_params;
	for (std::size_t i = 0; i < fields.size(); ++i)
	{
		auto const& field = fields[i];
		switch (field.kind)
		{
		case GeneratorField::Bool:
			result.insert(field.key, static_cast<QCheckBox*>(editors[i])->isChecked());
			break;
		case GeneratorField::Int:
			result.insert(field.key, static_cast<QSpinBox*>(editors[i])->value());
			break;
		case GeneratorField::Double:
			result.insert(field.key, static_cast<QDoubleSpinBox*>(editors[i])->value());
			break;
		case GeneratorField::Choice:
			result.insert(field.key, static_cast<QComboBox*>(editors[i])->currentData().toString());
			break;
		case GeneratorField::String:
			result.insert(field.key, static_cast<QLineEdit*>(editors[i])->text());
			break;
		case GeneratorField::LineSymbol:
		case GeneratorField::AreaSymbol:
		case GeneratorField::TextSymbol:
			{
				auto const* symbol = static_cast<SymbolDropDown*>(editors[i])->symbol();
				result.insert(field.key, symbol ? symbol->getNumberAsString() : QString{});
			}
			break;
		}
	}
	return result;
}


}  // namespace OpenOrienteering
