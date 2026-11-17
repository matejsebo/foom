/*
 *    Copyright 2026 Matej Šebo
 *
 *    This file is part of FOOM, the Fantastic Open Orienteering Mapper,
 *    a fork of OpenOrienteering Mapper.
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


#ifndef FOOM_MERGE_DIALOG_H
#define FOOM_MERGE_DIALOG_H

#include <vector>

#include <QAbstractTableModel>
#include <QDialog>
#include <QModelIndex>
#include <QObject>
#include <QRectF>
#include <QSortFilterProxyModel>
#include <QString>
#include <QVariant>

#include "sync/merge.h"

class QComboBox;
class QLabel;
class QPushButton;
class QTableView;
class QWidget;

namespace OpenOrienteering {

class Map;


/**
 * The differences of a merge as a table: one row per object, with what
 * happened on each side, the status and the chosen action.
 *
 * See the design notes, "Merge changes from file: detail", second change.
 */
class MergeModel : public QAbstractTableModel
{
Q_OBJECT
public:
	enum Column { ObjectColumn, SymbolColumn, MineColumn, TheirsColumn, StatusColumn, ActionColumn, ColumnCount };
	
	enum Role
	{
		StatusRole = Qt::UserRole,  ///< The MergeRow::Status as int.
		ResolvedRole,               ///< False for an undecided conflict.
		ExtentRole                  ///< The extent of the object in both versions, as QRectF.
	};
	
	MergeModel(const Map& mine, const Map& theirs, MergePlan plan, QObject* parent = nullptr);
	~MergeModel() override;
	
	int rowCount(const QModelIndex& parent = {}) const override;
	int columnCount(const QModelIndex& parent = {}) const override;
	QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
	QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
	
	/** The plan with the decisions made so far. */
	const MergePlan& plan() const { return merge_plan; }
	
	/** Gives the row the action and marks it decided. */
	void setAction(int row, MergeRow::Action action);
	
	/** Gives every incoming row "keep theirs". */
	void acceptNonConflicting();
	
	/** Gives every incoming row "keep mine". */
	void rejectIncoming();
	
signals:
	void decisionsChanged();
	
private:
	struct Info
	{
		QString type;
		QString symbol;
		QString mine;
		QString theirs;
		QRectF extent;
	};
	
	MergePlan merge_plan;
	std::vector<Info> infos;
};


/** Shows all rows, only the conflicts, or only the undecided rows. */
class MergeFilterModel : public QSortFilterProxyModel
{
Q_OBJECT
public:
	enum Filter { AllChanges, Conflicts, Undecided };
	
	explicit MergeFilterModel(QObject* parent = nullptr);
	~MergeFilterModel() override;
	
	void setFilter(Filter filter);
	
protected:
	bool filterAcceptsRow(int source_row, const QModelIndex& source_parent) const override;
	
private:
	Filter filter = AllChanges;
};


/**
 * The dialog of "Merge changes from file".
 *
 * exec() returns QDialog::Accepted for Apply, MergeDialog::Paused for
 * Pause and QDialog::Rejected otherwise. The caller carries out plan().
 */
class MergeDialog : public QDialog
{
Q_OBJECT
public:
	enum { Paused = 2 };
	
	MergeDialog(const Map& mine, const Map& theirs, const MergePlan& plan, QWidget* parent = nullptr);
	~MergeDialog() override;
	
	const MergePlan& plan() const { return model->plan(); }
	
signals:
	/** A row was selected: the extent of its object, to be shown in the map. */
	void showExtent(const QRectF& extent);
	
private:
	void decide(MergeRow::Action action);
	void updateState();
	void currentRowChanged(const QModelIndex& current);
	
	MergeModel* model;
	MergeFilterModel* filter_model;
	QTableView* view;
	QComboBox* filter_combo;
	QLabel* summary_label;
	QPushButton* apply_button;
};


}  // namespace OpenOrienteering

#endif
