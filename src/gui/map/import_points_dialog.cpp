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


#include "import_points_dialog.h"

#include <algorithm>
#include <initializer_list>

#include <Qt>
#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLatin1Char>
#include <QLatin1String>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVariant>

#include "core/georeferencing.h"
#include "core/map.h"
#include "core/symbols/symbol.h"
#include "gui/util_gui.h"
#include "gui/widgets/symbol_dropdown.h"


namespace OpenOrienteering {

namespace {

constexpr int preview_rows = 8;

/// The column whose header matches one of the names, or -1.
int guessColumn(const QStringList& header, std::initializer_list<const char*> names)
{
	for (int i = 0; i < header.size(); ++i)
	{
		auto const name = header[i].trimmed().toLower();
		for (auto const* candidate : names)
		{
			if (name == QLatin1String(candidate))
				return i;
		}
	}
	return -1;
}

const Symbol* firstSymbolOfType(const Map& map, Symbol::Type type)
{
	for (int i = 0; i < map.getNumSymbols(); ++i)
	{
		if (map.getSymbol(i)->getType() == type)
			return map.getSymbol(i);
	}
	return nullptr;
}

}  // namespace



ImportPointsDialog::ImportPointsDialog(const QString& text, const Map& map, const Symbol* initial_symbol, QWidget* parent)
: QDialog(parent, Qt::WindowSystemMenuHint | Qt::WindowTitleHint)
, map(map)
{
	setWindowTitle(tr("Import points"));
	static const QRegularExpression line_break(QStringLiteral("\r\n|\n|\r"));
	lines = text.split(line_break);
	
	auto* layout = new QFormLayout(this);
	
	delimiter_combo = new QComboBox();
	delimiter_combo->addItem(tr("Comma"), QVariant(QChar(QLatin1Char(','))));
	delimiter_combo->addItem(tr("Semicolon"), QVariant(QChar(QLatin1Char(';'))));
	delimiter_combo->addItem(tr("Tab"), QVariant(QChar(QLatin1Char('\t'))));
	delimiter_combo->addItem(tr("Blanks"), QVariant(QChar(QLatin1Char(' '))));
	auto const detected = ops::detectDelimiter(lines);
	delimiter_combo->setCurrentIndex(std::max(0, delimiter_combo->findData(QVariant(detected))));
	layout->addRow(tr("Delimiter:"), delimiter_combo);
	
	header_check = new QCheckBox(tr("The first row holds the column names"));
	header_check->setChecked(ops::detectHeader(lines, detected, 0, 1));
	layout->addRow(header_check);
	
	preview = new QTableWidget();
	preview->setEditTriggers(QAbstractItemView::NoEditTriggers);
	preview->verticalHeader()->hide();
	preview->setMinimumHeight(180);
	layout->addRow(preview);
	
	layout->addRow(Util::Headline::create(tr("Columns")));
	x_combo = new QComboBox();
	y_combo = new QComboBox();
	z_combo = new QComboBox();
	text_combo = new QComboBox();
	code_combo = new QComboBox();
	layout->addRow(tr("X, easting or longitude:"), x_combo);
	layout->addRow(tr("Y, northing or latitude:"), y_combo);
	layout->addRow(tr("Elevation:"), z_combo);
	layout->addRow(tr("Text:"), text_combo);
	layout->addRow(tr("Symbol number:"), code_combo);
	
	layout->addRow(Util::Headline::create(tr("Coordinates")));
	crs_combo = new QComboBox();
	auto const georeferenced = map.getGeoreferencing().getState() == Georeferencing::Geospatial;
	crs_combo->addItem(tr("Map coordinates"), int(ops::ImportPointsParams::MapCrs));
	if (georeferenced)
	{
		crs_combo->addItem(tr("WGS 84, longitude and latitude"), int(ops::ImportPointsParams::Wgs84));
		crs_combo->addItem(tr("Other coordinate reference system"), int(ops::ImportPointsParams::OtherCrs));
	}
	crs_combo->addItem(tr("Paper millimetres"), int(ops::ImportPointsParams::PaperMillimetres));
	layout->addRow(tr("System:"), crs_combo);
	crs_edit = new QLineEdit();
	crs_edit->setPlaceholderText(tr("EPSG code or PROJ specification"));
	layout->addRow(tr("Specification:"), crs_edit);
	
	layout->addRow(Util::Headline::create(tr("Symbols")));
	if (!initial_symbol || initial_symbol->getType() != Symbol::Point)
		initial_symbol = firstSymbolOfType(map, Symbol::Point);
	point_symbol_edit = new SymbolDropDown(&map, Symbol::Point, initial_symbol);
	layout->addRow(tr("Default point symbol:"), point_symbol_edit);
	text_symbol_edit = new SymbolDropDown(&map, Symbol::Text, firstSymbolOfType(map, Symbol::Text));
	layout->addRow(tr("Text symbol:"), text_symbol_edit);
	
	buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	layout->addRow(buttons);
	
	updateColumns();
	
	connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	connect(delimiter_combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ImportPointsDialog::updateColumns);
	connect(header_check, &QCheckBox::toggled, this, &ImportPointsDialog::updateColumns);
	for (auto* combo : { x_combo, y_combo, text_combo, crs_combo })
		connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ImportPointsDialog::updateWidgets);
	connect(crs_edit, &QLineEdit::textChanged, this, &ImportPointsDialog::updateWidgets);
	connect(point_symbol_edit, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ImportPointsDialog::updateWidgets);
	connect(text_symbol_edit, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ImportPointsDialog::updateWidgets);
	updateWidgets();
}


ImportPointsDialog::~ImportPointsDialog() = default;


QChar ImportPointsDialog::delimiter() const
{
	return delimiter_combo->currentData().toChar();
}


void ImportPointsDialog::updateColumns()
{
	updating = true;
	
	std::vector<QStringList> rows;
	for (auto const& line : lines)
	{
		if (line.trimmed().isEmpty())
			continue;
		rows.push_back(ops::splitDelimited(line, delimiter()));
		if (int(rows.size()) > preview_rows)
			break;
	}
	auto columns = 0;
	for (auto const& row : rows)
		columns = std::max(columns, int(row.size()));
	
	QStringList header;
	if (header_check->isChecked() && !rows.empty())
	{
		header = rows.front();
		rows.erase(rows.begin());
	}
	QStringList names;
	for (int i = 0; i < columns; ++i)
		names.push_back(i < header.size() && !header[i].isEmpty() ? header[i] : tr("Column %1").arg(i + 1));
	
	preview->clear();
	preview->setColumnCount(columns);
	preview->setRowCount(std::min(int(rows.size()), preview_rows));
	preview->setHorizontalHeaderLabels(names);
	for (int r = 0; r < preview->rowCount(); ++r)
	{
		for (int c = 0; c < rows[std::size_t(r)].size(); ++c)
			preview->setItem(r, c, new QTableWidgetItem(rows[std::size_t(r)][c]));
	}
	
	auto fill = [&names](QComboBox* combo, bool optional, int selected) {
		combo->clear();
		if (optional)
			combo->addItem(tr("None"), -1);
		for (int i = 0; i < names.size(); ++i)
			combo->addItem(names[i], i);
		combo->setCurrentIndex(std::max(0, combo->findData(selected)));
	};
	auto const guess_x = guessColumn(header, { "x", "e", "east", "easting", "lon", "long", "longitude" });
	auto const guess_y = guessColumn(header, { "y", "n", "north", "northing", "lat", "latitude" });
	fill(x_combo, false, guess_x >= 0 ? guess_x : 0);
	fill(y_combo, false, guess_y >= 0 ? guess_y : 1);
	fill(z_combo, true, guessColumn(header, { "z", "ele", "elevation", "height", "alt", "altitude" }));
	fill(text_combo, true, guessColumn(header, { "name", "text", "label" }));
	fill(code_combo, true, guessColumn(header, { "code", "symbol", "number" }));
	if (guessColumn(header, { "lon", "long", "longitude" }) >= 0 && guessColumn(header, { "lat", "latitude" }) >= 0)
	{
		auto const index = crs_combo->findData(int(ops::ImportPointsParams::Wgs84));
		if (index >= 0)
			crs_combo->setCurrentIndex(index);
	}
	
	updating = false;
	updateWidgets();
}


void ImportPointsDialog::updateWidgets()
{
	if (updating)
		return;
	auto const other = crs_combo->currentData().toInt() == ops::ImportPointsParams::OtherCrs;
	crs_edit->setEnabled(other);
	auto const with_text = text_combo->currentData().toInt() >= 0;
	text_symbol_edit->setEnabled(with_text);
	auto const ok = point_symbol_edit->symbol()
	                && x_combo->currentData().toInt() != y_combo->currentData().toInt()
	                && (!other || !crs_edit->text().trimmed().isEmpty())
	                && (!with_text || text_symbol_edit->symbol());
	buttons->button(QDialogButtonBox::Ok)->setEnabled(ok);
}


ops::ImportPointsParams ImportPointsDialog::params() const
{
	ops::ImportPointsParams params;
	params.delimiter = delimiter();
	params.header = header_check->isChecked();
	params.x_column = x_combo->currentData().toInt();
	params.y_column = y_combo->currentData().toInt();
	params.z_column = z_combo->currentData().toInt();
	params.text_column = text_combo->currentData().toInt();
	params.code_column = code_combo->currentData().toInt();
	params.crs = ops::ImportPointsParams::Crs(crs_combo->currentData().toInt());
	params.crs_spec = crs_edit->text().trimmed();
	params.default_symbol = point_symbol_edit->symbol();
	params.text_symbol = text_symbol_edit->symbol();
	return params;
}


}  // namespace OpenOrienteering
