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


#ifndef FOOM_MERGE_OVERLAY_H
#define FOOM_MERGE_OVERLAY_H

#include <QColor>
#include <QObject>
#include <QRectF>

#include "gui/map/map_editor_activity.h"

class QPainter;

namespace OpenOrienteering {

class Map;
class MapWidget;
class Object;


/**
 * The other version of an object, drawn over the map in a contrast colour
 * while a row of the merge dialog is selected.
 *
 * See the design notes, "Merge changes from file: detail", fifth change.
 */
class MergeOverlay
{
public:
	/** The contrast colour: magenta. */
	static QColor color();
	
	/**
	 * Draws the object in the contrast colour, and the line through its
	 * coordinates two pixels wide, so that a thin object shows as well.
	 * The painter is in map coordinates; scale is pixels per millimetre.
	 */
	static void draw(QPainter* painter, const Object& object, const QRectF& bounding_box, qreal scale);
};


/** The editor activity which draws the overlay. */
class MergeOverlayActivity : public MapEditorActivity
{
Q_OBJECT
public:
	explicit MergeOverlayActivity(Map& map);
	~MergeOverlayActivity() override;
	
	/** The object to draw, or none. It must stay alive while it is shown. */
	void setObject(const Object* object);
	
	void draw(QPainter* painter, MapWidget* widget) override;
	
private:
	Map& map;
	const Object* object = nullptr;
};


}  // namespace OpenOrienteering

#endif
