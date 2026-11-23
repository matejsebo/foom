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


#include "issues_widget.h"

#include <vector>

#include <Qt>
#include <QByteArray>
#include <QComboBox>
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QMessageBox>
#include <QModelIndex>
#include <QPushButton>
#include <QTableView>
#include <QVBoxLayout>

#include "check/issue.h"
#include "gui/map/map_editor.h"


namespace OpenOrienteering {

IssuesWidget::IssuesWidget(MapEditorController* editor, IssueModel* model, QWidget* parent)
: QWidget(parent)
, editor(editor)
, model(model)
{
	filter_box = new QComboBox();
	auto* check_button = new QPushButton(tr("Check again"));
	auto* filter_row = new QHBoxLayout();
	filter_row->addWidget(filter_box, 1);
	filter_row->addWidget(check_button);
	
	table = new QTableView();
	table->setModel(model);
	table->setSelectionBehavior(QAbstractItemView::SelectRows);
	table->setSelectionMode(QAbstractItemView::SingleSelection);
	table->setEditTriggers(QAbstractItemView::NoEditTriggers);
	table->horizontalHeader()->setStretchLastSection(true);
	table->verticalHeader()->hide();
	
	fix_button = new QPushButton(tr("Fix"));
	fix_all_button = new QPushButton(tr("Fix all shown"));
	assign_button = new QPushButton(tr("Assign symbol"));
	assign_button->setObjectName(QStringLiteral("assign_symbol_button"));
	assign_button->setToolTip(tr("Gives the objects of a symbol issue the symbol selected in the symbol pane."));
	auto* export_button = new QPushButton(tr("Export CSV..."));
	auto* button_row = new QHBoxLayout();
	button_row->addWidget(fix_button);
	button_row->addWidget(fix_all_button);
	button_row->addWidget(assign_button);
	button_row->addStretch();
	button_row->addWidget(export_button);
	
	auto* layout = new QVBoxLayout(this);
	layout->addLayout(filter_row);
	layout->addWidget(table, 1);
	layout->addLayout(button_row);
	
	connect(filter_box, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &IssuesWidget::filterChanged);
	connect(check_button, &QPushButton::clicked, editor, &MapEditorController::checkAgain);
	connect(table, &QTableView::activated, this, &IssuesWidget::rowActivated);
	connect(fix_button, &QPushButton::clicked, this, &IssuesWidget::fixSelected);
	connect(fix_all_button, &QPushButton::clicked, this, &IssuesWidget::fixAllShown);
	connect(assign_button, &QPushButton::clicked, this, &IssuesWidget::assignSymbolSelected);
	connect(export_button, &QPushButton::clicked, this, &IssuesWidget::exportCsv);
	connect(model, &IssueModel::modelReset, this, &IssuesWidget::updateFilterBox);
	updateFilterBox();
}

IssuesWidget::~IssuesWidget() = default;

void IssuesWidget::updateFilterBox()
{
	auto const current = model->ruleFilter();
	filter_box->blockSignals(true);
	filter_box->clear();
	filter_box->addItem(tr("All rules"), QByteArray());
	for (auto const& rule : model->rules())
		filter_box->addItem(QString::fromLatin1(rule), rule);
	auto const index = filter_box->findData(current);
	filter_box->setCurrentIndex(index < 0 ? 0 : index);
	filter_box->blockSignals(false);
	if (index < 0 && !current.isEmpty())
		model->setRuleFilter(QByteArray());
	table->resizeColumnsToContents();
}

void IssuesWidget::filterChanged(int index)
{
	model->setRuleFilter(filter_box->itemData(index).toByteArray());
}

void IssuesWidget::rowActivated(const QModelIndex& index)
{
	model->activate(index.row());
}

void IssuesWidget::fixSelected()
{
	auto const index = table->selectionModel() ? table->selectionModel()->currentIndex() : QModelIndex();
	if (!index.isValid())
		return;
	auto cancelled = false;
	if (!editor->fixIssueRow(index.row(), &cancelled) && !cancelled)
		QMessageBox::information(this, tr("Issues"), tr("This issue has no automatic fix, or nothing was left to fix."));
}

void IssuesWidget::assignSymbolSelected()
{
	// the design notes, "Map check", the assign-symbol fix.
	auto const index = table->selectionModel() ? table->selectionModel()->currentIndex() : QModelIndex();
	if (!index.isValid())
		return;
	if (!editor->assignSymbolToIssueRow(index.row()))
		QMessageBox::information(this, tr("Issues"), tr("Select an issue of a missing or wrong symbol, and in the symbol pane a symbol which fits its objects."));
}

void IssuesWidget::fixAllShown()
{
	editor->fixShownIssues();
}

void IssuesWidget::exportCsv()
{
	auto const path = QFileDialog::getSaveFileName(this, tr("Export issues"), QString{}, tr("CSV files (*.csv)"));
	if (path.isEmpty())
		return;
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) || file.write(model->toCsv()) < 0)
		QMessageBox::warning(this, tr("Error"), tr("Cannot write the file %1.").arg(path));
}


}  // namespace OpenOrienteering
