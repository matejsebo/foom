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
#include <cmath>
#include <cstddef>
#include <memory>
#include <vector>

#include <QtGlobal>
#include <QtTest>
#include <QBuffer>
#include <QByteArray>
#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QIODevice>
#include <QLatin1String>
#include <QList>
#include <QObject>
#include <QSignalSpy>
#include <QRectF>
#include <QString>
#include <QTemporaryDir>
#include <QUuid>
#include <QVariant>

#include "global.h"
#include "check/issue.h"
#include "check/legibility.h"
#include "check/legibility_results.h"
#include "check/map_check.h"
#include "core/color_group.h"
#include "core/map.h"
#include "core/map_color.h"
#include "core/map_coord.h"
#include "core/map_part.h"
#include "core/saved_selections.h"
#include "core/objects/object.h"
#include "core/objects/text_object.h"
#include "core/symbols/area_symbol.h"
#include "core/symbols/line_symbol.h"
#include "core/symbols/point_symbol.h"
#include "core/symbols/text_symbol.h"
#include "fileformats/file_import_export.h"
#include "fileformats/xml_file_format.h"
#include "templates/template.h"
#include "templates/template_image.h"
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
	void mapCheckAssignTest();
	void legibilityTest();
	void legibilitySymbolGapTest();
	void colourGroupTest();
	void legibilityResultsTest();  // foom
	void mapCheckRelinkTest();  // foom
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

namespace {

PathObject* addLine(Map& map, const Symbol* symbol, const MapCoord& from, const MapCoord& to)
{
	auto* line = new PathObject(symbol, { from, to });
	map.addObject(line);
	return line;
}

PathObject* addSquare(Map& map, const Symbol* symbol, double x, double y, double size, double hole = 0.0)
{
	auto* square = new PathObject(symbol);
	for (auto const& coord : { MapCoord(x, y), MapCoord(x + size, y), MapCoord(x + size, y + size), MapCoord(x, y + size) })
		square->addCoordinate(coord);
	square->parts().front().setClosed(true);
	if (hole > 0.0)
	{
		auto const offset = (size - hole) / 2;
		auto first = true;
		for (auto const& coord : { MapCoord(x + offset, y + offset), MapCoord(x + offset + hole, y + offset), MapCoord(x + offset + hole, y + offset + hole), MapCoord(x + offset, y + offset + hole) })
		{
			square->addCoordinate(coord, first);
			first = false;
		}
		square->parts().back().setClosed(true);
	}
	map.addObject(square);
	return square;
}

int countRule(const std::vector<Issue>& issues, const char* rule)
{
	int n = 0;
	for (auto const& issue : issues)
		n += issue.rule == rule;
	return n;
}

const Issue* findObjectIssue(const std::vector<Issue>& issues, const char* rule, const Object* object)
{
	for (auto const& issue : issues)
	{
		if (issue.rule == rule && issue.objects.contains(object->uuid()))
			return &issue;
	}
	return nullptr;
}

}  // namespace

void CheckTest::mapCheckAssignTest()
{
	// the design notes, "Map check", Accept (assign symbol).
	Map map;
	auto* black = new MapColor(QStringLiteral("black"), 0);
	map.addColor(black, 0);
	auto* line_symbol = new LineSymbol();
	line_symbol->setColor(black);
	line_symbol->setLineWidth(1);
	map.addSymbol(line_symbol, 0);
	auto* area_symbol = new AreaSymbol();
	area_symbol->setColor(black);
	map.addSymbol(area_symbol, 1);
	auto* point_symbol = new PointSymbol();
	map.addSymbol(point_symbol, 2);
	auto* text_symbol = new TextSymbol();
	map.addSymbol(text_symbol, 3);

	auto* wrong = new PathObject(point_symbol, { MapCoord(0.0, 0.0), MapCoord(10.0, 0.0) });
	map.addObject(wrong);
	auto* lost = new PathObject(Map::getUndefinedLine(), { MapCoord(0.0, 10.0), MapCoord(10.0, 10.0) });
	map.addObject(lost);
	auto* blank = new TextObject(text_symbol);
	blank->setText(QString{});
	blank->setAnchorPosition(MapCoord(50.0, 0.0));
	map.addObject(blank);
	auto* part = map.getCurrentPart();
	auto const num_objects = part->getNumObjects();

	auto const issues = checkMap(map);
	auto const* wrong_issue = findObjectIssue(issues, rule::symbol_wrong_type, wrong);
	auto const* lost_issue = findObjectIssue(issues, rule::symbol_undefined, lost);
	auto const* blank_issue = findObjectIssue(issues, rule::text_empty, blank);
	QVERIFY(wrong_issue && lost_issue && blank_issue);

	// The symbols offered fit the object's type: lines and areas for a path.
	auto const offered = assignableSymbols(map, *wrong_issue);
	QCOMPARE(offered.size(), std::size_t(2));
	QVERIFY(std::find(offered.begin(), offered.end(), line_symbol) != offered.end());
	QVERIFY(std::find(offered.begin(), offered.end(), area_symbol) != offered.end());
	QCOMPARE(assignableSymbols(map, *lost_issue).size(), std::size_t(2));
	QVERIFY(assignableSymbols(map, *blank_issue).empty());  // not a symbol rule

	// A symbol that does not fit, or another rule, changes nothing.
	auto const undo_steps = map.undoManager().undoStepCount();
	QVERIFY(!assignSymbol(map, *wrong_issue, point_symbol));
	QVERIFY(!assignSymbol(map, *wrong_issue, text_symbol));
	QVERIFY(!assignSymbol(map, *wrong_issue, nullptr));
	QVERIFY(!assignSymbol(map, *blank_issue, text_symbol));
	QCOMPARE(map.undoManager().undoStepCount(), undo_steps);

	// Assigning keeps the object, its identity and its coordinates; one undo step each.
	auto const wrong_uuid = wrong->uuid();
	auto const lost_uuid = lost->uuid();
	QVERIFY(assignSymbol(map, *wrong_issue, line_symbol));
	QCOMPARE(map.undoManager().undoStepCount(), undo_steps + 1);
	auto const* assigned = map.findObject(wrong_uuid);
	QVERIFY(assigned);
	QCOMPARE(assigned->getSymbol(), static_cast<const Symbol*>(line_symbol));
	QCOMPARE(assigned->getRawCoordinateVector().size(), std::size_t(2));
	QCOMPARE(part->getNumObjects(), num_objects);
	QVERIFY(assignSymbol(map, *lost_issue, area_symbol));
	QCOMPARE(map.undoManager().undoStepCount(), undo_steps + 2);
	QCOMPARE(map.findObject(lost_uuid)->getSymbol(), static_cast<const Symbol*>(area_symbol));

	auto const again = checkMap(map);
	QCOMPARE(countRule(again, rule::symbol_wrong_type), 0);
	QCOMPARE(countRule(again, rule::symbol_undefined), 0);

	// Undo brings the wrong symbol back.
	map.undoManager().undo(nullptr);
	map.undoManager().undo(nullptr);
	QCOMPARE(map.findObject(wrong_uuid)->getSymbol(), static_cast<const Symbol*>(point_symbol));
	QCOMPARE(countRule(checkMap(map), rule::symbol_wrong_type), 1);

	// Objects that are gone: nothing to do.
	map.deleteObject(const_cast<Object*>(map.findObject(wrong_uuid)));
	QVERIFY(!assignSymbol(map, *wrong_issue, line_symbol));
}

void CheckTest::legibilityTest()
{
	// the design notes, "Legibility", Accept.
	Map map;
	map.setScaleDenominator(15000);
	auto* black = new MapColor(QStringLiteral("Black"), 0);
	black->setCmyk(MapColorCmyk(0.0f, 0.0f, 0.0f, 1.0f));
	map.addColor(black, 0);
	auto* brown = new MapColor(QStringLiteral("Brown"), 1);
	brown->setCmyk(MapColorCmyk(0.0f, 0.56f, 1.0f, 0.18f));
	map.addColor(brown, 1);

	auto const line_symbol = [&map](const MapColor* color, double width_mm, int minimum_length, int number) {
		auto* symbol = new LineSymbol();
		symbol->setColor(color);
		symbol->setLineWidth(width_mm);
		symbol->setMinimumLength(minimum_length);
		symbol->setNumberComponent(0, number);
		map.addSymbol(symbol, map.getNumSymbols());
		return symbol;
	};
	auto* line = line_symbol(black, 0.5, 2000, 501);
	auto* wide = line_symbol(black, 0.5, 0, 502);
	auto* dashed = line_symbol(black, 0.25, 0, 503);
	dashed->setDashed(true);
	dashed->setDashLength(2000);
	dashed->setBreakLength(500);
	auto* brown_line = line_symbol(brown, 0.5, 0, 504);
	auto* contour = line_symbol(brown, 0.14, 0, 101);
	auto* form_line = line_symbol(brown, 0.14, 0, 103);
	auto* cliff = line_symbol(black, 0.35, 0, 202);
	auto* area = new AreaSymbol();
	area->setColor(black);
	area->setMinimumArea(1000);
	area->setNumberComponent(0, 401);
	map.addSymbol(area, map.getNumSymbols());

	auto* short_line = addLine(map, line, MapCoord(0.0, 0.0), MapCoord(1.0, 0.0));
	addLine(map, line, MapCoord(0.0, 100.0), MapCoord(10.0, 100.0));
	auto* three_dashes = addLine(map, dashed, MapCoord(0.0, 110.0), MapCoord(3.0, 110.0));
	addLine(map, dashed, MapCoord(0.0, 120.0), MapCoord(3.5, 120.0));
	auto* small_square = addSquare(map, area, 0.0, 130.0, 0.5);
	auto* holed_square = addSquare(map, area, 0.0, 140.0, 1.2, 0.8);
	auto* gap_a = addLine(map, wide, MapCoord(0.0, 200.0), MapCoord(10.0, 200.0));
	auto* gap_b = addLine(map, wide, MapCoord(0.0, 200.6), MapCoord(10.0, 200.6));
	addLine(map, wide, MapCoord(0.0, 210.0), MapCoord(10.0, 210.0));
	addLine(map, wide, MapCoord(0.0, 211.0), MapCoord(10.0, 211.0));
	addLine(map, wide, MapCoord(0.0, 220.0), MapCoord(10.0, 220.0));
	addLine(map, wide, MapCoord(0.0, 220.5), MapCoord(10.0, 220.5));
	addLine(map, wide, MapCoord(0.0, 230.0), MapCoord(10.0, 230.0));
	addLine(map, wide, MapCoord(5.0, 229.0), MapCoord(5.0, 231.0));
	addLine(map, wide, MapCoord(0.0, 240.0), MapCoord(10.0, 240.0));
	addLine(map, wide, MapCoord(10.0, 240.0), MapCoord(10.0, 250.0));
	addLine(map, wide, MapCoord(0.0, 260.0), MapCoord(10.0, 260.0));
	addLine(map, brown_line, MapCoord(0.0, 260.6), MapCoord(10.0, 260.6));
	addLine(map, contour, MapCoord(0.0, 270.0), MapCoord(10.0, 270.0));
	addLine(map, form_line, MapCoord(0.0, 270.25), MapCoord(10.0, 270.25));
	addSquare(map, area, 0.0, 300.0, 5.0);
	addSquare(map, area, 0.05, 300.05, 1.0);

	LegibilityParams params;
	QCOMPARE(defaultGap(map), 0.15);
	params.gap_mm = defaultGap(map);
	auto issues = checkLegibility(map, params);
	QCOMPARE(countRule(issues, rule::line_short), 2);
	QCOMPARE(countRule(issues, rule::area_small), 2);
	QCOMPARE(countRule(issues, rule::gap), 1);
	QCOMPARE(issues.size(), std::size_t(5));
	auto const* issue = findObjectIssue(issues, rule::line_short, short_line);
	QVERIFY(issue);
	QCOMPARE(issue->measured, 1.0);
	QCOMPARE(issue->limit, 2.0);
	issue = findObjectIssue(issues, rule::line_short, three_dashes);
	QVERIFY(issue);
	QCOMPARE(issue->measured, 3.0);
	QCOMPARE(issue->limit, 3.25);
	issue = findObjectIssue(issues, rule::area_small, small_square);
	QVERIFY(issue);
	QCOMPARE(issue->measured, 0.25);
	QCOMPARE(issue->limit, 1.0);
	issue = findObjectIssue(issues, rule::area_small, holed_square);
	QVERIFY(issue);
	QVERIFY(std::abs(issue->measured - 0.8) < 1e-6);
	issue = findObjectIssue(issues, rule::gap, gap_a);
	QVERIFY(issue);
	QVERIFY(issue->objects.contains(gap_b->uuid()));
	QVERIFY(std::abs(issue->measured - 0.1) < 1e-6);
	QCOMPARE(issue->limit, 0.15);
	QVERIFY(std::abs(issue->where.y() - 200.3) < 1e-6);
	QVERIFY(issue->where.x() >= 0.0 && issue->where.x() <= 10.0);

	// The colour filter off: the black and brown pair is a conflict too.
	params.same_colour_group_only = false;
	issues = checkLegibility(map, params);
	QCOMPARE(countRule(issues, rule::gap), 2);
	params.same_colour_group_only = true;

	// Between contours on: the contour pair is a conflict, until a cliff covers it.
	params.between_contours = true;
	issues = checkLegibility(map, params);
	QCOMPARE(countRule(issues, rule::gap), 2);
	addLine(map, cliff, MapCoord(0.0, 270.125), MapCoord(10.0, 270.125));
	issues = checkLegibility(map, params);
	QCOMPARE(countRule(issues, rule::gap), 1);
	params.between_contours = false;

	// Scope and extent.
	params.symbols = { line };
	issues = checkLegibility(map, params);
	QCOMPARE(issues.size(), std::size_t(1));
	QCOMPARE(issues.front().rule, QByteArray(rule::line_short));
	params.symbols.clear();
	params.extent = QRectF(-1.0, -1.0, 3.0, 3.0);
	issues = checkLegibility(map, params);
	QCOMPARE(issues.size(), std::size_t(1));
	QVERIFY(issues.front().objects.contains(short_line->uuid()));
	params.extent = QRectF();

	// Checks can be switched off one by one.
	params.gap = false;
	params.minimum_area = false;
	issues = checkLegibility(map, params);
	QCOMPARE(issues.size(), std::size_t(2));

	map.setScaleDenominator(10000);
	QCOMPARE(defaultGap(map), 0.23);
}

void CheckTest::legibilitySymbolGapTest()
{
	// the design notes, "Legibility", Accept (the minimum gap per symbol).
	Map map;
	map.setScaleDenominator(15000);
	auto* black = new MapColor(QStringLiteral("Black"), 0);
	black->setCmyk(MapColorCmyk(0.0f, 0.0f, 0.0f, 1.0f));
	map.addColor(black, 0);
	auto const line_symbol = [&map, black](int number, int minimum_gap) {
		auto* symbol = new LineSymbol();
		symbol->setColor(black);
		symbol->setLineWidth(0.5);
		symbol->setNumberComponent(0, number);
		symbol->setMinimumGap(minimum_gap);
		map.addSymbol(symbol, map.getNumSymbols());
		return symbol;
	};
	auto* plain = line_symbol(501, 0);
	auto* roomy = line_symbol(502, 2000);
	auto* tight = line_symbol(503, 50);
	QCOMPARE(plain->minimumGap(), 0);
	QCOMPARE(roomy->minimumGap(), 2000);
	plain->setMinimumGap(-5);
	QCOMPARE(plain->minimumGap(), 0);

	// Two plain lines 0.8 mm apart: gap 0.3, above the scan's 0.15.
	addLine(map, plain, MapCoord(0.0, 0.0), MapCoord(10.0, 0.0));
	addLine(map, plain, MapCoord(0.0, 0.8), MapCoord(10.0, 0.8));
	// A plain and a roomy line 2 mm apart: gap 1.5, below the roomy symbol's 2.
	auto* near_plain = addLine(map, plain, MapCoord(0.0, 100.0), MapCoord(10.0, 100.0));
	auto* near_roomy = addLine(map, roomy, MapCoord(0.0, 102.0), MapCoord(10.0, 102.0));
	// Two roomy lines 3 mm apart: gap 2.5, enough.
	addLine(map, roomy, MapCoord(0.0, 200.0), MapCoord(10.0, 200.0));
	addLine(map, roomy, MapCoord(0.0, 203.0), MapCoord(10.0, 203.0));
	// Two tight lines 0.6 mm apart: gap 0.1, enough for their 0.05.
	addLine(map, tight, MapCoord(0.0, 300.0), MapCoord(10.0, 300.0));
	addLine(map, tight, MapCoord(0.0, 300.6), MapCoord(10.0, 300.6));
	// A tight and a plain line 0.6 mm apart: the plain line's 0.15 counts, the larger.
	auto* mixed_tight = addLine(map, tight, MapCoord(0.0, 400.0), MapCoord(10.0, 400.0));
	addLine(map, plain, MapCoord(0.0, 400.6), MapCoord(10.0, 400.6));

	LegibilityParams params;
	params.gap_mm = 0.15;
	auto const issues = checkLegibility(map, params);
	QCOMPARE(countRule(issues, rule::gap), 2);
	auto const* roomy_issue = findObjectIssue(issues, rule::gap, near_roomy);
	QVERIFY(roomy_issue);
	QVERIFY(roomy_issue->objects.contains(near_plain->uuid()));
	QVERIFY(std::abs(roomy_issue->measured - 1.5) < 1e-6);
	QCOMPARE(roomy_issue->limit, 2.0);
	auto const* mixed_issue = findObjectIssue(issues, rule::gap, mixed_tight);
	QVERIFY(mixed_issue);
	QCOMPARE(mixed_issue->limit, 0.15);

	// Without the symbols' gaps the scan's own gap applies to every pair.
	roomy->setMinimumGap(0);
	tight->setMinimumGap(0);
	auto const plain_issues = checkLegibility(map, params);
	QCOMPARE(countRule(plain_issues, rule::gap), 2);  // the tight pair and the mixed pair
	QVERIFY(!findObjectIssue(plain_issues, rule::gap, near_roomy));
	roomy->setMinimumGap(2000);

	// Copies and equality.
	auto copy = Symbol::duplicate(*roomy);
	QCOMPARE(copy->minimumGap(), 2000);
	QVERIFY(copy->equals(roomy));
	copy->setMinimumGap(1000);
	QVERIFY(!copy->equals(roomy));

	// Saving and loading keeps the gap, and writes none for a symbol without one.
	XMLFileFormat format;
	QBuffer buffer;
	QVERIFY(buffer.open(QIODevice::ReadWrite));
	auto exporter = format.makeExporter({}, &map, nullptr);
	exporter->setDevice(&buffer);
	QVERIFY(exporter->doExport());
	auto const data = QString::fromUtf8(buffer.data());
	QCOMPARE(data.count(QLatin1String(" minimum_gap=")), 1);
	QVERIFY(data.contains(QLatin1String(" minimum_gap=\"2000\"")));
	QVERIFY(buffer.seek(0));
	Map reloaded;
	auto importer = format.makeImporter({}, &reloaded, nullptr);
	importer->setDevice(&buffer);
	QVERIFY(importer->doImport());
	QCOMPARE(reloaded.getNumSymbols(), 3);
	QCOMPARE(reloaded.getSymbol(0)->minimumGap(), 0);
	QCOMPARE(reloaded.getSymbol(1)->minimumGap(), 2000);
	QCOMPARE(reloaded.getSymbol(2)->minimumGap(), 0);
}

void CheckTest::colourGroupTest()
{
	// the design notes, "Legibility", Accept: the colour group guess.
	auto const guess = [](float c, float m, float y, float k) {
		MapColor color(QStringLiteral("c"), 0);
		color.setCmyk(MapColorCmyk(c, m, y, k));
		return guessColourGroup(color);
	};
	QVERIFY(guess(0.0f, 0.0f, 0.0f, 1.0f) == ColourGroup::Black);
	QVERIFY(guess(0.0f, 0.56f, 1.0f, 0.18f) == ColourGroup::Brown);
	QVERIFY(guess(0.0f, 0.28f, 0.5f, 0.09f) == ColourGroup::Brown);
	QVERIFY(guess(0.0f, 0.27f, 0.79f, 0.0f) == ColourGroup::Yellow);
	QVERIFY(guess(0.0f, 0.14f, 0.4f, 0.0f) == ColourGroup::Yellow);
	QVERIFY(guess(1.0f, 0.0f, 0.0f, 0.0f) == ColourGroup::Blue);
	QVERIFY(guess(0.5f, 0.0f, 0.0f, 0.0f) == ColourGroup::Blue);
	QVERIFY(guess(0.76f, 0.0f, 0.91f, 0.0f) == ColourGroup::Green);
	QVERIFY(guess(0.38f, 0.0f, 0.46f, 0.0f) == ColourGroup::Green);
	QVERIFY(guess(0.35f, 0.85f, 0.0f, 0.0f) == ColourGroup::Purple);
	QVERIFY(guess(0.0f, 0.0f, 0.0f, 0.0f) == ColourGroup::Other);
	QVERIFY(guess(0.0f, 0.0f, 0.0f, 0.3f) == ColourGroup::Black);

	MapColor named(QStringLiteral("Upper purple"), 0);
	QVERIFY(guessColourGroup(named) == ColourGroup::Purple);
	QVERIFY(colourGroupOf(&named) == ColourGroup::Purple);
	QVERIFY(named.getColourGroup() == ColourGroup::Undefined);
	named.setColourGroup(ColourGroup::Brown);
	QVERIFY(colourGroupOf(&named) == ColourGroup::Brown);
	QCOMPARE(colourGroupKey(ColourGroup::Brown), QStringLiteral("brown"));
	QVERIFY(colourGroupFromKey(QStringLiteral("yellow")) == ColourGroup::Yellow);
	QVERIFY(colourGroupFromKey(QStringLiteral("?")) == ColourGroup::Undefined);

	// A stored group survives saving and loading, and is not written without the foom additions.
	Map map;
	auto* stored = new MapColor(QStringLiteral("Stored"), 0);
	stored->setColourGroup(ColourGroup::Brown);
	map.addColor(stored, 0);
	map.addColor(new MapColor(QStringLiteral("Guessed"), 1), 1);
	XMLFileFormat format;
	QBuffer buffer;
	QVERIFY(buffer.open(QIODevice::ReadWrite));
	auto exporter = format.makeExporter({}, &map, nullptr);
	exporter->setDevice(&buffer);
	QVERIFY(exporter->doExport());
	auto const data = QString::fromUtf8(buffer.data());
	QVERIFY(data.contains(QLatin1String(" group=\"brown\"")));
	QVERIFY(buffer.seek(0));
	Map reloaded;
	auto importer = format.makeImporter({}, &reloaded, nullptr);
	importer->setDevice(&buffer);
	QVERIFY(importer->doImport());
	QCOMPARE(reloaded.getNumColors(), 2);
	QVERIFY(reloaded.getColor(0)->getColourGroup() == ColourGroup::Brown);
	QVERIFY(reloaded.getColor(1)->getColourGroup() == ColourGroup::Undefined);

	QBuffer plain_buffer;
	QVERIFY(plain_buffer.open(QIODevice::ReadWrite));
	auto plain_exporter = format.makeExporter({}, &map, nullptr);
	plain_exporter->setOption(QString::fromLatin1("foom"), false);
	plain_exporter->setDevice(&plain_buffer);
	QVERIFY(plain_exporter->doExport());
	QVERIFY(!QString::fromUtf8(plain_buffer.data()).contains(QLatin1String(" group=")));
}


namespace {

const SavedSelection* savedSelectionNamed(const Map& map, const char* name)
{
	for (auto const& selection : map.savedSelections())
	{
		if (selection.name == QLatin1String(name))
			return &selection;
	}
	return nullptr;
}

int partWithRole(const Map& map, MapPart::Role role)
{
	for (int p = 0; p < map.getNumParts(); ++p)
	{
		if (map.getPart(p)->role() == role)
			return p;
	}
	return -1;
}

void moveFirstVertex(PathObject* path, const MapCoord& coord)
{
	path->setCoordinate(0, coord);
	path->setOutputDirty();
	path->update();
}

}  // namespace


void CheckTest::legibilityResultsTest()
{
	// the design notes, "Legibility", Accept (the results as selections and markers).
	Map map;
	map.setScaleDenominator(15000);
	auto* black = new MapColor(QStringLiteral("Black"), 0);
	black->setCmyk(MapColorCmyk(0.0f, 0.0f, 0.0f, 1.0f));
	map.addColor(black, 0);
	auto* symbol = new LineSymbol();
	symbol->setColor(black);
	symbol->setLineWidth(0.5);
	symbol->setMinimumLength(2000);
	symbol->setNumberComponent(0, 501);
	map.addSymbol(symbol, 0);
	auto* a = addLine(map, symbol, MapCoord(0.0, 0.0), MapCoord(10.0, 0.0));
	auto* b = addLine(map, symbol, MapCoord(5.0, 0.6), MapCoord(5.0, 10.0));
	auto* c = addLine(map, symbol, MapCoord(20.0, 0.0), MapCoord(21.0, 0.0));
	map.setSavedSelections({ SavedSelection{ QStringLiteral("north"), { a->uuid() } } });

	LegibilityParams params;
	params.gap_mm = 0.15;

	// (1)
	auto issues = checkLegibility(map, params);
	QCOMPARE(int(issues.size()), 2);
	auto const gap = std::find_if(begin(issues), end(issues), [](auto const& issue) { return issue.rule == rule::gap; });
	QVERIFY(gap != end(issues));
	QCOMPARE(gap->points.size(), 2);
	// The points follow the order of the objects.
	auto const a_first = gap->objects[0] == a->uuid();
	auto const& on_a = gap->points[a_first ? 0 : 1];
	auto const& on_b = gap->points[a_first ? 1 : 0];
	QVERIFY(std::abs(on_a.x() - 5.0) < 1e-6 && std::abs(on_a.y() - 0.0) < 1e-6);
	QVERIFY(std::abs(on_b.x() - 5.0) < 1e-6 && std::abs(on_b.y() - 0.6) < 1e-6);

	// (2)
	auto const num_colors = map.getNumColors();
	auto const num_symbols = map.getNumSymbols();
	auto const undo_steps = map.undoManager().undoStepCount();
	recordLegibilityResults(map, params, issues);
	auto const* gaps = savedSelectionNamed(map, "Legibility: gaps");
	QVERIFY(gaps);
	QCOMPARE(gaps->uuids.size(), std::size_t(2));
	QVERIFY(std::count(begin(gaps->uuids), end(gaps->uuids), a->uuid()) == 1);
	QVERIFY(std::count(begin(gaps->uuids), end(gaps->uuids), b->uuid()) == 1);
	auto const* short_lines = savedSelectionNamed(map, "Legibility: short lines");
	QVERIFY(short_lines);
	QCOMPARE(short_lines->uuids, (std::vector<QUuid>{ c->uuid() }));
	QVERIFY(!savedSelectionNamed(map, "Legibility: small areas"));
	QVERIFY(savedSelectionNamed(map, "north"));
	QCOMPARE(map.getNumColors(), num_colors + 1);
	QCOMPARE(map.getColor(0)->getName(), QStringLiteral("Legibility conflict"));
	QCOMPARE(QColor(map.getColor(0)->getRgb()), QColor(255, 0, 0));
	QCOMPARE(map.getNumSymbols(), num_symbols + 2);
	auto const* dot = map.getSymbol(num_symbols);
	auto const* marker_line = map.getSymbol(num_symbols + 1);
	QCOMPARE(dot->getType(), Symbol::Point);
	QCOMPARE(dot->getName(), QStringLiteral("Legibility conflict (dot)"));
	QCOMPARE(marker_line->getType(), Symbol::Line);
	QCOMPARE(marker_line->getName(), QStringLiteral("Legibility conflict (line)"));
	QCOMPARE(dot->getNumberComponent(0), 502);
	QCOMPARE(dot->getNumberComponent(1), 1);
	QCOMPARE(marker_line->getNumberComponent(0), 502);
	QCOMPARE(marker_line->getNumberComponent(1), 2);
	QCOMPARE(map.getNumParts(), 2);
	auto const markers_index = partWithRole(map, MapPart::Role::Issues);
	QCOMPARE(markers_index, 1);
	auto const* markers = map.getPart(std::size_t(markers_index));
	QCOMPARE(markers->getName(), QStringLiteral("Legibility markers"));
	QCOMPARE(map.getCurrentPartIndex(), 0);
	auto const markers_ok = [&]() {
		if (markers->getNumObjects() != 2)
			return false;
		auto const first_is_dot = markers->getObject(0)->getSymbol() == dot;
		auto const* point = markers->getObject(first_is_dot ? 0 : 1);
		auto const* line = markers->getObject(first_is_dot ? 1 : 0);
		if (point->getSymbol() != dot || line->getSymbol() != marker_line)
			return false;
		auto const& p = point->getRawCoordinateVector();
		auto const& l = line->getRawCoordinateVector();
		return p.size() == 1 && std::abs(p[0].x() - 5.0) < 1e-3 && std::abs(p[0].y() - 0.3) < 1e-3
		       && l.size() == 2 && std::abs(l[0].x() - 5.0) < 1e-3 && std::abs(l[1].x() - 5.0) < 1e-3
		       && std::abs(std::min(l[0].y(), l[1].y())) < 1e-3 && std::abs(std::max(l[0].y(), l[1].y()) - 0.6) < 1e-3;
	};
	QVERIFY(markers_ok());
	QCOMPARE(map.undoManager().undoStepCount(), undo_steps + 1);

	// (3)
	issues = checkLegibility(map, params);
	QCOMPARE(int(issues.size()), 2);
	recordLegibilityResults(map, params, issues);
	QVERIFY(markers_ok());
	QCOMPARE(map.getNumParts(), 2);
	QCOMPARE(map.getNumSymbols(), num_symbols + 2);
	QCOMPARE(map.getNumColors(), num_colors + 1);

	// (4)
	moveFirstVertex(b, MapCoord(5.0, 2.0));
	issues = checkLegibility(map, params);
	QCOMPARE(int(issues.size()), 1);
	recordLegibilityResults(map, params, issues);
	QVERIFY(!savedSelectionNamed(map, "Legibility: gaps"));
	QVERIFY(savedSelectionNamed(map, "Legibility: short lines"));
	QCOMPARE(markers->getNumObjects(), 0);
	QCOMPARE(map.getNumSymbols(), num_symbols + 2);

	// (6)
	QVERIFY(map.undoManager().undo());
	QCOMPARE(markers->getNumObjects(), 2);

	// (5)
	moveFirstVertex(b, MapCoord(5.0, 0.6));
	issues = checkLegibility(map, params);
	recordLegibilityResults(map, params, issues);
	QVERIFY(markers_ok());
	auto only_length = params;
	only_length.gap = false;
	only_length.minimum_area = false;
	moveFirstVertex(b, MapCoord(5.0, 2.0));
	issues = checkLegibility(map, only_length);
	QCOMPARE(int(issues.size()), 1);
	recordLegibilityResults(map, only_length, issues);
	QVERIFY(markers_ok());
	QVERIFY(savedSelectionNamed(map, "Legibility: gaps"));

	// (7)
	QVERIFY(savedSelectionNamed(map, "north"));
}

void CheckTest::mapCheckRelinkTest()
{
	// the design notes, "Map check", Accept (relink).
	QTemporaryDir dir;
	QVERIFY(dir.isValid());
	QDir root(dir.path());
	QVERIFY(root.mkpath(QStringLiteral("work")));
	QVERIFY(root.mkpath(QStringLiteral("new")));
	QImage image(4, 4, QImage::Format_RGB32);
	image.fill(Qt::green);
	QVERIFY(image.save(root.absoluteFilePath(QStringLiteral("new/a.png")), "PNG"));
	{
		QFile text(root.absoluteFilePath(QStringLiteral("new/b.png")));
		QVERIFY(text.open(QIODevice::WriteOnly));
		text.write("not an image");
	}
	auto const old_path = root.absoluteFilePath(QStringLiteral("old/a.png"));
	auto const map_path = root.absoluteFilePath(QStringLiteral("work/map.xmap"));
	{
		Map map;
		map.addTemplate(-1, std::make_unique<TemplateImage>(old_path, &map));
		XMLFileFormat format;
		auto exporter = format.makeExporter(map_path, &map, nullptr);
		QVERIFY(exporter && exporter->doExport());
	}
	Map map;
	{
		XMLFileFormat format;
		auto importer = format.makeImporter(map_path, &map, nullptr);
		QVERIFY(importer && importer->doImport());
	}
	QCOMPARE(map.getNumTemplates(), 1);
	auto const missing = [&map]() {
		auto const issues = checkMap(map);
		auto const found = std::find_if(begin(issues), end(issues), [](auto const& issue) { return issue.rule == rule::template_missing; });
		return found == end(issues) ? Issue{} : *found;
	};
	auto const issue = missing();
	QCOMPARE(issue.rule, QByteArray(rule::template_missing));
	QCOMPARE(QFileInfo(issue.subject).fileName(), QStringLiteral("a.png"));

	map.setHasUnsavedChanges(false);
	QVERIFY(!relinkIssueTemplate(map, issue, root.absoluteFilePath(QStringLiteral("new/b.png"))));
	QCOMPARE(map.getTemplate(0)->getTemplateState(), Template::Invalid);
	QCOMPARE(map.getTemplate(0)->getTemplatePath(), issue.subject);
	QVERIFY(!map.hasUnsavedChanges());

	Issue other;
	other.rule = rule::text_empty;
	QVERIFY(!relinkIssueTemplate(map, other, root.absoluteFilePath(QStringLiteral("new/a.png"))));

	QVERIFY(relinkIssueTemplate(map, issue, root.absoluteFilePath(QStringLiteral("new/a.png"))));
	QCOMPARE(map.getTemplate(0)->getTemplateState(), Template::Loaded);
	QCOMPARE(QFileInfo(map.getTemplate(0)->getTemplatePath()).canonicalFilePath(), QFileInfo(root.absoluteFilePath(QStringLiteral("new/a.png"))).canonicalFilePath());
	QVERIFY(map.hasUnsavedChanges());
	QCOMPARE(missing().rule, QByteArray());

	// The template is no longer missing, so the issue has no template.
	QVERIFY(!relinkIssueTemplate(map, issue, root.absoluteFilePath(QStringLiteral("new/a.png"))));
}


QTEST_MAIN(CheckTest)
#include "check_t.moc"  // IWYU pragma: keep
