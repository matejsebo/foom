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

#include "global.h"
#include "core/map.h"
#include "core/map_color.h"
#include "core/map_coord.h"
#include "core/map_import_policy.h"
#include "core/map_part.h"
#include "core/objects/object.h"
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

int colorIndex(const Map& map, const char* name)
{
	for (int i = 0; i < map.getNumColors(); ++i)
	{
		if (map.getColor(i)->getName() == QLatin1String(name))
			return i;
	}
	return -1;
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
		auto const* old_102 = numbered(target, "102");
		auto const position = target.findSymbolIndex(old_102);
		setupImported(imported);
		addLine(imported, 199, imported.getColor(1), 1);  // not used by any object
		MapImportPolicy policy;
		policy.symbols = MapImportPolicy::AllSymbols;
		auto const result = importMapWithPolicy(target, imported, policy);
		QVERIFY2(result.ok, qPrintable(result.error));
		QCOMPARE(countNumbered(target, "102"), 1);
		auto const* new_102 = numbered(target, "102");
		QCOMPARE(static_cast<const LineSymbol*>(new_102)->getLineWidth(), 2000);
		QCOMPARE(target.findSymbolIndex(new_102), position);
		QCOMPARE(int(objectsOf(target, new_102).size()), 2);  // the target's object too
		QVERIFY(numbered(target, "199"));
		QCOMPARE(target.getNumSymbols(), 5);
		QCOMPARE(result.symbols_replaced, 1);
	}

	/// Accept (5)
	void colorsTest()
	{
		for (auto above : { true, false })
		{
			Map target, imported;
			setupTarget(target);
			addColor(target, "Green", 0.5f, 0.0f);
			setupImported(imported);
			MapImportPolicy policy;
			policy.colours_above = above;
			QVERIFY(importMapWithPolicy(target, imported, policy).ok);
			QCOMPARE(target.getNumColors(), 3);
			QCOMPARE(colorIndex(target, "Blue"), above ? 0 : 2);
			QCOMPARE(colorIndex(target, "Black"), above ? 1 : 0);
			QCOMPARE(colorIndex(target, "Green"), above ? 2 : 1);
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

			child<QRadioButton>(dialog, "renumber_radio")->setChecked(true);
			child<QComboBox>(dialog, "colours_combo")->setCurrentIndex(0);  // above
			child<QCheckBox>(dialog, "protect_check")->setChecked(true);
			child<QRadioButton>(dialog, "offset_radio")->setChecked(true);
			child<QDoubleSpinBox>(dialog, "offset_x_spin")->setValue(5.0);
			child<QDoubleSpinBox>(dialog, "offset_y_spin")->setValue(-3.0);
			auto const chosen = dialog.policy();
			QCOMPARE(chosen.symbols, MapImportPolicy::RenumberClashes);
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
			QCOMPARE(policy.symbols, MapImportPolicy::RenumberClashes);
			QCOMPARE(policy.colours_above, true);
			QCOMPARE(policy.protect_symbols, true);
			QCOMPARE(policy.placement, MapImportPolicy::ByGeoreferencing);
		}
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
