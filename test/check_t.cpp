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


#include <cmath>
#include <cstddef>
#include <vector>

#include <QtGlobal>
#include <QtTest>
#include <QBuffer>
#include <QByteArray>
#include <QFileInfo>
#include <QIODevice>
#include <QLatin1String>
#include <QList>
#include <QObject>
#include <QSignalSpy>
#include <QRectF>
#include <QString>
#include <QUuid>
#include <QVariant>

#include "global.h"
#include "check/issue.h"
#include "check/legibility.h"
#include "check/map_check.h"
#include "core/color_group.h"
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
#include "fileformats/file_import_export.h"
#include "fileformats/xml_file_format.h"
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
	void legibilityTest();
	void colourGroupTest();
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


QTEST_MAIN(CheckTest)
#include "check_t.moc"  // IWYU pragma: keep
