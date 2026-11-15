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


#include <memory>
#include <utility>
#include <vector>

#include <QtGlobal>
#include <QtTest>
#include <QChar>
#include <QLatin1Char>
#include <QLineF>
#include <QObject>
#include <QPointF>
#include <QString>
#include <QStringList>

#include "global.h"
#include "core/georeferencing.h"
#include "core/latlon.h"
#include "core/map.h"
#include "core/map_coord.h"
#include "core/objects/object.h"
#include "core/objects/text_object.h"
#include "core/ops/apply_op.h"
#include "core/ops/import_points.h"
#include "core/ops/op.h"
#include "core/symbols/point_symbol.h"
#include "core/symbols/symbol.h"
#include "core/symbols/text_symbol.h"
#include "undo/undo_manager.h"

using namespace OpenOrienteering;


namespace {

void setupMap(Map& map)
{
	map.setScaleDenominator(10000);
	Georeferencing georef;
	georef.setScaleDenominator(10000);
	georef.setProjectedCRS(QStringLiteral("UTM 13"), QStringLiteral("+proj=utm +zone=13 +datum=WGS84"));
	georef.setProjectedRefPoint(QPointF(500000.0, 5000000.0), false, false);
	map.setGeoreferencing(georef);
	for (auto number : { 101, 102 })
	{
		auto* point = new PointSymbol();
		point->setNumberComponent(0, number);
		point->setInnerRadius(250);
		map.addSymbol(point, map.getNumSymbols());
	}
	auto* text = new TextSymbol();
	text->setNumberComponent(0, 401);
	map.addSymbol(text, map.getNumSymbols());
}

const Symbol* symbolNumbered(const Map& map, const char* number)
{
	for (int i = 0; i < map.getNumSymbols(); ++i)
	{
		if (map.getSymbol(i)->getNumberAsString() == QLatin1String(number))
			return map.getSymbol(i);
	}
	return nullptr;
}

ops::ImportPointsParams defaultParams(const Map& map)
{
	ops::ImportPointsParams params;
	params.default_symbol = symbolNumbered(map, "102");
	params.text_symbol = symbolNumbered(map, "401");
	return params;
}

double distance(const QPointF& a, const QPointF& b)
{
	return QLineF(a, b).length();
}

QPointF position(const Object* object)
{
	return QPointF(MapCoordF(object->getRawCoordinateVector().front()));
}

const QString five_rows = QStringLiteral(
    "x;y;z;name;code\n"
    "500010;5000020;412.5;Alpha;101\n"
    "500020;5000030;413;Beta;102\n"
    "500030;5000040;414.25;;101\n"
    "500040;5000050;415;Delta;999\n"
    "500050;5000060;416;Echo;\n");

}  // namespace



/**
 * Tests the import of points from delimited text.
 *
 * See the design notes, "Import options", Delimited text, Accept.
 */
class ImportPointsTest : public QObject
{
Q_OBJECT
private slots:
	void initTestCase()
	{
		doStaticInitializations();
	}

	/// Accept (1)
	void delimiterTest()
	{
		QCOMPARE(ops::detectDelimiter({ QStringLiteral("1,2,3"), QStringLiteral("4,5,6") }), QChar(QLatin1Char(',')));
		QCOMPARE(ops::detectDelimiter({ QStringLiteral("1;2;3"), QStringLiteral("4;5;6") }), QChar(QLatin1Char(';')));
		QCOMPARE(ops::detectDelimiter({ QStringLiteral("1\t2\t3"), QStringLiteral("4\t5\t6") }), QChar(QLatin1Char('\t')));
		QCOMPARE(ops::detectDelimiter({ QStringLiteral("1  2   3"), QStringLiteral("4 5 6") }), QChar(QLatin1Char(' ')));
		// A comma inside quotes is not a delimiter: the counts per line differ without quote handling.
		QCOMPARE(ops::detectDelimiter({ QStringLiteral("\"a, b\";1;2"), QStringLiteral("c;3;4") }), QChar(QLatin1Char(';')));

		auto const fields = ops::splitDelimited(QStringLiteral("\"a, b\",1,2"), QLatin1Char(','));
		QCOMPARE(fields, (QStringList{ QStringLiteral("a, b"), QStringLiteral("1"), QStringLiteral("2") }));
		auto const blanks = ops::splitDelimited(QStringLiteral("  10   20 x "), QLatin1Char(' '));
		QCOMPARE(blanks, (QStringList{ QStringLiteral("10"), QStringLiteral("20"), QStringLiteral("x") }));
	}

	/// Accept (2)
	void headerTest()
	{
		QVERIFY(ops::detectHeader({ QStringLiteral("x;y;name"), QStringLiteral("1;2;a") }, QLatin1Char(';'), 0, 1));
		QVERIFY(!ops::detectHeader({ QStringLiteral("1;2;a"), QStringLiteral("3;4;b") }, QLatin1Char(';'), 0, 1));
	}

	/// Accept (3)
	void pointsTest()
	{
		Map map;
		setupMap(map);
		auto params = defaultParams(map);
		params.delimiter = QLatin1Char(';');
		params.header = true;
		params.z_column = 2;
		params.code_column = 4;
		auto const result = ops::importPoints(map, five_rows, params);
		QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
		QCOMPARE(int(result.out.size()), 5);

		const char* const expected_symbols[] = { "101", "102", "101", "102", "102" };
		const char* const expected_ele[] = { "412.5", "413", "414.25", "415", "416" };
		for (int i = 0; i < 5; ++i)
		{
			auto const* object = result.out[std::size_t(i)].get();
			QCOMPARE(object->getType(), Object::Point);
			auto const expected = QPointF(map.getGeoreferencing().toMapCoordF(QPointF(500010.0 + 10 * i, 5000020.0 + 10 * i)));
			QVERIFY(distance(position(object), expected) < 0.01);
			QCOMPARE(object->getSymbol(), symbolNumbered(map, expected_symbols[i]));
			QCOMPARE(object->getTag(QStringLiteral("ele")), QString::fromLatin1(expected_ele[i]));
		}
		QVERIFY2(result.warning.contains(QStringLiteral("2 point(s)")), qPrintable(result.warning));
	}

	/// Accept (4)
	void textTest()
	{
		Map map;
		setupMap(map);
		auto params = defaultParams(map);
		params.delimiter = QLatin1Char(';');
		params.header = true;
		params.text_column = 3;
		auto const result = ops::importPoints(map, five_rows, params);
		QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
		QCOMPARE(int(result.out.size()), 9);  // five points, four texts

		std::vector<const Object*> points;
		std::vector<const TextObject*> texts;
		for (auto const& object : result.out)
		{
			if (object->getType() == Object::Text)
				texts.push_back(static_cast<const TextObject*>(object.get()));
			else
				points.push_back(object.get());
		}
		QCOMPARE(int(points.size()), 5);
		QCOMPARE(int(texts.size()), 4);
		QStringList names;
		for (auto const* text : texts)
		{
			names.push_back(text->getText());
			QCOMPARE(text->getSymbol(), symbolNumbered(map, "401"));
			auto const anchor = QPointF(text->getAnchorCoordF());
			auto const at_point = std::any_of(begin(points), end(points), [&anchor](const Object* p) {
				return distance(position(p), anchor) < 0.001;
			});
			QVERIFY(at_point);
		}
		QCOMPARE(names, (QStringList{ QStringLiteral("Alpha"), QStringLiteral("Beta"), QStringLiteral("Delta"), QStringLiteral("Echo") }));
	}

	/// Accept (5)
	void geographicTest()
	{
		Map map;
		setupMap(map);
		auto params = defaultParams(map);
		params.crs = ops::ImportPointsParams::Wgs84;
		auto const result = ops::importPoints(map, QStringLiteral("-102.35,48.25\n-104.99,45.12\n"), params);
		QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
		QCOMPARE(int(result.out.size()), 2);
		QVERIFY(distance(position(result.out[0].get()), QPointF(map.getGeoreferencing().toMapCoordF(LatLon(48.25, -102.35)))) < 0.01);
		QVERIFY(distance(position(result.out[1].get()), QPointF(map.getGeoreferencing().toMapCoordF(LatLon(45.12, -104.99)))) < 0.01);

		params.crs = ops::ImportPointsParams::OtherCrs;
		params.crs_spec = QStringLiteral("+proj=utm +zone=13 +datum=WGS84");
		auto const other = ops::importPoints(map, QStringLiteral("500010,5000020\n"), params);
		QVERIFY2(other.error.isEmpty(), qPrintable(other.error));
		QCOMPARE(int(other.out.size()), 1);
		QVERIFY(distance(position(other.out[0].get()), QPointF(map.getGeoreferencing().toMapCoordF(QPointF(500010, 5000020)))) < 0.01);
	}

	/// Accept (6)
	void paperTest()
	{
		Map map;
		setupMap(map);
		auto params = defaultParams(map);
		params.delimiter = QLatin1Char(' ');
		params.crs = ops::ImportPointsParams::PaperMillimetres;
		auto const result = ops::importPoints(map, QStringLiteral("10 20\n30   40\n"), params);
		QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
		QCOMPARE(int(result.out.size()), 2);
		QVERIFY(distance(position(result.out[0].get()), QPointF(10, -20)) < 0.001);
		QVERIFY(distance(position(result.out[1].get()), QPointF(30, -40)) < 0.001);
	}

	/// Accept (7)
	void skippedRowTest()
	{
		Map map;
		setupMap(map);
		auto params = defaultParams(map);
		params.crs = ops::ImportPointsParams::PaperMillimetres;
		auto const result = ops::importPoints(map, QStringLiteral("1,2\n3,4\nabc,5\n6,7\n"), params);
		QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
		QCOMPARE(int(result.out.size()), 3);
		QVERIFY2(result.warning.contains(QStringLiteral("3")), qPrintable(result.warning));
	}

	/// Accept (8)
	void undoTest()
	{
		Map map;
		setupMap(map);
		auto params = defaultParams(map);
		params.delimiter = QLatin1Char(';');
		params.header = true;
		params.text_column = 3;
		auto result = ops::importPoints(map, five_rows, params);
		QVERIFY(result.error.isEmpty());
		QVERIFY(ops::applyOp(map, std::move(result)));
		QCOMPARE(map.getNumObjects(), 9);
		QVERIFY(map.undoManager().undo());
		QCOMPARE(map.getNumObjects(), 0);
	}

	void errorTest()
	{
		Map map;
		setupMap(map);
		ops::ImportPointsParams params;  // no symbol
		auto const result = ops::importPoints(map, QStringLiteral("1,2\n"), params);
		QVERIFY(!result.error.isEmpty());
		QVERIFY(result.out.empty());
	}
};


/*
 * We don't need a real GUI window.
 */
namespace  {
	auto Q_DECL_UNUSED qpa_selected = qputenv("QT_QPA_PLATFORM", "offscreen");  // clazy:exclude=non-pod-global-static
}


QTEST_MAIN(ImportPointsTest)
#include "import_points_t.moc"  // IWYU pragma: keep
