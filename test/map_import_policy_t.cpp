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


#include <memory>
#include <vector>

#include <QtGlobal>
#include <QtTest>
#include <QCoreApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QLatin1String>
#include <QObject>
#include <QPointF>
#include <QRadioButton>
#include <QRectF>
#include <QSettings>
#include <QString>
#include <QStringList>

#include "global.h"
#include "core/georeferencing.h"
#include "core/map.h"
#include "core/map_color.h"
#include "core/map_coord.h"
#include "core/map_import_policy.h"
#include "core/map_part.h"
#include "core/objects/object.h"
#include "core/objects/symbol_rule_set.h"
#include "core/symbols/area_symbol.h"
#include "core/symbols/line_symbol.h"
#include "core/symbols/symbol.h"
#include "gui/map/map_import_dialog.h"
#include "undo/undo_manager.h"

using namespace OpenOrienteering;


namespace {

MapColor* addColor(Map& map, const char* name, float c, float k)
{
	auto* color = new MapColor(QString::fromLatin1(name), map.getNumColors());
	color->setCmyk(MapColorCmyk(c, 0.0f, 0.0f, k));
	color->setOpacity(1.0f);
	map.addColor(color, map.getNumColors());
	return color;
}

LineSymbol* addLine(Map& map, int number, const MapColor* color, int width)
{
	auto* line = new LineSymbol();
	line->setNumberComponent(0, number);
	line->setName(QStringLiteral("Line %1").arg(number));
	line->setColor(color);
	line->setLineWidth(width);
	map.addSymbol(line, map.getNumSymbols());
	return line;
}

AreaSymbol* addArea(Map& map, int number, const MapColor* color)
{
	auto* area = new AreaSymbol();
	area->setNumberComponent(0, number);
	area->setName(QStringLiteral("Area %1").arg(number));
	area->setColor(color);
	map.addSymbol(area, map.getNumSymbols());
	return area;
}

void addPath(Map& map, const Symbol* symbol, std::vector<MapCoord> coords)
{
	auto* path = new PathObject(symbol);
	for (auto const& coord : coords)
		path->addCoordinate(coord);
	if (symbol->getType() == Symbol::Area)
		path->closeAllParts();
	map.addObject(path);
}

/// The map imported into: colour Black, line 101 (Black, 1 mm), line 102 (Black, 1 mm), one object of 102.
void setupTarget(Map& map)
{
	map.setScaleDenominator(10000);
	auto* black = addColor(map, "Black", 0.0f, 1.0f);
	addLine(map, 101, black, 1);
	auto* line_102 = addLine(map, 102, black, 1);
	addPath(map, line_102, { MapCoord(0, 0), MapCoord(5, 0) });
}

/**
 * The map which is imported: colours Black and Blue; line 101 equal to the target's,
 * line 102 2 mm wide, line 103 in Blue, area 104 in Blue; one object of each.
 */
void setupImported(Map& map, unsigned int scale = 10000)
{
	map.setScaleDenominator(scale);
	auto* black = addColor(map, "Black", 0.0f, 1.0f);
	auto* blue = addColor(map, "Blue", 1.0f, 0.0f);
	addPath(map, addLine(map, 101, black, 1), { MapCoord(10, 10), MapCoord(20, 10) });
	addPath(map, addLine(map, 102, black, 2), { MapCoord(10, 20), MapCoord(20, 20) });
	addPath(map, addLine(map, 103, blue, 1), { MapCoord(10, 30), MapCoord(20, 30) });
	addPath(map, addArea(map, 104, blue), { MapCoord(10, 40), MapCoord(20, 40), MapCoord(20, 50) });
}

const Symbol* numbered(const Map& map, const char* number)
{
	for (int i = 0; i < map.getNumSymbols(); ++i)
	{
		if (map.getSymbol(i)->getNumberAsString() == QLatin1String(number))
			return map.getSymbol(i);
	}
	return nullptr;
}

int countNumbered(const Map& map, const char* number)
{
	int count = 0;
	for (int i = 0; i < map.getNumSymbols(); ++i)
		count += map.getSymbol(i)->getNumberAsString() == QLatin1String(number);
	return count;
}

std::vector<const Object*> objectsOf(const Map& map, const Symbol* symbol)
{
	std::vector<const Object*> result;
	auto const* part = map.getPart(0);
	for (int i = 0; i < part->getNumObjects(); ++i)
	{
		if (part->getObject(i)->getSymbol() == symbol)
			result.push_back(part->getObject(i));
	}
	return result;
}

QStringList colorNames(const Map& map)
{
	QStringList names;
	for (int i = 0; i < map.getNumColors(); ++i)
		names.append(map.getColor(i)->getName());
	return names;
}

template <class T>
T* child(QObject& parent, const char* name)
{
	auto* found = parent.findChild<T*>(QString::fromLatin1(name));
	if (!found)
		qFatal("No child %s", name);
	return found;
}

}  // namespace



/**
 * Tests the policies of importing a map file.
 *
 * See the design notes, "Map import policies".
 */
class MapImportPolicyTest : public QObject
{
Q_OBJECT
private slots:
	void initTestCase()
	{
		QCoreApplication::setOrganizationName(QStringLiteral("OpenOrienteering.org"));
		QCoreApplication::setApplicationName(QString::fromLatin1(metaObject()->className()));
		doStaticInitializations();
	}

	/// Accept (1)
	void noSymbolsTest()
	{
		Map target, imported;
		setupTarget(target);
		setupImported(imported);
		MapImportPolicy policy;
		policy.symbols = MapImportPolicy::NoSymbols;
		auto const result = importMapWithPolicy(target, imported, policy);
		QVERIFY2(result.ok, qPrintable(result.error));
		QCOMPARE(target.getNumSymbols(), 2);
		QCOMPARE(target.getNumColors(), 1);
		QCOMPARE(result.objects, 2);
		QCOMPARE(result.objects_dropped, 2);
		QVERIFY(!result.warnings.isEmpty());
		QCOMPARE(target.getNumObjects(), 3);
		QCOMPARE(int(objectsOf(target, numbered(target, "101")).size()), 1);
		QCOMPARE(int(objectsOf(target, numbered(target, "102")).size()), 2);
	}

	/// Accept (2)
	void absentSymbolsTest()
	{
		Map target, imported;
		setupTarget(target);
		setupImported(imported);
		MapImportPolicy policy;
		policy.symbols = MapImportPolicy::AbsentSymbols;
		auto const result = importMapWithPolicy(target, imported, policy);
		QVERIFY2(result.ok, qPrintable(result.error));
		QCOMPARE(target.getNumSymbols(), 4);
		QCOMPARE(countNumbered(target, "102"), 1);
		QCOMPARE(static_cast<const LineSymbol*>(numbered(target, "102"))->getLineWidth(), 1000);
		QCOMPARE(int(objectsOf(target, numbered(target, "102")).size()), 2);
		QVERIFY(numbered(target, "103"));
		QVERIFY(numbered(target, "104"));
		QCOMPARE(int(objectsOf(target, numbered(target, "103")).size()), 1);
		QCOMPARE(int(objectsOf(target, numbered(target, "104")).size()), 1);
		QCOMPARE(result.symbols_added, 2);
		QCOMPARE(target.getNumColors(), 2);
		QCOMPARE(target.getNumObjects(), 5);
		QCOMPARE(result.objects, 4);
	}

	/// Accept (3)
	void renumberTest()
	{
		Map target, imported;
		setupTarget(target);
		setupImported(imported);
		MapImportPolicy policy;
		policy.symbols = MapImportPolicy::RenumberClashes;
		auto const result = importMapWithPolicy(target, imported, policy);
		QVERIFY2(result.ok, qPrintable(result.error));
		QCOMPARE(target.getNumSymbols(), 5);
		QCOMPARE(countNumbered(target, "101"), 1);  // equal: shared
		QCOMPARE(countNumbered(target, "102"), 1);
		QCOMPARE(static_cast<const LineSymbol*>(numbered(target, "102"))->getLineWidth(), 1000);
		auto const* renumbered = numbered(target, "102.1");
		QVERIFY(renumbered);
		QCOMPARE(static_cast<const LineSymbol*>(renumbered)->getLineWidth(), 2000);
		QCOMPARE(int(objectsOf(target, renumbered).size()), 1);
		QCOMPARE(int(objectsOf(target, numbered(target, "102")).size()), 1);
		QCOMPARE(result.symbols_renumbered, 1);

		// A second import of the same map takes the next free number
		Map again;
		setupImported(again);
		auto const second = importMapWithPolicy(target, again, policy);
		QVERIFY2(second.ok, qPrintable(second.error));
		QVERIFY(numbered(target, "102.1"));
		// The second 102 equals 102.1 except for the number, so it is renumbered once more
		QVERIFY(numbered(target, "102.2"));
	}

	/// Accept (4)
	void allSymbolsTest()
	{
		Map target, imported;
		setupTarget(target);
		auto const* old_101 = numbered(target, "101");
		auto const* old_102 = numbered(target, "102");
		setupImported(imported);
		addLine(imported, 199, imported.getColor(1), 1);  // not used by any object
		MapImportPolicy policy;
		policy.symbols = MapImportPolicy::AllSymbols;
		auto const result = importMapWithPolicy(target, imported, policy);
		QVERIFY2(result.ok, qPrintable(result.error));
		// The map's symbols stay as they are, with their objects.
		QCOMPARE(numbered(target, "101"), old_101);
		QCOMPARE(numbered(target, "102"), old_102);
		QCOMPARE(static_cast<const LineSymbol*>(old_102)->getLineWidth(), 1000);
		QCOMPARE(int(objectsOf(target, old_102).size()), 1);
		// Every symbol of the file comes; a taken number comes under the next free one, the equal 101 too.
		QCOMPARE(target.getNumSymbols(), 7);
		QCOMPARE(result.symbols_renumbered, 2);
		auto const* new_101 = numbered(target, "101.1");
		auto const* new_102 = numbered(target, "102.1");
		QVERIFY(new_101);
		QVERIFY(new_102);
		QCOMPARE(static_cast<const LineSymbol*>(new_102)->getLineWidth(), 2000);
		QCOMPARE(int(objectsOf(target, new_101).size()), 1);
		QCOMPARE(int(objectsOf(target, new_102).size()), 1);
		QVERIFY(numbered(target, "103"));
		QVERIFY(numbered(target, "104"));
		QVERIFY(numbered(target, "199"));
		// The file's colours are a block of their own below the map's, none shared, named as in the file.
		QCOMPARE(colorNames(target), (QStringList{ QStringLiteral("Black"), QStringLiteral("Black"), QStringLiteral("Blue") }));
		QCOMPARE(static_cast<const LineSymbol*>(old_101)->getColor(), target.getColor(0));
		QCOMPARE(static_cast<const LineSymbol*>(new_101)->getColor(), target.getColor(1));
		QCOMPARE(static_cast<const LineSymbol*>(numbered(target, "103"))->getColor(), target.getColor(2));
	}

	/// Accept (5)
	void colorsTest()
	{
		auto const black = QStringLiteral("Black"), blue = QStringLiteral("Blue"), green = QStringLiteral("Green"), red = QStringLiteral("Red");
		for (auto above : { true, false })
		{
			// All symbols and colours: the file's colours as a block, above or below the map's.
			Map target, imported;
			setupTarget(target);
			addColor(target, "Green", 0.5f, 0.0f);
			setupImported(imported);
			MapImportPolicy policy;
			policy.symbols = MapImportPolicy::AllSymbols;
			policy.colours_above = above;
			QVERIFY(importMapWithPolicy(target, imported, policy).ok);
			QCOMPARE(colorNames(target), above ? (QStringList{ black, blue, black, green }) : (QStringList{ black, green, black, blue }));
			for (int i = 0; i < target.getNumColors(); ++i)
				QCOMPARE(target.getColor(i)->getPriority(), i);
		}
		for (auto above : { true, false })
		{
			// The other choices: a colour equal to one of the map's is shared, and the others come at
			// their place in the file's order, whatever is said about above or below.
			Map target, imported;
			setupTarget(target);
			addColor(target, "Green", 0.5f, 0.0f);
			imported.setScaleDenominator(10000);
			auto* file_red = addColor(imported, "Red", 0.0f, 0.3f);
			auto* file_black = addColor(imported, "Black", 0.0f, 1.0f);
			auto* file_blue = addColor(imported, "Blue", 1.0f, 0.0f);
			addPath(imported, addLine(imported, 201, file_red, 1), { MapCoord(10, 10), MapCoord(20, 10) });
			addPath(imported, addLine(imported, 202, file_black, 1), { MapCoord(10, 20), MapCoord(20, 20) });
			addPath(imported, addLine(imported, 203, file_blue, 1), { MapCoord(10, 30), MapCoord(20, 30) });
			MapImportPolicy policy;
			policy.colours_above = above;
			QVERIFY(importMapWithPolicy(target, imported, policy).ok);
			QCOMPARE(colorNames(target), (QStringList{ red, black, green, blue }));
			for (int i = 0; i < target.getNumColors(); ++i)
				QCOMPARE(target.getColor(i)->getPriority(), i);
		}
	}

	/// Accept (6)
	void protectTest()
	{
		Map target, imported;
		setupTarget(target);
		setupImported(imported);
		MapImportPolicy policy;
		policy.protect_symbols = true;
		QVERIFY(importMapWithPolicy(target, imported, policy).ok);
		QVERIFY(numbered(target, "103")->isProtected());
		QVERIFY(numbered(target, "104")->isProtected());
		QVERIFY(!numbered(target, "101")->isProtected());
		QVERIFY(!numbered(target, "102")->isProtected());
	}

	/// Accept (7)
	void placementTest()
	{
		{
			Map target, imported;
			setupTarget(target);
			setupImported(imported);
			MapImportPolicy policy;
			policy.placement = MapImportPolicy::ByOffset;
			policy.offset_mm = QPointF(5.0, -3.0);
			QVERIFY(importMapWithPolicy(target, imported, policy).ok);
			auto const* object = objectsOf(target, numbered(target, "101")).front();
			auto const* path = static_cast<const PathObject*>(object);
			QCOMPARE(path->getCoordinate(0), MapCoord(15, 7));
			QCOMPARE(path->getCoordinate(1), MapCoord(25, 7));
		}
		{
			Map target, imported;
			setupTarget(target);
			setupImported(imported);
			MapImportPolicy policy;
			policy.placement = MapImportPolicy::AtPoint;
			policy.offset_mm = QPointF(100.0, 100.0);
			QVERIFY(importMapWithPolicy(target, imported, policy).ok);
			// The imported objects span (10, 10) to (20, 50): their centre (15, 30) goes to (100, 100)
			auto const* path = static_cast<const PathObject*>(objectsOf(target, numbered(target, "101")).front());
			QCOMPARE(path->getCoordinate(0), MapCoord(95, 80));
		}
		{
			Map target, imported;
			setupTarget(target);
			setupImported(imported);
			MapImportPolicy policy;  // by georeferencing: both maps are local and alike
			QVERIFY(importMapWithPolicy(target, imported, policy).ok);
			auto const* path = static_cast<const PathObject*>(objectsOf(target, numbered(target, "101")).front());
			QCOMPARE(path->getCoordinate(0), MapCoord(10, 10));
		}
	}

	/// Accept (8)
	void undoAndScaleTest()
	{
		Map target, imported;
		setupTarget(target);
		setupImported(imported, 5000);
		MapImportPolicy policy;
		auto const result = importMapWithPolicy(target, imported, policy);
		QVERIFY2(result.ok, qPrintable(result.error));
		QCOMPARE(target.getNumObjects(), 5);
		auto const* path = static_cast<const PathObject*>(objectsOf(target, numbered(target, "103")).front());
		QCOMPARE(path->getCoordinate(0), MapCoord(5, 15));
		QVERIFY(target.undoManager().undo());
		QCOMPARE(target.getNumObjects(), 1);

		Map empty;
		QVERIFY(!importMapWithPolicy(target, empty, policy).ok);
	}

	/// Accept (9)
	void dialogTest()
	{
		QSettings().remove(QStringLiteral("MapImport"));
		Map target, imported;
		setupTarget(target);
		setupImported(imported);
		{
			MapImportDialog dialog(imported, target);
			auto const policy = dialog.policy();
			QCOMPARE(policy.symbols, MapImportPolicy::AbsentSymbols);
			QCOMPARE(policy.colours_above, false);
			QCOMPARE(policy.protect_symbols, false);
			QCOMPARE(policy.placement, MapImportPolicy::ByGeoreferencing);
			QVERIFY(dialog.findChild<QLabel*>(QStringLiteral("summary_label"))->text().contains(QLatin1String("102")));

			// Above or below is a choice for all symbols and colours alone.
			QVERIFY(!child<QComboBox>(dialog, "colours_combo")->isEnabled());
			child<QRadioButton>(dialog, "renumber_radio")->setChecked(true);
			QVERIFY(!child<QComboBox>(dialog, "colours_combo")->isEnabled());
			child<QRadioButton>(dialog, "all_radio")->setChecked(true);
			QVERIFY(child<QComboBox>(dialog, "colours_combo")->isEnabled());
			child<QComboBox>(dialog, "colours_combo")->setCurrentIndex(0);  // above
			child<QCheckBox>(dialog, "protect_check")->setChecked(true);
			child<QRadioButton>(dialog, "offset_radio")->setChecked(true);
			child<QDoubleSpinBox>(dialog, "offset_x_spin")->setValue(5.0);
			child<QDoubleSpinBox>(dialog, "offset_y_spin")->setValue(-3.0);
			auto const chosen = dialog.policy();
			QCOMPARE(chosen.symbols, MapImportPolicy::AllSymbols);
			QCOMPARE(chosen.colours_above, true);
			QCOMPARE(chosen.protect_symbols, true);
			QCOMPARE(chosen.placement, MapImportPolicy::ByOffset);
			QCOMPARE(chosen.offset_mm, QPointF(5.0, -3.0));
			dialog.accept();
		}
		{
			// The choices of the last accepted import come back, except the placement
			MapImportDialog dialog(imported, target);
			auto const policy = dialog.policy();
			QCOMPARE(policy.symbols, MapImportPolicy::AllSymbols);
			QCOMPARE(policy.colours_above, true);
			QCOMPARE(policy.protect_symbols, true);
			QCOMPARE(policy.placement, MapImportPolicy::ByGeoreferencing);
		}
	}

	/// Seen on 2026-11-22: by mouse, the objects of a file of several parts
	/// went to new parts and arrived not selected.
	void byMouseSelectedTest()
	{
		Map target, imported;
		setupTarget(target);
		setupImported(imported);
		auto* second = new MapPart(QStringLiteral("Design"), &imported);
		imported.addPart(second, 1);
		imported.setCurrentPartIndex(1);
		addPath(imported, numbered(imported, "103"), { MapCoord(10, 60), MapCoord(20, 60) });
		imported.setCurrentPartIndex(0);
		QCOMPARE(imported.getNumParts(), 2);

		MapImportPolicy policy;
		policy.placement = MapImportPolicy::AtPoint;
		policy.offset_mm = QPointF(100.0, 100.0);
		auto const result = importMapWithPolicy(target, imported, policy);
		QVERIFY2(result.ok, qPrintable(result.error));
		QCOMPARE(target.getNumParts(), 1);
		QCOMPARE(target.getNumObjects(), 6);
		QCOMPARE(target.getNumSelectedObjects(), 5);
		QVERIFY(!target.isObjectSelected(target.getPart(0)->getObject(0)));
	}

	/// Seen on 2026-11-22: objects placed by georeferencing beyond the bounds
	/// of the file were saved and then dropped as irregular when opened again.
	void outOfBoundsTest()
	{
		{
			Map target, imported;
			setupTarget(target);
			setupImported(imported);
			auto georef = imported.getGeoreferencing();
			georef.setProjectedRefPoint(QPointF(2000000.0, 0.0));  // 2000 km: 200 m of paper at 1:10,000
			imported.setGeoreferencing(georef);
			MapImportPolicy policy;
			auto const result = importMapWithPolicy(target, imported, policy);
			QVERIFY(!result.ok);
			QVERIFY(!result.error.isEmpty());
			QCOMPARE(target.getNumObjects(), 1);
			QCOMPARE(target.getNumSymbols(), 2);
		}
		{
			Map target, imported;
			setupTarget(target);
			setupImported(imported);
			MapImportPolicy policy;
			policy.placement = MapImportPolicy::ByOffset;
			policy.offset_mm = QPointF(0.0, -150000.0);
			QVERIFY(!importMapWithPolicy(target, imported, policy).ok);
			policy.offset_mm = QPointF(0.0, -90000.0);
			QVERIFY(importMapWithPolicy(target, imported, policy).ok);
		}
	}

	/// Seen on 2026-11-24: the map's own symbols, which the symbol replacement had copied into the file's data,
	/// were scaled with the file's symbols and came back as smaller duplicates. Accept (10)
	void replacedSymbolsScaleTest()
	{
		Map target, imported;
		setupTarget(target);
		setupImported(imported, 5000);
		auto const file_symbols = mapSymbols(imported);
		QCOMPARE(int(file_symbols.size()), 4);
		// What OOM's "Assign new symbols" does: the file's objects of 101 and 102 take copies of the map's symbols.
		auto rules = SymbolRuleSet::forUsedSymbols(imported);
		rules.matchQuerySymbolNumber(target);
		rules.apply(imported, target);
		auto const copies = mapSymbolsAddedSince(imported, file_symbols);
		QCOMPARE(int(copies.size()), 2);
		MapImportPolicy policy;
		policy.symbols = MapImportPolicy::RenumberClashes;
		auto const result = importMapWithPolicy(target, imported, policy, copies);
		QVERIFY2(result.ok, qPrintable(result.error));
		QCOMPARE(result.symbols_renumbered, 0);
		QCOMPARE(target.getNumSymbols(), 4);
		QCOMPARE(countNumbered(target, "101"), 1);
		QCOMPARE(countNumbered(target, "102"), 1);
		QCOMPARE(static_cast<const LineSymbol*>(numbered(target, "101"))->getLineWidth(), 1000);
		QCOMPARE(static_cast<const LineSymbol*>(numbered(target, "102"))->getLineWidth(), 1000);
		QCOMPARE(int(objectsOf(target, numbered(target, "102")).size()), 2);
		// The file's own symbols are scaled with its objects, from 1:5,000 to 1:10,000.
		QCOMPARE(static_cast<const LineSymbol*>(numbered(target, "103"))->getLineWidth(), 500);
		auto const* path = static_cast<const PathObject*>(objectsOf(target, numbered(target, "103")).front());
		QCOMPARE(path->getCoordinate(0), MapCoord(5, 15));
	}

	/// Accept (11): under all symbols and colours, objects which were given the map's symbols before keep them.
	void replacedSymbolsAllTest()
	{
		Map target, imported;
		setupTarget(target);
		auto const* old_101 = numbered(target, "101");
		auto const* old_102 = numbered(target, "102");
		setupImported(imported);
		auto const file_symbols = mapSymbols(imported);
		auto rules = SymbolRuleSet::forUsedSymbols(imported);
		rules.matchQuerySymbolNumber(target);
		rules.apply(imported, target);
		MapImportPolicy policy;
		policy.symbols = MapImportPolicy::AllSymbols;
		auto const result = importMapWithPolicy(target, imported, policy, mapSymbolsAddedSince(imported, file_symbols));
		QVERIFY2(result.ok, qPrintable(result.error));
		QCOMPARE(numbered(target, "101"), old_101);
		QCOMPARE(numbered(target, "102"), old_102);
		QCOMPARE(int(objectsOf(target, old_101).size()), 1);
		QCOMPARE(int(objectsOf(target, old_102).size()), 2);
		// The file's own 101 and 102, which no object uses any longer, come under new numbers as its other symbols do.
		QCOMPARE(target.getNumSymbols(), 6);
		QCOMPARE(result.symbols_renumbered, 2);
		QCOMPARE(int(objectsOf(target, numbered(target, "102.1")).size()), 0);
		QCOMPARE(colorNames(target), (QStringList{ QStringLiteral("Black"), QStringLiteral("Black"), QStringLiteral("Blue") }));
	}

	/// Seen on 2026-11-22: the summary named clashing numbers twice.
	void summaryTest()
	{
		Map target, imported;
		setupTarget(target);
		setupImported(imported);
		addLine(imported, 102, imported.getColor(0), 3);  // a second 102 in the file
		MapImportDialog dialog(imported, target);
		auto const text = dialog.findChild<QLabel*>(QStringLiteral("summary_label"))->text();
		QVERIFY(text.contains(QLatin1String("5 symbols")));
		QCOMPARE(text.count(QLatin1String("102")), 1);
		QCOMPARE(text.count(QLatin1String("101")), 1);
	}
};


/*
 * We don't need a real GUI window.
 */
namespace  {
	auto Q_DECL_UNUSED qpa_selected = qputenv("QT_QPA_PLATFORM", "offscreen");  // clazy:exclude=non-pod-global-static
}


QTEST_MAIN(MapImportPolicyTest)
#include "map_import_policy_t.moc"  // IWYU pragma: keep
