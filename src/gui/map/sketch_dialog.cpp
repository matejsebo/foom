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


#include "sketch_dialog.h"

#include <cstddef>

#include <Qt>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLatin1String>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

#include "core/map.h"
#include "core/symbols/symbol.h"
#include "gui/widgets/symbol_dropdown.h"


namespace OpenOrienteering {

SketchManageDialog::SketchManageDialog(Map& map, QWidget* parent)
: QDialog(parent)
, map(map)
{
	setWindowTitle(tr("Manage sketch"));
	setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
	
	list = new QListWidget();
	list->setObjectName(QStringLiteral("items"));
	
	symbol_box = new SymbolDropDown(&map, Symbol::Point | Symbol::Line | Symbol::Area | Symbol::Combined | Symbol::Text);
	symbol_box->setObjectName(QStringLiteral("symbol"));
	convert_button = new QPushButton(tr("Convert to symbol"));
	convert_button->setObjectName(QStringLiteral("convert"));
	auto* convert_layout = new QHBoxLayout();
	convert_layout->addWidget(new QLabel(tr("Symbol:")));
	convert_layout->addWidget(symbol_box, 1);
	convert_layout->addWidget(convert_button);
	delete_button = new QPushButton(tr("Delete"));
	delete_button->setObjectName(QStringLiteral("delete"));
	convert_layout->addWidget(delete_button);
	
	message_label = new QLabel();
	message_label->setObjectName(QStringLiteral("message"));
	message_label->setWordWrap(true);
	
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, Qt::Horizontal);
	
	auto* layout = new QVBoxLayout();
	layout->addWidget(new QLabel(tr("Strokes and notes of the sketch layer. A stroke becomes a line or an area, a note a text or a point.")));
	layout->addWidget(list, 1);
	layout->addLayout(convert_layout);
	layout->addWidget(message_label);
	layout->addWidget(buttons);
	setLayout(layout);
	resize(480, 420);
	
	connect(list, &QListWidget::currentRowChanged, this, &SketchManageDialog::rowChanged);
	connect(convert_button, &QPushButton::clicked, this, &SketchManageDialog::convert);
	connect(delete_button, &QPushButton::clicked, this, &SketchManageDialog::deleteItem);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	
	refresh();
}

SketchManageDialog::~SketchManageDialog() = default;

void SketchManageDialog::refresh()
{
	auto const row = list->currentRow();
	items = sketchItems(map);
	list->clear();
	for (auto const& item : items)
	{
		QString text = item.kind == SketchItem::Note ? tr("Note") : tr("Stroke");
		text += QLatin1String(": ");
		text += item.kind == SketchItem::Note ? item.label : item.label.section(QLatin1String(", "), 1);
		list->addItem(text);
	}
	if (!items.empty())
		list->setCurrentRow(qBound(0, row, int(items.size()) - 1));
	convert_button->setEnabled(!items.empty());
	delete_button->setEnabled(!items.empty());
}

void SketchManageDialog::rowChanged(int row)
{
	if (row < 0 || row >= int(items.size()))
		return;
	auto const& extent = items[std::size_t(row)].extent;
	if (extent.isValid())
		emit showExtent(extent);
}

void SketchManageDialog::convert()
{
	auto const row = list->currentRow();
	if (row < 0 || row >= int(items.size()))
		return;
	auto const* symbol = symbol_box->symbol();
	if (!symbol)
	{
		message_label->setText(tr("Choose a symbol first."));
		return;
	}
	QString error;
	if (!convertSketchItem(map, items[std::size_t(row)], symbol, &error))
	{
		message_label->setText(error);
		return;
	}
	message_label->clear();
	refresh();
}

void SketchManageDialog::deleteItem()
{
	auto const row = list->currentRow();
	if (row < 0 || row >= int(items.size()))
		return;
	deleteSketchItem(map, items[std::size_t(row)]);
	message_label->clear();
	refresh();
}


}  // namespace OpenOrienteering
