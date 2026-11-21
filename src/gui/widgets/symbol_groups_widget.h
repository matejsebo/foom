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

#ifndef FOOM_GUI_SYMBOL_GROUPS_WIDGET_H
#define FOOM_GUI_SYMBOL_GROUPS_WIDGET_H

#include <QObject>
#include <QWidget>

class QTreeWidget;
class QTreeWidgetItem;

namespace OpenOrienteering {

class Map;
class SymbolWidget;


/**
 * The symbol groups dock: the symbol tree with "All symbols" at its root; the
 * chosen group filters the symbol pane to its symbols and its subgroups'.
 *
 * See the design notes, "Symbol tree".
 */
class SymbolGroupsWidget : public QWidget
{
Q_OBJECT
public:
	SymbolGroupsWidget(Map& map, SymbolWidget* symbol_widget, QWidget* parent = nullptr);
	~SymbolGroupsWidget() override;
	
	/// Rebuilds the tree from the map.
	void rebuild();
	
	/// Chooses a group by index, -1 for all symbols, and filters the symbol pane.
	void selectGroup(int group);
	
	/// The chosen group, -1 for all symbols.
	int currentGroup() const;
	
	/// The tree, for tests.
	const QTreeWidget* tree() const { return tree_widget; }
	
	void newGroup();
	void newSubgroup();
	void renameGroup();
	void deleteGroup();
	void addSelectedSymbols();
	void removeSelectedSymbols();
	
private:
	void applyFilter();
	void itemExpansionChanged(QTreeWidgetItem* item, bool expanded);
	
	Map& map;
	SymbolWidget* symbol_widget;
	QTreeWidget* tree_widget;
	bool rebuilding = false;
};


}  // namespace OpenOrienteering

#endif
