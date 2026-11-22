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


#include <vector>

#include <QtGlobal>
#include <QtTest>
#include <QBuffer>
#include <QDir>
#include <QFileInfo>
#include <QIODevice>
#include <QLatin1String>
#include <QObject>
#include <QString>

#include "global.h"
#include "core/foreign_data.h"
#include "core/guides.h"
#include "core/map.h"
#include "core/map_coord.h"
#include "fileformats/file_format.h"
#include "fileformats/file_format_registry.h"
#include "fileformats/file_import_export.h"
#include "fileformats/ocd_file_format.h"
#include "fileformats/ocd_guide.h"

using namespace OpenOrienteering;


namespace {

bool roundTrip(const Map& map, const char* format_id, Map& reloaded, std::vector<QString>& export_warnings)
{
	auto const* format = FileFormats.findFormat(format_id);
	if (!format)
		return false;
	QBuffer buffer;
	if (!buffer.open(QIODevice::ReadWrite))
		return false;
	auto exporter = format->makeExporter({}, &map, nullptr);
	exporter->setDevice(&buffer);
	if (!exporter->doExport())
		return false;
	export_warnings = exporter->warnings();
	buffer.seek(0);
	auto importer = format->makeImporter({}, &reloaded, nullptr);
	importer->setDevice(&buffer);
	return importer->doImport();
}

int foreignGuideStrings(const Map& map)
{
	auto count = 0;
	for (auto const& string : map.foreignData().strings)
		count += string.type == ocd_guide_string_type ? 1 : 0;
	return count;
}

Guide guide(Qt::Orientation orientation, qint32 position)
{
	Guide result;
	result.orientation = orientation;
	result.position = position;
	return result;
}

}  // namespace



/**
 * Tests OCD string type 26, guides.
 *
 * See the design notes, "OCD string type 26".
 */
class OcdGuideTest : public QObject
{
Q_OBJECT
private slots:
	void initTestCase()
	{
		doStaticInitializations();
	}

	void parseTest()
	{
		Guide parsed;
		QVERIFY(parseOcdGuide(QStringLiteral("\th0\tc-9348608"), parsed));
		QCOMPARE(parsed.orientation, Qt::Vertical);
		QCOMPARE(parsed.position, -365180);
		QVERIFY(parseOcdGuide(QStringLiteral("\th1\tc2560000"), parsed));
		QCOMPARE(parsed.orientation, Qt::Horizontal);
		QCOMPARE(parsed.position, -100000);
		QVERIFY(!parseOcdGuide(QStringLiteral("\th0"), parsed));
		QVERIFY(!parseOcdGuide(QStringLiteral("\th0\tcx"), parsed));
	}

	void stringTest()
	{
		QCOMPARE(ocdGuideString(guide(Qt::Vertical, 1234), MapCoord()), QStringLiteral("\th0\tc31488"));
		QCOMPARE(ocdGuideString(guide(Qt::Horizontal, -100000), MapCoord()), QStringLiteral("\th1\tc2560000"));
		auto const offset = MapCoord::fromNative(1000, 2000);
		QCOMPARE(ocdGuideString(guide(Qt::Vertical, 1234), offset), QStringLiteral("\th0\tc5888"));
		QCOMPARE(ocdGuideString(guide(Qt::Horizontal, -100000), offset), QStringLiteral("\th1\tc2611200"));
	}

	void roundTripTest()
	{
		Map map;
		Guides guides;
		guides.list = { guide(Qt::Vertical, 5000), guide(Qt::Horizontal, -12500) };
		map.setGuides(guides);
		for (auto version : { 12, 2018 })
		{
			Map reloaded;
			std::vector<QString> warnings;
			QVERIFY(roundTrip(map, OcdFileFormat::idForVersion(quint16(version)), reloaded, warnings));
			QCOMPARE(int(reloaded.guides().list.size()), 2);
			QVERIFY(reloaded.guides().list[0] == guides.list[0]);
			QVERIFY(reloaded.guides().list[1] == guides.list[1]);
			QCOMPARE(foreignGuideStrings(reloaded), 0);
		}

		Map old;
		std::vector<QString> warnings;
		QVERIFY(roundTrip(map, OcdFileFormat::idForVersion(9), old, warnings));
		QVERIFY(old.guides().list.empty());
		auto warned = false;
		for (auto const& warning : warnings)
			warned = warned || warning.contains(QLatin1String("guide"));
		QVERIFY(warned);
	}

};


QTEST_MAIN(OcdGuideTest)
#include "ocd_guide_t.moc"  // IWYU pragma: keep
