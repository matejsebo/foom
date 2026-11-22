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


#ifndef OPENORIENTEERING_VIEW_AIDS_H
#define OPENORIENTEERING_VIEW_AIDS_H

#include <vector>

#include <QtGlobal>
#include <QObject>
#include <QPointer>
#include <QRectF>
#include <QString>
#include <QWidget>

#include "core/guides.h"
#include "core/map_coord.h"

class QContextMenuEvent;
class QEvent;
class QMenu;
class QMouseEvent;
class QPaintEvent;
class QPainter;
class QPoint;

namespace OpenOrienteering {

class Map;
class MapEditorController;
class MapWidget;
class RulerWidget;


/**
 * One tick of a ruler: its value in the ruler's unit and its pixel along the ruler.
 */
struct RulerTick
{
	double value;
	double pixel;
	bool labelled;
};

/**
 * Returns the step between labelled ticks in ruler units: 1, 2 or 5 times a
 * power of ten, the smallest whose ticks are at least min_label_pixels apart.
 */
double rulerStep(double pixels_per_unit, double min_label_pixels = 60);

/**
 * Returns the ticks of a ruler of the given length in pixels, where the value
 * at pixel 0 is value_at_zero and each pixel adds units_per_pixel (negative
 * for a ruler whose values grow towards pixel 0).
 * 
 * Labelled ticks lie at multiples of rulerStep(); between them, minor ticks
 * divide the step in 10, 5 or 2, the finest which keeps them 5 pixels apart.
 */
std::vector<RulerTick> rulerTicks(double value_at_zero, double units_per_pixel, int length, double min_label_pixels = 60);


/**
 * The view aids of a map editor: rulers and guides, find selected and move to
 * coordinates.
 * 
 * See the design notes, "Rulers and guides", "find-selected"
 * and "view-move-to".
 */
class ViewAids : public QObject
{
Q_OBJECT
public:
	enum RulerUnit
	{
		PaperMillimetres = 0,  ///< mm on paper: map coordinates
		GroundMetres = 1       ///< metres on the ground, at the map scale
	};
	
	/** The kinds of coordinates which move to accepts. */
	enum CoordinateKind
	{
		MapCoordinates = 0,        ///< mm on the map, x right and y up as shown in the status bar
		ProjectedCoordinates = 1,  ///< easting and northing in metres
		GeographicCoordinates = 2  ///< latitude and longitude in degrees
	};
	
	ViewAids(MapEditorController* editor, Map* map, MapWidget* map_widget);
	~ViewAids() override;
	
	/**
	 * Puts the map widget into a frame with the rulers along its top and left
	 * edges, and returns the frame.
	 */
	QWidget* wrapMapWidget();
	
	/** Draws the guides, and the guide being dragged, over the map widget. */
	void drawGuides(QPainter* painter, MapWidget* widget) const;
	
	bool rulersVisible() const { return rulers_visible; }
	RulerUnit rulerUnit() const { return ruler_unit; }
	
	/** The origin of the rulers in map coordinates. */
	const MapCoordF& rulerOrigin() const { return ruler_origin; }
	
	/**
	 * Returns the value which the ruler of the orientation shows at a map coordinate:
	 * x from the origin for the horizontal ruler, y upwards from the origin for the
	 * vertical one, in the ruler unit.
	 */
	double rulerValue(Qt::Orientation orientation, const MapCoordF& coord) const;
	
	/** Adds the entries of the view aids to the View menu. */
	void addToViewMenu(QMenu* menu);
	
	/** Sets the check marks of the actions from the state. */
	void syncActions();
	
	/** Returns the ruler of the orientation, or nullptr before wrapMapWidget(). */
	RulerWidget* ruler(Qt::Orientation orientation) const;
	
	/**
	 * Adds a guide as one undo step. Does nothing while the guides are locked.
	 */
	void addGuide(const Guide& guide);
	
	/** Moves a guide to the position (native map units) as one undo step. */
	void moveGuide(int index, qint32 position);
	
	/** Removes a guide as one undo step. */
	void removeGuide(int index);
	
	/**
	 * Starts dragging a guide: a new one of the orientation when index is -1,
	 * else the guide of that index.
	 */
	void beginGuideDrag(Qt::Orientation orientation, int index = -1);
	
	/** Moves the dragged guide to the position in map widget pixels. */
	void updateGuideDrag(const QPoint& widget_pos);
	
	/**
	 * Ends the drag at the position in map widget pixels: the guide is set there
	 * when it lies inside the map widget, else a new guide is dropped and an
	 * existing one is removed (dragged back onto the ruler).
	 */
	void finishGuideDrag(const QPoint& widget_pos);
	
	bool isDraggingGuide() const { return drag_active; }
	
	/** The index of the next object which find selected brings into view. */
	int findSelectedIndex() const { return find_index; }
	
	/**
	 * Returns the map coordinates of a typed position, or sets ok to false when
	 * it cannot be converted (no georeferencing for geographic coordinates).
	 * Map coordinates are taken as the status bar shows them: y up.
	 */
	MapCoordF toMapCoordinates(CoordinateKind kind, double first, double second, bool* ok) const;
	
	/** Centres the view at the map position, keeping the zoom. */
	void moveTo(const MapCoordF& position);
	
	bool eventFilter(QObject* watched, QEvent* event) override;
	
public slots:
	void setRulersVisible(bool visible);
	void setRulerUnit(int unit);
	void setRulerOrigin(const MapCoordF& origin);
	void resetRulerOrigin();
	void setGuidesVisible(bool visible);
	void setGuidesLocked(bool locked);
	void clearGuides();
	
	/**
	 * Brings the next selected object into view: centres it and zooms out when
	 * it does not fit. Repeated calls step through the selection, in the order
	 * of the selection, and begin again after the last.
	 */
	void findSelected();
	
	/** Asks for coordinates and moves the view there. */
	void moveToClicked();
	
	/** Called when the selection changes: find selected starts again. */
	void selectionChanged();
	
private:
	void setGuides(Guides guides);
	void updateRulers();
	
	MapEditorController* editor;
	Map* map;
	MapWidget* map_widget;
	QPointer<QWidget> frame;
	QPointer<RulerWidget> top_ruler;
	QPointer<RulerWidget> left_ruler;
	QPointer<QWidget> corner;
	QPointer<QWidget> guide_layer;
	
	MapCoordF ruler_origin;
	RulerUnit ruler_unit = PaperMillimetres;
	bool rulers_visible = false;
	
	bool drag_active = false;
	bool drag_inside = false;
	Qt::Orientation drag_orientation = Qt::Horizontal;
	int drag_index = -1;
	qint32 drag_position = 0;
	
	int find_index = 0;
};


/**
 * A ruler along an edge of the map widget. Dragging from it creates a guide.
 */
class RulerWidget : public QWidget
{
Q_OBJECT
public:
	/** The width of a ruler in pixels. */
	static constexpr int thickness = 18;
	
	RulerWidget(Qt::Orientation orientation, ViewAids* aids, MapWidget* map_widget, QWidget* parent = nullptr);
	~RulerWidget() override;
	
	Qt::Orientation orientation() const { return ruler_orientation; }
	
	/** Returns the ticks for the current view. */
	std::vector<RulerTick> ticks() const;
	
protected:
	void paintEvent(QPaintEvent* event) override;
	void mousePressEvent(QMouseEvent* event) override;
	void mouseMoveEvent(QMouseEvent* event) override;
	void mouseReleaseEvent(QMouseEvent* event) override;
	void contextMenuEvent(QContextMenuEvent* event) override;
	
private:
	QPoint toMapWidget(const QPoint& pos) const;
	
	Qt::Orientation ruler_orientation;
	ViewAids* aids;
	MapWidget* map_widget;
};


}  // namespace OpenOrienteering

#endif
