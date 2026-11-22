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


#include "merge_decisions.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QLatin1Char>
#include <QLatin1String>
#include <QUuid>

#include "sync/merge.h"


namespace OpenOrienteering {

namespace {

QLatin1String actionName(MergeRow::Action action)
{
	switch (action)
	{
	case MergeRow::KeepTheirs:
		return QLatin1String("theirs");
	case MergeRow::KeepBoth:
		return QLatin1String("both");
	case MergeRow::KeepMine:
		break;
	}
	return QLatin1String("mine");
}

MergeRow::Action actionNamed(const QString& name)
{
	if (name == QLatin1String("theirs"))
		return MergeRow::KeepTheirs;
	if (name == QLatin1String("both"))
		return MergeRow::KeepBoth;
	return MergeRow::KeepMine;
}

}  // namespace


QString decisionsFilePath(const QString& map_path, const QString& id)
{
	const QFileInfo info(map_path);
	QString path = info.path();
	path += QLatin1Char('/');
	path += info.completeBaseName();
	path += QLatin1String(".merge-");
	path += id;
	path += QLatin1String(".json");
	return path;
}

bool saveDecisions(const QString& path, const MergePlan& plan, QString* error)
{
	QJsonArray rows;
	for (auto const& row : plan.rows)
	{
		QJsonObject entry;
		entry.insert(QLatin1String("uuid"), row.uuid.toString(QUuid::WithoutBraces));
		entry.insert(QLatin1String("status"), int(row.status));
		entry.insert(QLatin1String("mine"), int(row.mine));
		entry.insert(QLatin1String("theirs"), int(row.theirs));
		entry.insert(QLatin1String("action"), actionName(row.action));
		entry.insert(QLatin1String("decided"), row.resolved);
		rows.append(entry);
	}
	QJsonArray symbols;
	for (auto const& symbol : plan.symbols)
	{
		if (symbol.status != MergeSymbolRow::Differs)
			continue;
		QJsonObject entry;
		entry.insert(QLatin1String("number"), symbol.number);
		entry.insert(QLatin1String("action"), symbol.action == MergeSymbolRow::KeepTheirs ? QLatin1String("theirs") : QLatin1String("mine"));
		symbols.append(entry);
	}
	QJsonArray colors;
	for (auto const& color : plan.colors)
	{
		if (color.status != MergeColorRow::Differs)
			continue;
		QJsonObject entry;
		entry.insert(QLatin1String("name"), color.name);
		entry.insert(QLatin1String("action"), color.action == MergeSymbolRow::KeepTheirs ? QLatin1String("theirs") : QLatin1String("mine"));
		colors.append(entry);
	}
	QJsonObject document;
	document.insert(QLatin1String("rows"), rows);
	document.insert(QLatin1String("symbols"), symbols);
	document.insert(QLatin1String("colors"), colors);
	
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) || file.write(QJsonDocument(document).toJson()) < 0)
	{
		if (error)
			*error = QCoreApplication::translate("OpenOrienteering::Merge", "Cannot write %1.").arg(path);
		return false;
	}
	return true;
}

int loadDecisions(const QString& path, MergePlan& plan)
{
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly))
		return 0;
	auto const document = QJsonDocument::fromJson(file.readAll()).object();
	auto const rows = document.value(QLatin1String("rows")).toArray();
	auto taken = 0;
	for (auto const& value : rows)
	{
		auto const entry = value.toObject();
		auto const uuid = QUuid(entry.value(QLatin1String("uuid")).toString());
		for (auto& row : plan.rows)
		{
			if (row.uuid != uuid)
				continue;
			if (int(row.status) == entry.value(QLatin1String("status")).toInt(-1)
			    && int(row.mine) == entry.value(QLatin1String("mine")).toInt(-1)
			    && int(row.theirs) == entry.value(QLatin1String("theirs")).toInt(-1))
			{
				row.action = actionNamed(entry.value(QLatin1String("action")).toString());
				row.resolved = entry.value(QLatin1String("decided")).toBool(row.resolved);
				++taken;
			}
			break;
		}
	}
	for (auto const& value : document.value(QLatin1String("symbols")).toArray())
	{
		auto const entry = value.toObject();
		auto const number = entry.value(QLatin1String("number")).toString();
		auto const action = entry.value(QLatin1String("action")).toString() == QLatin1String("theirs")
		                    ? MergeSymbolRow::KeepTheirs : MergeSymbolRow::KeepMine;
		for (auto& symbol : plan.symbols)
		{
			if (symbol.status == MergeSymbolRow::Differs && symbol.number == number)
				symbol.action = action;
		}
	}
	for (auto const& value : document.value(QLatin1String("colors")).toArray())
	{
		auto const entry = value.toObject();
		auto const name = entry.value(QLatin1String("name")).toString();
		auto const action = entry.value(QLatin1String("action")).toString() == QLatin1String("theirs")
		                    ? MergeSymbolRow::KeepTheirs : MergeSymbolRow::KeepMine;
		for (auto& color : plan.colors)
		{
			if (color.status == MergeColorRow::Differs && color.name == name)
				color.action = action;
		}
	}
	return taken;
}


}  // namespace OpenOrienteering
