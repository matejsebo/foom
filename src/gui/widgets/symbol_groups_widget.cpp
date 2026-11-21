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

#include "symbol_groups_widget.h"

#include <cstddef>
#include <vector>

#include <QGridLayout>
#include <QInputDialog>
#include <QLineEdit>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "core/map.h"
#include "core/symbol_tree.h"
#include "core/symbols/symbol.h"
#include "gui/widgets/symbol_widget.h"


namespace OpenOrienteering {

namespace {

constexpr int group_role = Qt::UserRole;

}  // namespace



SymbolGroupsWidget::SymbolGroupsWidget(Map& map, SymbolWidget* symbol_widget, QWidget* parent)
: QWidget(parent)
, map(map)
, symbol_widget(symbol_widget)
{
	// the design notes, "Symbol tree".
	tree_widget = new QTreeWidget();
	tree_widget->setHeaderHidden(true);
	connect(tree_widget, &QTreeWidget::currentItemChanged, this, &SymbolGroupsWidget::applyFilter);
	connect(tree_widget, &QTreeWidget::itemExpanded, this, [this](QTreeWidgetItem* item) { itemExpansionChanged(item, true); });
	connect(tree_widget, &QTreeWidget::itemCollapsed, this, [this](QTreeWidgetItem* item) { itemExpansionChanged(item, false); });
	
	auto* buttons = new QGridLayout();
	auto const button = [this, buttons](const QString& text, int row, int column, void (SymbolGroupsWidget::*slot)()) {
		auto* b = new QPushButton(text);
		connect(b, &QPushButton::clicked, this, slot);
		buttons->addWidget(b, row, column);
	};
	button(tr("New group..."), 0, 0, &SymbolGroupsWidget::newGroup);
	button(tr("New subgroup..."), 0, 1, &SymbolGroupsWidget::newSubgroup);
	button(tr("Rename..."), 1, 0, &SymbolGroupsWidget::renameGroup);
	button(tr("Delete"), 1, 1, &SymbolGroupsWidget::deleteGroup);
	button(tr("Add selected symbols"), 2, 0, &SymbolGroupsWidget::addSelectedSymbols);
	button(tr("Remove selected symbols"), 2, 1, &SymbolGroupsWidget::removeSelectedSymbols);
	
	auto* layout = new QVBoxLayout(this);
	layout->addWidget(tree_widget, 1);
	layout->addLayout(buttons);
	rebuild();
}

SymbolGroupsWidget::~SymbolGroupsWidget() = default;

void SymbolGroupsWidget::rebuild()
{
	auto const current = currentGroup();
	rebuilding = true;
	tree_widget->clear();
	auto* root = new QTreeWidgetItem(tree_widget, QStringList{ tr("All symbols") });
	root->setData(0, group_role, -1);
	auto const& groups = map.symbolTree();
	std::vector<QTreeWidgetItem*> items(groups.size(), nullptr);
	// Parents before children, whatever the order of the list.
	for (std::size_t pass = 0; pass <= groups.size(); ++pass)
	{
		for (std::size_t g = 0; g < groups.size(); ++g)
		{
			if (items[g])
				continue;
			auto const parent = groups[g].parent;
			QTreeWidgetItem* parent_item = parent < 0 ? root : (std::size_t(parent) < items.size() ? items[std::size_t(parent)] : root);
			if (!parent_item)
				continue;
			items[g] = new QTreeWidgetItem(parent_item, QStringList{ groups[g].name });
			items[g]->setData(0, group_role, int(g));
		}
	}
	root->setExpanded(true);
	for (std::size_t g = 0; g < groups.size(); ++g)
	{
		if (items[g])
			items[g]->setExpanded(groups[g].expanded);
	}
	rebuilding = false;
	selectGroup(current < int(groups.size()) ? current : -1);
}

void SymbolGroupsWidget::selectGroup(int group)
{
	QTreeWidgetItemIterator it(tree_widget);
	while (*it)
	{
		if ((*it)->data(0, group_role).toInt() == group)
		{
			tree_widget->setCurrentItem(*it);
			break;
		}
		++it;
	}
	applyFilter();
}

int SymbolGroupsWidget::currentGroup() const
{
	auto const* item = tree_widget->currentItem();
	return item ? item->data(0, group_role).toInt() : -1;
}

void SymbolGroupsWidget::applyFilter()
{
	if (rebuilding || !symbol_widget)
		return;
	auto const group = currentGroup();
	symbol_widget->setGroupFilter(group >= 0, symbolGroupNumbers(map, group));
}

void SymbolGroupsWidget::itemExpansionChanged(QTreeWidgetItem* item, bool expanded)
{
	auto const group = item->data(0, group_role).toInt();
	if (rebuilding || group < 0 || group >= int(map.symbolTree().size()))
		return;
	auto groups = map.symbolTree();
	groups[std::size_t(group)].expanded = expanded;
	map.setSymbolTree(groups);
}

void SymbolGroupsWidget::newGroup()
{
	bool ok = false;
	auto const name = QInputDialog::getText(this, tr("New group"), tr("Name:"), QLineEdit::Normal, {}, &ok);
	if (!ok || name.trimmed().isEmpty())
		return;
	auto const group = addSymbolGroup(map, name.trimmed());
	rebuild();
	selectGroup(group);
}

void SymbolGroupsWidget::newSubgroup()
{
	auto const parent = currentGroup();
	bool ok = false;
	auto const name = QInputDialog::getText(this, tr("New subgroup"), tr("Name:"), QLineEdit::Normal, {}, &ok);
	if (!ok || name.trimmed().isEmpty())
		return;
	auto const group = addSymbolGroup(map, name.trimmed(), parent);
	rebuild();
	selectGroup(group);
}

void SymbolGroupsWidget::renameGroup()
{
	auto const group = currentGroup();
	if (group < 0)
		return;
	bool ok = false;
	auto const name = QInputDialog::getText(this, tr("Rename group"), tr("Name:"), QLineEdit::Normal, map.symbolTree()[std::size_t(group)].name, &ok);
	if (!ok || name.trimmed().isEmpty())
		return;
	renameSymbolGroup(map, group, name.trimmed());
	rebuild();
}

void SymbolGroupsWidget::deleteGroup()
{
	auto const group = currentGroup();
	if (group < 0)
		return;
	removeSymbolGroup(map, group);
	rebuild();
	selectGroup(-1);
}

void SymbolGroupsWidget::addSelectedSymbols()
{
	auto const group = currentGroup();
	if (group < 0 || !symbol_widget)
		return;
	std::vector<const Symbol*> symbols;
	for (int i = 0; i < map.getNumPublicSymbols(); ++i)
	{
		if (symbol_widget->isSymbolSelected(map.getSymbol(i)))
			symbols.push_back(map.getSymbol(i));
	}
	addToSymbolGroup(map, group, symbols);
	applyFilter();
}

void SymbolGroupsWidget::removeSelectedSymbols()
{
	auto const group = currentGroup();
	if (group < 0 || !symbol_widget)
		return;
	std::vector<const Symbol*> symbols;
	for (int i = 0; i < map.getNumPublicSymbols(); ++i)
	{
		if (symbol_widget->isSymbolSelected(map.getSymbol(i)))
			symbols.push_back(map.getSymbol(i));
	}
	removeFromSymbolGroup(map, group, symbols);
	applyFilter();
}


}  // namespace OpenOrienteering
