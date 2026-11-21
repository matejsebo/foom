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
#include <QFile>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QTransform>
#include <QtMath>

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
	
	
	void fitTest()
	{
		// the design notes, "Affine by point pairs", Accept (1).
		auto const maps = [](const QTransform& t, const MapCoordF& from, const MapCoordF& to, double tolerance) {
			return near(MapCoordF(t.map(QPointF(from))), to, tolerance);
		};
		auto fit = fitPointPairs({ { MapCoordF(0.0, 0.0), MapCoordF(3.0, -4.0) } });
		QVERIFY(fit.ok);
		QVERIFY(fit.transform.type() <= QTransform::TxTranslate);
		QVERIFY(maps(fit.transform, MapCoordF(50.0, 60.0), MapCoordF(53.0, 56.0), 1e-9));
		
		std::vector<PointPair> pairs = {
		    { MapCoordF(10.0, 20.0), MapCoordF(14.0, 18.0) },
		    { MapCoordF(110.0, -30.0), MapCoordF(118.5, -41.0) },
		};
		fit = fitPointPairs(pairs);
		QVERIFY(fit.ok);
		for (auto const& pair : pairs)
			QVERIFY(maps(fit.transform, pair.map, pair.reference, 1e-9));
		// A similarity: equal scales, no shear.
		QVERIFY(std::abs(fit.transform.m11() - fit.transform.m22()) < 1e-12);
		QVERIFY(std::abs(fit.transform.m12() + fit.transform.m21()) < 1e-12);
		QCOMPARE(int(fit.residuals.size()), 2);
		
		pairs.push_back({ MapCoordF(40.0, 70.0), MapCoordF(41.0, 80.0) });
		fit = fitPointPairs(pairs);
		QVERIFY(fit.ok);
		for (auto const& pair : pairs)
			QVERIFY(maps(fit.transform, pair.map, pair.reference, 1e-9));
		QVERIFY(std::abs(fit.transform.m11() - fit.transform.m22()) > 1e-3);  // not a similarity
		
		pairs.push_back({ MapCoordF(-20.0, -50.0), MapCoordF(-25.0, -47.0) });
		fit = fitPointPairs(pairs);
		QVERIFY(fit.ok);
		// The least-squares solution: the normal equations, solved here by Cramer's rule.
		double sxx = 0, sxy = 0, syy = 0, sx = 0, sy = 0, n = 0;
		double ux = 0, uy = 0, u = 0, vx = 0, vy = 0, v = 0;
		for (auto const& pair : pairs)
		{
			auto const x = pair.map.x(), y = pair.map.y();
			sxx += x * x; sxy += x * y; syy += y * y; sx += x; sy += y; n += 1;
			ux += x * pair.reference.x(); uy += y * pair.reference.x(); u += pair.reference.x();
			vx += x * pair.reference.y(); vy += y * pair.reference.y(); v += pair.reference.y();
		}
		auto const det3 = [](double a, double b, double c, double d, double e, double f, double g, double h, double i) {
			return a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
		};
		auto const d = det3(sxx, sxy, sx, sxy, syy, sy, sx, sy, n);
		auto const a = det3(ux, sxy, sx, uy, syy, sy, u, sy, n) / d;
		auto const b = det3(sxx, ux, sx, sxy, uy, sy, sx, u, n) / d;
		auto const c = det3(sxx, sxy, ux, sxy, syy, uy, sx, sy, u) / d;
		auto const e = det3(vx, sxy, sx, vy, syy, sy, v, sy, n) / d;
		auto const f = det3(sxx, vx, sx, sxy, vy, sy, sx, v, n) / d;
		auto const g = det3(sxx, sxy, vx, sxy, syy, vy, sx, sy, v) / d;
		QVERIFY(std::abs(fit.transform.m11() - a) < 1e-9);
		QVERIFY(std::abs(fit.transform.m21() - b) < 1e-9);
		QVERIFY(std::abs(fit.transform.dx() - c) < 1e-9);
		QVERIFY(std::abs(fit.transform.m12() - e) < 1e-9);
		QVERIFY(std::abs(fit.transform.m22() - f) < 1e-9);
		QVERIFY(std::abs(fit.transform.dy() - g) < 1e-9);
		QCOMPARE(int(fit.residuals.size()), 4);
		for (std::size_t i = 0; i < pairs.size(); ++i)
		{
			auto const p = fit.transform.map(QPointF(pairs[i].map));
			QVERIFY(std::abs(fit.residuals[i] - std::hypot(p.x() - pairs[i].reference.x(), p.y() - pairs[i].reference.y())) < 1e-9);
		}
		QVERIFY(fit.residuals[3] > 0.01);
		
		QVERIFY(!fitPointPairs({}).ok);
		QVERIFY(!fitPointPairs({ { MapCoordF(1.0, 1.0), MapCoordF(2.0, 2.0) }, { MapCoordF(1.0, 1.0), MapCoordF(5.0, 2.0) } }).ok);
		QVERIFY(!fitPointPairs({ { MapCoordF(0.0, 0.0), MapCoordF(0.0, 1.0) }, { MapCoordF(1.0, 1.0), MapCoordF(1.0, 2.0) }, { MapCoordF(2.0, 2.0), MapCoordF(2.0, 4.0) } }).ok);
	}
	
	
	void orientationTest()
	{
		// Accept (2).
		auto const angle = qDegreesToRadians(30.0);
		// The similarity of two pairs which turns by 30 degrees counter-clockwise on paper (y grows south).
		auto const turn = [angle](const MapCoordF& p) {
			return MapCoordF(std::cos(angle) * p.x() + std::sin(angle) * p.y(), -std::sin(angle) * p.x() + std::cos(angle) * p.y());
		};
		auto const fit = fitPointPairs({ { MapCoordF(0.0, 0.0), turn(MapCoordF(0.0, 0.0)) }, { MapCoordF(100.0, 0.0), turn(MapCoordF(100.0, 0.0)) } });
		QVERIFY(fit.ok);
		
		TestMap t;
		auto* fixed_symbol = new PointSymbol();
		fixed_symbol->setRotatable(false);
		t.map.addSymbol(fixed_symbol, t.map.getNumSymbols());
		auto* fixed = new PointObject(fixed_symbol);
		fixed->setPosition(MapCoord(1.0, 1.0));
		t.map.addObject(fixed);
		MapTransformOptions options;
		options.rotate_orientations = true;
		QVERIFY(transformMap(t.map, fit.transform, options));
		QVERIFY(std::abs(t.rotated->getRotation() - (0.5 + angle)) < 1e-9);
		QCOMPARE(fixed->getRotation(), 0.0);
		QVERIFY(near(MapCoordF(t.rotated->getCoordF()), turn(MapCoordF(-30.0, 12.0))));
		
		TestMap plain;
		QVERIFY(transformMap(plain.map, fit.transform, MapTransformOptions()));
		QCOMPARE(plain.rotated->getRotation(), 0.5);
	}
	
	
	void rubbersheetFieldTest()
	{
		// the design notes, "Rubbersheeting", Accept (1).
		RubbersheetParams params;
		params.perimeter = { { MapCoordF(-1000, -1000), MapCoordF(1000, -1000), MapCoordF(1000, 1000), MapCoordF(-1000, 1000) } };
		params.pairs = { { MapCoordF(3.0, 4.0), MapCoordF(5.0, 1.0) } };
		for (auto const& p : { MapCoordF(0, 0), MapCoordF(3, 4), MapCoordF(-200, 500) })
			QVERIFY(near(rubbersheetDisplacement(params, p), MapCoordF(2.0, -3.0), 1e-9));
		
		params.pairs = { { MapCoordF(0, 0), MapCoordF(0, 0) }, { MapCoordF(100, 0), MapCoordF(110, 0) } };
		QVERIFY(near(rubbersheetDisplacement(params, MapCoordF(0, 0)), MapCoordF(0, 0), 1e-9));
		QVERIFY(near(rubbersheetDisplacement(params, MapCoordF(100, 0)), MapCoordF(10, 0), 1e-9));
		QVERIFY(near(rubbersheetDisplacement(params, MapCoordF(50, 0)), MapCoordF(10.0 * 2500 / (2500 + 3000), 0), 1e-9));
	}
	
	
	void rubbersheetPerimeterTest()
	{
		// Accept (2).
		RubbersheetParams params;
		params.perimeter = { { MapCoordF(-10, -10), MapCoordF(10, -10), MapCoordF(10, 10), MapCoordF(-10, 10) } };
		params.pairs = { { MapCoordF(0, 0), MapCoordF(2, 0) } };
		
		TestMap t;
		auto* crossing = new PathObject(t.line, MapCoordVector{ MapCoord(0.0, 0.0), MapCoord(20.0, 0.0) });
		t.map.addObject(crossing);
		QVERIFY(rubbersheetMap(t.map, params));
		QVERIFY(near(MapCoordF(crossing->getRawCoordinateVector()[0]), MapCoordF(2, 0)));
		QVERIFY(near(MapCoordF(crossing->getRawCoordinateVector()[1]), MapCoordF(20, 0)));
		
		params.decrease_near_perimeter = true;
		auto const w = 1 / (5 * std::sqrt(29.0));
		QVERIFY(near(rubbersheetDisplacement(params, MapCoordF(0, 5)), MapCoordF(2 * w / (w + 4.0 / 25), 0), 1e-9));
		QVERIFY(near(rubbersheetDisplacement(params, MapCoordF(10, 0)), MapCoordF(0, 0), 1e-9));
		QVERIFY(near(rubbersheetDisplacement(params, MapCoordF(0, 0)), MapCoordF(2, 0), 1e-9));
	}
	
	
	void rubbersheetObjectsTest()
	{
		// Accept (3).
		TestMap t;
		auto* protected_symbol = new LineSymbol();
		protected_symbol->setLineWidth(0.2);
		protected_symbol->setProtected(true);
		t.map.addSymbol(protected_symbol, t.map.getNumSymbols());
		auto* guarded = new PathObject(protected_symbol, MapCoordVector{ MapCoord(1.0, 1.0), MapCoord(2.0, 2.0) });
		t.map.addObject(guarded);
		
		RubbersheetParams params;
		params.perimeter = { { MapCoordF(-500, -500), MapCoordF(500, -500), MapCoordF(500, 500), MapCoordF(-500, 500) } };
		params.pairs = { { MapCoordF(0, 0), MapCoordF(4, 0) }, { MapCoordF(60, 60), MapCoordF(60, 60) }, { MapCoordF(-60, 30), MapCoordF(-60, 33) } };
		auto const path_before = t.path->getRawCoordinateVector();
		auto const point_before = MapCoordF(t.rotated->getCoordF());
		auto const anchor_before = MapCoordF(t.label->getAnchorCoordF());
		auto const box = t.label->getBoxSize();
		auto const guarded_before = guarded->getRawCoordinateVector();
		auto const layout_before = t.layout_path->getRawCoordinateVector();
		QVERIFY(rubbersheetMap(t.map, params));
		// The path's handles 2 and 3 belong to the vertices 1 and 4.
		std::size_t const vertex[] = { 0, 1, 1, 4, 4 };
		for (std::size_t i = 0; i < path_before.size(); ++i)
		{
			auto const at = MapCoordF(path_before[vertex[i]]);
			QVERIFY(near(MapCoordF(t.path->getRawCoordinateVector()[i]), MapCoordF(path_before[i]) + rubbersheetDisplacement(params, at)));
		}
		QVERIFY(near(MapCoordF(t.rotated->getCoordF()), point_before + rubbersheetDisplacement(params, point_before)));
		QVERIFY(near(MapCoordF(t.label->getAnchorCoordF()), anchor_before + rubbersheetDisplacement(params, anchor_before)));
		QCOMPARE(t.label->getBoxSize(), box);
		QVERIFY(guarded->getRawCoordinateVector() == guarded_before);
		QVERIFY(t.layout_path->getRawCoordinateVector() == layout_before);
		
		TestMap all;
		auto* symbol = new LineSymbol();
		symbol->setProtected(true);
		all.map.addSymbol(symbol, all.map.getNumSymbols());
		auto* other = new PathObject(symbol, MapCoordVector{ MapCoord(1.0, 1.0), MapCoord(2.0, 2.0) });
		all.map.addObject(other);
		params.include_protected = true;
		params.include_layout = true;
		auto const layout_all = all.layout_path->getRawCoordinateVector();
		QVERIFY(rubbersheetMap(all.map, params));
		QVERIFY(other->getRawCoordinateVector() != guarded_before);
		QVERIFY(all.layout_path->getRawCoordinateVector() != layout_all);
	}
	
	
	void rubbersheetFilesTest()
	{
		// Accept (4).
		TestMap t;
		QTemporaryDir dir;
		std::vector<PointPair> pairs = { { MapCoordF(12.5, -3.25), MapCoordF(14.0, -2.0) }, { MapCoordF(-40.0, 22.0), MapCoordF(-40.0, 22.0) } };
		std::vector<std::vector<MapCoordF>> perimeter = { { MapCoordF(-50, -50), MapCoordF(50, -50), MapCoordF(50, 50), MapCoordF(-50, 50) } };
		auto const points_path = dir.filePath(QStringLiteral("points.txt"));
		auto const perimeter_path = dir.filePath(QStringLiteral("perimeter.txt"));
		QString error;
		QVERIFY2(writeRubbersheetPoints(points_path, t.map, pairs, &error), qPrintable(error));
		QVERIFY2(writeRubbersheetPerimeter(perimeter_path, t.map, perimeter, &error), qPrintable(error));
		// 0.005 m is 0.00125 mm at 1:4000.
		auto const read_pairs = readRubbersheetPoints(points_path, t.map, &error);
		QCOMPARE(read_pairs.size(), pairs.size());
		for (std::size_t i = 0; i < pairs.size(); ++i)
		{
			QVERIFY(near(read_pairs[i].map, pairs[i].map, 0.00125));
			QVERIFY(near(read_pairs[i].reference, pairs[i].reference, 0.00125));
		}
		auto const read_perimeter = readRubbersheetPerimeter(perimeter_path, t.map, &error);
		QCOMPARE(read_perimeter.size(), perimeter.size());
		QCOMPARE(read_perimeter.front().size(), perimeter.front().size());
		for (std::size_t i = 0; i < perimeter.front().size(); ++i)
			QVERIFY(near(read_perimeter.front()[i], perimeter.front()[i], 0.00125));
		
		QFile file(points_path);
		QVERIFY(file.open(QIODevice::ReadOnly));
		auto const lines = QString::fromUtf8(file.readAll()).split(QStringLiteral("\r\n"));
		file.close();
		QVERIFY(lines.size() >= 5);
		QCOMPARE(lines[1], QStringLiteral("m"));
		QCOMPARE(lines[2], QStringLiteral("2"));
		QCOMPARE(lines[3].split(QLatin1Char(';')).size(), 6);
		
		// A pair which is not used.
		auto text = lines;
		auto fields = text[4].split(QLatin1Char(';'));
		fields[4] = QStringLiteral("0");
		text[4] = fields.join(QLatin1Char(';'));
		QVERIFY(file.open(QIODevice::WriteOnly));
		file.write(text.join(QStringLiteral("\r\n")).toUtf8());
		file.close();
		auto const left_out = readRubbersheetPoints(points_path, t.map, &error);
		QCOMPARE(left_out.size(), std::size_t(2));
		QVERIFY(left_out[0].used);
		QVERIFY(!left_out[1].used);
	}
	
	
	void rubbersheetUndoTest()
	{
		// Accept (5).
		TestMap t;
		auto const before = t.snapshot();
		RubbersheetParams params;
		params.perimeter = { { MapCoordF(-500, -500), MapCoordF(500, -500), MapCoordF(500, 500), MapCoordF(-500, 500) } };
		params.pairs = { { MapCoordF(0, 0), MapCoordF(4, 0) }, { MapCoordF(60, 60), MapCoordF(60, 60) } };
		QVERIFY(rubbersheetMap(t.map, params));
		QCOMPARE(t.map.undoManager().undoStepCount(), 1);
		QVERIFY(!near(t.snapshot().route.front(), before.route.front(), 0.001));
		QVERIFY(t.map.undoManager().undo());
		auto const undone = t.snapshot();
		for (std::size_t i = 0; i < before.coords.size(); ++i)
			QVERIFY(near(undone.coords[i], before.coords[i], 0.0));
		for (std::size_t i = 0; i < before.route.size(); ++i)
			QVERIFY(near(undone.route[i], before.route[i], 0.0));
	}
	
	
	void localFieldTest()
	{
		// the design notes, "Local transformation", Accept (1).
		LocalWarpParams params;
		params.center = MapCoordF(0, 0);
		params.target = MapCoordF(10, 0);
		params.radius = 100;
		QVERIFY(near(localWarpDisplacement(params, MapCoordF(0, 0)), MapCoordF(10, 0), 1e-9));
		QVERIFY(near(localWarpDisplacement(params, MapCoordF(50, 0)), MapCoordF(5, 0), 1e-9));
		QVERIFY(near(localWarpDisplacement(params, MapCoordF(0, 50)), MapCoordF(5, 0), 1e-9));
		QVERIFY(near(localWarpDisplacement(params, MapCoordF(100, 0)), MapCoordF(0, 0), 1e-9));
		QVERIFY(near(localWarpDisplacement(params, MapCoordF(0, -250)), MapCoordF(0, 0), 1e-9));
	}
	
	
	void localMapTest()
	{
		// Accept (2).
		TestMap t;
		MapCoordVector coords = { MapCoord(0.0, 0.0), MapCoord(80.0, 0.0), MapCoord(95.0, 10.0), MapCoord(98.0, -10.0), MapCoord(150.0, 0.0) };
		coords[1].setCurveStart(true);
		auto* curve = new PathObject(t.line, coords);
		t.map.addObject(curve);
		auto const index = t.map.getCurrentPart()->findObjectIndex(curve);
		LocalWarpParams params;
		params.center = MapCoordF(0, 0);
		params.target = MapCoordF(10, 0);
		params.radius = 100;
		auto const steps = t.map.undoManager().undoStepCount();
		QVERIFY(localWarpMap(t.map, params));
		auto const& after = curve->getRawCoordinateVector();
		QVERIFY(near(MapCoordF(after[0]), MapCoordF(10, 0)));
		QVERIFY(near(MapCoordF(after[1]), MapCoordF(82, 0)));
		QVERIFY(near(MapCoordF(after[2]), MapCoordF(97, 10)));
		QVERIFY(near(MapCoordF(after[3]), MapCoordF(98, -10)));
		QVERIFY(near(MapCoordF(after[4]), MapCoordF(150, 0)));
		QVERIFY(after[1].isCurveStart());
		QCOMPARE(t.map.undoManager().undoStepCount(), steps + 1);
		QVERIFY(t.map.undoManager().undo());
		// Undo puts the stored copy in the object's place.
		QVERIFY(t.map.getCurrentPart()->getObject(index)->getRawCoordinateVector() == coords);
		
		// The release must lie within the radius.
		params.target = MapCoordF(120, 0);
		QVERIFY(!localWarpMap(t.map, params));
	}
	
	
	void reprojectTest()
	{
		// the design notes, "Change of coordinate system", Accept (1) to (4).
		TestMap t;
		Georeferencing planar;
		planar.setScaleDenominator(4000);
		QVERIFY(planar.setProjectedCRS(QStringLiteral("EPSG"), QStringLiteral("EPSG:26954"), { QStringLiteral("26954") }));
		planar.setMapRefPoint(MapCoord(0.0, 0.0));
		planar.setProjectedRefPoint(QPointF(955000.0, 525000.0));
		t.map.setGeoreferencing(planar);
		auto const& before = t.map.getGeoreferencing();
		auto const grivation = before.getGrivation();
		
		auto* a = new PathObject(t.line, MapCoordVector{ MapCoord(before.toMapCoordF(QPointF(955400.0, 525200.0))), MapCoord(before.toMapCoordF(QPointF(954000.0, 526000.0))) });
		t.map.addObject(a);
		auto* fixed_symbol = new PointSymbol();
		fixed_symbol->setRotatable(false);
		t.map.addSymbol(fixed_symbol, t.map.getNumSymbols());
		auto* fixed = new PointObject(fixed_symbol);
		fixed->setPosition(MapCoord(10.0, 10.0));
		t.map.addObject(fixed);
		auto const index = t.map.getCurrentPart()->findObjectIndex(a);
		auto const coords_before = a->getRawCoordinateVector();
		auto const extent_before = t.map.extents().front().rect_mm;
		t.map.undoManager().clear();
		
		QVERIFY(reprojectMap(t.map, QStringLiteral("EPSG"), QStringLiteral("EPSG:26913"), { QStringLiteral("26913") }));
		auto const& after = t.map.getGeoreferencing();
		QCOMPARE(after.getProjectedCRSSpec(), QStringLiteral("EPSG:26913"));
		auto const ref = after.getProjectedRefPoint();
		QVERIFY2(std::abs(ref.x() - 497787.34) < 0.01 && std::abs(ref.y() - 4407329.66) < 0.01, qPrintable(QStringLiteral("%1 %2").arg(ref.x(), 0, 'f', 3).arg(ref.y(), 0, 'f', 3)));
		QCOMPARE(after.getScaleDenominator(), 4000u);
		QCOMPARE(after.getGrivation(), grivation);
		
		auto const& moved = a->getRawCoordinateVector();
		QVERIFY(near(MapCoordF(moved[0]), after.toMapCoordF(QPointF(498188.27, 4407527.37)), 0.0025));
		QVERIFY(near(MapCoordF(moved[1]), after.toMapCoordF(QPointF(496793.28, 4408334.74)), 0.0025));
		
		auto const turned = qRadiansToDegrees(t.rotated->getRotation() - 0.5);
		QVERIFY2(turned < -0.25 && turned > -0.4, qPrintable(QString::number(turned)));
		QCOMPARE(fixed->getRotation(), 0.0);
		QVERIFY(t.map.extents().front().rect_mm != extent_before);
		
		QCOMPARE(t.map.undoManager().undoStepCount(), 1);
		QVERIFY(t.map.undoManager().undo());
		QCOMPARE(t.map.getGeoreferencing().getProjectedCRSSpec(), QStringLiteral("EPSG:26954"));
		QCOMPARE(t.map.getGeoreferencing().getProjectedRefPoint(), QPointF(955000.0, 525000.0));
		QVERIFY(t.map.getCurrentPart()->getObject(index)->getRawCoordinateVector() == coords_before);
		QCOMPARE(t.map.extents().front().rect_mm, extent_before);
		
		QVERIFY(!reprojectMap(t.map, QStringLiteral("PROJ.4"), QStringLiteral("+proj=nonsense"), {}));
	}
	
	void magneticNorthTest()
	{
		// the design notes, "Rotation to magnetic north and centring", Accept (1) to (4).
		TestMap t;
		auto georef = t.map.getGeoreferencing();
		georef.setMapRefPoint(MapCoord(10.0, 20.0));
		t.map.setGeoreferencing(georef);
		auto* a = new PathObject(t.line, MapCoordVector{ MapCoord(60.0, 20.0), MapCoord(-35.0, 48.0) });
		t.map.addObject(a);
		auto* fixed_symbol = new PointSymbol();
		fixed_symbol->setRotatable(true);
		t.map.addSymbol(fixed_symbol, t.map.getNumSymbols());
		auto* fixed = new PointObject(fixed_symbol);
		fixed->setPosition(MapCoord(10.0, 10.0));
		fixed->setRotation(0.25);
		t.map.addObject(fixed);
		t.map.undoManager().clear();
		
		Georeferencing const old(t.map.getGeoreferencing());
		QCOMPARE(old.getDeclination(), 0.0);
		auto const before = t.snapshot();
		auto const index = t.map.getCurrentPart()->findObjectIndex(a);
		auto const coords_before = a->getRawCoordinateVector();
		auto const rotated_before = old.toProjectedCoords(t.rotated->getCoordF());
		auto const layout_before = t.layout_path->getRawCoordinateVector();
		auto const extent_before = t.map.extents().front().rect_mm;
		
		MapTransformOptions options;
		options.fixed_symbols = { fixed_symbol };
		options.keep_layout = true;
		QVERIFY(rotateMapToMagneticNorth(t.map, 3.3, options));
		auto const& after = t.map.getGeoreferencing();
		QCOMPARE(after.getDeclination(), 3.3);
		QCOMPARE(after.getGrivation(), 3.3);
		QCOMPARE(after.getMapRefPoint(), MapCoord(10.0, 20.0));
		
		// Counter-clockwise on screen, about the reference point.
		auto const angle = qDegreesToRadians(3.3);
		auto const& moved = a->getRawCoordinateVector();
		QVERIFY(near(MapCoordF(moved[0]), MapCoordF(10.0 + 50.0 * std::cos(angle), 20.0 - 50.0 * std::sin(angle))));
		
		// The real world kept.
		for (std::size_t i = 0; i < moved.size(); ++i)
		{
			auto const p = old.toProjectedCoords(MapCoordF(coords_before[i]));
			auto const q = after.toProjectedCoords(MapCoordF(moved[i]));
			QVERIFY2(std::abs(p.x() - q.x()) < 0.01 && std::abs(p.y() - q.y()) < 0.01, qPrintable(QStringLiteral("%1 %2").arg(q.x() - p.x()).arg(q.y() - p.y())));
		}
		auto const rotated_after = after.toProjectedCoords(t.rotated->getCoordF());
		QVERIFY(std::abs(rotated_after.x() - rotated_before.x()) < 0.01 && std::abs(rotated_after.y() - rotated_before.y()) < 0.01);
		
		// Orientation and options.
		QVERIFY(std::abs(t.rotated->getRotation() - (0.5 + angle)) < 1e-9);
		QCOMPARE(fixed->getCoord(), MapCoord(10.0, 10.0));
		QCOMPARE(fixed->getRotation(), 0.25);
		QVERIFY(t.layout_path->getRawCoordinateVector() == layout_before);
		auto const turned = MapCoordF(10.0, 20.0) + MapCoordF((before.template_origin.x() - 10.0) * std::cos(angle) + (before.template_origin.y() - 20.0) * std::sin(angle),
		                                                     -(before.template_origin.x() - 10.0) * std::sin(angle) + (before.template_origin.y() - 20.0) * std::cos(angle));
		QVERIFY(near(t.snapshot().template_origin, turned));
		QVERIFY(near(t.snapshot().georeferenced_origin, before.georeferenced_origin));
		QVERIFY(t.map.extents().front().rect_mm != extent_before);
		
		// Undo.
		QCOMPARE(t.map.undoManager().undoStepCount(), 1);
		QVERIFY(t.map.undoManager().undo());
		QCOMPARE(t.map.getGeoreferencing().getDeclination(), 0.0);
		QCOMPARE(t.map.getGeoreferencing().getGrivation(), 0.0);
		QVERIFY(t.map.getCurrentPart()->getObject(index)->getRawCoordinateVector() == coords_before);
		QCOMPARE(t.map.extents().front().rect_mm, extent_before);
		
		// The layout turns when it is not kept.
		options.keep_layout = false;
		QVERIFY(rotateMapToMagneticNorth(t.map, 3.3, options));
		QVERIFY(t.layout_path->getRawCoordinateVector() != layout_before);
	}
	
	void centerTest()
	{
		// the design notes, "Rotation to magnetic north and centring", Accept (5) and (6).
		{
			Map m;
			Georeferencing g;
			g.setScaleDenominator(4000);
			g.setProjectedRefPoint(QPointF(955000.0, 525000.0), false, false);
			g.setMapRefPoint(MapCoord(0.0, 0.0));
			g.setGrivation(3.3);
			m.setGeoreferencing(g);
			auto* symbol = new LineSymbol();
			m.addSymbol(symbol, 0);
			auto const c = m.getGeoreferencing().toMapCoordF(QPointF(957590.0, 528578.0));
			m.addObject(new PathObject(symbol, MapCoordVector{ MapCoord(c.x() - 5.0, c.y() - 5.0), MapCoord(c.x() + 5.0, c.y() + 5.0) }));
			QCOMPARE(proposedMapOffset(m), QPointF(958000.0, 529000.0));
			auto const offset = mapOffset(m);
			QVERIFY(std::abs(offset.x() - 955000.0) < 0.001 && std::abs(offset.y() - 525000.0) < 0.001);
		}
		
		TestMap t(3.3);
		Georeferencing const old(t.map.getGeoreferencing());
		auto const before = t.snapshot();
		auto const path_before = t.path->getRawCoordinateVector();
		auto const layout_before = t.layout_path->getRawCoordinateVector();
		
		QVERIFY(centerMap(t.map, QPointF(953000.0, 504000.0)));
		auto const offset = mapOffset(t.map);
		QVERIFY2(std::abs(offset.x() - 953000.0) < 0.004 && std::abs(offset.y() - 504000.0) < 0.004, qPrintable(QStringLiteral("%1 %2").arg(offset.x(), 0, 'f', 4).arg(offset.y(), 0, 'f', 4)));
		auto const& georef = t.map.getGeoreferencing();
		QCOMPARE(georef.getProjectedRefPoint(), old.getProjectedRefPoint());
		QCOMPARE(georef.getGrivation(), 3.3);
		
		auto const after = t.snapshot();
		QCOMPARE(after.coords.size(), before.coords.size());
		auto const shift = after.coords.front() - before.coords.front();
		QVERIFY2(near(shift, MapCoordF(-691.19, 1041.52), 0.01), qPrintable(QStringLiteral("%1 %2").arg(shift.x()).arg(shift.y())));
		for (std::size_t i = 0; i < after.coords.size(); ++i)
		{
			auto const p = old.toProjectedCoords(before.coords[i]);
			auto const q = georef.toProjectedCoords(after.coords[i]);
			QVERIFY(std::abs(p.x() - q.x()) < 0.01 && std::abs(p.y() - q.y()) < 0.01);
		}
		QVERIFY(near(MapCoordF(t.layout_path->getRawCoordinateVector().front()), MapCoordF(layout_before.front()) + shift));
		QVERIFY(near(after.template_origin, before.template_origin + shift));
		QVERIFY(near(MapCoordF(after.extents.front().rect_mm.topLeft()), MapCoordF(before.extents.front().rect_mm.topLeft()) + shift));
		QVERIFY(near(MapCoordF(after.bookmarks.front().center), MapCoordF(before.bookmarks.front().center) + shift));
		
		QCOMPARE(t.map.undoManager().undoStepCount(), 1);
		QVERIFY(t.map.undoManager().undo());
		auto const restored = mapOffset(t.map);
		QVERIFY(std::abs(restored.x() - 950000.0) < 0.001 && std::abs(restored.y() - 500000.0) < 0.001);
		QVERIFY(t.map.getPart(0)->getObject(0)->getRawCoordinateVector() == path_before);
		QCOMPARE(t.map.extents().front().rect_mm, before.extents.front().rect_mm);
	}
	
	
	// the design notes, "Pair tables, perimeters and the dialogs beside the map".
	
	void pairSwitchesTest()
	{
		// Accept (1).
		std::vector<PointPair> pairs = {
		    { MapCoordF(0, 0), MapCoordF(3, -4) },
		    { MapCoordF(20, 0), MapCoordF(23, -4) },
		    { MapCoordF(0, 30), MapCoordF(3, 26) },
		    { MapCoordF(25, 25), MapCoordF(38, 21) },  // 10 mm off the translation
		};
		pairs[3].used = false;
		auto const fit = fitPointPairs(pairs);
		QVERIFY(fit.ok);
		for (auto const& p : { QPointF(0, 0), QPointF(100, -50), QPointF(-7, 13) })
			QVERIFY(near(MapCoordF(fit.transform.map(p)), MapCoordF(p) + MapCoordF(3, -4), 1e-9));
		QCOMPARE(int(fit.residuals.size()), 4);
		for (int i = 0; i < 3; ++i)
			QVERIFY(std::abs(fit.residuals[std::size_t(i)]) < 1e-9);
		QVERIFY(std::abs(fit.residuals[3] - 10.0) < 1e-9);
	
		pairs[0].used = false;
		pairs[2].used = false;
		auto const one = fitPointPairs(pairs);
		QVERIFY(one.ok);
		QVERIFY(near(MapCoordF(one.transform.map(QPointF(5, 5))), MapCoordF(8, 1), 1e-9));
		pairs[1].used = false;
		QVERIFY(!fitPointPairs(pairs).ok);
	
		// Accept (2).
		RubbersheetParams params;
		params.perimeter = { { MapCoordF(-1000, -1000), MapCoordF(1000, -1000), MapCoordF(1000, 1000), MapCoordF(-1000, 1000) } };
		params.pairs = { { MapCoordF(0, 0), MapCoordF(0, 0) }, { MapCoordF(100, 0), MapCoordF(110, 0) } };
		auto with_unused = params;
		with_unused.pairs.push_back({ MapCoordF(40, 40), MapCoordF(-40, 80), false });
		for (auto const& p : { MapCoordF(50, 0), MapCoordF(40, 40), MapCoordF(-300, 20) })
			QVERIFY(near(rubbersheetDisplacement(with_unused, p), rubbersheetDisplacement(params, p), 1e-12));
		with_unused.pairs = { { MapCoordF(40, 40), MapCoordF(-40, 80), false } };
		TestMap t;
		QVERIFY(!rubbersheetMap(t.map, with_unused));
	}
	
	
	void pointsFileUsedTest()
	{
		// Accept (3).
		TestMap t;
		QTemporaryDir dir;
		std::vector<PointPair> pairs = { { MapCoordF(12.5, -3.25), MapCoordF(14.0, -2.0) },
		                                 { MapCoordF(-40.0, 22.0), MapCoordF(-41.0, 22.0), false },
		                                 { MapCoordF(5.0, 5.0), MapCoordF(5.0, 6.0) } };
		auto const path = dir.filePath(QStringLiteral("points.txt"));
		QString error;
		QVERIFY2(writeRubbersheetPoints(path, t.map, pairs, &error), qPrintable(error));
		auto const read = readRubbersheetPoints(path, t.map, &error);
		QCOMPARE(read.size(), pairs.size());
		for (std::size_t i = 0; i < pairs.size(); ++i)
		{
			QCOMPARE(read[i].used, pairs[i].used);
			QVERIFY(near(read[i].map, pairs[i].map, 0.00125));
			QVERIFY(near(read[i].reference, pairs[i].reference, 0.00125));
		}
		QFile file(path);
		QVERIFY(file.open(QIODevice::ReadOnly));
		auto const lines = QString::fromUtf8(file.readAll()).split(QStringLiteral("\r\n"));
		QVERIFY(lines.size() >= 6);
		QCOMPARE(lines[2], QStringLiteral("3"));
		QVERIFY2(lines[4].endsWith(QStringLiteral(";0;4.00")), qPrintable(lines[4]));  // 1 mm at 1:4000
		QVERIFY(lines[3].split(QLatin1Char(';'))[4] == QStringLiteral("1"));
	}
	
	
	void perimetersTest()
	{
		// Accept (4).
		RubbersheetParams params;
		params.perimeter = { { MapCoordF(-10, -10), MapCoordF(10, -10), MapCoordF(10, 10), MapCoordF(-10, 10) },
		                     { MapCoordF(40, -10), MapCoordF(60, -10), MapCoordF(60, 10), MapCoordF(40, 10) } };
		params.pairs = { { MapCoordF(0, 0), MapCoordF(2, 0) } };
		TestMap t;
		auto* probe = new PathObject(t.line, MapCoordVector{ MapCoord(0.0, 0.0), MapCoord(30.0, 0.0), MapCoord(50.0, 0.0) });
		t.map.addObject(probe);
		QVERIFY(rubbersheetMap(t.map, params));
		auto const& moved = probe->getRawCoordinateVector();
		QVERIFY(near(MapCoordF(moved[0]), MapCoordF(2, 0)));
		QVERIFY(near(MapCoordF(moved[1]), MapCoordF(30, 0)));
		QVERIFY(near(MapCoordF(moved[2]), MapCoordF(52, 0)));
	
		params.decrease_near_perimeter = true;
		auto const p = MapCoordF(50, 5);
		auto const w = 1 / (std::hypot(50.0, 5.0) * std::hypot(48.0, 5.0));
		QVERIFY(near(rubbersheetDisplacement(params, p), MapCoordF(2 * w / (w + 4.0 / 25), 0), 1e-9));
	
		QTemporaryDir dir;
		auto const path = dir.filePath(QStringLiteral("perimeter.txt"));
		QString error;
		QVERIFY2(writeRubbersheetPerimeter(path, t.map, params.perimeter, &error), qPrintable(error));
		auto const read = readRubbersheetPerimeter(path, t.map, &error);
		QCOMPARE(read.size(), std::size_t(2));
		for (std::size_t k = 0; k < 2; ++k)
		{
			QCOMPARE(read[k].size(), params.perimeter[k].size());
			for (std::size_t i = 0; i < read[k].size(); ++i)
				QVERIFY(near(read[k][i], params.perimeter[k][i], 0.00125));
		}
		QFile file(path);
		QVERIFY(file.open(QIODevice::ReadOnly));
		auto const lines = QString::fromUtf8(file.readAll()).split(QStringLiteral("\r\n"));
		file.close();
		QCOMPARE(lines[2], QStringLiteral("2"));
	
		// One polygon with its last vertex repeated, as written before.
		auto const& georef = t.map.getGeoreferencing();
		QStringList old_file = { QStringLiteral("EPSG:26954"), QStringLiteral("m"), QStringLiteral("1") };
		for (auto const& c : { MapCoordF(-10, -10), MapCoordF(10, -10), MapCoordF(10, 10), MapCoordF(10, 10) })
		{
			auto const q = georef.toProjectedCoords(c);
			old_file << QString::number(q.x(), 'f', 2) + QLatin1Char(';') + QString::number(q.y(), 'f', 2);
		}
		QVERIFY(file.open(QIODevice::WriteOnly));
		file.write(old_file.join(QStringLiteral("\r\n")).toUtf8());
		file.close();
		auto const single = readRubbersheetPerimeter(path, t.map, &error);
		QCOMPARE(single.size(), std::size_t(1));
		QCOMPARE(single.front().size(), std::size_t(3));
	}
	
	
	void rubbersheetReportTest()
	{
		// Accept (5).
		RubbersheetParams params;
		params.perimeter = { { MapCoordF(-100, -100), MapCoordF(100, -100), MapCoordF(100, 100), MapCoordF(-100, 100) } };
		params.pairs = { { MapCoordF(0, 0), MapCoordF(2, 0) } };
		Map map;
		auto* line = new LineSymbol();
		map.addSymbol(line, 0);
		map.addObject(new PathObject(line, MapCoordVector{ MapCoord(0.0, 0.0), MapCoord(30.0, 0.0), MapCoord(50.0, 20.0) }));
		WarpReport report;
		QVERIFY(rubbersheetMap(map, params, &report));
		QCOMPARE(report.count, 3);
		QVERIFY(std::abs(report.largest - 2.0) < 1e-9);
		QVERIFY(std::abs(report.mean - 2.0) < 1e-9);
	}
	
	
	void reprojectOffsetTest()
	{
		// Accept (6).
		TestMap t;
		Georeferencing planar;
		planar.setScaleDenominator(4000);
		QVERIFY(planar.setProjectedCRS(QStringLiteral("EPSG"), QStringLiteral("EPSG:26954"), { QStringLiteral("26954") }));
		planar.setMapRefPoint(MapCoord(0.0, 0.0));
		planar.setProjectedRefPoint(QPointF(955000.0, 525000.0));
		t.map.setGeoreferencing(planar);
		auto const grivation = t.map.getGeoreferencing().getGrivation();
		auto* a = new PathObject(t.line, MapCoordVector{ MapCoord(planar.toMapCoordF(QPointF(955400.0, 525200.0))), MapCoord(10.0, 10.0) });
		t.map.addObject(a);
		auto const index = t.map.getCurrentPart()->findObjectIndex(a);
		auto const coords_before = a->getRawCoordinateVector();
		t.map.undoManager().clear();
	
		bool ok = false;
		auto const offset = proposedReprojectOffset(t.map.getGeoreferencing(), QStringLiteral("EPSG"), QStringLiteral("EPSG:26913"), { QStringLiteral("26913") }, &ok);
		QVERIFY(ok);
		QCOMPARE(offset, QPointF(498000.0, 4407000.0));
		QVERIFY(reprojectMap(t.map, QStringLiteral("EPSG"), QStringLiteral("EPSG:26913"), { QStringLiteral("26913") }, &offset));
		auto const origin = mapOffset(t.map);
		QVERIFY2(std::abs(origin.x() - 498000.0) < 0.004 && std::abs(origin.y() - 4407000.0) < 0.004,
		         qPrintable(QStringLiteral("%1 %2").arg(origin.x(), 0, 'f', 4).arg(origin.y(), 0, 'f', 4)));
		auto const& after = t.map.getGeoreferencing();
		auto const projected = after.toProjectedCoords(MapCoordF(a->getRawCoordinateVector()[0]));
		QVERIFY2(std::abs(projected.x() - 498188.27) < 0.01 && std::abs(projected.y() - 4407527.37) < 0.01,
		         qPrintable(QStringLiteral("%1 %2").arg(projected.x(), 0, 'f', 3).arg(projected.y(), 0, 'f', 3)));
		QCOMPARE(after.getGrivation(), grivation);
	
		QCOMPARE(t.map.undoManager().undoStepCount(), 1);
		QVERIFY(t.map.undoManager().undo());
		QCOMPARE(t.map.getGeoreferencing().getProjectedCRSSpec(), QStringLiteral("EPSG:26954"));
		QCOMPARE(t.map.getGeoreferencing().getProjectedRefPoint(), QPointF(955000.0, 525000.0));
		QCOMPARE(t.map.getGeoreferencing().getMapRefPoint(), MapCoord(0.0, 0.0));
		QVERIFY(t.map.getCurrentPart()->getObject(index)->getRawCoordinateVector() == coords_before);
	}
	
	
	void geomagneticTest()
	{
		// Accept (7), the reply measured on 2026-11-21, shortened.
		QByteArray const reply =
		    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
		    "<geomagnetic-field-model-result>\n"
		    "  <model revision=\"2025\">wmm</model>\n"
		    "  <field-value>\n"
		    "    <total-intensity units=\"nT\">48291</total-intensity>\n"
		    "    <declination units=\"deg (east)\">3.421</declination>\n"
		    "    <inclination units=\"deg (down)\">63.288</inclination>\n"
		    "    <horizontal-intensity units=\"nT\">21707</horizontal-intensity>\n"
		    "  </field-value>\n"
		    "  <secular-variation>\n"
		    "    <total-intensity units=\"nT/y\">44.6</total-intensity>\n"
		    "    <declination units=\"arcmin/y (east)\">7.6</declination>\n"
		    "    <horizontal-intensity units=\"nT/y\">6.7</horizontal-intensity>\n"
		    "  </secular-variation>\n"
		    "</geomagnetic-field-model-result>\n";
		auto const field = parseGeomagneticReply(reply);
		QVERIFY(field.has_declination && field.has_horizontal_intensity && field.has_yearly_change);
		QCOMPARE(field.declination, 3.421);
		QCOMPARE(field.horizontal_intensity, 21707.0);
		QVERIFY(std::abs(field.yearly_change - 7.6 / 60) < 1e-12);
		QVERIFY(std::abs(declinationUncertainty(21707) - 0.36038) < 5e-5);
	
		auto shorter = reply;
		auto const start = shorter.indexOf("  <secular-variation>");
		auto const end = shorter.indexOf("</secular-variation>") + int(qstrlen("</secular-variation>\n"));
		shorter.remove(start, end - start);
		auto const without = parseGeomagneticReply(shorter);
		QVERIFY(without.has_declination);
		QCOMPARE(without.declination, 3.421);
		QVERIFY(!without.has_yearly_change);
	}
};


QTEST_MAIN(MapTransformTest)


#include "map_transform_t.moc"  // IWYU pragma: keep
