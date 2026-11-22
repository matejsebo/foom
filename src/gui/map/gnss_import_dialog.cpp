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


#include "gnss_import_dialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QVariant>
#include <QVBoxLayout>

#include "core/map.h"
#include "core/symbols/symbol.h"


namespace OpenOrienteering {

GnssImportDialog::GnssImportDialog(const Map& map, const GnssFileData& data, const Symbol* current_symbol, QWidget* parent)
: QDialog(parent)
, map(map)
, data(data)
, current_symbol(current_symbol)
{
	setWindowTitle(tr("Import GNSS file"));

	summary = new QLabel(tr("%n position(s)", nullptr, int(data.fixes.size()))
	                     + QLatin1String(", ") + tr("%n waypoint(s)", nullptr, int(data.waypoints.size()))
	                     + (data.skipped > 0 ? QLatin1String(", ") + tr("%n without a valid position", nullptr, data.skipped) : QString{}));

	kind_box = new QComboBox();
	kind_box->setObjectName(QStringLiteral("gnss-import-kind"));
	if (data.fixes.size() >= 2)
		kind_box->addItem(tr("Track"), int(GnssImportKind::Track));
	if (!data.waypoints.empty())
		kind_box->addItem(tr("Waypoints"), int(GnssImportKind::Waypoints));
	if (!data.fixes.empty())
		kind_box->addItem(tr("Positions"), int(GnssImportKind::Positions));

	symbol_box = new QComboBox();
	symbol_box->setObjectName(QStringLiteral("gnss-import-symbol"));

	auto* form = new QFormLayout();
	form->addRow(summary);
	form->addRow(tr("Load as:"), kind_box);
	form->addRow(tr("Symbol:"), symbol_box);

	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

	auto* layout = new QVBoxLayout(this);
	layout->addLayout(form);
	layout->addWidget(buttons);

	connect(kind_box, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &GnssImportDialog::kindChanged);
	kindChanged();

	auto const update_ok = [this, buttons]() {
		buttons->button(QDialogButtonBox::Ok)->setEnabled(kind_box->count() > 0 && symbol());
	};
	connect(symbol_box, QOverload<int>::of(&QComboBox::currentIndexChanged), this, update_ok);
	update_ok();
}

GnssImportDialog::~GnssImportDialog() = default;


GnssImportKind GnssImportDialog::kind() const
{
	return GnssImportKind(kind_box->currentData().toInt());
}

const Symbol* GnssImportDialog::symbol() const
{
	auto const index = symbol_box->currentData().toInt();
	return (index >= 0 && index < map.getNumSymbols() && symbol_box->count() > 0) ? map.getSymbol(index) : nullptr;
}

void GnssImportDialog::setKind(GnssImportKind kind)
{
	auto const index = kind_box->findData(int(kind));
	if (index >= 0)
		kind_box->setCurrentIndex(index);
}


void GnssImportDialog::kindChanged()
{
	auto const wants_line = kind() == GnssImportKind::Track;
	auto const fits = [wants_line](const Symbol* symbol) {
		return wants_line ? (symbol->getType() == Symbol::Line || symbol->getType() == Symbol::Combined)
		                  : symbol->getType() == Symbol::Point;
	};
	symbol_box->clear();
	if (current_symbol && fits(current_symbol))
		symbol_box->addItem(current_symbol->getNumberAsString() + QLatin1Char(' ') + current_symbol->getPlainTextName(), map.findSymbolIndex(current_symbol));
	for (int i = 0; i < map.getNumSymbols(); ++i)
	{
		auto const* symbol = map.getSymbol(i);
		if (symbol == current_symbol || symbol->isHidden() || !fits(symbol))
			continue;
		symbol_box->addItem(symbol->getNumberAsString() + QLatin1Char(' ') + symbol->getPlainTextName(), i);
	}
}


}  // namespace OpenOrienteering
