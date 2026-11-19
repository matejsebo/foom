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


#include <algorithm>
#include <set>
#include <vector>

#include <QtGlobal>
#include <QtTest>
#include <QDir>
#include <QFileInfo>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QUuid>

#include "global.h"
#include "core/map.h"
#include "core/map_part.h"
#include "core/objects/object.h"
#include "course/course_data.h"
#include "course/ocd_course_records.h"
#include "fileformats/file_format.h"
#include "fileformats/file_format_registry.h"
#include "fileformats/file_import_export.h"

using namespace OpenOrienteering;


namespace {

QString tabbed(const char* text)
{
	// The records are written here with a bar where the file has a tab.
	return QString::fromUtf8(text).replace(QLatin1Char('|'), QLatin1Char('\t'));
}

QString itemTypes(const Course& course)
{
	static const char letters[] = "scmftkwgxrvqlbp";
	QString result;
	for (auto const& item : course.items)
		result += QLatin1Char(letters[int(item.type)]);
	return result;
}

}  // namespace



/**
 * @test Tests the course data model and the reading and writing of
 *       the OCD course records.
 *
 * the design notes, "Data model and OCD records".
 */
class CourseModelTest : public QObject
{
Q_OBJECT
private slots:
	void initTestCase();
	void symbolNumberTest();
	void objectRecordTest();
	void courseRecordTest();
	void classRecordTest();
	void overrideRecordTest();
	void optionsRecordTest();
};


void CourseModelTest::initTestCase()
{
	Q_INIT_RESOURCE(resources);
	doStaticInitializations();
}


/// Accept (1)
void CourseModelTest::symbolNumberTest()
{
	QCOMPARE(OcdCourse::symbolNumberFromText(QStringLiteral("12.401")), 12401);
	QCOMPARE(OcdCourse::symbolNumberFromText(QStringLiteral("5.001")), 5001);
	QCOMPARE(OcdCourse::symbolNumberFromText(QStringLiteral("0.101")), 101);
	QCOMPARE(OcdCourse::symbolNumberFromText(QStringLiteral("5")), 5000);
	QCOMPARE(OcdCourse::symbolNumberFromText(QStringLiteral("5.01")), 5010);
	QCOMPARE(OcdCourse::symbolNumberFromText(QStringLiteral("")), 0);
	QCOMPARE(OcdCourse::symbolNumberFromText(QStringLiteral("x.y")), 0);
	QCOMPARE(OcdCourse::symbolNumberToText(12401), QStringLiteral("12.401"));
	QCOMPARE(OcdCourse::symbolNumberToText(5001), QStringLiteral("5.001"));
	QCOMPARE(OcdCourse::symbolNumberToText(101), QStringLiteral("0.101"));
}


/// Accept (1)
void CourseModelTest::objectRecordTest()
{
	CourseObject object;
	QVERIFY(OcdCourse::parseObject(tabbed("1044|Yc|a44|d1.010|g12.102|tKnoll, West Side, Refreshment point|h13.002"), object));
	QCOMPARE(object.kind, CourseObject::Control);
	QCOMPARE(object.code, QStringLiteral("44"));
	QCOMPARE(object.cell[CourseObject::ColumnC], 0);
	QCOMPARE(object.cell[CourseObject::ColumnD], 1010);
	QCOMPARE(object.cell[CourseObject::ColumnE], 0);
	QCOMPARE(object.cell[CourseObject::ColumnF], 0);
	QCOMPARE(object.cell[CourseObject::ColumnG], 12102);
	QCOMPARE(object.cell[CourseObject::ColumnH], 13002);
	QCOMPARE(object.text, QStringLiteral("Knoll, West Side, Refreshment point"));
	QVERIFY(object.other.empty());
	QVERIFY(object.object.isNull());
	// Written in the fixed order: kind and code, Y, a, the cells C to H, the text.
	QCOMPARE(OcdCourse::formatObject(object), tabbed("1044|Yc|a44|d1.010|g12.102|h13.002|tKnoll, West Side, Refreshment point"));
	{
		CourseObject again;
		QVERIFY(OcdCourse::parseObject(OcdCourse::formatObject(object), again));
		QVERIFY(again == object);
	}
	
	// The other kinds.
	QVERIFY(OcdCourse::parseObject(tabbed("00S2|Ys|aS2|d5.002|e5.002|f10.002|tPath junction"), object));
	QCOMPARE(object.kind, CourseObject::Start);
	QCOMPARE(object.code, QStringLiteral("S2"));
	QCOMPARE(object.cell[CourseObject::ColumnE], 5002);
	QCOMPARE(object.cell[CourseObject::ColumnF], 10002);
	QCOMPARE(OcdCourse::formatObject(object), tabbed("00S2|Ys|aS2|d5.002|e5.002|f10.002|tPath junction"));
	
	QVERIFY(OcdCourse::parseObject(tabbed("1033|Yc|a33|d4.009|g12.103|c0.101"), object));
	QCOMPARE(object.cell[CourseObject::ColumnC], 101);
	QCOMPARE(OcdCourse::formatObject(object), tabbed("1033|Yc|a33|c0.101|d4.009|g12.103"));
	
	QVERIFY(OcdCourse::parseObject(tabbed("20M1|Ym|aM1"), object));
	QCOMPARE(object.kind, CourseObject::MarkedRoute);
	QCOMPARE(OcdCourse::formatObject(object), tabbed("20M1|Ym|aM1"));
	
	QVERIFY(OcdCourse::parseObject(tabbed("30F1|Yf|aF1"), object));
	QCOMPARE(object.kind, CourseObject::Finish);
	
	// A bare record, and one which ends with a tab.
	QVERIFY(OcdCourse::parseObject(tabbed("00S1"), object));
	QCOMPARE(object.kind, CourseObject::Start);
	QCOMPARE(object.code, QStringLiteral("S1"));
	QVERIFY(OcdCourse::parseObject(tabbed("1031|Yc|"), object));
	QCOMPARE(object.code, QStringLiteral("31"));
	
	// A description object: the fields p and o are not understood, and kept.
	QVERIFY(OcdCourse::parseObject(tabbed("40D2|Yd|aD2|p0|ot"), object));
	QCOMPARE(object.kind, CourseObject::Description);
	QCOMPARE(object.code, QStringLiteral("D2"));
	QCOMPARE(int(object.other.size()), 2);
	QCOMPARE(object.other[0].letter, QChar(QLatin1Char('p')));
	QCOMPARE(object.other[0].value, QStringLiteral("0"));
	QCOMPARE(object.other[1].letter, QChar(QLatin1Char('o')));
	QCOMPARE(object.other[1].value, QStringLiteral("t"));
	QCOMPARE(OcdCourse::formatObject(object), tabbed("40D2|Yd|aD2|p0|ot"));
	
	// Course title, variant code and start number, as OCD course files have them.
	QVERIFY(OcdCourse::parseObject(tabbed("50N1|aN1|Yn"), object));
	QCOMPARE(object.kind, CourseObject::CourseTitle);
	QCOMPARE(object.code, QStringLiteral("N1"));
	QVERIFY(object.other.empty());
	QCOMPARE(OcdCourse::formatObject(object), tabbed("50N1|Yn|aN1"));
	QVERIFY(OcdCourse::parseObject(tabbed("60V1|aV1|Yv"), object));
	QCOMPARE(object.kind, CourseObject::VariantCode);
	QCOMPARE(OcdCourse::formatObject(object), tabbed("60V1|Yv|aV1"));
	QVERIFY(OcdCourse::parseObject(tabbed("70U1|Yu|aU1"), object));
	QCOMPARE(object.kind, CourseObject::StartNumber);
	QCOMPARE(OcdCourse::formatObject(object), tabbed("70U1|Yu|aU1"));
	// Kind and code alone are read too, and written with the two fields.
	QVERIFY(OcdCourse::parseObject(tabbed("50N1"), object));
	QCOMPARE(object.kind, CourseObject::CourseTitle);
	QCOMPARE(OcdCourse::formatObject(object), tabbed("50N1|Yn|aN1"));
	
	// A text block, as it stands in a parameter string.
	QVERIFY(OcdCourse::parseObject(tabbed("80T1|aT1|Yt|tAttention: Dangerous Road!"), object));
	QCOMPARE(object.kind, CourseObject::TextBlock);
	QCOMPARE(object.code, QStringLiteral("T1"));
	QCOMPARE(object.text, QStringLiteral("Attention: Dangerous Road!"));
	QCOMPARE(OcdCourse::formatObject(object), tabbed("80T1|Yt|aT1|tAttention: Dangerous Road!"));
	
	// What is no course object.
	QVERIFY(!OcdCourse::parseObject(tabbed("90X1|Yc"), object));
	QVERIFY(!OcdCourse::parseObject(tabbed("15X1"), object));
	QVERIFY(!OcdCourse::parseObject(tabbed("S1|Ys"), object));
	QVERIFY(!OcdCourse::parseObject(tabbed("Easy Course|d31"), object));
	QVERIFY(!OcdCourse::parseObject(QString{}, object));
}


/// Accept (2)
void CourseModelTest::courseRecordTest()
{
	Course course;
	QVERIFY(OcdCourse::parseCourse(tabbed("Relay Women|Ys|mM6|sS4|r3|v|c48|c55|v|c50|c57|v|c49|c56|q|c59|l|b1-2|r2|v|c64|v|c65|q|c62|b3|c60|c61|p|c63|mM4|fF3|L3|C50"), course));
	QCOMPARE(course.name, QStringLiteral("Relay Women"));
	QCOMPARE(course.type, Course::Relay);
	QCOMPARE(course.legs, 3);
	QCOMPARE(course.climb_m, 50);
	QVERIFY(course.climb_manual);
	QVERIFY(!course.extra_set);
	QVERIFY(course.other.empty());
	QCOMPARE(int(course.items.size()), 30);
	QCOMPARE(itemTypes(course), QStringLiteral("msrvccvccvccqclbrvcvcqcbccpcmf"));
	QCOMPARE(course.items[0].code, QStringLiteral("M6"));
	QCOMPARE(course.items[1].code, QStringLiteral("S4"));
	QCOMPARE(course.items[2].count, 3);
	QCOMPARE(course.items[4].code, QStringLiteral("48"));
	QCOMPARE(course.items[15].type, CourseItem::LegBranch);
	QCOMPARE(course.items[15].legs, QStringLiteral("1-2"));
	QCOMPARE(course.items[16].count, 2);
	QCOMPARE(course.items[23].legs, QStringLiteral("3"));
	QCOMPARE(course.items[29].code, QStringLiteral("F3"));
	QVERIFY(course.items[0].namesObject());
	QVERIFY(!course.items[2].namesObject());
	// Written with type and legs before the items, the climb behind them.
	QCOMPARE(OcdCourse::formatCourse(course), tabbed("Relay Women|Ys|L3|mM6|sS4|r3|v|c48|c55|v|c50|c57|v|c49|c56|q|c59|l|b1-2|r2|v|c64|v|c65|q|c62|b3|c60|c61|p|c63|mM4|fF3|C50"));
	
	QVERIFY(OcdCourse::parseCourse(tabbed("One-Man Relay|Yo|L2|sS1|r2|v|c36|v|c67|q|c37|fF4|C40"), course));
	QCOMPARE(course.type, Course::OneManRelay);
	QCOMPARE(course.legs, 2);
	QCOMPARE(OcdCourse::formatCourse(course), tabbed("One-Man Relay|Yo|L2|sS1|r2|v|c36|v|c67|q|c37|fF4|C40"));
	
	// A normal course says so, or says nothing.
	QVERIFY(OcdCourse::parseCourse(tabbed("Easy Course|sS1|c31|c32|c33|c34|c35|fF1|Yn|C10|E0.18"), course));
	QCOMPARE(course.type, Course::Normal);
	QCOMPARE(course.legs, 0);
	QCOMPARE(course.climb_m, 10);
	QVERIFY(course.extra_set);
	QCOMPARE(course.extra_km, 0.18);
	QCOMPARE(itemTypes(course), QStringLiteral("scccccf"));
	QCOMPARE(OcdCourse::formatCourse(course), tabbed("Easy Course|sS1|c31|c32|c33|c34|c35|fF1|C10|E0.18"));
	
	QVERIFY(OcdCourse::parseCourse(tabbed("Difficult Course|sS2|c36|fF1|C20|E-0.14"), course));
	QCOMPARE(course.extra_km, -0.14);
	QCOMPARE(OcdCourse::formatCourse(course), tabbed("Difficult Course|sS2|c36|fF1|C20|E-0.14"));
	
	// Passage, crossing point, text blocks; an extra length of nothing is still stated.
	QVERIFY(OcdCourse::parseCourse(tabbed("Passages|sS3|c41|c42|w|c43|mM2|k|c38|tT1|tT2|c69|k|c47|mM3|fF2|C50|E0.00"), course));
	QCOMPARE(itemTypes(course), QStringLiteral("sccwcmkcttckcmf"));
	QCOMPARE(course.items[3].type, CourseItem::Passage);
	QCOMPARE(course.items[6].type, CourseItem::CrossingPoint);
	QCOMPARE(course.items[8].type, CourseItem::TextBlock);
	QCOMPARE(course.items[8].code, QStringLiteral("T1"));
	QVERIFY(course.extra_set);
	QCOMPARE(course.extra_km, 0.0);
	QCOMPARE(OcdCourse::formatCourse(course), tabbed("Passages|sS3|c41|c42|w|c43|mM2|k|c38|tT1|tT2|c69|k|c47|mM3|fF2|C50|E0.00"));
	
	// Map exchange and map flip.
	QVERIFY(OcdCourse::parseCourse(tabbed("Map Exchange|mM1|sS2|c36|g|sS5|c52|mM5|g|sS1|c33|mM4|fF3|C50"), course));
	QCOMPARE(itemTypes(course), QStringLiteral("mscgscmgscmf"));
	QVERIFY(OcdCourse::parseCourse(tabbed("Map Flip|mM1|sS2|c36|x|sS5|c52|fF1|C50"), course));
	QCOMPARE(course.items[3].type, CourseItem::MapFlip);
	QCOMPARE(OcdCourse::formatCourse(course), tabbed("Map Flip|mM1|sS2|c36|x|sS5|c52|fF1|C50"));
	
	// A field which is not understood is kept, behind the known ones.
	QVERIFY(OcdCourse::parseCourse(tabbed("New|sS1|Z7|c31|fF1"), course));
	QCOMPARE(itemTypes(course), QStringLiteral("scf"));
	QCOMPARE(int(course.other.size()), 1);
	QCOMPARE(OcdCourse::formatCourse(course), tabbed("New|sS1|c31|fF1|Z7"));
	
	// A course without a climb states none.
	QVERIFY(OcdCourse::parseCourse(tabbed("Bare|sS1|fF1"), course));
	QVERIFY(!course.climb_manual);
	QCOMPARE(OcdCourse::formatCourse(course), tabbed("Bare|sS1|fF1"));
	
	QVERIFY(!OcdCourse::parseCourse(tabbed("|sS1|fF1"), course));
	QVERIFY(!OcdCourse::parseCourse(QString{}, course));
}


/// Accept (3)
void CourseModelTest::classRecordTest()
{
	CourseClass course_class;
	QVERIFY(OcdCourse::parseClass(tabbed("Relay Women|cRelay Women|r50|f101|t150"), course_class));
	QCOMPARE(course_class.name, QStringLiteral("Relay Women"));
	QCOMPARE(course_class.course, QStringLiteral("Relay Women"));
	QCOMPARE(course_class.runners, 50);
	QCOMPARE(course_class.first_no, 101);
	QCOMPARE(course_class.last_no, 150);
	QCOMPARE(OcdCourse::formatClass(course_class), tabbed("Relay Women|cRelay Women|r50|f101|t150"));
	
	QVERIFY(OcdCourse::parseClass(tabbed("M12|cEasy Course|r|f|t"), course_class));
	QCOMPARE(course_class.course, QStringLiteral("Easy Course"));
	QCOMPARE(course_class.runners, -1);
	QCOMPARE(course_class.first_no, -1);
	QCOMPARE(course_class.last_no, -1);
	QCOMPARE(OcdCourse::formatClass(course_class), tabbed("M12|cEasy Course|r|f|t"));
	
	// A blank also stands for a number which is not given.
	QVERIFY(OcdCourse::parseClass(tabbed("W12|cEasy Course|r |f |t"), course_class));
	QCOMPARE(course_class.runners, -1);
	QCOMPARE(OcdCourse::formatClass(course_class), tabbed("W12|cEasy Course|r|f|t"));
	
	// A field which is not understood, here d, is kept.
	QVERIFY(OcdCourse::parseClass(tabbed("WE|cDifficult|r|f|t|d"), course_class));
	QCOMPARE(int(course_class.other.size()), 1);
	QCOMPARE(OcdCourse::formatClass(course_class), tabbed("WE|cDifficult|r|f|t|d"));
	
	QVERIFY(!OcdCourse::parseClass(tabbed("|cEasy Course"), course_class));
}


/// Accept (4)
void CourseModelTest::overrideRecordTest()
{
	CourseOverride override;
	QVERIFY(OcdCourse::parseOverride(tabbed("Easy Course|d31"), override));
	QCOMPARE(override.course, QStringLiteral("Easy Course"));
	QCOMPARE(override.what, CourseOverride::Number);
	QCOMPARE(override.code_from, QStringLiteral("31"));
	QVERIFY(override.code_to.isEmpty());
	QCOMPARE(OcdCourse::formatOverride(override), tabbed("Easy Course|d31"));
	
	QVERIFY(OcdCourse::parseOverride(tabbed("Passages|f42|t43"), override));
	QCOMPARE(override.what, CourseOverride::Line);
	QCOMPARE(override.code_from, QStringLiteral("42"));
	QCOMPARE(override.code_to, QStringLiteral("43"));
	QCOMPARE(OcdCourse::formatOverride(override), tabbed("Passages|f42|t43"));
	
	QVERIFY(!OcdCourse::parseOverride(tabbed("Passages|f42"), override));
	QVERIFY(!OcdCourse::parseOverride(tabbed("Passages"), override));
	QVERIFY(!OcdCourse::parseOverride(tabbed("Passages|d31|t43"), override));
	QVERIFY(!OcdCourse::parseOverride(tabbed("1044|Yc|a44"), override));
}


/// Accept (5)
void CourseModelTest::optionsRecordTest()
{
	auto const record = tabbed("|t1|c0|h1|i25|n1.00|p0|l0.00|s6.00|b1|B0|d1|r1|w0|g1|a0|f0|eCourse Setting Example|u0");
	auto const options = OcdCourse::parseOptions(record);
	QCOMPARE(int(options.fields.size()), 18);
	QCOMPARE(options.eventTitle(), QStringLiteral("Course Setting Example"));
	QCOMPARE(options.boxSize(), 6.0);
	QCOMPARE(options.maximumRows(), 25);
	QCOMPARE(options.titleMode(), 1);
	QCOMPARE(options.thickerRule(), 1);
	QCOMPARE(options.numberDistance(), 1.0);
	QCOMPARE(options.lineDistance(), 0.0);
	QVERIFY(!options.fullStopAfterNumber());
	QVERIFY(options.whiteBackground());
	// b and B are two fields.
	QCOMPARE(options.value(QLatin1Char('B')), QStringLiteral("0"));
	QVERIFY(options.has(QLatin1Char('u')));
	QVERIFY(!options.has(QLatin1Char('z')));
	// The fields keep their order, so the record is written as it was read.
	QCOMPARE(OcdCourse::formatOptions(options), record);
	
	auto changed = options;
	changed.setValue(QLatin1Char('s'), QStringLiteral("5.00"));
	changed.setValue(QLatin1Char('z'), QStringLiteral("1"));
	QCOMPARE(changed.boxSize(), 5.0);
	QCOMPARE(OcdCourse::formatOptions(changed), tabbed("|t1|c0|h1|i25|n1.00|p0|l0.00|s5.00|b1|B0|d1|r1|w0|g1|a0|f0|eCourse Setting Example|u0|z1"));
	QVERIFY(changed != options);
	
	// Without a record.
	CourseOptions none;
	QCOMPARE(none.boxSize(), 6.0);
	QCOMPARE(none.numberDistance(), 1.0);
	QVERIFY(none.eventTitle().isEmpty());
	QVERIFY(CourseData{}.isEmpty());
}



QTEST_MAIN(CourseModelTest)
#include "course_model_t.moc"  // IWYU pragma: keep
