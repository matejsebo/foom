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


#ifndef OPENORIENTEERING_CHECK_ISSUE_H
#define OPENORIENTEERING_CHECK_ISSUE_H

#include <vector>

#include <QAbstractTableModel>
#include <QByteArray>
#include <QList>
#include <QObject>
#include <QString>
#include <QUuid>
#include <QVariant>
#include <QVector>

#include "core/map_coord.h"

class QModelIndex;

namespace OpenOrienteering {

/**
 * A problem found by a checker.
 *
 * See the design notes, "Issue list".
 */
struct Issue
{
	enum Severity
	{
		Info,
		Warning,
		Error
	};
	
	QByteArray rule;
	Severity severity = Warning;
	QVector<QUuid> objects;
	MapCoordF where;
	QVector<MapCoordF> points;  ///< foom: for a gap, the closest points on the two objects, in their order.
	double measured = 0.0;
	double limit = 0.0;
	QString message;
	QString subject;  ///< What the issue is about when it is not an object: a symbol number, a colour, a template path.
};

/** Returns the lower-case name of the severity, as the CSV writes it. */
QString severityName(Issue::Severity severity);


/**
 * A table of issues with a filter by rule.
 */
class IssueModel : public QAbstractTableModel
{
Q_OBJECT
public:
	enum Column
	{
		RuleColumn,
		SeverityColumn,
		MessageColumn,
		MeasuredColumn,
		LimitColumn,
		ColumnCount
	};
	
	explicit IssueModel(QObject* parent = nullptr);
	~IssueModel() override;
	
	void setIssues(std::vector<Issue> issues);
	const std::vector<Issue>& issues() const { return all_issues; }
	
	/** Returns the issue shown in the row. */
	const Issue& issueAt(int row) const;
	
	/** Shows only the issues of the rule; an empty rule shows all. */
	void setRuleFilter(const QByteArray& rule);
	const QByteArray& ruleFilter() const { return rule_filter; }
	
	/** Returns the distinct rules of all issues, sorted. */
	QList<QByteArray> rules() const;
	
	/** Returns all issues as CSV with the header rule,severity,objects,x,y,measured,limit,message. */
	QByteArray toCsv() const;
	
	/** Emits issueActivated() for the row. */
	void activate(int row);
	
	int rowCount(const QModelIndex& parent = QModelIndex()) const override;
	int columnCount(const QModelIndex& parent = QModelIndex()) const override;
	QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
	QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

signals:
	void issueActivated(const QVector<QUuid>& objects, const OpenOrienteering::MapCoordF& where);

private:
	void rebuild();
	
	std::vector<Issue> all_issues;
	std::vector<int> shown;  ///< indexes into all_issues
	QByteArray rule_filter;
};


}  // namespace OpenOrienteering

#endif
