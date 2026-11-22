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


#include "merge_dialog.h"

#include <cstddef>
#include <utility>

#include <Qt>
#include <QAbstractItemView>
#include <QBrush>
#include <QColor>
#include <QComboBox>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLatin1Char>
#include <QLatin1String>
#include <QPushButton>
#include <QStringList>
#include <QTableView>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include "core/map.h"
#include "core/objects/object.h"
#include "core/symbols/symbol.h"
#include "util/util.h"


namespace OpenOrienteering {

namespace {

QString typeName(const Object& object)
{
	switch (object.getType())
	{
	case Object::Point:
		return MergeModel::tr("Point");
	case Object::Path:
		return MergeModel::tr("Path");
	case Object::Text:
		return MergeModel::tr("Text");
	default:
		return MergeModel::tr("Image");
	}
}

QString symbolName(const Object& object)
{
	auto const* symbol = object.getSymbol();
	if (!symbol)
		return {};
	QString name = symbol->getNumberAsString();
	name += QLatin1Char(' ');
	name += symbol->getPlainTextName();
	return name.trimmed();
}

QString sideText(MergeRow::Side side, const Object* object)
{
	QString text;
	switch (side)
	{
	case MergeRow::Absent:
		text = MergeModel::tr("absent");
		break;
	case MergeRow::Unchanged:
		text = MergeModel::tr("unchanged");
		break;
	case MergeRow::Changed:
		text = MergeModel::tr("changed");
		break;
	case MergeRow::Added:
		text = MergeModel::tr("added");
		break;
	case MergeRow::Deleted:
		text = MergeModel::tr("deleted");
		break;
	}
	if (object && object->modifiedTime() > 0)
	{
		text += QLatin1String(", ");
		text += QDateTime::fromSecsSinceEpoch(object->modifiedTime()).toString(Qt::ISODate);
	}
	return text;
}

QString statusText(const MergeRow& row)
{
	switch (row.status)
	{
	case MergeRow::Incoming:
		return MergeModel::tr("Incoming");
	case MergeRow::Outgoing:
		return MergeModel::tr("Outgoing");
	case MergeRow::Conflict:
		break;
	}
	return row.resolved ? MergeModel::tr("Conflict, decided") : MergeModel::tr("Conflict");
}

QString actionText(MergeRow::Action action)
{
	switch (action)
	{
	case MergeRow::KeepTheirs:
		return MergeModel::tr("Keep theirs");
	case MergeRow::KeepBoth:
		return MergeModel::tr("Keep both");
	case MergeRow::KeepMine:
		break;
	}
	return MergeModel::tr("Keep mine");
}

}  // namespace


// ### MergeModel ###

MergeModel::MergeModel(const Map& mine, const Map& theirs, MergePlan plan, QObject* parent)
: QAbstractTableModel(parent)
, merge_plan(std::move(plan))
{
	infos.reserve(merge_plan.rows.size());
	for (auto const& row : merge_plan.rows)
	{
		auto const* own = mine.findObject(row.uuid);
		auto const* other = theirs.findObject(row.uuid);
		Info info;
		if (own || other)
			info.type = typeName(own ? *own : *other);
		info.symbol = own ? symbolName(*own) : other ? symbolName(*other) : QString{};
		if (own && other && symbolName(*other) != info.symbol)
		{
			info.symbol += QLatin1String(" / ");
			info.symbol += symbolName(*other);
		}
		info.mine = sideText(row.mine, own);
		info.theirs = sideText(row.theirs, other);
		for (auto const* object : { own, other })
		{
			if (!object)
				continue;
			object->update();
			rectIncludeSafe(info.extent, object->getExtent());
		}
		infos.push_back(info);
	}
}

MergeModel::~MergeModel() = default;

int MergeModel::rowCount(const QModelIndex& parent) const
{
	return parent.isValid() ? 0 : int(merge_plan.rows.size());
}

int MergeModel::columnCount(const QModelIndex& parent) const
{
	return parent.isValid() ? 0 : int(ColumnCount);
}

QVariant MergeModel::data(const QModelIndex& index, int role) const
{
	if (!index.isValid() || index.row() < 0 || index.row() >= int(merge_plan.rows.size()))
		return {};
	auto const& row = merge_plan.rows[std::size_t(index.row())];
	auto const& info = infos[std::size_t(index.row())];
	switch (role)
	{
	case Qt::DisplayRole:
	case Qt::ToolTipRole:
		switch (index.column())
		{
		case ObjectColumn:
			return info.type;
		case SymbolColumn:
			return info.symbol;
		case MineColumn:
			return info.mine;
		case TheirsColumn:
			return info.theirs;
		case StatusColumn:
			return statusText(row);
		case ActionColumn:
			return actionText(row.action);
		default:
			return {};
		}
	case Qt::ForegroundRole:
		if (!row.resolved)
			return QBrush(QColor(Qt::red));
		return {};
	case StatusRole:
		return int(row.status);
	case ResolvedRole:
		return row.resolved;
	case ExtentRole:
		return info.extent;
	default:
		return {};
	}
}

QVariant MergeModel::headerData(int section, Qt::Orientation orientation, int role) const
{
	if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
		return QAbstractTableModel::headerData(section, orientation, role);
	switch (section)
	{
	case ObjectColumn:
		return tr("Object");
	case SymbolColumn:
		return tr("Symbol");
	case MineColumn:
		return tr("Mine");
	case TheirsColumn:
		return tr("Theirs");
	case StatusColumn:
		return tr("Status");
	case ActionColumn:
		return tr("Action");
	default:
		return {};
	}
}

void MergeModel::setAction(int row, MergeRow::Action action)
{
	if (row < 0 || row >= int(merge_plan.rows.size()))
		return;
	auto& entry = merge_plan.rows[std::size_t(row)];
	entry.action = action;
	entry.resolved = true;
	emit dataChanged(index(row, 0), index(row, ColumnCount - 1));
	emit decisionsChanged();
}

void MergeModel::acceptNonConflicting()
{
	for (auto& row : merge_plan.rows)
	{
		if (row.status == MergeRow::Incoming)
			row.action = MergeRow::KeepTheirs;
	}
	if (!merge_plan.rows.empty())
		emit dataChanged(index(0, 0), index(int(merge_plan.rows.size()) - 1, ColumnCount - 1));
	emit decisionsChanged();
}

void MergeModel::rejectIncoming()
{
	for (auto& row : merge_plan.rows)
	{
		if (row.status == MergeRow::Incoming)
			row.action = MergeRow::KeepMine;
	}
	if (!merge_plan.rows.empty())
		emit dataChanged(index(0, 0), index(int(merge_plan.rows.size()) - 1, ColumnCount - 1));
	emit decisionsChanged();
}


void MergeModel::setSymbolAction(int index, MergeSymbolRow::Action action)
{
	if (index < 0 || index >= int(merge_plan.symbols.size()))
		return;
	merge_plan.symbols[std::size_t(index)].action = action;
	emit decisionsChanged();
}


void MergeModel::setColorAction(int index, MergeSymbolRow::Action action)
{
	if (index < 0 || index >= int(merge_plan.colors.size()))
		return;
	merge_plan.colors[std::size_t(index)].action = action;
	emit decisionsChanged();
}


// ### MergeFilterModel ###

MergeFilterModel::MergeFilterModel(QObject* parent)
: QSortFilterProxyModel(parent)
{
	// nothing else
}

MergeFilterModel::~MergeFilterModel() = default;

void MergeFilterModel::setFilter(Filter new_filter)
{
	filter = new_filter;
	invalidateFilter();
}

bool MergeFilterModel::filterAcceptsRow(int source_row, const QModelIndex& source_parent) const
{
	auto const index = sourceModel()->index(source_row, 0, source_parent);
	switch (filter)
	{
	case Conflicts:
		return index.data(MergeModel::StatusRole).toInt() == int(MergeRow::Conflict);
	case Undecided:
		return !index.data(MergeModel::ResolvedRole).toBool();
	case AllChanges:
		break;
	}
	return true;
}


// ### MergeDialog ###

MergeDialog::MergeDialog(const Map& mine, const Map& theirs, const MergePlan& plan, QWidget* parent)
: QDialog(parent)
{
	setWindowTitle(tr("Merge changes"));
	setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
	
	model = new MergeModel(mine, theirs, plan, this);
	filter_model = new MergeFilterModel(this);
	filter_model->setSourceModel(model);
	
	auto* notice = new QLabel(tr("The base snapshot of this field copy is missing. Every difference is shown as a conflict, with the newer side filled in."));
	notice->setObjectName(QStringLiteral("two-way-notice"));
	notice->setWordWrap(true);
	notice->setVisible(plan.two_way);
	
	summary_label = new QLabel();
	summary_label->setObjectName(QStringLiteral("summary"));
	
	filter_combo = new QComboBox();
	filter_combo->setObjectName(QStringLiteral("filter"));
	filter_combo->addItem(tr("All changes"), int(MergeFilterModel::AllChanges));
	filter_combo->addItem(tr("Only conflicts"), int(MergeFilterModel::Conflicts));
	filter_combo->addItem(tr("Only undecided"), int(MergeFilterModel::Undecided));
	auto* filter_layout = new QHBoxLayout();
	filter_layout->addWidget(new QLabel(tr("Show:")));
	filter_layout->addWidget(filter_combo);
	filter_layout->addStretch(1);
	
	view = new QTableView();
	view->setObjectName(QStringLiteral("rows"));
	view->setModel(filter_model);
	view->setSelectionBehavior(QAbstractItemView::SelectRows);
	view->setSelectionMode(QAbstractItemView::ExtendedSelection);
	view->setEditTriggers(QAbstractItemView::NoEditTriggers);
	view->verticalHeader()->hide();
	// Every column as wide as its content, except the symbol's name,
	// which gives way so that the action stays in sight.
	view->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
	view->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
	view->setTextElideMode(Qt::ElideMiddle);
	
	auto* mine_button = new QPushButton(tr("Keep mine"));
	mine_button->setObjectName(QStringLiteral("keep-mine"));
	auto* theirs_button = new QPushButton(tr("Keep theirs"));
	theirs_button->setObjectName(QStringLiteral("keep-theirs"));
	auto* both_button = new QPushButton(tr("Keep both"));
	both_button->setObjectName(QStringLiteral("keep-both"));
	auto* accept_button = new QPushButton(tr("Accept all non-conflicting"));
	accept_button->setObjectName(QStringLiteral("accept-all"));
	auto* reject_button = new QPushButton(tr("Reject all incoming"));
	reject_button->setObjectName(QStringLiteral("reject-incoming"));
	auto* decide_layout = new QHBoxLayout();
	decide_layout->addWidget(new QLabel(tr("Selected rows:")));
	decide_layout->addWidget(mine_button);
	decide_layout->addWidget(theirs_button);
	decide_layout->addWidget(both_button);
	decide_layout->addStretch(1);
	decide_layout->addWidget(accept_button);
	decide_layout->addWidget(reject_button);
	
	// The symbols and colours which are new or differ. A definition which differs can be taken.
	definitions = new QTableWidget(0, 3);
	definitions->setObjectName(QStringLiteral("definitions"));
	definitions->setHorizontalHeaderLabels({ tr("Symbols and colours"), tr("Status"), tr("Use theirs") });
	definitions->setSelectionMode(QAbstractItemView::NoSelection);
	definitions->setEditTriggers(QAbstractItemView::NoEditTriggers);
	definitions->verticalHeader()->hide();
	definitions->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
	definitions->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
	for (std::size_t i = 0; i < plan.symbols.size(); ++i)
	{
		auto const& symbol = plan.symbols[i];
		QString name = tr("Symbol %1").arg(symbol.number);
		name += QLatin1Char(' ');
		name += symbol.name;
		auto const differs = symbol.status == MergeSymbolRow::Differs;
		auto const row = definitions->rowCount();
		definitions->insertRow(row);
		definitions->setItem(row, 0, new QTableWidgetItem(name.trimmed()));
		definitions->setItem(row, 1, new QTableWidgetItem(differs ? tr("Differs") : tr("New, imported when an object needs it")));
		auto* take = new QTableWidgetItem();
		take->setData(Qt::UserRole, int(i));
		take->setFlags(differs ? Qt::ItemIsEnabled | Qt::ItemIsUserCheckable : Qt::ItemFlags{});
		if (differs)
			take->setCheckState(symbol.action == MergeSymbolRow::KeepTheirs ? Qt::Checked : Qt::Unchecked);
		definitions->setItem(row, 2, take);
	}
	for (std::size_t i = 0; i < plan.colors.size(); ++i)
	{
		auto const& color = plan.colors[i];
		auto const differs = color.status == MergeColorRow::Differs;
		auto name = tr("Colour %1").arg(color.name);
		if (differs && color.own_name != color.name)
			name = tr("Colour %1 (here: %2)").arg(color.name, color.own_name);
		auto const row = definitions->rowCount();
		definitions->insertRow(row);
		definitions->setItem(row, 0, new QTableWidgetItem(name));
		definitions->setItem(row, 1, new QTableWidgetItem(differs ? tr("Differs") : tr("New, appended below the colours")));
		auto* take = new QTableWidgetItem();
		take->setData(Qt::UserRole, int(i));
		take->setData(Qt::UserRole + 1, true);
		take->setFlags(differs ? Qt::ItemIsEnabled | Qt::ItemIsUserCheckable : Qt::ItemFlags{});
		if (differs)
			take->setCheckState(color.action == MergeSymbolRow::KeepTheirs ? Qt::Checked : Qt::Unchecked);
		definitions->setItem(row, 2, take);
	}
	definitions->setVisible(definitions->rowCount() > 0);
	definitions->setMaximumHeight(160);
	
	auto* notes_label = new QLabel(tr("Templates, extents, bookmarks, guides, course data and groups are not merged."));
	notes_label->setObjectName(QStringLiteral("notes"));
	notes_label->setWordWrap(true);
	
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, Qt::Horizontal);
	apply_button = buttons->addButton(tr("Apply"), QDialogButtonBox::AcceptRole);
	apply_button->setObjectName(QStringLiteral("apply"));
	auto* pause_button = buttons->addButton(tr("Pause"), QDialogButtonBox::ActionRole);
	pause_button->setObjectName(QStringLiteral("pause"));
	
	auto* layout = new QVBoxLayout();
	layout->addWidget(notice);
	layout->addWidget(summary_label);
	layout->addLayout(filter_layout);
	layout->addWidget(view, 1);
	layout->addLayout(decide_layout);
	layout->addWidget(definitions);
	layout->addWidget(notes_label);
	layout->addWidget(buttons);
	setLayout(layout);
	resize(1000, 560);
	
	connect(filter_combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
		filter_model->setFilter(MergeFilterModel::Filter(filter_combo->itemData(index).toInt()));
	});
	connect(mine_button, &QPushButton::clicked, this, [this]() { decide(MergeRow::KeepMine); });
	connect(theirs_button, &QPushButton::clicked, this, [this]() { decide(MergeRow::KeepTheirs); });
	connect(both_button, &QPushButton::clicked, this, [this]() { decide(MergeRow::KeepBoth); });
	connect(accept_button, &QPushButton::clicked, model, &MergeModel::acceptNonConflicting);
	connect(reject_button, &QPushButton::clicked, model, &MergeModel::rejectIncoming);
	connect(model, &MergeModel::decisionsChanged, this, &MergeDialog::updateState);
	connect(view->selectionModel(), &QItemSelectionModel::currentRowChanged, this, &MergeDialog::currentRowChanged);
	connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	connect(pause_button, &QPushButton::clicked, this, [this]() { done(Paused); });
	connect(definitions, &QTableWidget::itemChanged, this, &MergeDialog::definitionChanged);
	
	updateState();
}

MergeDialog::~MergeDialog() = default;

void MergeDialog::decide(MergeRow::Action action)
{
	// The source rows first: deciding a row may take it out of the filtered view.
	std::vector<int> rows;
	for (auto const& index : view->selectionModel()->selectedRows())
		rows.push_back(filter_model->mapToSource(index).row());
	for (auto row : rows)
		model->setAction(row, action);
}

void MergeDialog::updateState()
{
	auto const& current = model->plan();
	summary_label->setText(tr("Incoming: %1. Outgoing: %2. Conflicts: %3, of which undecided: %4.")
	                       .arg(current.count(MergeRow::Incoming))
	                       .arg(current.count(MergeRow::Outgoing))
	                       .arg(current.count(MergeRow::Conflict))
	                       .arg(current.unresolved()));
	apply_button->setEnabled(current.unresolved() == 0);
	filter_model->invalidate();
}

void MergeDialog::definitionChanged(QTableWidgetItem* item)
{
	if (!item || item->column() != 2 || !(item->flags() & Qt::ItemIsUserCheckable))
		return;
	auto const action = item->checkState() == Qt::Checked ? MergeSymbolRow::KeepTheirs : MergeSymbolRow::KeepMine;
	if (item->data(Qt::UserRole + 1).toBool())
		model->setColorAction(item->data(Qt::UserRole).toInt(), action);
	else
		model->setSymbolAction(item->data(Qt::UserRole).toInt(), action);
}

void MergeDialog::currentRowChanged(const QModelIndex& current)
{
	if (!current.isValid())
		return;
	auto const extent = current.data(MergeModel::ExtentRole).toRectF();
	if (extent.isValid())
		emit showExtent(extent);
}


}  // namespace OpenOrienteering
