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

#ifndef OPENORIENTEERING_VIEW_HIGHLIGHTS_H
#define OPENORIENTEERING_VIEW_HIGHLIGHTS_H

#include <vector>

#include <QColor>
#include <QObject>

class QPainter;

namespace OpenOrienteering {

class Map;
class MapEditorController;
class MapWidget;
class Object;


/**
 * Draws objects highlighted over the map on screen: the impassable
 * features and the objects without symbol.
 *
 * What is highlighted is found anew whenever it is drawn or counted.
 *
 * See the design notes, "Show impassable features",
 * and the design notes, "Show objects without symbol".
 */
class ViewHighlights : public QObject
{
Q_OBJECT
public:
	ViewHighlights(MapEditorController* editor, Map* map);
	~ViewHighlights() override;

	/** The number of impassable objects shown, 0 while they are not. */
	int impassableObjectCount() const;

	/** The number of objects without symbol shown, 0 while they are not. */
	int unsymbolizedObjectCount() const;

	/** Draws the highlights which are switched on. */
	void draw(QPainter* painter, MapWidget* widget) const;

	/** Draws the outlines of the objects in the colour, 1 mm wide on paper and at least 3 pixels. */
	static void drawObjects(QPainter* painter, MapWidget* widget, const std::vector<const Object*>& objects, const QColor& color);

public slots:
	void setImpassableShown(bool shown);
	void setUnsymbolizedShown(bool shown);

private:
	void repaint() const;

	MapEditorController* editor;
	Map* map;
	bool impassable_shown = false;
	bool unsymbolized_shown = false;
};


}  // namespace OpenOrienteering

#endif
