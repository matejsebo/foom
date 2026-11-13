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


#include "issue.h"

#include <algorithm>
#include <cstddef>
#include <utility>
#include <vector>

#include <Qt>
#include <QByteArray>
#include <QLatin1Char>
#include <QLatin1String>
#include <QModelIndex>
#include <QString>
#include <QStringList>
#include <QUuid>
#include <QVariant>


namespace OpenOrienteering {

QString severityName(Issue::Severity severity)
{
	switch (severity)
	{
	case Issue::Info:
		return QStringLiteral("info");
	case Issue::Warning:
		return QStringLiteral("warning");
	case Issue::Error:
		return QStringLiteral("error");
	}
	return {};
}

IssueModel::IssueModel(QObject* parent)
: QAbstractTableModel(parent)
{
	// nothing else
}

IssueModel::~IssueModel() = default;

void IssueModel::setIssues(std::vector<Issue> issues)
{
	beginResetModel();
	all_issues = std::move(issues);
	rebuild();
	endResetModel();
}

const Issue& IssueModel::issueAt(int row) const
{
	return all_issues[std::size_t(shown[std::size_t(row)])];
}

void IssueModel::setRuleFilter(const QByteArray& rule)
{
	if (rule == rule_filter)
		return;
	beginResetModel();
	rule_filter = rule;
	rebuild();
	endResetModel();
}

void IssueModel::rebuild()
{
	shown.clear();
	for (std::size_t i = 0; i < all_issues.size(); ++i)
	{
		if (rule_filter.isEmpty() || all_issues[i].rule == rule_filter)
			shown.push_back(int(i));
	}
}

QList<QByteArray> IssueModel::rules() const
{
	QList<QByteArray> rules;
	for (auto const& issue : all_issues)
	{
		if (!rules.contains(issue.rule))
			rules.append(issue.rule);
	}
	std::sort(rules.begin(), rules.end());
	return rules;
}

QByteArray IssueModel::toCsv() const
{
	auto const quoted = [](QString text) -> QString { return QLatin1Char('"') + text.replace(QLatin1Char('"'), QLatin1String("\"\"")) + QLatin1Char('"'); };
	QString csv = QStringLiteral("rule,severity,objects,x,y,measured,limit,message\n");
	for (auto const& issue : all_issues)
	{
		QStringList uuids;
		for (auto const& uuid : issue.objects)
			uuids << uuid.toString(QUuid::WithoutBraces);
		csv += QString::fromLatin1(issue.rule) + QLatin1Char(',')
	       + severityName(issue.severity) + QLatin1Char(',')
	       + uuids.join(QLatin1Char(' ')) + QLatin1Char(',')
	       + QString::number(issue.where.x(), 'f', 3) + QLatin1Char(',')
	       + QString::number(issue.where.y(), 'f', 3) + QLatin1Char(',')
	       + QString::number(issue.measured, 'f', 6) + QLatin1Char(',')
	       + QString::number(issue.limit, 'f', 6) + QLatin1Char(',')
	       + quoted(issue.message) + QLatin1Char('\n');
	}
	return csv.toUtf8();
}

void IssueModel::activate(int row)
{
	if (row < 0 || std::size_t(row) >= shown.size())
		return;
	auto const& issue = issueAt(row);
	emit issueActivated(issue.objects, issue.where);
}

int IssueModel::rowCount(const QModelIndex& parent) const
{
	return parent.isValid() ? 0 : int(shown.size());
}

int IssueModel::columnCount(const QModelIndex& parent) const
{
	return parent.isValid() ? 0 : ColumnCount;
}

QVariant IssueModel::data(const QModelIndex& index, int role) const
{
	if (!index.isValid() || role != Qt::DisplayRole || std::size_t(index.row()) >= shown.size())
		return {};
	auto const& issue = issueAt(index.row());
	switch (index.column())
	{
	case RuleColumn:
		return QString::fromLatin1(issue.rule);
	case SeverityColumn:
		return severityName(issue.severity);
	case MessageColumn:
		return issue.message;
	case MeasuredColumn:
		return issue.measured == 0.0 ? QVariant() : QVariant(issue.measured);
	case LimitColumn:
		return issue.limit == 0.0 ? QVariant() : QVariant(issue.limit);
	default:
		return {};
	}
}

QVariant IssueModel::headerData(int section, Qt::Orientation orientation, int role) const
{
	if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
		return {};
	switch (section)
	{
	case RuleColumn:
		return tr("Rule");
	case SeverityColumn:
		return tr("Severity");
	case MessageColumn:
		return tr("Message");
	case MeasuredColumn:
		return tr("Measured");
	case LimitColumn:
		return tr("Limit");
	default:
		return {};
	}
}


}  // namespace OpenOrienteering
