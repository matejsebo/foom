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


#include <cmath>
#include <cstddef>
#include <map>
#include <set>
#include <vector>

#include <Qt>
#include <QtGlobal>
#include <QtTest>
#include <QAction>
#include <QByteArray>
#include <QChar>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QIODevice>
#include <QLabel>
#include <QLatin1Char>
#include <QLatin1String>
#include <QObject>
#include <QPushButton>
#include <QSpinBox>
#include <QString>
#include <QStringList>
#include <QTreeWidget>
#include <QTreeWidgetItem>

#include "global.h"
#include "test_config.h"
#include "check/issue.h"
#include "core/map.h"
#include "core/map_coord.h"
#include "core/objects/object.h"
#include "core/symbols/point_symbol.h"
#include "core/symbols/symbol.h"
#include "course/course_data.h"
#include "course/course_edit.h"
#include "course/course_relay.h"
#include "course/course_relay_edit.h"
#include "course/course_render.h"
#include "course/course_report.h"
#include "course/course_storage.h"
#include "fileformats/file_format.h"
#include "fileformats/file_format_registry.h"
#include "fileformats/file_import_export.h"
#include "gui/widgets/course_widget.h"
#include "undo/object_undo.h"
#include "undo/undo.h"
#include "undo/undo_manager.h"

using namespace OpenOrienteering;


namespace {

const char* one_man_items = "S1 r2 v 36 v 67 q 37 r2 v 60 58 v 46 61 q 56 r2 v 65 59 v 38 q 39 r2 v 33 v 44 32 q 40 F4";
const char* women_items = "M6 S4 r3 v 48 55 v 50 57 v 49 56 q 59 l b1-2 r2 v 64 v 65 q 62 b3 60 61 p 63 M4 F3";
const char* sprint_items = "M1 S1 r4 v 31 v 44 v 56 v 55 q 46 47 l b1-4 r2 v 32 v 33 q b2-3 r2 v 34 v 35 q p 37 "
                           "l b1-4 36 b2-3 38 39 41 40 p 40 l b1-4 r2 v 42 v 43 q b2-3 r2 v 42 v 43 q p 48 M3 "
                           "r4 v 49 52 v 51 v 49 52 v 51 q 53 50 45 M2 F1";

/// A course of items which are written as the OCD records have them.
Course courseOf(const QString& name, Course::Type type, int legs, const char* items)
{
	Course course;
	course.name = name;
	course.type = type;
	course.legs = legs;
	for (auto const& word : QString::fromLatin1(items).split(QLatin1Char(' '), QString::SkipEmptyParts))
	{
		CourseItem item;
		auto const first = word.at(0);
		if (word == QLatin1String("v"))
		{
			item.type = CourseItem::ForkBranch;
		}
		else if (word == QLatin1String("q"))
		{
			item.type = CourseItem::TeamForkEnd;
		}
		else if (word == QLatin1String("l"))
		{
			item.type = CourseItem::LegForkBegin;
		}
		else if (word == QLatin1String("p"))
		{
			item.type = CourseItem::LegForkEnd;
		}
		else if (first == QLatin1Char('r'))
		{
			item.type = CourseItem::TeamForkBegin;
			item.count = word.mid(1).toInt();
		}
		else if (first == QLatin1Char('b'))
		{
			item.type = CourseItem::LegBranch;
			item.legs = word.mid(1);
		}
		else
		{
			item.type = first == QLatin1Char('S') ? CourseItem::Start
			            : first == QLatin1Char('M') ? CourseItem::MarkedRoute
			            : first == QLatin1Char('F') ? CourseItem::Finish : CourseItem::Control;
			item.code = word;
		}
		course.items.push_back(item);
	}
	return course;
}

Course oneManRelay()
{
	auto course = courseOf(QStringLiteral("One-Man Relay"), Course::OneManRelay, 2, one_man_items);
	course.climb_m = 40;
	return course;
}

Course relayWomen()
{
	auto course = courseOf(QStringLiteral("Relay Women"), Course::Relay, 3, women_items);
	course.climb_m = 50;
	return course;
}

Course sprintRelay()
{
	return courseOf(QStringLiteral("Sprint Relay"), Course::Relay, 4, sprint_items);
}

/// The codes of items, as "S1 31 F1".
QString codesOf(const std::vector<CourseItem>& items)
{
	QStringList codes;
	for (auto const& item : items)
		codes.push_back(item.namesObject() ? item.code : QStringLiteral("?"));
	return codes.join(QLatin1Char(' '));
}

/// The items as the records write them, "S1 r2 v 31 v 32 q F1".
QString wordsOf(const std::vector<CourseItem>& items)
{
	QStringList words;
	for (auto const& item : items)
	{
		switch (item.type)
		{
		case CourseItem::TeamForkBegin:  words.push_back(QLatin1Char('r') + QString::number(item.count)); break;
		case CourseItem::ForkBranch:     words.push_back(QStringLiteral("v")); break;
		case CourseItem::TeamForkEnd:    words.push_back(QStringLiteral("q")); break;
		case CourseItem::LegForkBegin:   words.push_back(QStringLiteral("l")); break;
		case CourseItem::LegBranch:      words.push_back(QLatin1Char('b') + item.legs); break;
		case CourseItem::LegForkEnd:     words.push_back(QStringLiteral("p")); break;
		default:                         words.push_back(item.namesObject() ? item.code : QStringLiteral("?")); break;
		}
	}
	return words.join(QLatin1Char(' '));
}

QStringList codesOf(const std::vector<RelayVariant>& variants)
{
	QStringList codes;
	for (auto const& variant : variants)
		codes.push_back(relayVariantCode(variant));
	return codes;
}

/// The variant with the code, or one of the leg 0.
RelayVariant variantOf(const RelayStructure& structure, const QString& code)
{
	for (auto const& variant : relayVariants(structure))
	{
		if (relayVariantCode(variant) == code)
			return variant;
	}
	return {};
}

/// Says whether a team runs every branch of every team variation as often as the others.
bool isFair(const RelayStructure& structure, const std::vector<RelayVariant>& team)
{
	for (std::size_t f = 0; f < structure.team_forks.size(); ++f)
	{
		auto const& fork = structure.team_forks[f];
		std::vector<int> runs(std::size_t(fork.branches), 0);
		for (auto const leg : fork.legs)
		{
			if (std::size_t(leg) > team.size())
				return false;
			auto const branch = team[std::size_t(leg - 1)].branches[f];
			if (branch < 0 || branch >= fork.branches)
				return false;
			++runs[std::size_t(branch)];
		}
		for (auto const count : runs)
		{
			if (count * fork.branches != int(fork.legs.size()))
				return false;
		}
	}
	return true;
}

QString keyOf(const std::vector<RelayVariant>& team)
{
	return codesOf(team).join(QLatin1Char('|'));
}

PointSymbol* addSymbol(Map& map, int number, Symbol::CourseRole role)
{
	auto* symbol = new PointSymbol();
	symbol->setNumberComponent(0, number);
	symbol->setCourseRole(role);
	map.addSymbol(symbol, map.getNumSymbols());
	return symbol;
}

void draw(Map& map, const Symbol* symbol, double x, double y)
{
	auto* object = new PointObject(symbol);
	object->setPosition(MapCoordF(x, y));
	auto const index = map.addObject(object);
	auto* step = new DeleteObjectsUndoStep(&map);
	step->addObject(index);
	map.push(step);
}

/// A map with the objects of a relay R of two legs, S1, a variation of 31
/// and 32, 33 and F1, and a normal course N.
void fillRelayMap(Map& map)
{
	map.setScaleDenominator(10000);
	auto const* start = addSymbol(map, 701, Symbol::CourseStart);
	auto const* control = addSymbol(map, 703, Symbol::CourseControl);
	auto const* finish = addSymbol(map, 706, Symbol::CourseFinish);
	draw(map, start, 0.0, 0.0);
	draw(map, control, 10.0, 5.0);
	draw(map, control, 10.0, -10.0);
	draw(map, control, 20.0, 0.0);
	draw(map, finish, 30.0, 0.0);
	auto data = map.courseData();
	data.courses.push_back(courseOf(QStringLiteral("R"), Course::Relay, 2, "S1 r2 v 31 v 32 q 33 F1"));
	data.courses.push_back(courseOf(QStringLiteral("N"), Course::Normal, 0, "S1 31 33 F1"));
	changeCourseData(map, data);
}

std::vector<Issue> issuesOf(const std::vector<Issue>& issues, const char* rule)
{
	std::vector<Issue> result;
	for (auto const& issue : issues)
	{
		if (issue.rule == rule)
			result.push_back(issue);
	}
	return result;
}

}  // namespace



/**
 * @test Tests the relays: the structure of their variations, the
 * variants, and the allocation of the variants to the teams.
 *
 * See the design notes, "Relays: variations, variants and
 * their allocation".
 */
class CourseRelayTest : public QObject
{
Q_OBJECT
private slots:
	void initTestCase();
	void structureTest();
	void faultTest_data();
	void faultTest();
	void variantTest();
	void allocationTest();
	void oneManAllocationTest();
	void rotationTest();
	void textTest();
	void measureTest();
	void checkTest();
	void dockTest();
	void editTest();
	void dockEditTest();
};


void CourseRelayTest::initTestCase()
{
	Q_INIT_RESOURCE(resources);
	doStaticInitializations();
}


/// Accept (1)
void CourseRelayTest::structureTest()
{
	auto structure = relayStructure(relayWomen());
	QVERIFY2(structure.isValid(), qPrintable(structure.faults.join(QLatin1Char(' '))));
	QCOMPARE(structure.legs, 3);
	QCOMPARE(int(structure.team_forks.size()), 2);
	QCOMPARE(structure.team_forks[0].branches, 3);
	QCOMPARE(structure.team_forks[0].legs, (std::vector<int>{ 1, 2, 3 }));
	QCOMPARE(structure.team_forks[0].begin, 2);
	QCOMPARE(structure.team_forks[1].branches, 2);
	QCOMPARE(structure.team_forks[1].legs, (std::vector<int>{ 1, 2 }));
	
	structure = relayStructure(sprintRelay());
	QVERIFY2(structure.isValid(), qPrintable(structure.faults.join(QLatin1Char(' '))));
	QCOMPARE(structure.legs, 4);
	QCOMPARE(int(structure.team_forks.size()), 6);
	QCOMPARE(structure.team_forks[0].legs, (std::vector<int>{ 1, 2, 3, 4 }));
	QCOMPARE(structure.team_forks[1].legs, (std::vector<int>{ 1, 4 }));
	QCOMPARE(structure.team_forks[2].legs, (std::vector<int>{ 2, 3 }));
	QCOMPARE(structure.team_forks[5].branches, 4);
	
	structure = relayStructure(oneManRelay());
	QVERIFY(structure.isValid());
	QCOMPARE(int(structure.team_forks.size()), 4);
	for (auto const& fork : structure.team_forks)
		QCOMPARE(fork.legs, (std::vector<int>{ 1, 2 }));
	
	structure = relayStructure(courseOf(QStringLiteral("N"), Course::Normal, 0, "S1 31 32 F1"));
	QVERIFY(structure.isValid());
	QVERIFY(structure.team_forks.empty());
	QVERIFY(!structure.has_variations);
	QVERIFY(relayVariants(structure).empty());
	
	// A relay without variations has a variant for every leg.
	structure = relayStructure(courseOf(QStringLiteral("P"), Course::Relay, 3, "S1 31 32 F1"));
	QVERIFY(structure.isValid());
	QCOMPARE(codesOf(relayVariants(structure)), (QStringList{ QStringLiteral("1"), QStringLiteral("2"), QStringLiteral("3") }));
}


void CourseRelayTest::faultTest_data()
{
	QTest::addColumn<int>("type");
	QTest::addColumn<int>("legs");
	QTest::addColumn<QString>("items");
	QTest::addColumn<bool>("valid");
	
	QTest::newRow("normal course") << int(Course::Normal) << 0 << "S1 r2 v 31 v 32 q 33 F1" << false;
	QTest::newRow("one leg") << int(Course::Relay) << 1 << "S1 r2 v 31 v 32 q 33 F1" << false;
	QTest::newRow("no end") << int(Course::Relay) << 2 << "S1 r2 v 31 v 32 33 F1" << false;
	QTest::newRow("end alone") << int(Course::Relay) << 2 << "S1 31 q 33 F1" << false;
	QTest::newRow("branch alone") << int(Course::Relay) << 2 << "S1 31 v 33 F1" << false;
	QTest::newRow("count") << int(Course::Relay) << 3 << "S1 r3 v 31 v 32 q 33 F1" << false;
	QTest::newRow("one branch") << int(Course::Relay) << 2 << "S1 r1 v 31 q 33 F1" << false;
	QTest::newRow("three on two legs") << int(Course::Relay) << 2 << "S1 r3 v 31 v 32 v 34 q 33 F1" << false;
	QTest::newRow("two on three legs") << int(Course::Relay) << 3 << "S1 r2 v 31 v 32 q 33 F1" << false;
	QTest::newRow("two on four legs") << int(Course::Relay) << 4 << "S1 r2 v 31 v 32 q 33 F1" << true;
	QTest::newRow("leg missing") << int(Course::Relay) << 4 << "S1 l b1-2 31 b3 32 p 33 F1" << false;
	QTest::newRow("one leg branch") << int(Course::Relay) << 3 << "S1 l b1-2 31 p 33 F1" << false;
	QTest::newRow("leg twice") << int(Course::Relay) << 3 << "S1 l b1-2 31 b2-3 32 p 33 F1" << false;
	QTest::newRow("leg unknown") << int(Course::Relay) << 3 << "S1 l b1-2 31 b3-4 32 p 33 F1" << false;
	QTest::newRow("leg variation") << int(Course::Relay) << 3 << "S1 l b1-2 31 b3 32 p 33 F1" << true;
	QTest::newRow("leg end missing") << int(Course::Relay) << 3 << "S1 l b1-2 31 b3 32 33 F1" << false;
	QTest::newRow("no control between") << int(Course::Relay) << 2 << "S1 r2 v 31 v 32 q r2 v 34 v 35 q 33 F1" << false;
	QTest::newRow("control between") << int(Course::Relay) << 2 << "S1 r2 v 31 v 32 q 36 r2 v 34 v 35 q 33 F1" << true;
	QTest::newRow("nested") << int(Course::Relay) << 2 << "S1 l b1 r2 v 31 v 32 q b2 34 p 33 F1" << false;
}

/// Accept (2)
void CourseRelayTest::faultTest()
{
	QFETCH(int, type);
	QFETCH(int, legs);
	QFETCH(QString, items);
	QFETCH(bool, valid);
	auto const course = courseOf(QStringLiteral("X"), Course::Type(type), legs, items.toLatin1().constData());
	auto const structure = relayStructure(course);
	QCOMPARE(structure.isValid(), valid);
	if (valid)
	{
		QVERIFY(!relayVariants(structure).empty());
		QVERIFY(allocateRelay(structure, 4).ok);
	}
	else
	{
		QCOMPARE(structure.faults.size(), 1);
		QVERIFY(!structure.faults.front().isEmpty());
		QVERIFY(relayVariants(structure).empty());
		QVERIFY(!allocateRelay(structure, 4).ok);
		// Each fault has its own words.
		static QStringList seen;
		QVERIFY2(!seen.contains(structure.faults.front()) || QByteArray(QTest::currentDataTag()) == "leg end missing"
		         || QByteArray(QTest::currentDataTag()) == "nested" || QByteArray(QTest::currentDataTag()) == "two on three legs"
		         || QByteArray(QTest::currentDataTag()) == "branch alone" || QByteArray(QTest::currentDataTag()) == "one leg branch",
		         qPrintable(structure.faults.front()));
		seen.push_back(structure.faults.front());
	}
}


/// Accept (3)
void CourseRelayTest::variantTest()
{
	auto const women = relayWomen();
	auto structure = relayStructure(women);
	auto const variants = relayVariants(structure);
	QCOMPARE(int(variants.size()), 15);
	QCOMPARE(codesOf(variants), (QStringList{
	    QStringLiteral("1AA"), QStringLiteral("1AB"), QStringLiteral("1BA"), QStringLiteral("1BB"), QStringLiteral("1CA"), QStringLiteral("1CB"),
	    QStringLiteral("2AA"), QStringLiteral("2AB"), QStringLiteral("2BA"), QStringLiteral("2BB"), QStringLiteral("2CA"), QStringLiteral("2CB"),
	    QStringLiteral("3A "), QStringLiteral("3B "), QStringLiteral("3C ") }));
	
	auto variant = variantOf(structure, QStringLiteral("1CB"));
	QCOMPARE(variant.leg, 1);
	QCOMPARE(variant.branches, (std::vector<int>{ 2, 1 }));
	QCOMPARE(codesOf(relayVariantItems(women, structure, variant)), QStringLiteral("M6 S4 49 56 59 65 62 63 M4 F3"));
	variant = variantOf(structure, QStringLiteral("3B "));
	QCOMPARE(variant.branches, (std::vector<int>{ 1, -1 }));
	QCOMPARE(codesOf(relayVariantItems(women, structure, variant)), QStringLiteral("M6 S4 50 57 59 60 61 63 M4 F3"));
	
	auto const course = relayVariantCourse(women, structure, variantOf(structure, QStringLiteral("1CB")));
	QCOMPARE(course.name, QStringLiteral("Relay Women 1CB"));
	QCOMPARE(course.type, Course::Normal);
	QCOMPARE(course.legs, 0);
	QCOMPARE(course.climb_m, 50);
	QCOMPARE(int(course.items.size()), 10);
	QCOMPARE(course.items[0].type, CourseItem::MarkedRoute);
	QCOMPARE(course.items[2].type, CourseItem::Control);
	QCOMPARE(relayVariantCourse(women, structure, variantOf(structure, QStringLiteral("3B "))).name, QStringLiteral("Relay Women 3B"));
	
	auto const one_man = oneManRelay();
	structure = relayStructure(one_man);
	QCOMPARE(int(relayVariants(structure).size()), 32);
	QCOMPARE(codesOf(relayVariantItems(one_man, structure, variantOf(structure, QStringLiteral("1ABAB")))),
	         QStringLiteral("S1 36 37 46 61 56 65 59 39 44 32 40 F4"));
	
	auto const sprint = sprintRelay();
	structure = relayStructure(sprint);
	QCOMPARE(int(relayVariants(structure).size()), 256);
	auto const first = relayVariants(structure).front();
	QCOMPARE(relayVariantCode(first), QStringLiteral("1AA A A"));
	auto const codes = codesOf(relayVariantItems(sprint, structure, first)).split(QLatin1Char(' '));
	QVERIFY(codes.contains(QStringLiteral("36")));
	QVERIFY(codes.contains(QStringLiteral("32")));
	QVERIFY(!codes.contains(QStringLiteral("34")));
	QVERIFY(!codes.contains(QStringLiteral("38")));
	
	// Course data with a variant in the place of the relay.
	CourseData data;
	data.courses.push_back(women);
	auto const with = withRelayVariant(data, QStringLiteral("Relay Women"), variantOf(relayStructure(women), QStringLiteral("3B ")));
	QCOMPARE(int(with.courses.size()), 1);
	QCOMPARE(with.courses[0].name, QStringLiteral("Relay Women"));
	QCOMPARE(with.courses[0].type, Course::Normal);
	QCOMPARE(codesOf(with.courses[0].items), QStringLiteral("M6 S4 50 57 59 60 61 63 M4 F3"));
	// What is no variant of the course changes nothing.
	QVERIFY(withRelayVariant(data, QStringLiteral("Relay Women"), RelayVariant{}) == data);
	QVERIFY(withRelayVariant(data, QStringLiteral("Nowhere"), variant) == data);
}


/// Accept (4)
void CourseRelayTest::allocationTest()
{
	auto const structure = relayStructure(relayWomen());
	auto const allocation = allocateRelay(structure, 50);
	QVERIFY(allocation.ok);
	QVERIFY(!allocation.by_rotation);
	QCOMPARE(int(allocation.teams.size()), 50);
	
	std::map<QString, int> used;
	QString before;
	for (auto const& team : allocation.teams)
	{
		QCOMPARE(int(team.size()), 3);
		QCOMPARE(team[0].leg, 1);
		QCOMPARE(team[2].leg, 3);
		QVERIFY(isFair(structure, team));
		auto const key = keyOf(team);
		QVERIFY(key != before);
		before = key;
		++used[key];
	}
	QCOMPARE(int(used.size()), 12);
	for (auto const& entry : used)
		QVERIFY2(entry.second == 4 || entry.second == 5, qPrintable(entry.first));
	
	QCOMPARE(codesOf(allocation.teams[0]), (QStringList{ QStringLiteral("1AA"), QStringLiteral("2BB"), QStringLiteral("3C ") }));
	// The second team differs from the first wherever it can.
	QCOMPARE(codesOf(allocation.teams[1]), (QStringList{ QStringLiteral("1BB"), QStringLiteral("2CA"), QStringLiteral("3A ") }));
	
	auto const again = allocateRelay(structure, 50);
	for (std::size_t t = 0; t < 50; ++t)
		QCOMPARE(keyOf(again.teams[t]), keyOf(allocation.teams[t]));
	// More teams begin as fewer do.
	auto const fewer = allocateRelay(structure, 7);
	QCOMPARE(int(fewer.teams.size()), 7);
	for (std::size_t t = 0; t < 7; ++t)
		QCOMPARE(keyOf(fewer.teams[t]), keyOf(allocation.teams[t]));
	
	QVERIFY(allocateRelay(structure, 0).ok);
	QVERIFY(allocateRelay(structure, 0).teams.empty());
}


/// Accept (5)
void CourseRelayTest::oneManAllocationTest()
{
	auto const structure = relayStructure(oneManRelay());
	auto const allocation = allocateRelay(structure, 10);
	QVERIFY(allocation.ok);
	std::set<QString> used;
	for (auto const& team : allocation.teams)
	{
		QCOMPARE(int(team.size()), 2);
		QVERIFY(isFair(structure, team));
		for (std::size_t f = 0; f < 4; ++f)
			QCOMPARE(team[0].branches[f] + team[1].branches[f], 1);
		used.insert(keyOf(team));
	}
	QCOMPARE(int(used.size()), 10);
	QCOMPARE(codesOf(allocation.teams[0]), (QStringList{ QStringLiteral("1AAAA"), QStringLiteral("2BBBB") }));
	// The second team is the first one turned round, as far from it as can be.
	QCOMPARE(codesOf(allocation.teams[1]), (QStringList{ QStringLiteral("1BBBB"), QStringLiteral("2AAAA") }));
}



/// Accept (7)
void CourseRelayTest::rotationTest()
{
	QString items = QStringLiteral("S1");
	auto code = 31;
	for (int fork = 0; fork < 5; ++fork)
	{
		items += QStringLiteral(" r6");
		for (int branch = 0; branch < 6; ++branch)
			items += QStringLiteral(" v %1").arg(code++);
		items += QStringLiteral(" q %1").arg(code++);
	}
	items += QStringLiteral(" F1");
	auto const structure = relayStructure(courseOf(QStringLiteral("Big"), Course::Relay, 6, items.toLatin1().constData()));
	QVERIFY2(structure.isValid(), qPrintable(structure.faults.join(QLatin1Char(' '))));
	QCOMPARE(int(structure.team_forks.size()), 5);
	
	auto const allocation = allocateRelay(structure, 40);
	QVERIFY(allocation.ok);
	QVERIFY(allocation.by_rotation);
	QCOMPARE(int(allocation.teams.size()), 40);
	std::set<int> first_branches;
	for (std::size_t t = 0; t < allocation.teams.size(); ++t)
	{
		QVERIFY(isFair(structure, allocation.teams[t]));
		if (t < 6)
			first_branches.insert(allocation.teams[t][0].branches[0]);
	}
	QCOMPARE(int(first_branches.size()), 6);
	// The seventh team differs from the first in the second variation.
	QCOMPARE(allocation.teams[6][0].branches[0], allocation.teams[0][0].branches[0]);
	QVERIFY(allocation.teams[6][0].branches[1] != allocation.teams[0][0].branches[1]);
}


/// Accept (8)
void CourseRelayTest::textTest()
{
	CourseData data;
	data.courses.push_back(courseOf(QStringLiteral("Normal"), Course::Normal, 0, "S1 31 F1"));
	data.courses.push_back(oneManRelay());
	data.courses.push_back(relayWomen());
	CourseClass one_man;
	one_man.name = QStringLiteral("One-Man Relay");
	one_man.course = QStringLiteral("One-Man Relay");
	one_man.runners = 10;
	one_man.first_no = 1;
	one_man.last_no = 10;
	data.classes.push_back(one_man);
	CourseClass women;
	women.name = QStringLiteral("Women");
	women.course = QStringLiteral("Relay Women");
	women.first_no = 101;
	women.last_no = 150;
	data.classes.push_back(women);
	
	auto teams = relayTeams(data, QStringLiteral("Relay Women"));
	QCOMPARE(teams.count, 50);
	QCOMPARE(teams.first_number, 101);
	QCOMPARE(teams.class_name, QStringLiteral("Women"));
	teams = relayTeams(data, QStringLiteral("One-Man Relay"));
	QCOMPARE(teams.count, 10);
	QCOMPARE(teams.first_number, 1);
	QCOMPARE(relayTeams(data, QStringLiteral("Normal")).count, 0);
	
	auto const text = relayAllocationText(data);
	QVERIFY(!text.contains(QLatin1Char('\r')));
	auto const lines = text.split(QLatin1Char('\n'));
	QVERIFY(lines.contains(QStringLiteral("One-Man Relay;One-Man Relay")));
	QVERIFY(lines.contains(QStringLiteral("Relay Women;Women")));
	QVERIFY(!text.contains(QStringLiteral("Normal")));
	QVERIFY(lines.contains(QStringLiteral("1: \tAAAA\tBBBB")));
	QVERIFY(lines.contains(QStringLiteral("2: \tBBBB\tAAAA")));
	QVERIFY(lines.contains(QStringLiteral("101.1: 1AA")));
	QVERIFY(lines.contains(QStringLiteral("101.2: 2BB")));
	QVERIFY(lines.contains(QStringLiteral("101.3: 3C ")));
	QVERIFY(lines.contains(QStringLiteral("150.3: 3A ")) || lines.contains(QStringLiteral("150.3: 3B ")) || lines.contains(QStringLiteral("150.3: 3C ")));
	auto one_man_lines = 0;
	auto women_lines = 0;
	for (auto const& line : lines)
	{
		if (line.contains(QStringLiteral(": \t")))
			++one_man_lines;
		else if (line.contains(QStringLiteral(": ")) && line.contains(QLatin1Char('.')))
			++women_lines;
	}
	QCOMPARE(one_man_lines, 10);
	QCOMPARE(women_lines, 150);
}


/// Accept (9)
void CourseRelayTest::measureTest()
{
	Map map;
	fillRelayMap(map);
	auto const& data = map.courseData();
	auto const& relay = *data.findCourse(QStringLiteral("R"));
	auto const structure = relayStructure(relay);
	QVERIFY(structure.isValid());
	QCOMPARE(codesOf(relayVariants(structure)), (QStringList{ QStringLiteral("1A"), QStringLiteral("1B"), QStringLiteral("2A"), QStringLiteral("2B") }));
	QVERIFY(!measureCourse(map, data, relay).known);
	
	auto const first = variantOf(structure, QStringLiteral("1A"));
	auto const second = variantOf(structure, QStringLiteral("1B"));
	auto length = measureCourse(map, data, relayVariantCourse(relay, structure, first));
	QVERIFY(length.known);
	QVERIFY(std::abs(length.measured_m - 323.6) < 0.1);
	length = measureCourse(map, data, relayVariantCourse(relay, structure, second));
	QVERIFY(length.known);
	QVERIFY(std::abs(length.measured_m - 382.8) < 0.1);
	
	QVERIFY(!drawCourse(map, data, QStringLiteral("R")).drawn);
	auto const drawing = drawCourse(map, withRelayVariant(data, QStringLiteral("R"), second), QStringLiteral("R"));
	QVERIFY(drawing.drawn);
	QCOMPARE(int(drawing.legs.size()), 3);
	QCOMPARE(drawing.legs[0].from, QStringLiteral("S1"));
	QCOMPARE(drawing.legs[0].to, QStringLiteral("32"));
	QCOMPARE(drawing.legs[1].to, QStringLiteral("33"));
}


/// Accept (10)
void CourseRelayTest::checkTest()
{
	Map map;
	fillRelayMap(map);
	auto data = map.courseData();
	data.courses.push_back(courseOf(QStringLiteral("Broken"), Course::Relay, 2, "S1 r2 v 31 v 32 33 F1"));
	
	auto const issues = checkCourses(map, data);
	auto const found = issuesOf(issues, "relay-structure");
	QCOMPARE(int(found.size()), 1);
	QCOMPARE(found[0].severity, Issue::Error);
	QCOMPARE(found[0].subject, QStringLiteral("Broken"));
	QVERIFY(!found[0].message.isEmpty());
	
	CourseData women;
	women.courses.push_back(relayWomen());
	QVERIFY(issuesOf(checkCourses(map, women), "relay-structure").empty());
}


/// Accept (11)
void CourseRelayTest::dockTest()
{
	Map map;
	fillRelayMap(map);
	CourseWidget widget(&map);
	auto* variants = widget.findChild<QComboBox*>(QStringLiteral("course-variant"));
	auto* length = widget.findChild<QLabel*>(QStringLiteral("course-length"));
	QVERIFY(variants && length);
	QVERIFY(variants->isHidden());
	QVERIFY(widget.previewData() == map.courseData());
	
	widget.setCurrentCourse(QStringLiteral("R"));
	QVERIFY(!variants->isHidden());
	QCOMPARE(variants->count(), 4);
	QStringList texts;
	for (int i = 0; i < variants->count(); ++i)
		texts.push_back(variants->itemText(i));
	QCOMPARE(texts, (QStringList{ QStringLiteral("1A"), QStringLiteral("1B"), QStringLiteral("2A"), QStringLiteral("2B") }));
	QCOMPARE(variants->currentIndex(), 0);
	QCOMPARE(length->text(), formatCourseLength(323.6));
	auto preview = widget.previewData();
	QCOMPARE(codesOf(preview.findCourse(QStringLiteral("R"))->items), QStringLiteral("S1 31 33 F1"));
	// The data of the map are as they were.
	QCOMPARE(int(map.courseData().findCourse(QStringLiteral("R"))->items.size()), 9);
	
	QSignalSpy changes(&widget, &CourseWidget::currentCourseChanged);
	variants->setCurrentIndex(1);
	QVERIFY(changes.count() >= 1);
	QCOMPARE(length->text(), formatCourseLength(382.8));
	preview = widget.previewData();
	QCOMPARE(codesOf(preview.findCourse(QStringLiteral("R"))->items), QStringLiteral("S1 32 33 F1"));
	
	widget.setCurrentCourse(QStringLiteral("N"));
	QVERIFY(variants->isHidden());
	QVERIFY(widget.previewData() == map.courseData());
	
	// A relay whose structure has a fault: no variants, and a dash.
	auto data = map.courseData();
	data.courses.push_back(courseOf(QStringLiteral("Broken"), Course::Relay, 2, "S1 r2 v 31 v 32 33 F1"));
	changeCourseData(map, data);
	widget.setCurrentCourse(QStringLiteral("Broken"));
	QVERIFY(variants->isHidden());
	QCOMPARE(length->text(), QStringLiteral("-"));
	QVERIFY(widget.previewData() == map.courseData());
}



/// Editing relays, Accept (1) to (6)
void CourseRelayTest::editTest()
{
	auto const plain = "S1 31 33 F1";
	auto const one_variation = "S1 r2 v 31 v 32 q 33 F1";
	auto const two_variations = "S1 r2 v 31 v 32 q 33 r2 v 34 v 35 q F1";
	auto const itemsOf = [](const CourseData& data, const char* name) {
		return wordsOf(data.findCourse(QString::fromLatin1(name))->items);
	};
	
	// (1) Type and legs.
	{
		CourseData data;
		data.courses.push_back(courseOf(QStringLiteral("N"), Course::Normal, 0, plain));
		data.courses.push_back(courseOf(QStringLiteral("R"), Course::Relay, 2, one_variation));
		QString why;
		QVERIFY(setCourseType(data, QStringLiteral("N"), Course::Relay, 3, &why));
		QCOMPARE(data.findCourse(QStringLiteral("N"))->type, Course::Relay);
		QCOMPARE(data.findCourse(QStringLiteral("N"))->legs, 3);
		QVERIFY(setCourseType(data, QStringLiteral("N"), Course::OneManRelay, 2, &why));
		QCOMPARE(data.findCourse(QStringLiteral("N"))->type, Course::OneManRelay);
		QCOMPARE(data.findCourse(QStringLiteral("N"))->legs, 2);
		QVERIFY(setCourseType(data, QStringLiteral("N"), Course::Normal, 5, &why));
		QCOMPARE(data.findCourse(QStringLiteral("N"))->type, Course::Normal);
		QCOMPARE(data.findCourse(QStringLiteral("N"))->legs, 0);
		
		auto const before = data;
		QStringList reasons;
		QVERIFY(!setCourseType(data, QStringLiteral("N"), Course::Relay, 1, &why));
		reasons.push_back(why);
		QVERIFY(!setCourseType(data, QStringLiteral("R"), Course::Normal, 0, &why));
		reasons.push_back(why);
		QVERIFY(!setCourseType(data, QStringLiteral("R"), Course::Relay, 3, &why));
		reasons.push_back(why);
		QVERIFY(!setCourseType(data, QStringLiteral("Nobody"), Course::Relay, 3, &why));
		QVERIFY(data == before);
		for (auto const& reason : reasons)
			QVERIFY(!reason.isEmpty());
		QCOMPARE(int(std::set<QString>(reasons.begin(), reasons.end()).size()), 3);
		
		QVERIFY(setCourseType(data, QStringLiteral("R"), Course::Relay, 4, &why));
		QCOMPARE(data.findCourse(QStringLiteral("R"))->legs, 4);
		QVERIFY(relayStructure(*data.findCourse(QStringLiteral("R"))).isValid());
		QVERIFY(setCourseType(data, QStringLiteral("R"), Course::OneManRelay, 4, &why));
		
		// One class at most.
		CourseClass first;
		first.name = QStringLiteral("H21");
		first.course = QStringLiteral("N");
		auto second = first;
		second.name = QStringLiteral("D21");
		data.classes = { first, second };
		why.clear();
		QVERIFY(!setCourseType(data, QStringLiteral("N"), Course::Relay, 3, &why));
		QVERIFY(!why.isEmpty());
		QCOMPARE(data.findCourse(QStringLiteral("N"))->type, Course::Normal);
		data.classes = { first };
		QVERIFY(setCourseType(data, QStringLiteral("N"), Course::Relay, 3, &why));
	}
	
	// (2) The legs at a place.
	{
		auto const women = relayWomen();
		QCOMPARE(relayLegsAt(women, 2), (std::vector<int>{ 1, 2, 3 }));
		QCOMPARE(relayLegsAt(women, 22), (std::vector<int>{ 1, 2 }));
		QCOMPARE(relayLegsAt(women, 24), (std::vector<int>{ 3 }));
		QCOMPARE(relayLegsAt(women, 27), (std::vector<int>{ 1, 2, 3 }));
		QVERIFY(relayLegsAt(courseOf(QStringLiteral("N"), Course::Normal, 0, plain), 1).empty());
	}
	
	// (3) Team variation.
	{
		CourseData data;
		data.courses.push_back(courseOf(QStringLiteral("T"), Course::Relay, 3, plain));
		data.courses.push_back(courseOf(QStringLiteral("N"), Course::Normal, 0, plain));
		data.courses.push_back(relayWomen());
		QString why;
		auto before = data;
		QVERIFY(!insertTeamVariation(data, QStringLiteral("T"), 2, 2, &why));
		QVERIFY2(why.contains(QLatin1Char('3')), qPrintable(why));
		QVERIFY(!insertTeamVariation(data, QStringLiteral("T"), 2, 1, &why));
		QVERIFY(!why.isEmpty());
		QVERIFY(!insertTeamVariation(data, QStringLiteral("T"), 5, 3, &why));
		QVERIFY(!insertTeamVariation(data, QStringLiteral("T"), -1, 3, &why));
		why.clear();
		QVERIFY(!insertTeamVariation(data, QStringLiteral("N"), 2, 2, &why));
		QVERIFY(!why.isEmpty());
		QVERIFY(data == before);
		
		QVERIFY(insertTeamVariation(data, QStringLiteral("T"), 2, 3, &why));
		QCOMPARE(itemsOf(data, "T"), QStringLiteral("S1 31 r3 v v v q 33 F1"));
		QVERIFY(relayStructure(*data.findCourse(QStringLiteral("T"))).isValid());
		QCOMPARE(int(relayVariants(relayStructure(*data.findCourse(QStringLiteral("T")))).size()), 9);
		before = data;
		why.clear();
		QVERIFY(!insertTeamVariation(data, QStringLiteral("T"), 7, 3, &why));
		QVERIFY(!why.isEmpty());
		QVERIFY(data == before);
		
		// In a branch of a leg variation the legs are those of the branch.
		QVERIFY(!insertTeamVariation(data, QStringLiteral("Relay Women"), 23, 3, &why));
		QVERIFY(!insertTeamVariation(data, QStringLiteral("Relay Women"), 24, 2, &why));
		QVERIFY(!insertTeamVariation(data, QStringLiteral("Relay Women"), 24, 3, &why));
		QVERIFY(data == before);
		QVERIFY(insertTeamVariation(data, QStringLiteral("Relay Women"), 23, 2, &why));
		QCOMPARE(itemsOf(data, "Relay Women"),
		         QStringLiteral("M6 S4 r3 v 48 55 v 50 57 v 49 56 q 59 l b1-2 r2 v 64 v 65 q 62 r2 v v q b3 60 61 p 63 M4 F3"));
		auto const structure = relayStructure(*data.findCourse(QStringLiteral("Relay Women")));
		QVERIFY(structure.isValid());
		QCOMPARE(int(structure.team_forks.size()), 3);
		QCOMPARE(structure.team_forks[2].legs, (std::vector<int>{ 1, 2 }));
	}
	
	// (4) Leg variation.
	{
		CourseData data;
		data.courses.push_back(courseOf(QStringLiteral("T"), Course::Relay, 3, plain));
		QString why;
		auto const before = data;
		QVERIFY(!insertLegVariation(data, QStringLiteral("T"), 2, { QStringLiteral("1-2") }, &why));
		QVERIFY(!why.isEmpty());
		QVERIFY(!insertLegVariation(data, QStringLiteral("T"), 2, { QStringLiteral("1-2"), QStringLiteral("2-3") }, &why));
		QVERIFY(!insertLegVariation(data, QStringLiteral("T"), 2, { QStringLiteral("1"), QStringLiteral("2") }, &why));
		QVERIFY(!insertLegVariation(data, QStringLiteral("T"), 2, { QStringLiteral("1-2"), QStringLiteral("x") }, &why));
		QVERIFY(!insertLegVariation(data, QStringLiteral("T"), 9, { QStringLiteral("1-2"), QStringLiteral("3") }, &why));
		QVERIFY(data == before);
		
		QVERIFY(insertLegVariation(data, QStringLiteral("T"), 2, { QStringLiteral(" 1 - 2 "), QStringLiteral("3") }, &why));
		QCOMPARE(itemsOf(data, "T"), QStringLiteral("S1 31 l b1-2 b3 p 33 F1"));
		QVERIFY(relayStructure(*data.findCourse(QStringLiteral("T"))).isValid());
	}
	
	// (5) Range and removal.
	{
		CourseData data;
		data.courses.push_back(relayWomen());
		data.courses.push_back(courseOf(QStringLiteral("Open"), Course::Relay, 2, "S1 r2 v 31 v 32 33 F1"));
		auto const& women = *data.findCourse(QStringLiteral("Relay Women"));
		auto first = -1;
		auto last = -1;
		QVERIFY(variationRange(women, 6, first, last));
		QCOMPARE(first, 2);
		QCOMPARE(last, 12);
		QVERIFY(variationRange(women, 19, first, last));
		QCOMPARE(first, 16);
		QCOMPARE(last, 21);
		QVERIFY(variationRange(women, 23, first, last));
		QCOMPARE(first, 14);
		QCOMPARE(last, 26);
		QVERIFY(variationRange(women, 26, first, last));
		QCOMPARE(first, 14);
		QCOMPARE(last, 26);
		QVERIFY(variationRange(women, 2, first, last));
		QCOMPARE(first, 2);
		QCOMPARE(last, 12);
		QVERIFY(!variationRange(women, 4, first, last));
		QVERIFY(!variationRange(women, 30, first, last));
		
		auto const before = data;
		QVERIFY(!removeVariation(data, QStringLiteral("Relay Women"), 4));
		QVERIFY(!removeVariation(data, QStringLiteral("Nobody"), 2));
		QVERIFY(data == before);
		QVERIFY(removeVariation(data, QStringLiteral("Relay Women"), 19));
		QCOMPARE(itemsOf(data, "Relay Women"), QStringLiteral("M6 S4 r3 v 48 55 v 50 57 v 49 56 q 59 l b1-2 62 b3 60 61 p 63 M4 F3"));
		QVERIFY(relayStructure(*data.findCourse(QStringLiteral("Relay Women"))).isValid());
		QVERIFY(removeVariation(data, QStringLiteral("Relay Women"), 17));
		QCOMPARE(itemsOf(data, "Relay Women"), QStringLiteral("M6 S4 r3 v 48 55 v 50 57 v 49 56 q 59 63 M4 F3"));
		QVERIFY(relayStructure(*data.findCourse(QStringLiteral("Relay Women"))).isValid());
		
		// A variation which is not whole: every marker for itself.
		auto const& open = *data.findCourse(QStringLiteral("Open"));
		QVERIFY(!relayStructure(open).isValid());
		for (auto const index : { 1, 2, 4 })
		{
			QVERIFY(variationRange(open, index, first, last));
			QCOMPARE(first, index);
			QCOMPARE(last, index);
		}
		QVERIFY(removeVariation(data, QStringLiteral("Open"), 1));
		QCOMPARE(itemsOf(data, "Open"), QStringLiteral("S1 v 31 v 32 33 F1"));
		QVERIFY(removeVariation(data, QStringLiteral("Open"), 1));
		QVERIFY(removeVariation(data, QStringLiteral("Open"), 2));
		QCOMPARE(itemsOf(data, "Open"), QStringLiteral("S1 31 32 33 F1"));
		QVERIFY(relayStructure(*data.findCourse(QStringLiteral("Open"))).isValid());
	}
	
	// (6) The rule.
	{
		CourseData data;
		data.courses.push_back(courseOf(QStringLiteral("Two"), Course::Relay, 2, two_variations));
		data.courses.push_back(courseOf(QStringLiteral("Open"), Course::Relay, 2, "S1 r2 v 31 v 32 33 F1"));
		auto changed = data;
		QVERIFY(removeCourseItem(changed, QStringLiteral("Two"), 7));
		auto const fault = relayFaultOfChange(data, changed, QStringLiteral("Two"));
		QVERIFY(!fault.isEmpty());
		QCOMPARE(fault, relayStructure(*changed.findCourse(QStringLiteral("Two"))).faults.constFirst());
		// The other course did not change.
		QVERIFY(relayFaultOfChange(data, changed, QStringLiteral("Open")).isEmpty());
		
		changed = data;
		QVERIFY(removeCourseItem(changed, QStringLiteral("Two"), 3));
		QVERIFY(relayFaultOfChange(data, changed, QStringLiteral("Two")).isEmpty());
		
		// A course which has a fault can be changed.
		changed = data;
		QVERIFY(removeCourseItem(changed, QStringLiteral("Open"), 3));
		QVERIFY(!relayStructure(*changed.findCourse(QStringLiteral("Open"))).isValid());
		QVERIFY(relayFaultOfChange(data, changed, QStringLiteral("Open")).isEmpty());
	}
}


/// Editing relays, Accept (7)
void CourseRelayTest::dockEditTest()
{
	Map map;
	fillRelayMap(map);
	CourseWidget widget(&map);
	auto* type = widget.findChild<QComboBox*>(QStringLiteral("course-type"));
	auto* legs = widget.findChild<QSpinBox*>(QStringLiteral("course-legs"));
	auto* team_button = widget.findChild<QAction*>(QStringLiteral("course-team-variation"));
	auto* leg_button = widget.findChild<QAction*>(QStringLiteral("course-leg-variation"));
	auto* remove_button = widget.findChild<QPushButton*>(QStringLiteral("course-variation-remove"));
	auto* items = widget.findChild<QTreeWidget*>(QStringLiteral("course-items"));
	auto* objects = widget.findChild<QTreeWidget*>(QStringLiteral("course-objects"));
	auto* variants = widget.findChild<QComboBox*>(QStringLiteral("course-variant"));
	QVERIFY(type && legs && team_button && leg_button && remove_button && items && objects && variants);
	auto const itemsOf = [&map](const char* name) {
		return wordsOf(map.courseData().findCourse(QString::fromLatin1(name))->items);
	};
	auto const currentIndex = [items]() { return items->indexOfTopLevelItem(items->currentItem()); };
	
	// Without a course nothing can be set.
	QVERIFY(!type->isEnabled());
	QVERIFY(!widget.setCourseType(Course::Relay, 2));
	
	widget.setCurrentCourse(QStringLiteral("N"));
	QCOMPARE(type->count(), 3);
	QVERIFY(type->isEnabled());
	QCOMPARE(type->currentIndex(), 0);
	QCOMPARE(type->currentText(), CourseWidget::tr("Normal course"));
	QVERIFY(!legs->isEnabled());
	QVERIFY(!team_button->isEnabled());
	QVERIFY(!leg_button->isEnabled());
	QVERIFY(!remove_button->isEnabled());
	
	auto steps = map.undoManager().undoStepCount();
	QVERIFY(widget.setCourseType(Course::Relay, 2));
	QCOMPARE(map.undoManager().undoStepCount(), steps + 1);
	QCOMPARE(type->currentIndex(), 1);
	QVERIFY(legs->isEnabled());
	QCOMPARE(legs->value(), 2);
	QVERIFY(team_button->isEnabled());
	QVERIFY(leg_button->isEnabled());
	QVERIFY(!remove_button->isEnabled());
	QVERIFY(map.undoManager().undo());
	QCOMPARE(widget.currentCourse(), QStringLiteral("N"));
	QCOMPARE(type->currentIndex(), 0);
	QVERIFY(!legs->isEnabled());
	QCOMPARE(map.courseData().findCourse(QStringLiteral("N"))->type, Course::Normal);
	
	// A team variation behind the current item; its first branch is current then.
	QVERIFY(widget.setCourseType(Course::Relay, 2));
	items->setCurrentItem(items->topLevelItem(1));
	steps = map.undoManager().undoStepCount();
	QVERIFY(widget.addTeamVariation(2));
	QCOMPARE(map.undoManager().undoStepCount(), steps + 1);
	QCOMPARE(itemsOf("N"), QStringLiteral("S1 31 r2 v v q 33 F1"));
	QCOMPARE(currentIndex(), 3);
	QVERIFY(remove_button->isEnabled());
	
	// Insert puts the selected object into the first branch.
	objects->clearSelection();
	for (int i = 0; i < objects->topLevelItemCount(); ++i)
	{
		if (objects->topLevelItem(i)->text(0) == QLatin1String("32"))
			objects->topLevelItem(i)->setSelected(true);
	}
	QCOMPARE(currentIndex(), 3);
	widget.insertSelectedObjects();
	QCOMPARE(itemsOf("N"), QStringLiteral("S1 31 r2 v 32 v q 33 F1"));
	QStringList codes;
	for (int i = 0; i < variants->count(); ++i)
		codes.push_back(variants->itemText(i));
	QCOMPARE(codes.join(QLatin1Char(' ')), QStringLiteral("1A 1B 2A 2B"));
	
	// The list shows the structure.
	QCOMPARE(items->topLevelItemCount(), 9);
	QStringList rows;
	for (int i = 0; i < items->topLevelItemCount(); ++i)
		rows.push_back(items->topLevelItem(i)->text(1));
	auto const indent = QStringLiteral("    ");
	QCOMPARE(rows, (QStringList{
	             QStringLiteral("S1"),
	             QStringLiteral("31"),
	             CourseWidget::tr("Team variation, %1 branches").arg(2),
	             indent + CourseWidget::tr("Branch %1").arg(QLatin1Char('A')),
	             indent + indent + QStringLiteral("32"),
	             indent + CourseWidget::tr("Branch %1").arg(QLatin1Char('B')),
	             CourseWidget::tr("End of team variation"),
	             QStringLiteral("33"),
	             QStringLiteral("F1") }));
	
	// What cannot be run is refused, with its reason.
	QVERIFY(widget.lastRefusal().isEmpty());
	QVERIFY(!widget.addTeamVariation(3));
	QVERIFY(!widget.lastRefusal().isEmpty());
	QCOMPARE(itemsOf("N"), QStringLiteral("S1 31 r2 v 32 v q 33 F1"));
	QVERIFY(!widget.addLegVariation({ QStringLiteral("1") }));
	QVERIFY(!widget.lastRefusal().isEmpty());
	QCOMPARE(itemsOf("N"), QStringLiteral("S1 31 r2 v 32 v q 33 F1"));
	
	// A leg variation behind 33, and away again.
	items->setCurrentItem(items->topLevelItem(7));
	QVERIFY(widget.addLegVariation({ QStringLiteral("1"), QStringLiteral("2") }));
	QVERIFY(widget.lastRefusal().isEmpty());
	QCOMPARE(itemsOf("N"), QStringLiteral("S1 31 r2 v 32 v q 33 l b1 b2 p F1"));
	QCOMPARE(currentIndex(), 9);
	QVERIFY(widget.removeCurrentVariation());
	QCOMPARE(itemsOf("N"), QStringLiteral("S1 31 r2 v 32 v q 33 F1"));
	
	// Removing the variation at its end marker, and bringing it back.
	items->setCurrentItem(items->topLevelItem(1));
	QVERIFY(!remove_button->isEnabled());
	QVERIFY(!widget.removeCurrentVariation());
	items->setCurrentItem(items->topLevelItem(6));
	QVERIFY(remove_button->isEnabled());
	steps = map.undoManager().undoStepCount();
	QVERIFY(widget.removeCurrentVariation());
	QCOMPARE(map.undoManager().undoStepCount(), steps + 1);
	QCOMPARE(itemsOf("N"), QStringLiteral("S1 31 33 F1"));
	// A relay without variations has a variant for every leg.
	QCOMPARE(variants->count(), 2);
	QVERIFY(map.undoManager().undo());
	QCOMPARE(itemsOf("N"), QStringLiteral("S1 31 r2 v 32 v q 33 F1"));
	
	// The relay R keeps its type and its legs while it has its variation.
	widget.setCurrentCourse(QStringLiteral("R"));
	QCOMPARE(type->currentIndex(), 1);
	QCOMPARE(legs->value(), 2);
	QVERIFY(!widget.setCourseType(Course::Normal, 0));
	QVERIFY(!widget.lastRefusal().isEmpty());
	QVERIFY(!widget.setCourseType(Course::Relay, 3));
	QVERIFY(!widget.lastRefusal().isEmpty());
	QCOMPARE(type->currentIndex(), 1);
	QCOMPARE(legs->value(), 2);
	QVERIFY(widget.setCourseType(Course::OneManRelay, 2));
	QCOMPARE(type->currentIndex(), 2);
	QVERIFY(widget.setCourseType(Course::Relay, 2));
	
	// One class.
	QVERIFY(widget.addClass(QStringLiteral("Teams")));
	QVERIFY(widget.lastRefusal().isEmpty());
	QVERIFY(!widget.addClass(QStringLiteral("More teams")));
	QVERIFY(!widget.lastRefusal().isEmpty());
	QCOMPARE(int(map.courseData().classes.size()), 1);
	
	// The control between two variations stays where it is.
	auto data = map.courseData();
	data.courses.push_back(courseOf(QStringLiteral("Two"), Course::Relay, 2, "S1 r2 v 31 v 32 q 33 r2 v 31 v 32 q F1"));
	changeCourseData(map, data);
	widget.setCurrentCourse(QStringLiteral("Two"));
	QCOMPARE(items->topLevelItemCount(), 15);
	items->setCurrentItem(items->topLevelItem(7));
	widget.removeCurrentItem();
	QVERIFY(!widget.lastRefusal().isEmpty());
	QCOMPARE(itemsOf("Two"), QStringLiteral("S1 r2 v 31 v 32 q 33 r2 v 31 v 32 q F1"));
	widget.moveCurrentItem(-1);
	QVERIFY(!widget.lastRefusal().isEmpty());
	QCOMPARE(itemsOf("Two"), QStringLiteral("S1 r2 v 31 v 32 q 33 r2 v 31 v 32 q F1"));
	// A control of a branch may leave.
	items->setCurrentItem(items->topLevelItem(3));
	widget.removeCurrentItem();
	QVERIFY(widget.lastRefusal().isEmpty());
	QCOMPARE(itemsOf("Two"), QStringLiteral("S1 r2 v v 32 q 33 r2 v 31 v 32 q F1"));
}


/*
 * We don't need a real GUI window.
 *
 * But we discovered QTBUG-58768 macOS: Crash when using QPrinter
 * while running with "minimal" platform plugin.
 */
#ifndef Q_OS_MACOS
namespace {
	auto Q_DECL_UNUSED qpa_selected = qputenv("QT_QPA_PLATFORM", "minimal");  // clazy:exclude=non-pod-global-static
}
#endif


QTEST_MAIN(CourseRelayTest)
#include "course_relay_t.moc"  // IWYU pragma: keep
