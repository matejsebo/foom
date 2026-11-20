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


#include "route_settings_dialog.h"

#include <algorithm>
#include <cstddef>

#include <Qt>
#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFont>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLatin1Char>
#include <QPushButton>
#include <QSpinBox>
#include <QStringList>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include "core/map.h"
#include "core/symbols/symbol.h"
#include "course/course_data.h"
#include "course/route_terrain.h"
#include "templates/template.h"
#include "templates/template_map.h"

namespace OpenOrienteering {

namespace {

enum Column { NumberColumn, NameColumn, KindColumn, PercentColumn };

const Runnability::Kind kinds[] = {
    Runnability::None, Runnability::Ground, Runnability::Vegetation, Runnability::Hindrance,
    Runnability::Surface, Runnability::Path, Runnability::Impassable,
};

}  // namespace



RouteSettingsDialog::RouteSettingsDialog(const Map* map, bool has_elevation, QWidget* parent)
: QDialog(parent)
, initial(map->courseData().route)
{
	setWindowTitle(tr("Route settings"));
	
	pace_edit = new QSpinBox();
	pace_edit->setObjectName(QStringLiteral("route-pace"));
	pace_edit->setRange(30, 1200);
	pace_edit->setSingleStep(5);
	pace_edit->setSuffix(QLatin1Char(' ') + tr("s/km"));
	pace_edit->setValue(initial.base_pace);
	pace_edit->setToolTip(tr("The pace on flat ground of full runnability. It does not change how the routes compare."));
	runnability_check = new QCheckBox(tr("Use runnability"));
	runnability_check->setObjectName(QStringLiteral("route-use-runnability"));
	runnability_check->setChecked(initial.use_runnability);
	elevation_check = new QCheckBox(has_elevation ? tr("Use elevation") : tr("Use elevation (the map has no elevation model)"));
	elevation_check->setObjectName(QStringLiteral("route-use-elevation"));
	elevation_check->setChecked(initial.use_elevation);
	elevation_check->setEnabled(has_elevation);
	
	// The symbols of the map and of its background maps which are lines or areas, each number once.
	std::vector<const Map*> maps = { map };
	for (int i = 0; i < map->getNumTemplates(); ++i)
	{
		auto const* temp = map->getTemplate(i);
		if (temp->getTemplateState() != Template::Loaded)
			continue;
		if (auto const* template_map = qobject_cast<const TemplateMap*>(temp))
		{
			if (template_map->templateMap())
				maps.push_back(template_map->templateMap());
		}
	}
	table = new QTableWidget(0, 4);
	table->setObjectName(QStringLiteral("route-symbols"));
	table->setHorizontalHeaderLabels({ tr("Number"), tr("Symbol"), tr("Runnability"), tr("Percent") });
	table->verticalHeader()->hide();
	table->setSelectionMode(QAbstractItemView::NoSelection);
	QStringList kind_texts = { tr("Does not count"), tr("Open ground"), tr("Forest with undergrowth"), tr("Hindrance"),
	                           tr("Stony or broken ground"), tr("Road, path or ride"), tr("Impassable") };
	for (auto const* source : maps)
	{
		for (int s = 0; s < source->getNumSymbols(); ++s)
		{
			auto const* symbol = source->getSymbol(s);
			if (!(symbol->getContainedTypes() & (Symbol::Line | Symbol::Area)))
				continue;
			auto const number = routeSymbolNumber(*symbol);
			if (std::find(numbers.begin(), numbers.end(), number) != numbers.end())
				continue;
			auto const row = table->rowCount();
			table->insertRow(row);
			numbers.push_back(number);
			defaults.push_back(defaultRunnability(*symbol));
			auto* number_item = new QTableWidgetItem(symbol->getNumberAsString());
			number_item->setFlags(Qt::ItemIsEnabled);
			table->setItem(row, NumberColumn, number_item);
			auto* name_item = new QTableWidgetItem(symbol->getPlainTextName());
			name_item->setFlags(Qt::ItemIsEnabled);
			table->setItem(row, NameColumn, name_item);
			auto* kind_box = new QComboBox();
			kind_box->addItems(kind_texts);
			table->setCellWidget(row, KindColumn, kind_box);
			auto* percent_edit = new QSpinBox();
			percent_edit->setRange(0, 100);
			percent_edit->setSuffix(QStringLiteral(" %"));
			table->setCellWidget(row, PercentColumn, percent_edit);
			
			auto const runnability = runnabilityOf(initial, *symbol);
			kind_box->setCurrentIndex(int(runnability.kind));
			percent_edit->setValue(runnability.percent);
			connect(kind_box, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, row]() { updateRow(row); });
			connect(percent_edit, QOverload<int>::of(&QSpinBox::valueChanged), this, [this, row]() { updateRow(row); });
			updateRow(row);
		}
	}
	table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
	table->horizontalHeader()->setSectionResizeMode(NameColumn, QHeaderView::Stretch);
	
	auto* defaults_button = new QPushButton(tr("Defaults"));
	defaults_button->setObjectName(QStringLiteral("route-defaults"));
	defaults_button->setToolTip(tr("Give every symbol the runnability of its number in ISOM 2017-2."));
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	buttons->addButton(defaults_button, QDialogButtonBox::ResetRole);
	
	auto* form = new QFormLayout();
	form->addRow(tr("Base pace:"), pace_edit);
	form->addRow(QString{}, runnability_check);
	form->addRow(QString{}, elevation_check);
	auto* layout = new QVBoxLayout(this);
	layout->addLayout(form);
	layout->addWidget(new QLabel(tr("The runnability of the symbols, in percent of the full speed; what differs from the default is bold:")));
	layout->addWidget(table, 1);
	layout->addWidget(buttons);
	resize(640, 560);
	
	connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	connect(defaults_button, &QPushButton::clicked, this, &RouteSettingsDialog::resetSymbols);
	connect(runnability_check, &QCheckBox::toggled, table, &QWidget::setEnabled);
	table->setEnabled(initial.use_runnability);
}

RouteSettingsDialog::~RouteSettingsDialog() = default;


RouteSettings RouteSettingsDialog::settings() const
{
	auto result = initial;
	result.base_pace = pace_edit->value();
	result.use_runnability = runnability_check->isChecked();
	result.use_elevation = elevation_check->isChecked();
	result.symbols.clear();
	for (int row = 0; row < table->rowCount(); ++row)
	{
		Runnability runnability;
		runnability.kind = kinds[qobject_cast<QComboBox*>(table->cellWidget(row, KindColumn))->currentIndex()];
		runnability.percent = qobject_cast<QSpinBox*>(table->cellWidget(row, PercentColumn))->value();
		auto const& standard = defaults[std::size_t(row)];
		// The percentage of a symbol which does not count, or which is impassable, says nothing.
		auto const same = runnability.kind == standard.kind
		                  && (runnability.percent == standard.percent || runnability.kind == Runnability::None || runnability.kind == Runnability::Impassable);
		if (!same)
			result.symbols.push_back({ numbers[std::size_t(row)], runnability });
	}
	// A setting for a symbol which the table has not is kept.
	for (auto const& entry : initial.symbols)
	{
		if (std::find(numbers.begin(), numbers.end(), entry.first) == numbers.end())
			result.symbols.push_back(entry);
	}
	return result;
}


bool RouteSettingsDialog::setRunnability(const QString& number, const Runnability& runnability)
{
	auto const found = std::find(numbers.begin(), numbers.end(), number);
	if (found == numbers.end())
		return false;
	auto const row = int(found - numbers.begin());
	qobject_cast<QComboBox*>(table->cellWidget(row, KindColumn))->setCurrentIndex(int(runnability.kind));
	qobject_cast<QSpinBox*>(table->cellWidget(row, PercentColumn))->setValue(runnability.percent);
	return true;
}


void RouteSettingsDialog::resetSymbols()
{
	for (int row = 0; row < table->rowCount(); ++row)
	{
		qobject_cast<QComboBox*>(table->cellWidget(row, KindColumn))->setCurrentIndex(int(defaults[std::size_t(row)].kind));
		qobject_cast<QSpinBox*>(table->cellWidget(row, PercentColumn))->setValue(defaults[std::size_t(row)].percent);
	}
}


void RouteSettingsDialog::updateRow(int row)
{
	auto* kind_box = qobject_cast<QComboBox*>(table->cellWidget(row, KindColumn));
	auto* percent_edit = qobject_cast<QSpinBox*>(table->cellWidget(row, PercentColumn));
	if (!kind_box || !percent_edit)
		return;
	auto const kind = kinds[kind_box->currentIndex()];
	percent_edit->setEnabled(kind != Runnability::None && kind != Runnability::Impassable);
	auto const& standard = defaults[std::size_t(row)];
	auto const changed = kind != standard.kind || (percent_edit->isEnabled() && percent_edit->value() != standard.percent);
	for (auto column : { NumberColumn, NameColumn })
	{
		if (auto* item = table->item(row, column))
		{
			auto font = item->font();
			font.setBold(changed);
			item->setFont(font);
		}
	}
}


}  // namespace OpenOrienteering
