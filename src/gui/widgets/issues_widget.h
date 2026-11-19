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


#ifndef OPENORIENTEERING_ISSUES_WIDGET_H
#define OPENORIENTEERING_ISSUES_WIDGET_H

#include <QObject>
#include <QString>
#include <QWidget>

class QComboBox;
class QModelIndex;
class QPushButton;
class QTableView;

namespace OpenOrienteering {

class IssueModel;
class MapEditorController;


/**
 * The Issues dock: the issue table with a rule filter and the fix buttons.
 *
 * See the design notes, "Issue list".
 */
class IssuesWidget : public QWidget
{
Q_OBJECT
public:
	IssuesWidget(MapEditorController* editor, IssueModel* model, QWidget* parent = nullptr);
	~IssuesWidget() override;

public slots:
	/** Rebuilds the rule filter box from the model. */
	void updateFilterBox();

private slots:
	void filterChanged(int index);
	void rowActivated(const QModelIndex& index);
	void fixSelected();
	void fixAllShown();
	void exportCsv();

private:
	MapEditorController* editor;
	IssueModel* model;
	QComboBox* filter_box;
	QTableView* table;
	QPushButton* fix_button;
	QPushButton* fix_all_button;
};


}  // namespace OpenOrienteering

#endif
