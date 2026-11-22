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

#include "map_import_dialog.h"

#include <initializer_list>
#include <utility>

#include <QAbstractButton>
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLatin1Char>
#include <QLatin1String>
#include <QLocale>
#include <QRadioButton>
#include <QSettings>
#include <QStringList>
#include <QVBoxLayout>

#include "core/map.h"
#include "core/symbols/symbol.h"
#include "gui/util_gui.h"


namespace OpenOrienteering {

MapImportDialog::MapImportDialog(const Map& imported, const Map& map, QWidget* parent)
: QDialog(parent, Qt::WindowSystemMenuHint | Qt::WindowTitleHint)
{
	setWindowTitle(tr("Import map"));
	auto* layout = new QFormLayout(this);

	// What is in the file, and which numbers clash
	QStringList clashes;
	for (int i = 0; i < imported.getNumSymbols(); ++i)
	{
		auto const number = imported.getSymbol(i)->getNumberAsString();
		for (int j = 0; j < map.getNumSymbols(); ++j)
		{
			if (map.getSymbol(j)->getNumberAsString() == number)
			{
				clashes.append(number);
				break;
			}
		}
	}
	auto summary = tr("The file has %1 objects, %2 symbols and %3 colours.")
	               .arg(imported.getNumObjects()).arg(imported.getNumSymbols()).arg(imported.getNumColors());
	if (!clashes.isEmpty())
		summary += QLatin1Char(' ') + tr("Symbol numbers which this map has too: %1").arg(clashes.join(QLatin1String(", ")));
	auto* summary_label = new QLabel(summary);
	summary_label->setObjectName(QStringLiteral("summary_label"));
	summary_label->setWordWrap(true);
	layout->addRow(summary_label);

	layout->addRow(Util::Headline::create(tr("Symbols and colours")));
	none_radio = new QRadioButton(tr("No symbols or colours"));
	absent_radio = new QRadioButton(tr("Only symbols whose number is absent"));
	renumber_radio = new QRadioButton(tr("Absent symbols, renumbering on a clash"));
	all_radio = new QRadioButton(tr("All symbols and colours, replacing on a clash"));
	auto* symbols_group = new QButtonGroup(this);
	for (auto* radio : { none_radio, absent_radio, renumber_radio, all_radio })
	{
		symbols_group->addButton(radio);
		layout->addRow(radio);
	}
	colours_combo = new QComboBox();
	colours_combo->addItem(tr("Above the map's colours"), true);
	colours_combo->addItem(tr("Below the map's colours"), false);
	layout->addRow(tr("New colours:"), colours_combo);
	protect_check = new QCheckBox(tr("Protect the imported symbols"));
	layout->addRow(protect_check);
	rescale_check = new QCheckBox(tr("Scale from 1:%1 to 1:%2")
	                              .arg(QLocale().toString(imported.getScaleDenominator()), QLocale().toString(map.getScaleDenominator())));
	rescale_check->setChecked(true);
	rescale_check->setVisible(imported.getScaleDenominator() != map.getScaleDenominator());
	layout->addRow(rescale_check);

	layout->addRow(Util::Headline::create(tr("Placement")));
	georef_radio = new QRadioButton(tr("By georeferencing"));
	offset_radio = new QRadioButton(tr("By offset:"));
	mouse_radio = new QRadioButton(tr("By mouse: at the centre of the view, selected for moving"));
	auto* placement_group = new QButtonGroup(this);
	offset_x_spin = Util::SpinBox::create(2, -100000.0, 100000.0, tr("mm"));
	offset_y_spin = Util::SpinBox::create(2, -100000.0, 100000.0, tr("mm"));
	auto* offset_layout = new QHBoxLayout();
	offset_layout->addWidget(offset_x_spin);
	offset_layout->addWidget(offset_y_spin);
	for (auto* radio : { georef_radio, offset_radio, mouse_radio })
		placement_group->addButton(radio);
	layout->addRow(georef_radio);
	layout->addRow(offset_radio, offset_layout);
	layout->addRow(mouse_radio);
	georef_radio->setChecked(true);

	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Help);
	layout->addRow(buttons);

	for (auto const& pair : std::initializer_list<std::pair<QObject*, const char*>>{
	         { none_radio, "none_radio" }, { absent_radio, "absent_radio" },
	         { renumber_radio, "renumber_radio" }, { all_radio, "all_radio" },
	         { colours_combo, "colours_combo" }, { protect_check, "protect_check" },
	         { rescale_check, "rescale_check" }, { georef_radio, "georef_radio" },
	         { offset_radio, "offset_radio" }, { mouse_radio, "mouse_radio" },
	         { offset_x_spin, "offset_x_spin" }, { offset_y_spin, "offset_y_spin" } })
		pair.first->setObjectName(QLatin1String(pair.second));

	// The choices of the last import
	QSettings settings;
	settings.beginGroup(QStringLiteral("MapImport"));
	auto const symbols = settings.value(QStringLiteral("symbols")).toString();
	if (symbols == QLatin1String("none"))
		none_radio->setChecked(true);
	else if (symbols == QLatin1String("renumber"))
		renumber_radio->setChecked(true);
	else if (symbols == QLatin1String("all"))
		all_radio->setChecked(true);
	else
		absent_radio->setChecked(true);
	colours_combo->setCurrentIndex(settings.value(QStringLiteral("colours_above"), false).toBool() ? 0 : 1);
	protect_check->setChecked(settings.value(QStringLiteral("protect"), false).toBool());

	connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	connect(buttons, &QDialogButtonBox::helpRequested, this, [this]() { Util::showHelp(this, "import_geodata.html", "map-files"); });
	connect(none_radio, &QAbstractButton::toggled, this, &MapImportDialog::updateWidgets);
	connect(offset_radio, &QAbstractButton::toggled, this, &MapImportDialog::updateWidgets);
	updateWidgets();
}


MapImportDialog::~MapImportDialog() = default;


void MapImportDialog::updateWidgets()
{
	colours_combo->setEnabled(!none_radio->isChecked());
	protect_check->setEnabled(!none_radio->isChecked());
	offset_x_spin->setEnabled(offset_radio->isChecked());
	offset_y_spin->setEnabled(offset_radio->isChecked());
}


MapImportPolicy MapImportDialog::policy() const
{
	MapImportPolicy policy;
	if (none_radio->isChecked())
		policy.symbols = MapImportPolicy::NoSymbols;
	else if (renumber_radio->isChecked())
		policy.symbols = MapImportPolicy::RenumberClashes;
	else if (all_radio->isChecked())
		policy.symbols = MapImportPolicy::AllSymbols;
	else
		policy.symbols = MapImportPolicy::AbsentSymbols;
	policy.colours_above = colours_combo->currentData().toBool();
	policy.protect_symbols = protect_check->isChecked();
	policy.rescale = rescale_check->isChecked();
	if (offset_radio->isChecked())
	{
		policy.placement = MapImportPolicy::ByOffset;
		policy.offset_mm = QPointF(offset_x_spin->value(), offset_y_spin->value());
	}
	else if (mouse_radio->isChecked())
	{
		policy.placement = MapImportPolicy::AtPoint;
		policy.offset_mm = view_center;
	}
	return policy;
}


void MapImportDialog::accept()
{
	QSettings settings;
	settings.beginGroup(QStringLiteral("MapImport"));
	auto const chosen = policy();
	auto const* symbols = chosen.symbols == MapImportPolicy::NoSymbols ? "none"
	                      : chosen.symbols == MapImportPolicy::RenumberClashes ? "renumber"
	                      : chosen.symbols == MapImportPolicy::AllSymbols ? "all" : "absent";
	settings.setValue(QStringLiteral("symbols"), QLatin1String(symbols));
	settings.setValue(QStringLiteral("colours_above"), chosen.colours_above);
	settings.setValue(QStringLiteral("protect"), chosen.protect_symbols);
	QDialog::accept();
}


}  // namespace OpenOrienteering
