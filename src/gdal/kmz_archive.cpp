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


#include "kmz_archive.h"

#include <cpl_string.h>
#include <cpl_vsi.h>

#include <QFileInfo>

namespace OpenOrienteering {

QByteArray readZipEntry(const QString& archive_path, const QString& entry)
{
	auto const base = QByteArray("/vsizip/" + QFileInfo(archive_path).absoluteFilePath().toUtf8());
	auto name = entry.toUtf8();
	if (name.isEmpty())
	{
		auto** list = VSIReadDirRecursive(base.constData());
		for (auto** item = list; item && *item; ++item)
		{
			auto const listed = QByteArray(*item);
			if (listed.toLower() == "doc.kml")
			{
				name = listed;
				break;
			}
			if (name.isEmpty() && listed.toLower().endsWith(".kml"))
				name = listed;
		}
		CSLDestroy(list);
		if (name.isEmpty())
			return {};
	}

	auto* file = VSIFOpenL(QByteArray(base + '/' + name).constData(), "rb");
	if (!file)
		return {};
	QByteArray bytes;
	char buffer[65536];
	for (auto count = VSIFReadL(buffer, 1, sizeof(buffer), file); count > 0; count = VSIFReadL(buffer, 1, sizeof(buffer), file))
		bytes.append(buffer, int(count));
	VSIFCloseL(file);
	return bytes;
}

}  // namespace OpenOrienteering
