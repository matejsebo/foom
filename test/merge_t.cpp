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


#include <cstddef>
#include <memory>
#include <vector>

#include <QtGlobal>
#include <QtTest>
#include <QBuffer>
#include <QBrush>
#include <QByteArray>
#include <QColor>
#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QLabel>
#include <QPointF>
#include <QPushButton>
#include <QRectF>
#include <QSignalSpy>
#include <QTableView>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLatin1String>
#include <QObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QString>
#include <QTemporaryDir>
#include <QUuid>

#include "global.h"
#include "core/map.h"
#include "core/map_color.h"
#include "core/map_coord.h"
#include "core/map_part.h"
#include "core/objects/object.h"
#include "core/objects/text_object.h"
#include "core/symbols/line_symbol.h"
#include "core/symbols/symbol.h"
#include "core/symbols/text_symbol.h"
#include "core/sync_record.h"
#include "fileformats/file_import_export.h"
#include "fileformats/xml_file_format.h"
#include "gui/map/merge_dialog.h"
#include "sync/field_copy.h"
#include "sync/merge.h"
#include "sync/merge_decisions.h"
#include "undo/undo_manager.h"

using namespace OpenOrienteering;


namespace {

/// The uuid of the test object with the letter.
QUuid id(char letter)
{
	return QUuid(QStringLiteral("{00000000-0000-4000-8000-0000000000%1}").arg(int(letter), 2, 16, QLatin1Char('0')));
}

MapColor* addColor(Map& map, const QString& name, float magenta)
{
	auto* color = new MapColor(name, map.getNumColors());
	color->setCmyk({ 0.0f, magenta, 0.0f, 1.0f - magenta });
	color->setRgbFromCmyk();
	map.addColor(color, map.getNumColors());
	return color;
}

LineSymbol* addLine(Map& map, const QString& name, int number, const MapColor* color, int width = 200)
{
	auto* symbol = new LineSymbol();
	symbol->setName(name);
	symbol->setNumberComponent(0, number);
	symbol->setColor(color);
	symbol->setLineWidth(width / 1000.0);
	map.addSymbol(symbol, map.getNumSymbols());
	return symbol;
}

const Symbol* symbolNumbered(const Map& map, int number)
{
	for (int i = 0; i < map.getNumSymbols(); ++i)
	{
		if (map.getSymbol(i)->getNumberComponent(0) == number)
			return map.getSymbol(i);
	}
	return nullptr;
}

/// Adds a line of two points at the row of the letter, with the letter's uuid.
PathObject* addObject(Map& map, char letter, int symbol_number = 1, int part = -1, qint64 time = 1000)
{
	auto const y = 5.0 * (letter - 'a');
	auto* object = new PathObject(symbolNumbered(map, symbol_number), MapCoordVector{ MapCoord(0.0, y), MapCoord(20.0, y) });
	object->setIdentity(id(letter), time, time);
	map.addObject(object, part);
	return object;
}

Object* find(const Map& map, char letter)
{
	return map.findObject(id(letter));
}

/// Moves the object's second point to x, at the time.
void change(Map& map, char letter, double x, qint64 time)
{
	auto* object = find(map, letter)->asPath();
	auto coord = object->getCoordinate(1);
	coord.setX(x);
	object->setCoordinate(1, coord);
	object->update();
	object->touch(time);
}

void remove(Map& map, char letter)
{
	map.deleteObject(find(map, letter));
}

double endX(const Map& map, char letter)
{
	return find(map, letter)->asPath()->getCoordinate(1).x();
}

/// The base map: objects a to m, the symbols 1 and 2, one part.
void setupBase(Map& map)
{
	map.setScaleDenominator(10000);
	auto const* black = addColor(map, QStringLiteral("Black"), 0.0f);
	addLine(map, QStringLiteral("Line"), 1, black);
	addLine(map, QStringLiteral("Other"), 2, black, 300);
	for (char letter = 'a'; letter <= 'm'; ++letter)
	{
		if (letter != 'e' && letter != 'f')
			addObject(map, letter, letter == 'c' ? 2 : 1);
	}
}

/// My edits: c, h, i and m changed, d and k deleted, f added.
void editMine(Map& map)
{
	change(map, 'c', 30, 2000);
	remove(map, 'd');
	addObject(map, 'f', 1, -1, 2000);
	change(map, 'g', 25, 2000);
	change(map, 'h', 30, 2000);
	change(map, 'i', 30, 2000);
	remove(map, 'j');
	remove(map, 'k');
	change(map, 'm', 30, 5000);
}

/// Their edits: a, h, j and m changed, b, i and k deleted, e added.
void editTheirs(Map& map)
{
	change(map, 'a', 40, 3000);
	remove(map, 'b');
	addObject(map, 'e', 1, -1, 3000);
	change(map, 'g', 25, 3000);
	change(map, 'h', 40, 3000);
	remove(map, 'i');
	change(map, 'j', 40, 3000);
	remove(map, 'k');
	change(map, 'm', 40, 5000);
}

QByteArray save(const Map& map)
{
	XMLFileFormat format;
	QBuffer buffer;
	buffer.open(QIODevice::ReadWrite);
	auto exporter = format.makeExporter({}, &map, nullptr);
	exporter->setDevice(&buffer);
	if (!exporter->doExport())
		return {};
	return buffer.data();
}

bool saveTo(const Map& map, const QString& path)
{
	XMLFileFormat format;
	auto exporter = format.makeExporter(path, &map, nullptr);
	return exporter && exporter->doExport();
}

QByteArray bytes(const QString& path)
{
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly))
		return {};
	return file.readAll();
}

MergeRow::Status statusOf(const MergePlan& plan, char letter)
{
	auto const* row = plan.row(id(letter));
	return row ? row->status : MergeRow::Status(-1);
}

}  // namespace


/**
 * @test Tests the comparison and the apply of "Merge changes from file",
 *       the field copy files and the runner's steps.
 *
 * the design notes, "Merge changes from file: detail", first change.
 */
class MergeTest : public QObject
{
Q_OBJECT
	QTemporaryDir dir;
	std::unique_ptr<Map> base;
	std::unique_ptr<Map> mine;
	std::unique_ptr<Map> theirs;
	
	QString file(const char* name) const
	{
		return QDir(dir.path()).absoluteFilePath(QString::fromLatin1(name));
	}
	
	/// Three maps read from one file, so that they share nothing but the uuids.
	void loadThree()
	{
		auto const path = file("three.foom");
		if (!QFileInfo::exists(path))
		{
			Map map;
			setupBase(map);
			QVERIFY(saveTo(map, path));
		}
		for (auto* target : { &base, &mine, &theirs })
		{
			*target = std::make_unique<Map>();
			QVERIFY((*target)->loadFrom(path));
		}
	}
	
	void loadEdited()
	{
		loadThree();
		editMine(*mine);
		editTheirs(*theirs);
	}
	
	int runJob(const QJsonArray& steps, const char* name, QByteArray* output = nullptr) const
	{
		auto const job_path = file(name);
		{
			QFile job_file(job_path);
			if (!job_file.open(QIODevice::WriteOnly | QIODevice::Truncate))
				return -1;
			job_file.write(QJsonDocument(QJsonObject{ { QStringLiteral("steps"), steps } }).toJson());
		}
		QProcess process;
		auto env = QProcessEnvironment::systemEnvironment();
		env.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
		process.setProcessEnvironment(env);
		process.start(QString::fromUtf8(FOOM_CLI_EXECUTABLE), { QStringLiteral("--run"), job_path });
		if (!process.waitForFinished(120000))
			return -1;
		if (output)
		{
			*output = process.readAllStandardOutput();
			*output += process.readAllStandardError();
		}
		return process.exitCode();
	}
	
	static QJsonObject step(const char* name, const QString& path)
	{
		return QJsonObject{ { QStringLiteral("step"), QLatin1String(name) }, { QStringLiteral("path"), path } };
	}
	
private slots:
	void initTestCase()
	{
		doStaticInitializations();
		QVERIFY(dir.isValid());
	}
	
	/// Accept (1)
	void contentHashTest()
	{
		loadThree();
		auto* object = find(*mine, 'a')->asPath();
		auto const hash = contentHash(*object);
		QCOMPARE(hash.size(), 20);
		QCOMPARE(contentHash(*find(*theirs, 'a')), hash);
		
		std::unique_ptr<Object> copy { object->duplicate() };
		QCOMPARE(contentHash(*copy), hash);
		copy->setIdentity(QUuid::createUuid(), 5, 6);
		QCOMPARE(contentHash(*copy), hash);
		
		auto coord = object->getCoordinate(1);
		coord.setNativeX(coord.nativeX() + 1);
		copy->asPath()->setCoordinate(1, coord);
		QVERIFY(contentHash(*copy) != hash);
		
		copy.reset(object->duplicate());
		copy->setSymbol(symbolNumbered(*mine, 2), true);
		QVERIFY(contentHash(*copy) != hash);
		
		copy.reset(object->duplicate());
		copy->setRotation(0.5);
		QVERIFY(contentHash(*copy) != hash);
		
		copy.reset(object->duplicate());
		copy->setTag(QStringLiteral("name"), QStringLiteral("x"));
		copy->setTag(QStringLiteral("height"), QStringLiteral("2"));
		auto const tagged = contentHash(*copy);
		QVERIFY(tagged != hash);
		std::unique_ptr<Object> other { object->duplicate() };
		other->setTag(QStringLiteral("height"), QStringLiteral("2"));
		other->setTag(QStringLiteral("name"), QStringLiteral("x"));
		QCOMPARE(contentHash(*other), tagged);
		other->setTag(QStringLiteral("name"), QStringLiteral("y"));
		QVERIFY(contentHash(*other) != tagged);
		
		TextSymbol text_symbol;
		text_symbol.setNumberComponent(0, 7);
		TextObject text(&text_symbol);
		text.setText(QStringLiteral("Birch"));
		auto const text_hash = contentHash(text);
		text.setText(QStringLiteral("Birches"));
		QVERIFY(contentHash(text) != text_hash);
	}
	
	/// Accept (2), (3)
	void threeWayTest()
	{
		loadEdited();
		auto const before = save(*mine);
		
		auto const plan = compareMaps(base.get(), *mine, *theirs);
		QVERIFY(!plan.two_way);
		QCOMPARE(plan.rows.size(), std::size_t(10));
		for (auto letter : { 'a', 'b', 'e' })
			QCOMPARE(statusOf(plan, letter), MergeRow::Incoming);
		for (auto letter : { 'c', 'd', 'f' })
			QCOMPARE(statusOf(plan, letter), MergeRow::Outgoing);
		for (auto letter : { 'h', 'i', 'j', 'm' })
			QCOMPARE(statusOf(plan, letter), MergeRow::Conflict);
		for (auto letter : { 'g', 'k', 'l' })
			QVERIFY(!plan.row(id(letter)));
		QCOMPARE(plan.unresolved(), 4);
		QCOMPARE(plan.row(id('a'))->theirs, MergeRow::Changed);
		QCOMPARE(plan.row(id('a'))->mine, MergeRow::Unchanged);
		QCOMPARE(plan.row(id('b'))->theirs, MergeRow::Deleted);
		QCOMPARE(plan.row(id('e'))->theirs, MergeRow::Added);
		QCOMPARE(plan.row(id('e'))->mine, MergeRow::Absent);
		QCOMPARE(plan.row(id('i'))->mine, MergeRow::Changed);
		QCOMPARE(plan.row(id('i'))->theirs, MergeRow::Deleted);
		QCOMPARE(plan.row(id('a'))->action, MergeRow::KeepTheirs);
		QCOMPARE(plan.row(id('h'))->action, MergeRow::KeepMine);
		QVERIFY(plan.symbols.empty());
		
		auto const steps = mine->undoManager().undoStepCount();
		MergeResult result;
		QString error;
		QVERIFY2(applyMerge(*mine, *theirs, plan, &result, &error), qPrintable(error));
		QCOMPARE(result.replaced, 1);
		QCOMPARE(result.deleted, 1);
		QCOMPARE(result.added, 1);
		QCOMPARE(result.symbols, 0);
		QCOMPARE(mine->undoManager().undoStepCount(), steps + 1);
		
		QCOMPARE(endX(*mine, 'a'), 40.0);
		QCOMPARE(find(*mine, 'a')->modifiedTime(), qint64(3000));
		QVERIFY(!find(*mine, 'b'));
		QVERIFY(find(*mine, 'e'));
		QCOMPARE(find(*mine, 'e')->modifiedTime(), qint64(3000));
		QCOMPARE(find(*mine, 'e')->getSymbol(), symbolNumbered(*mine, 1));
		QCOMPARE(endX(*mine, 'c'), 30.0);
		QVERIFY(!find(*mine, 'd'));
		QVERIFY(find(*mine, 'f'));
		QCOMPARE(endX(*mine, 'h'), 30.0);
		QCOMPARE(endX(*mine, 'i'), 30.0);
		QVERIFY(!find(*mine, 'j'));
		QCOMPARE(endX(*mine, 'm'), 30.0);
		
		QVERIFY(mine->undoManager().undo());
		QCOMPARE(endX(*mine, 'a'), 20.0);
		QCOMPARE(find(*mine, 'a')->modifiedTime(), qint64(1000));
		QVERIFY(find(*mine, 'b'));
		QVERIFY(!find(*mine, 'e'));
		// The undo history aside, the file is as before.
		mine->undoManager().clear();
		QCOMPARE(save(*mine), before);
	}
	
	/// Accept (4)
	void conflictActionsTest()
	{
		loadEdited();
		auto plan = compareMaps(base.get(), *mine, *theirs);
		plan.resolveConflicts(MergeRow::KeepTheirs);
		QCOMPARE(plan.unresolved(), 0);
		MergeResult result;
		QString error;
		QVERIFY2(applyMerge(*mine, *theirs, plan, &result, &error), qPrintable(error));
		QCOMPARE(endX(*mine, 'h'), 40.0);
		QVERIFY(!find(*mine, 'i'));
		QVERIFY(find(*mine, 'j'));
		QCOMPARE(endX(*mine, 'j'), 40.0);
		QCOMPARE(endX(*mine, 'm'), 40.0);
		QVERIFY(mine->undoManager().undo());
		QCOMPARE(endX(*mine, 'h'), 30.0);
		QVERIFY(find(*mine, 'i'));
		QVERIFY(!find(*mine, 'j'));
		
		loadEdited();
		plan = compareMaps(base.get(), *mine, *theirs);
		plan.resolveConflicts(MergeRow::KeepBoth);
		auto const objects = mine->getNumObjects();
		QVERIFY2(applyMerge(*mine, *theirs, plan, &result, &error), qPrintable(error));
		// e, and their h, j and m; b is deleted.
		QCOMPARE(mine->getNumObjects(), objects + 4 - 1);
		QCOMPARE(endX(*mine, 'h'), 30.0);
		QCOMPARE(endX(*mine, 'i'), 30.0);
		QCOMPARE(endX(*mine, 'j'), 40.0);  // I had none, so theirs keeps its uuid
		auto derived = 0;
		auto const* part = mine->getPart(0);
		for (int i = 0; i < part->getNumObjects(); ++i)
		{
			auto const* object = part->getObject(i);
			if (object->derivedFrom() == id('h'))
			{
				++derived;
				QVERIFY(object->uuid() != id('h'));
				QCOMPARE(object->asPath()->getCoordinate(1).x(), 40.0);
				QCOMPARE(object->getSymbol(), symbolNumbered(*mine, 1));
			}
		}
		QCOMPARE(derived, 1);
		
		loadEdited();
		plan = compareMaps(base.get(), *mine, *theirs);
		plan.resolveConflicts(MergeRow::KeepMine);
		QVERIFY2(applyMerge(*mine, *theirs, plan, &result, &error), qPrintable(error));
		QCOMPARE(endX(*mine, 'h'), 30.0);
		QCOMPARE(result.changes(), 3);
	}
	
	/// Accept (5)
	void twiceTest()
	{
		loadEdited();
		auto plan = compareMaps(base.get(), *mine, *theirs);
		MergeResult result;
		QString error;
		QVERIFY(applyMerge(*mine, *theirs, plan, &result, &error));
		
		// The copy as it was merged is the new base.
		plan = compareMaps(theirs.get(), *mine, *theirs);
		QCOMPARE(plan.count(MergeRow::Incoming), 0);
		QCOMPARE(plan.count(MergeRow::Conflict), 0);
		QVERIFY(plan.count(MergeRow::Outgoing) > 0);
		auto const steps = mine->undoManager().undoStepCount();
		QVERIFY(applyMerge(*mine, *theirs, plan, &result, &error));
		QCOMPARE(result.changes(), 0);
		QCOMPARE(mine->undoManager().undoStepCount(), steps);
	}
	
	/// Accept (6)
	void twoWayTest()
	{
		loadEdited();
		auto const plan = compareMaps(nullptr, *mine, *theirs);
		QVERIFY(plan.two_way);
		for (auto letter : { 'g', 'k', 'l' })
			QVERIFY(!plan.row(id(letter)));
		QCOMPARE(plan.rows.size(), std::size_t(10));
		QCOMPARE(plan.count(MergeRow::Conflict), 10);
		QCOMPARE(plan.unresolved(), 10);
		// Theirs is newer, or only they have it.
		for (auto letter : { 'a', 'h', 'e', 'd', 'j' })
			QCOMPARE(plan.row(id(letter))->action, MergeRow::KeepTheirs);
		// Mine is newer, the times are equal, or only I have it.
		for (auto letter : { 'c', 'm', 'f', 'b', 'i' })
			QCOMPARE(plan.row(id(letter))->action, MergeRow::KeepMine);
	}
	
	/// Accept (7)
	void cutTest()
	{
		loadThree();
		// I cut l: the longer piece keeps the uuid, the other is new.
		change(*mine, 'l', 15, 2000);
		auto* piece = new PathObject(symbolNumbered(*mine, 1), MapCoordVector{ MapCoord(15.0, 55.0), MapCoord(20.0, 55.0) });
		piece->setDerivedFrom(id('l'));
		mine->addObject(piece);
		auto const piece_uuid = piece->uuid();
		// They gave it a tag.
		find(*theirs, 'l')->setTag(QStringLiteral("name"), QStringLiteral("wall"));
		
		auto const plan = compareMaps(base.get(), *mine, *theirs);
		QCOMPARE(plan.rows.size(), std::size_t(2));
		QCOMPARE(statusOf(plan, 'l'), MergeRow::Conflict);
		QVERIFY(plan.row(piece_uuid));
		QCOMPARE(plan.row(piece_uuid)->status, MergeRow::Outgoing);
		QCOMPARE(plan.row(piece_uuid)->mine, MergeRow::Added);
	}
	
	/// Accept (8)
	void symbolsTest()
	{
		loadThree();
		// A symbol which I lack, with a colour which I lack.
		auto const* red = addColor(*theirs, QStringLiteral("Red"), 1.0f);
		addLine(*theirs, QStringLiteral("New line"), 9, red);
		addObject(*theirs, 'e', 9, -1, 3000);
		// A symbol which both have, changed on their side, on a changed object.
		const_cast<Symbol*>(symbolNumbered(*theirs, 2))->asLine()->setLineWidth(0.9);
		change(*theirs, 'c', 40, 3000);
		
		auto const plan = compareMaps(base.get(), *mine, *theirs);
		QCOMPARE(plan.rows.size(), std::size_t(2));
		QCOMPARE(plan.symbols.size(), std::size_t(2));
		QCOMPARE(plan.symbols[0].number, QStringLiteral("2"));
		QCOMPARE(plan.symbols[0].status, MergeSymbolRow::Differs);
		QCOMPARE(plan.symbols[1].number, QStringLiteral("9"));
		QCOMPARE(plan.symbols[1].name, QStringLiteral("New line"));
		QCOMPARE(plan.symbols[1].status, MergeSymbolRow::New);
		
		auto const colors = mine->getNumColors();
		auto const symbols = mine->getNumSymbols();
		MergeResult result;
		QString error;
		QVERIFY2(applyMerge(*mine, *theirs, plan, &result, &error), qPrintable(error));
		QCOMPARE(result.symbols, 1);
		QCOMPARE(mine->getNumSymbols(), symbols + 1);
		QCOMPARE(mine->getNumColors(), colors + 1);
		auto const* imported = symbolNumbered(*mine, 9);
		QVERIFY(imported);
		QVERIFY(imported != symbolNumbered(*theirs, 9));
		QCOMPARE(find(*mine, 'e')->getSymbol(), imported);
		QCOMPARE(imported->asLine()->getColor()->getName(), QStringLiteral("Red"));
		QVERIFY(mine->findColorIndex(imported->asLine()->getColor()) >= 0);
		
		// The changed object uses my definition of symbol 2.
		QCOMPARE(endX(*mine, 'c'), 40.0);
		QCOMPARE(find(*mine, 'c')->getSymbol(), symbolNumbered(*mine, 2));
		QCOMPARE(symbolNumbered(*mine, 2)->asLine()->getLineWidth(), 300);
	}
	
	/// Accept (9)
	void partsTest()
	{
		loadThree();
		auto* sketch = new MapPart(QStringLiteral("Notes"), theirs.get());
		sketch->setRole(MapPart::Role::Sketch);
		theirs->addPart(sketch, std::size_t(theirs->getNumParts()));
		auto const sketch_index = theirs->findPartIndex(sketch);
		// They move a into the new part and add e there.
		auto* moved = find(*theirs, 'a')->duplicate();
		remove(*theirs, 'a');
		theirs->addObject(moved, sketch_index);
		QCOMPARE(moved->uuid(), id('a'));
		addObject(*theirs, 'e', 1, sketch_index, 3000);
		
		auto const plan = compareMaps(base.get(), *mine, *theirs);
		QCOMPARE(plan.rows.size(), std::size_t(2));
		QCOMPARE(statusOf(plan, 'a'), MergeRow::Incoming);
		QCOMPARE(plan.row(id('a'))->theirs, MergeRow::Changed);
		QCOMPARE(statusOf(plan, 'e'), MergeRow::Incoming);
		
		QCOMPARE(mine->getNumParts(), 1);
		auto const in_first = mine->getPart(0)->getNumObjects();
		MergeResult result;
		QString error;
		QVERIFY2(applyMerge(*mine, *theirs, plan, &result, &error), qPrintable(error));
		QCOMPARE(result.parts, 1);
		QCOMPARE(mine->getNumParts(), 2);
		QCOMPARE(mine->getPart(1)->getName(), QStringLiteral("Notes"));
		QCOMPARE(mine->getPart(1)->role(), MapPart::Role::Sketch);
		QCOMPARE(mine->getPart(1)->getNumObjects(), 2);
		QCOMPARE(mine->getPart(0)->getNumObjects(), in_first - 1);
		QVERIFY(mine->getPart(1)->findObjectIndex(find(*mine, 'a')) >= 0);
		QCOMPARE(mine->getCurrentPartIndex(), std::size_t(0));
		
		QVERIFY(mine->undoManager().undo());
		QCOMPARE(mine->getNumParts(), 1);
		QCOMPARE(mine->getPart(0)->getNumObjects(), in_first);
		QVERIFY(mine->getPart(0)->findObjectIndex(find(*mine, 'a')) >= 0);
		QVERIFY(!find(*mine, 'e'));
		
		// Merged, the two maps compare as equal.
		QVERIFY(mine->undoManager().redo());
		QVERIFY(compareMaps(nullptr, *mine, *theirs).rows.empty());
	}
	
	/// Accept (10)
	void syncRecordTest()
	{
		Map map;
		setupBase(map);
		QVERIFY(!save(map).contains("<sync"));
		
		SyncCopy first;
		first.id = QStringLiteral("11111111-2222-4333-8444-555555555555");
		first.base = QStringLiteral("map.base-11111111-2222-4333-8444-555555555555.xmap");
		first.created = 1794000000;
		SyncCopy second;
		second.id = QStringLiteral("aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee");
		second.created = 1794000100;
		map.setSyncCopies({ first, second });
		auto const path = file("sync.foom");
		QVERIFY(saveTo(map, path));
		QVERIFY(bytes(path).contains("<sync"));
		Map loaded;
		QVERIFY(loaded.loadFrom(path));
		QCOMPARE(loaded.syncCopies().size(), std::size_t(2));
		QVERIFY(loaded.syncCopies()[0] == first);
		QVERIFY(loaded.syncCopies()[1] == second);
	}
	
	/// Accept (11)
	void fieldCopyTest()
	{
		auto const map_path = file("work.foom");
		auto const copy_path = file("field.foom");
		Map map;
		setupBase(map);
		QVERIFY(saveTo(map, map_path));
		
		QString error;
		QVERIFY(createFieldCopy(map, QString{}, copy_path, &error).isEmpty());
		QVERIFY(!error.isEmpty());
		
		auto const sync_id = createFieldCopy(map, map_path, copy_path, &error);
		QVERIFY2(!sync_id.isEmpty(), qPrintable(error));
		QCOMPARE(map.syncCopies().size(), std::size_t(1));
		QCOMPARE(map.syncCopies()[0].id, sync_id);
		auto const base_path = baseFilePath(map_path, sync_id);
		QCOMPARE(QFileInfo(base_path).fileName(), QString(QStringLiteral("work.base-") + sync_id + QStringLiteral(".xmap")));
		QCOMPARE(map.syncCopies()[0].base, QFileInfo(base_path).fileName());
		QVERIFY(map.syncCopies()[0].created > 0);
		QVERIFY(QFileInfo::exists(copy_path));
		QCOMPARE(bytes(base_path), bytes(copy_path));
		
		Map copy;
		QVERIFY(copy.loadFrom(copy_path));
		QCOMPARE(copy.syncCopies().size(), std::size_t(1));
		QCOMPARE(copy.syncCopies()[0].id, sync_id);
		QVERIFY(copy.syncCopies()[0].base.isEmpty());
		
		MergeInput input;
		QVERIFY2(loadForMerge(map, map_path, copy_path, input, &error), qPrintable(error));
		QCOMPARE(input.copy.id, sync_id);
		QVERIFY(input.theirs && input.base);
		QVERIFY(compareMaps(input.base.get(), map, *input.theirs).rows.empty());
		
		// Not a copy of this map; a copy at another scale.
		Map stranger;
		setupBase(stranger);
		auto const stranger_path = file("stranger.foom");
		QVERIFY(saveTo(stranger, stranger_path));
		error.clear();
		QVERIFY(!loadForMerge(map, map_path, stranger_path, input, &error));
		QVERIFY(!error.isEmpty());
		copy.setScaleDenominator(15000);
		auto const scaled_path = file("scaled.foom");
		QVERIFY(saveTo(copy, scaled_path));
		error.clear();
		QVERIFY(!loadForMerge(map, map_path, scaled_path, input, &error));
		QVERIFY(!error.isEmpty());
		
		// Without the base file.
		QVERIFY(QFile::remove(base_path));
		QVERIFY(loadForMerge(map, map_path, copy_path, input, &error));
		QVERIFY(input.theirs && !input.base);
		QVERIFY(refreshBase(map_path, copy_path, input.copy, &error));
		QCOMPARE(bytes(base_path), bytes(copy_path));
		
		QVERIFY(!closeFieldCopy(map, map_path, QStringLiteral("no such id"), &error));
		QVERIFY(closeFieldCopy(map, map_path, QString{}, &error));
		QVERIFY(map.syncCopies().empty());
		QVERIFY(!QFileInfo::exists(base_path));
		QVERIFY(!closeFieldCopy(map, map_path, QString{}, &error));
	}
	
	/// Accept (13), (14), (15)
	void modelTest()
	{
		loadEdited();
		MergeModel model(*mine, *theirs, compareMaps(base.get(), *mine, *theirs));
		QCOMPARE(model.rowCount(), 10);
		QCOMPARE(model.columnCount(), 6);
		auto const rowOf = [&model](char letter) {
			for (int i = 0; i < model.rowCount(); ++i)
			{
				if (model.plan().rows[std::size_t(i)].uuid == id(letter))
					return i;
			}
			return -1;
		};
		auto const text = [&model](int row, int column) {
			return model.index(row, column).data().toString();
		};
		auto const a = rowOf('a');
		auto const h = rowOf('h');
		QVERIFY(a >= 0 && h >= 0);
		QCOMPARE(text(a, MergeModel::ObjectColumn), QStringLiteral("Path"));
		QCOMPARE(text(a, MergeModel::SymbolColumn), QStringLiteral("1 Line"));
		QVERIFY(text(a, MergeModel::MineColumn).startsWith(QLatin1String("unchanged")));
		QVERIFY(text(a, MergeModel::TheirsColumn).startsWith(QLatin1String("changed, ")));
		QVERIFY(text(a, MergeModel::TheirsColumn).endsWith(QDateTime::fromSecsSinceEpoch(3000).toString(Qt::ISODate)));
		QCOMPARE(text(a, MergeModel::StatusColumn), QStringLiteral("Incoming"));
		QCOMPARE(text(a, MergeModel::ActionColumn), QStringLiteral("Keep theirs"));
		QVERIFY(!model.index(a, 0).data(Qt::ForegroundRole).isValid());
		QCOMPARE(model.index(h, 0).data(Qt::ForegroundRole).value<QBrush>().color(), QColor(Qt::red));
		QCOMPARE(text(h, MergeModel::StatusColumn), QStringLiteral("Conflict"));
		QVERIFY(model.headerData(MergeModel::ActionColumn, Qt::Horizontal).isValid());
		
		MergeFilterModel filter;
		filter.setSourceModel(&model);
		QCOMPARE(filter.rowCount(), 10);
		filter.setFilter(MergeFilterModel::Conflicts);
		QCOMPARE(filter.rowCount(), 4);
		filter.setFilter(MergeFilterModel::Undecided);
		QCOMPARE(filter.rowCount(), 4);
		
		QSignalSpy changed(&model, &MergeModel::decisionsChanged);
		model.setAction(h, MergeRow::KeepBoth);
		QCOMPARE(changed.count(), 1);
		QVERIFY(!model.index(h, 0).data(Qt::ForegroundRole).isValid());
		QVERIFY(model.index(h, 0).data(MergeModel::ResolvedRole).toBool());
		QCOMPARE(text(h, MergeModel::StatusColumn), QStringLiteral("Conflict, decided"));
		QCOMPARE(text(h, MergeModel::ActionColumn), QStringLiteral("Keep both"));
		filter.setFilter(MergeFilterModel::Undecided);
		QCOMPARE(filter.rowCount(), 3);
		filter.setFilter(MergeFilterModel::Conflicts);
		QCOMPARE(filter.rowCount(), 4);
		
		model.rejectIncoming();
		for (auto letter : { 'a', 'b', 'e' })
			QCOMPARE(model.plan().row(id(letter))->action, MergeRow::KeepMine);
		QCOMPARE(model.plan().row(id('h'))->action, MergeRow::KeepBoth);
		model.acceptNonConflicting();
		for (auto letter : { 'a', 'b', 'e' })
			QCOMPARE(model.plan().row(id(letter))->action, MergeRow::KeepTheirs);
		QCOMPARE(model.plan().row(id('i'))->action, MergeRow::KeepMine);
		QCOMPARE(model.plan().unresolved(), 3);
	}
	
	/// Accept (16)
	void dialogTest()
	{
		loadEdited();
		MergeDialog dialog(*mine, *theirs, compareMaps(base.get(), *mine, *theirs));
		auto* apply = dialog.findChild<QPushButton*>(QStringLiteral("apply"));
		auto* keep_theirs = dialog.findChild<QPushButton*>(QStringLiteral("keep-theirs"));
		auto* view = dialog.findChild<QTableView*>(QStringLiteral("rows"));
		auto* filter = dialog.findChild<QComboBox*>(QStringLiteral("filter"));
		auto* notice = dialog.findChild<QLabel*>(QStringLiteral("two-way-notice"));
		QVERIFY(apply && keep_theirs && view && filter && notice);
		QVERIFY(!apply->isEnabled());
		QVERIFY(notice->isHidden());
		QCOMPARE(view->model()->rowCount(), 10);
		
		// The first row is the object a, which they made longer.
		QSignalSpy shown(&dialog, &MergeDialog::showExtent);
		view->selectRow(0);
		QCOMPARE(shown.count(), 1);
		auto const extent = shown.first().first().toRectF();
		QVERIFY(extent.contains(QPointF(10, 0)));
		QVERIFY(extent.contains(QPointF(35, 0)));
		
		filter->setCurrentIndex(1);
		QCOMPARE(view->model()->rowCount(), 4);
		view->selectAll();
		keep_theirs->click();
		QVERIFY(apply->isEnabled());
		QCOMPARE(dialog.plan().unresolved(), 0);
		for (auto letter : { 'h', 'i', 'j', 'm' })
			QCOMPARE(dialog.plan().row(id(letter))->action, MergeRow::KeepTheirs);
		QCOMPARE(dialog.plan().row(id('c'))->action, MergeRow::KeepMine);
		QCOMPARE(view->model()->rowCount(), 4);
		QSignalSpy accepted(&dialog, &QDialog::accepted);
		apply->click();
		QCOMPARE(accepted.count(), 1);
		QCOMPARE(dialog.result(), int(QDialog::Accepted));
		
		// What the dialog decided can be carried out.
		MergeResult result;
		QString error;
		QVERIFY2(applyMerge(*mine, *theirs, dialog.plan(), &result, &error), qPrintable(error));
		QCOMPARE(endX(*mine, 'h'), 40.0);
		
		loadEdited();
		MergeDialog second(*mine, *theirs, compareMaps(nullptr, *mine, *theirs));
		QVERIFY(!second.findChild<QLabel*>(QStringLiteral("two-way-notice"))->isHidden());
		QVERIFY(!second.findChild<QPushButton*>(QStringLiteral("apply"))->isEnabled());
		QSignalSpy finished(&second, &QDialog::finished);
		second.findChild<QPushButton*>(QStringLiteral("pause"))->click();
		QCOMPARE(finished.count(), 1);
		QCOMPARE(finished.first().first().toInt(), int(MergeDialog::Paused));
	}
	
	/// Accept (17)
	void decisionsTest()
	{
		loadEdited();
		auto const path = decisionsFilePath(file("paused.foom"), QStringLiteral("abc"));
		QCOMPARE(QFileInfo(path).fileName(), QStringLiteral("paused.merge-abc.json"));
		QCOMPARE(QFileInfo(path).absolutePath(), QFileInfo(file("paused.foom")).absolutePath());
		
		auto plan = compareMaps(base.get(), *mine, *theirs);
		QCOMPARE(loadDecisions(path, plan), 0);
		for (auto& row : plan.rows)
		{
			if (row.uuid == id('h'))
			{
				row.action = MergeRow::KeepBoth;
				row.resolved = true;
			}
			else if (row.uuid == id('a'))
			{
				row.action = MergeRow::KeepMine;
			}
		}
		QString error;
		QVERIFY2(saveDecisions(path, plan, &error), qPrintable(error));
		
		auto fresh = compareMaps(base.get(), *mine, *theirs);
		QCOMPARE(fresh.row(id('a'))->action, MergeRow::KeepTheirs);
		QCOMPARE(loadDecisions(path, fresh), 10);
		QCOMPARE(fresh.row(id('h'))->action, MergeRow::KeepBoth);
		QVERIFY(fresh.row(id('h'))->resolved);
		QCOMPARE(fresh.row(id('a'))->action, MergeRow::KeepMine);
		QCOMPARE(fresh.unresolved(), 3);
		
		// I delete c, which I had only changed: its row is another one now.
		remove(*mine, 'c');
		fresh = compareMaps(base.get(), *mine, *theirs);
		QCOMPARE(fresh.row(id('c'))->mine, MergeRow::Deleted);
		QCOMPARE(loadDecisions(path, fresh), 9);
	}
	
	/// Accept (12)
	void runnerTest()
	{
		QVERIFY2(QFileInfo::exists(QString::fromUtf8(FOOM_CLI_EXECUTABLE)), FOOM_CLI_EXECUTABLE);
		auto const map_path = file("runner.foom");
		auto const copy_path = file("runner-field.foom");
		{
			Map map;
			setupBase(map);
			QVERIFY(saveTo(map, map_path));
		}
		QByteArray output;
		QVERIFY2(runJob({ step("open", map_path), step("field-copy-create", copy_path) }, "create.json", &output) == 0, output.constData());
		QVERIFY(QFileInfo::exists(copy_path));
		QString sync_id;
		{
			Map map;
			QVERIFY(map.loadFrom(map_path));
			QCOMPARE(map.syncCopies().size(), std::size_t(1));
			sync_id = map.syncCopies()[0].id;
			// My edits, without a conflict.
			change(map, 'c', 30, 2000);
			QVERIFY(saveTo(map, map_path));
		}
		auto const base_path = baseFilePath(map_path, sync_id);
		QVERIFY(QFileInfo::exists(base_path));
		{
			Map copy;
			QVERIFY(copy.loadFrom(copy_path));
			change(copy, 'a', 40, 3000);
			addObject(copy, 'e', 1, -1, 3000);
			QVERIFY(saveTo(copy, copy_path));
		}
		
		QVERIFY2(runJob({ step("open", map_path), step("merge-from-file", copy_path) }, "merge.json", &output) == 0, output.constData());
		QVERIFY2(output.contains("\"incoming\":2") || output.contains("\"incoming\": 2"), output.constData());
		{
			Map map;
			QVERIFY(map.loadFrom(map_path));
			QCOMPARE(endX(map, 'a'), 40.0);
			QCOMPARE(endX(map, 'c'), 30.0);
			QVERIFY(find(map, 'e'));
			QCOMPARE(map.syncCopies().size(), std::size_t(1));
		}
		QCOMPARE(bytes(base_path), bytes(copy_path));
		
		// Again: nothing comes in.
		QVERIFY2(runJob({ step("open", map_path), step("merge-from-file", copy_path) }, "again.json", &output) == 0, output.constData());
		QVERIFY2(output.contains("\"incoming\":0") || output.contains("\"incoming\": 0"), output.constData());
		
		// A conflict: both change h.
		{
			Map map;
			QVERIFY(map.loadFrom(map_path));
			change(map, 'h', 30, 4000);
			QVERIFY(saveTo(map, map_path));
			Map copy;
			QVERIFY(copy.loadFrom(copy_path));
			change(copy, 'h', 40, 4500);
			QVERIFY(saveTo(copy, copy_path));
		}
		auto const map_before = bytes(map_path);
		auto const base_before = bytes(base_path);
		QVERIFY(runJob({ step("open", map_path), step("merge-from-file", copy_path) }, "conflict.json", &output) != 0);
		QVERIFY2(output.contains("conflict"), output.constData());
		QCOMPARE(bytes(map_path), map_before);
		QCOMPARE(bytes(base_path), base_before);
		
		auto merge = step("merge-from-file", copy_path);
		merge.insert(QStringLiteral("conflicts"), QStringLiteral("theirs"));
		QVERIFY2(runJob({ step("open", map_path), merge }, "theirs.json", &output) == 0, output.constData());
		{
			Map map;
			QVERIFY(map.loadFrom(map_path));
			QCOMPARE(endX(map, 'h'), 40.0);
		}
		QCOMPARE(bytes(base_path), bytes(copy_path));
		
		QVERIFY2(runJob({ step("open", map_path), QJsonObject{ { QStringLiteral("step"), QStringLiteral("field-copy-close") } } }, "close.json", &output) == 0, output.constData());
		QVERIFY(!bytes(map_path).contains("<sync"));
		QVERIFY(!QFileInfo::exists(base_path));
	}
};


/*
 * We don't need a real GUI window.
 */
namespace {
	auto Q_DECL_UNUSED qpa_selected = qputenv("QT_QPA_PLATFORM", "offscreen");  // clazy:exclude=non-pod-global-static
}


QTEST_MAIN(MergeTest)
#include "merge_t.moc"  // IWYU pragma: keep
