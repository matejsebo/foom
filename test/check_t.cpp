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


#include <cstddef>
#include <vector>

#include <QtGlobal>
#include <QtTest>
#include <QByteArray>
#include <QList>
#include <QObject>
#include <QSignalSpy>
#include <QString>
#include <QUuid>
#include <QVariant>

#include "global.h"
#include "check/issue.h"
#include "check/map_check.h"
#include "core/map.h"
#include "core/map_color.h"
#include "core/map_coord.h"
#include "core/map_part.h"
#include "core/objects/object.h"
#include "core/objects/text_object.h"
#include "core/symbols/area_symbol.h"
#include "core/symbols/line_symbol.h"
#include "core/symbols/point_symbol.h"
#include "core/symbols/text_symbol.h"
#include "undo/undo_manager.h"

using namespace OpenOrienteering;

Q_DECLARE_METATYPE(OpenOrienteering::MapCoordF)


/**
 * Tests the issue model and the map check.
 *
 * See the design notes, "Issue list", and
 * the design notes, "Map check".
 */
class CheckTest : public QObject
{
Q_OBJECT
private slots:
	void initTestCase();
	void issueModelTest();
	void mapCheckTest();
};


void CheckTest::initTestCase()
{
	doStaticInitializations();
	qRegisterMetaType<QVector<QUuid>>("QVector<QUuid>");
	qRegisterMetaType<MapCoordF>("OpenOrienteering::MapCoordF");
}

void CheckTest::issueModelTest()
{
	Issue a;
	a.rule = "rule-a";
	a.objects.append(QUuid::createUuid());
	a.objects.append(QUuid::createUuid());
	a.where = MapCoordF(1.5, -2.0);
	a.message = QStringLiteral("first, with \"quotes\"");
	Issue b = a;
	b.objects = { QUuid::createUuid() };
	b.message = QStringLiteral("second");
	Issue c;
	c.rule = "rule-c";
	c.severity = Issue::Error;
	c.measured = 0.25;
	c.limit = 0.5;
	c.message = QStringLiteral("third");
	
	IssueModel model;
	model.setIssues({ a, b, c });
	QCOMPARE(model.rowCount(), 3);
	QCOMPARE(model.rules(), (QList<QByteArray>{ "rule-a", "rule-c" }));
	model.setRuleFilter("rule-c");
	QCOMPARE(model.rowCount(), 1);
	QCOMPARE(model.data(model.index(0, IssueModel::MessageColumn)).toString(), QStringLiteral("third"));
	QCOMPARE(model.data(model.index(0, IssueModel::SeverityColumn)).toString(), QStringLiteral("error"));
	model.setRuleFilter(QByteArray());
	QCOMPARE(model.rowCount(), 3);
	
	auto const csv = QString::fromUtf8(model.toCsv());
	auto const lines = csv.split(QLatin1Char('\n'), QString::SkipEmptyParts);
	QCOMPARE(lines.size(), 4);
	QCOMPARE(lines[0], QStringLiteral("rule,severity,objects,x,y,measured,limit,message"));
	QString const expected_objects = a.objects[0].toString(QUuid::WithoutBraces) + QLatin1Char(' ') + a.objects[1].toString(QUuid::WithoutBraces);
	QCOMPARE(lines[1], QString(QStringLiteral("rule-a,warning,") + expected_objects + QStringLiteral(",1.500,-2.000,0.000000,0.000000,\"first, with \"\"quotes\"\"\"")));
	QCOMPARE(lines[3], QStringLiteral("rule-c,error,,0.000,0.000,0.250000,0.500000,\"third\""));
	
	QSignalSpy spy(&model, &IssueModel::issueActivated);
	model.activate(1);
	QCOMPARE(spy.count(), 1);
	QCOMPARE(spy.first().at(0).value<QVector<QUuid>>(), b.objects);
	model.activate(7);
	QCOMPARE(spy.count(), 1);
}

void CheckTest::mapCheckTest()
{
	Map map;
	auto* black = new MapColor(QStringLiteral("black"), 0);
	map.addColor(black, 0);
	auto* unused_color = new MapColor(QStringLiteral("spare"), 1);
	map.addColor(unused_color, 1);
	auto* line_symbol = new LineSymbol();
	line_symbol->setColor(black);
	line_symbol->setLineWidth(1);
	map.addSymbol(line_symbol, 0);
	auto* area_symbol = new AreaSymbol();
	area_symbol->setColor(black);
	map.addSymbol(area_symbol, 1);
	auto* text_symbol = new TextSymbol();
	map.addSymbol(text_symbol, 2);
	auto* point_symbol = new PointSymbol();
	map.addSymbol(point_symbol, 3);
	auto* unused_symbol = new LineSymbol();
	unused_symbol->setColor(black);
	map.addSymbol(unused_symbol, 4);
	
	auto* sound = new PathObject(line_symbol, { MapCoord(0.0, 0.0), MapCoord(10.0, 0.0) });
	map.addObject(sound);
	auto* blank = new TextObject(text_symbol);
	blank->setText(QStringLiteral("  "));
	blank->setAnchorPosition(MapCoord(50.0, 0.0));
	map.addObject(blank);
	auto* stub = new PathObject(line_symbol, { MapCoord(20.0, 0.0) });
	map.addObject(stub);
	auto* sliver = new PathObject(area_symbol, { MapCoord(30.0, 0.0), MapCoord(35.0, 0.0) });
	map.addObject(sliver);
	auto* wrong = new PathObject(point_symbol, { MapCoord(40.0, 0.0), MapCoord(45.0, 0.0) });
	map.addObject(wrong);
	auto* twin_a = new PathObject(line_symbol, { MapCoord(0.0, 10.0), MapCoord(10.0, 10.0) });
	map.addObject(twin_a);
	auto* twin_b = new PathObject(line_symbol, { MapCoord(0.0, 10.0), MapCoord(10.0, 10.0) });
	map.addObject(twin_b);
	auto* stutter = new PathObject(line_symbol, { MapCoord(0.0, 20.0), MapCoord(5.0, 20.0), MapCoord(5.0, 20.0), MapCoord(10.0, 20.0) });
	map.addObject(stutter);
	auto* open = new PathObject(area_symbol, { MapCoord(0.0, 30.0), MapCoord(10.0, 30.0), MapCoord(10.0, 40.0) });
	map.addObject(open);
	auto* part = map.getCurrentPart();
	auto const num_objects = part->getNumObjects();
	
	auto const issues = checkMap(map);
	auto const find = [&issues](const char* rule) -> const Issue* {
		for (auto const& issue : issues)
		{
			if (issue.rule == rule)
				return &issue;
		}
		return nullptr;
	};
	auto const count = [&issues](const char* rule) { int n = 0; for (auto const& issue : issues) n += issue.rule == rule; return n; };
	for (auto const* rule : { rule::text_empty, rule::path_degenerate, rule::symbol_wrong_type, rule::object_duplicate, rule::vertex_duplicate, rule::area_open, rule::symbol_unused, rule::color_unused })
		QVERIFY2(find(rule), rule);
	QCOMPARE(count(rule::path_degenerate), 2);  // the stub and the sliver
	QCOMPARE(count(rule::object_duplicate), 1);
	QCOMPARE(count(rule::symbol_unused), 1);
	QCOMPARE(count(rule::color_unused), 1);
	QCOMPARE(find(rule::text_empty)->objects, (QVector<QUuid>{ blank->uuid() }));
	QCOMPARE(find(rule::object_duplicate)->objects.size(), 2);
	QCOMPARE(find(rule::symbol_unused)->subject, unused_symbol->getNumberAsString());
	QCOMPARE(find(rule::color_unused)->subject, QStringLiteral("spare"));
	for (auto const& issue : issues)
		QVERIFY(!issue.objects.contains(sound->uuid()));
	
	// Fixes, each one undo step for the object rules.
	auto const undo_steps = map.undoManager().undoStepCount();
	QVERIFY(fixIssue(map, *find(rule::text_empty)));
	QVERIFY(!map.findObject(blank->uuid()));
	QCOMPARE(map.undoManager().undoStepCount(), undo_steps + 1);
	QVERIFY(fixIssue(map, *find(rule::object_duplicate)));
	QCOMPARE(part->getNumObjects(), num_objects - 2);
	auto const twin_uuid = find(rule::vertex_duplicate)->objects.front();
	QVERIFY(fixIssue(map, *find(rule::vertex_duplicate)));
	QCOMPARE(map.findObject(twin_uuid)->getRawCoordinateVector().size(), std::size_t(3));
	QVERIFY(fixIssue(map, *find(rule::area_open)));
	QVERIFY(map.findObject(open->uuid())->asPath()->parts().front().isClosed());
	QVERIFY(fixIssue(map, *find(rule::symbol_unused)));
	QCOMPARE(map.getNumSymbols(), 4);
	QVERIFY(fixIssue(map, *find(rule::color_unused)));
	QCOMPARE(map.getNumColors(), 1);
	for (auto const& issue : issues)
	{
		if (issue.rule == rule::path_degenerate || issue.rule == rule::symbol_wrong_type)
			QVERIFY(fixIssue(map, issue));
	}
	// The symbols of the deleted text and of the wrong-typed object are unused now; nothing else remains.
	auto const remaining = checkMap(map);
	for (auto const& issue : remaining)
		QVERIFY2(issue.rule == rule::symbol_unused, issue.rule.constData());
	QCOMPARE(remaining.size(), std::size_t(2));
	QVERIFY(!fixIssue(map, *find(rule::text_empty)));  // already gone
}


QTEST_MAIN(CheckTest)
#include "check_t.moc"  // IWYU pragma: keep
