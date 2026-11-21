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
#include <memory>
#include <vector>

#include <QtGlobal>
#include <QtTest>
#include <QObject>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QTransform>

#include "core/bookmarks.h"
#include "core/extents.h"
#include "core/georeferencing.h"
#include "core/map.h"
#include "core/map_coord.h"
#include "core/map_part.h"
#include "core/map_transform.h"
#include "core/objects/object.h"
#include "core/objects/text_object.h"
#include "core/symbols/line_symbol.h"
#include "core/symbols/point_symbol.h"
#include "core/symbols/text_symbol.h"
#include "course/course_data.h"
#include "templates/template.h"
#include "templates/template_image.h"
#include "undo/undo_manager.h"

using namespace OpenOrienteering;


namespace {

// the design notes, "Move, stretch and mirror", Accept.

/// A template image which is georeferenced from the start, without a file.
class GeoreferencedImage : public TemplateImage
{
public:
	GeoreferencedImage(const QString& path, Map* map) : TemplateImage(path, map) { is_georeferenced = true; }
};


/// Every coordinate of every object, in part and object order, and the rotations.
struct Snapshot
{
	std::vector<MapCoordF> coords;
	std::vector<int> flags;
	std::vector<qreal> rotations;
	std::vector<Extent> extents;
	std::vector<Bookmark> bookmarks;
	std::vector<MapCoordF> route;
	MapCoordF template_origin;
	MapCoordF georeferenced_origin;
};


struct TestMap
{
	Map map;
	LineSymbol* line = nullptr;
	PointSymbol* point = nullptr;
	TextSymbol* text = nullptr;
	PathObject* path = nullptr;
	PointObject* rotated = nullptr;
	TextObject* label = nullptr;
	PathObject* layout_path = nullptr;
	Template* image = nullptr;
	Template* georeferenced = nullptr;
	
	explicit TestMap(double grivation = 0.0)
	{
		Georeferencing georef;
		georef.setScaleDenominator(4000);
		georef.setProjectedRefPoint(QPointF(950000.0, 500000.0), false, false);
		georef.setMapRefPoint(MapCoord(0.0, 0.0));
		georef.setGrivation(grivation);
		map.setGeoreferencing(georef);
		
		line = new LineSymbol();
		line->setLineWidth(0.3);
		map.addSymbol(line, 0);
		point = new PointSymbol();
		point->setRotatable(true);
		map.addSymbol(point, 1);
		text = new TextSymbol();
		map.addSymbol(text, 2);
		
		// A path with a curve: the second coordinate starts a cubic.
		MapCoordVector coords = { MapCoord(10.0, 20.0), MapCoord(15.0, 20.0), MapCoord(20.0, 25.0), MapCoord(25.0, 30.0), MapCoord(40.0, -10.0) };
		coords[1].setCurveStart(true);
		coords[4].setDashPoint(true);
		path = new PathObject(line, coords);
		map.addObject(path);
		rotated = new PointObject(point);
		rotated->setPosition(MapCoord(-30.0, 12.0));
		rotated->setRotation(0.5);
		map.addObject(rotated);
		label = new TextObject(text);
		label->setAnchorPosition(MapCoord(5.0, -7.0));
		label->setText(QStringLiteral("Label"));
		map.addObject(label);
		
		auto* layout = new MapPart(QStringLiteral("Layout"), &map);
		layout->setRole(MapPart::Role::Layout);
		map.addPart(layout, 1);
		layout_path = new PathObject(line, MapCoordVector{ MapCoord(-100.0, -100.0), MapCoord(100.0, -100.0) });
		map.addObject(layout_path, 1);
		
		Extent extent;
		extent.name = QStringLiteral("East");
		extent.rect_mm = QRectF(0.0, 0.0, 50.0, 40.0);
		map.setExtents({ extent });
		Bookmark bookmark;
		bookmark.name = QStringLiteral("Bridge");
		bookmark.center = MapCoord(12.0, 8.0);
		map.setBookmarks({ bookmark });
		CourseData data;
		StoredRoute route;
		route.from = QStringLiteral("S1");
		route.to = QStringLiteral("31");
		route.points = { MapCoordF(1.0, 2.0), MapCoordF(30.0, 40.0) };
		route.via = { MapCoordF(15.0, 21.0) };
		data.routes.routes.push_back(route);
		map.setCourseData(data);
		
		auto plain = std::make_unique<TemplateImage>(QStringLiteral("plain.png"), &map);
		TemplateTransform placement;
		placement.template_x = 3000;
		placement.template_y = -4000;
		placement.template_rotation = 0.2;
		placement.template_scale_x = 0.5;
		placement.template_scale_y = 0.5;
		plain->setTransform(placement);
		image = plain.get();
		map.addTemplate(0, std::move(plain));
		auto fixed = std::make_unique<GeoreferencedImage>(QStringLiteral("fixed.tif"), &map);
		georeferenced = fixed.get();
		map.addTemplate(1, std::move(fixed));
		
		map.undoManager().clear();
	}
	
	Snapshot snapshot() const
	{
		Snapshot s;
		map.applyOnAllObjects([&s](const Object* object) {
			for (auto const& c : object->getRawCoordinateVector())
			{
				s.coords.emplace_back(c);
				s.flags.push_back(int(c.flags()));
			}
			s.rotations.push_back(object->getRotation());
		});
		s.extents = map.extents();
		s.bookmarks = map.bookmarks();
		auto const& route = map.courseData().routes.routes.front();
		s.route = route.points;
		s.route.insert(s.route.end(), route.via.begin(), route.via.end());
		s.template_origin = image->templateToMap(QPointF(10.0, 20.0));
		s.georeferenced_origin = georeferenced->templateToMap(QPointF(10.0, 20.0));
		return s;
	}
};

bool near(const MapCoordF& a, const MapCoordF& b, double tolerance = 0.001)
{
	return std::abs(a.x() - b.x()) <= tolerance && std::abs(a.y() - b.y()) <= tolerance;
}

}  // namespace



class MapTransformTest : public QObject
{
Q_OBJECT
	
private slots:
	void moveTest()
	{
		// Accept (1).
		TestMap t;
		auto const before = t.snapshot();
		auto const georef = t.map.getGeoreferencing();
		auto const transform = mapMoveTransform(t.map, 40.0, -20.0);
		QVERIFY(near(MapCoordF(transform.map(QPointF(0.0, 0.0))), MapCoordF(10.0, 5.0)));
		QVERIFY(transformMap(t.map, transform, MapTransformOptions()));
		auto const after = t.snapshot();
		QCOMPARE(after.coords.size(), before.coords.size());
		for (std::size_t i = 0; i < before.coords.size(); ++i)
			QVERIFY2(near(after.coords[i], before.coords[i] + MapCoordF(10.0, 5.0)), qPrintable(QString::number(i)));
		QCOMPARE(after.flags, before.flags);
		QCOMPARE(after.rotations, before.rotations);
		auto const& now = t.map.getGeoreferencing();
		QCOMPARE(now.getMapRefPoint(), georef.getMapRefPoint());
		QCOMPARE(now.getProjectedRefPoint(), georef.getProjectedRefPoint());
		QCOMPARE(now.getGrivation(), georef.getGrivation());
		QCOMPARE(now.getScaleDenominator(), georef.getScaleDenominator());
		QCOMPARE(after.extents.front().rect_mm, before.extents.front().rect_mm.translated(10.0, 5.0));
		QVERIFY(near(MapCoordF(after.bookmarks.front().center), MapCoordF(before.bookmarks.front().center) + MapCoordF(10.0, 5.0)));
		for (std::size_t i = 0; i < before.route.size(); ++i)
			QVERIFY(near(after.route[i], before.route[i] + MapCoordF(10.0, 5.0)));
		QVERIFY(near(after.template_origin, before.template_origin + MapCoordF(10.0, 5.0)));
		QVERIFY(near(after.georeferenced_origin, before.georeferenced_origin));
		
		// Grivation: the move follows grid north.
		TestMap turned(10.0);
		auto const& g = turned.map.getGeoreferencing();
		std::vector<QPointF> projected;
		turned.map.applyOnAllObjects([&](const Object* object) {
			for (auto const& c : object->getRawCoordinateVector())
				projected.push_back(g.toProjectedCoords(MapCoordF(c)));
		});
		QVERIFY(transformMap(turned.map, mapMoveTransform(turned.map, 40.0, -20.0), MapTransformOptions()));
		std::size_t i = 0;
		turned.map.applyOnAllObjects([&](const Object* object) {
			for (auto const& c : object->getRawCoordinateVector())
			{
				auto const moved = g.toProjectedCoords(MapCoordF(c)) - projected[i++];
				QVERIFY(std::abs(moved.x() - 40.0) < 0.01 && std::abs(moved.y() + 20.0) < 0.01);
			}
		});
	}
	
	
	void carryOffTest()
	{
		// Accept (2).
		TestMap t;
		auto const before = t.snapshot();
		auto const layout_before = t.layout_path->getRawCoordinateVector();
		MapTransformOptions options;
		options.carry = false;
		QVERIFY(transformMap(t.map, mapMoveTransform(t.map, 40.0, -20.0), options));
		auto const after = t.snapshot();
		QVERIFY(t.layout_path->getRawCoordinateVector() == layout_before);
		QVERIFY(near(MapCoordF(t.path->getRawCoordinateVector().front()), MapCoordF(20.0, 25.0)));
		QCOMPARE(after.extents.front().rect_mm, before.extents.front().rect_mm);
		QCOMPARE(after.bookmarks, before.bookmarks);
		QVERIFY(near(after.template_origin, before.template_origin));
		// Routes follow the objects whatever the option.
		QVERIFY(near(after.route.front(), before.route.front() + MapCoordF(10.0, 5.0)));
	}
	
	
	void stretchTest()
	{
		// Accept (3).
		TestMap t;
		auto const before = t.snapshot();
		auto const text_box = t.label->getBoxSize();
		QVERIFY(transformMap(t.map, mapStretchTransform(2.0, 0.5, false, false), MapTransformOptions()));
		auto const after = t.snapshot();
		for (std::size_t i = 0; i < before.coords.size(); ++i)
			QVERIFY(near(after.coords[i], MapCoordF(2.0 * before.coords[i].x(), 0.5 * before.coords[i].y())));
		QCOMPARE(after.rotations, before.rotations);
		QCOMPARE(t.rotated->getRotation(), 0.5);
		QVERIFY(near(MapCoordF(t.label->getAnchorCoordF()), MapCoordF(10.0, -3.5)));
		QCOMPARE(t.label->getBoxSize(), text_box);
		QCOMPARE(after.extents.front().rect_mm, QRectF(0.0, 0.0, 100.0, 20.0));
	}
	
	
	void mirrorTest()
	{
		// Accept (4).
		TestMap t;
		auto const before = t.snapshot();
		QVERIFY(transformMap(t.map, mapStretchTransform(1.0, 1.0, true, false), MapTransformOptions()));
		auto after = t.snapshot();
		for (std::size_t i = 0; i < before.coords.size(); ++i)
			QVERIFY(near(after.coords[i], MapCoordF(-before.coords[i].x(), before.coords[i].y())));
		QCOMPARE(after.flags, before.flags);
		QCOMPARE(after.rotations, before.rotations);
		QCOMPARE(after.extents.front().rect_mm, QRectF(-50.0, 0.0, 50.0, 40.0));
		
		TestMap v;
		QVERIFY(transformMap(v.map, mapStretchTransform(1.0, 1.0, false, true), MapTransformOptions()));
		after = v.snapshot();
		for (std::size_t i = 0; i < before.coords.size(); ++i)
			QVERIFY(near(after.coords[i], MapCoordF(before.coords[i].x(), -before.coords[i].y())));
		
		TestMap both;
		QVERIFY(transformMap(both.map, mapStretchTransform(1.0, 1.0, true, true), MapTransformOptions()));
		after = both.snapshot();
		for (std::size_t i = 0; i < before.coords.size(); ++i)
			QVERIFY(near(after.coords[i], MapCoordF(-before.coords[i].x(), -before.coords[i].y())));
	}
	
	
	void undoTest()
	{
		// Accept (5).
		for (int kind = 0; kind < 3; ++kind)
		{
			TestMap t;
			auto const before = t.snapshot();
			auto const transform = kind == 0 ? mapMoveTransform(t.map, 40.0, -20.0)
			                                 : mapStretchTransform(kind == 1 ? 2.0 : 1.0, kind == 1 ? 0.5 : 1.0, kind == 2, false);
			QVERIFY(transformMap(t.map, transform, MapTransformOptions()));
			auto const done = t.snapshot();
			QCOMPARE(t.map.undoManager().undoStepCount(), 1);
			QVERIFY(t.map.undoManager().undo());
			auto const undone = t.snapshot();
			QCOMPARE(undone.coords.size(), before.coords.size());
			for (std::size_t i = 0; i < before.coords.size(); ++i)
				QVERIFY(near(undone.coords[i], before.coords[i], 0.0));
			QCOMPARE(undone.flags, before.flags);
			QCOMPARE(undone.extents.front().rect_mm, before.extents.front().rect_mm);
			QCOMPARE(undone.bookmarks, before.bookmarks);
			for (std::size_t i = 0; i < before.route.size(); ++i)
				QVERIFY(near(undone.route[i], before.route[i], 0.0));
			QVERIFY(near(undone.template_origin, before.template_origin, 1e-6));
			QVERIFY(t.map.undoManager().redo());
			auto const redone = t.snapshot();
			for (std::size_t i = 0; i < done.coords.size(); ++i)
				QVERIFY(near(redone.coords[i], done.coords[i], 0.0));
			QCOMPARE(redone.extents.front().rect_mm, done.extents.front().rect_mm);
			QVERIFY(near(redone.template_origin, done.template_origin, 1e-6));
		}
	}
};


QTEST_MAIN(MapTransformTest)


#include "map_transform_t.moc"  // IWYU pragma: keep
