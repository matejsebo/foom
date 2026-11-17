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


#include "field_copy.h"

#include <algorithm>
#include <vector>

#include <QtGlobal>
#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QLatin1Char>
#include <QLatin1String>
#include <QPointF>
#include <QUuid>

#include "core/georeferencing.h"
#include "core/map.h"
#include "core/map_coord.h"
#include "fileformats/file_import_export.h"
#include "fileformats/xml_file_format.h"


namespace OpenOrienteering {

namespace {

QString tr(const char* text)
{
	return QCoreApplication::translate("OpenOrienteering::Merge", text);
}

bool fail(QString* error, const QString& message)
{
	if (error)
		*error = message;
	return false;
}

bool copyFile(const QString& from, const QString& to, QString* error)
{
	if (QFile::exists(to) && !QFile::remove(to))
		return fail(error, tr("Cannot replace %1.").arg(to));
	if (!QFile::copy(from, to))
		return fail(error, tr("Cannot write %1.").arg(to));
	return true;
}

bool sameFrame(const Map& mine, const Map& theirs)
{
	auto const& a = mine.getGeoreferencing();
	auto const& b = theirs.getGeoreferencing();
	return mine.getScaleDenominator() == theirs.getScaleDenominator()
	       && a.getState() == b.getState()
	       && a.getProjectedCRSSpec() == b.getProjectedCRSSpec()
	       && a.getMapRefPoint() == b.getMapRefPoint()
	       && QPointF(a.getProjectedRefPoint() - b.getProjectedRefPoint()).manhattanLength() < 0.001
	       && qAbs(a.getGrivation() - b.getGrivation()) < 0.005
	       && qAbs(a.getDeclination() - b.getDeclination()) < 0.005;
}

}  // namespace


QString baseFilePath(const QString& map_path, const QString& id)
{
	const QFileInfo info(map_path);
	QString path = info.path();
	path += QLatin1Char('/');
	path += info.completeBaseName();
	path += QLatin1String(".base-");
	path += id;
	path += QLatin1String(".xmap");
	return path;
}

QString createFieldCopy(Map& map, const QString& map_path, const QString& copy_path, QString* error)
{
	if (map_path.isEmpty())
	{
		fail(error, tr("The map has no file yet. Save it first."));
		return {};
	}
	if (QFileInfo(copy_path) == QFileInfo(map_path))
	{
		fail(error, tr("The copy needs a file of its own."));
		return {};
	}
	
	SyncCopy record;
	record.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
	record.created = QDateTime::currentSecsSinceEpoch();
	auto const base_path = baseFilePath(map_path, record.id);
	record.base = QFileInfo(base_path).fileName();
	
	// The copy lists its own id only.
	auto const open_copies = map.syncCopies();
	auto own_record = record;
	own_record.base.clear();
	map.setSyncCopies({ own_record });
	XMLFileFormat format;
	auto exporter = format.makeExporter(copy_path, &map, nullptr);
	auto const written = exporter && exporter->doExport();
	map.setSyncCopies(open_copies);
	if (!written)
	{
		fail(error, tr("Cannot write %1.").arg(copy_path));
		return {};
	}
	if (!copyFile(copy_path, base_path, error))
		return {};
	
	auto copies = open_copies;
	copies.push_back(record);
	map.setSyncCopies(copies);
	map.setHasUnsavedChanges(true);
	return record.id;
}

bool closeFieldCopy(Map& map, const QString& map_path, const QString& id, QString* error)
{
	auto copies = map.syncCopies();
	if (copies.empty())
		return fail(error, tr("The map has no open field copy."));
	if (id.isEmpty() && copies.size() > 1)
		return fail(error, tr("The map has several open field copies. Name the one to close."));
	auto const found = id.isEmpty() ? copies.begin()
	                                : std::find_if(copies.begin(), copies.end(), [&id](const SyncCopy& copy) { return copy.id == id; });
	if (found == copies.end())
		return fail(error, tr("The map has no open field copy %1.").arg(id));
	
	if (!map_path.isEmpty())
		QFile::remove(baseFilePath(map_path, found->id));
	copies.erase(found);
	map.setSyncCopies(copies);
	map.setHasUnsavedChanges(true);
	return true;
}

bool loadForMerge(const Map& mine, const QString& map_path, const QString& copy_path, MergeInput& input, QString* error)
{
	input = {};
	auto theirs = std::make_unique<Map>();
	if (!theirs->loadFrom(copy_path))
		return fail(error, tr("Cannot read %1.").arg(copy_path));
	
	auto const& open_copies = mine.syncCopies();
	auto const found = std::find_if(open_copies.begin(), open_copies.end(), [&theirs](const SyncCopy& copy) {
		auto const& listed = theirs->syncCopies();
		return std::any_of(listed.begin(), listed.end(), [&copy](const SyncCopy& other) { return other.id == copy.id; });
	});
	if (found == open_copies.end())
		return fail(error, tr("%1 is not a field copy of this map: the sync ids do not match.").arg(copy_path));
	if (!sameFrame(mine, *theirs))
		return fail(error, tr("The copy has another scale or georeferencing than this map."));
	
	input.copy = *found;
	input.theirs = std::move(theirs);
	auto const base_path = baseFilePath(map_path, found->id);
	if (!map_path.isEmpty() && QFile::exists(base_path))
	{
		auto base = std::make_unique<Map>();
		if (base->loadFrom(base_path))
			input.base = std::move(base);
	}
	return true;
}

bool refreshBase(const QString& map_path, const QString& copy_path, const SyncCopy& copy, QString* error)
{
	if (map_path.isEmpty())
		return fail(error, tr("The map has no file yet. Save it first."));
	return copyFile(copy_path, baseFilePath(map_path, copy.id), error);
}


}  // namespace OpenOrienteering
