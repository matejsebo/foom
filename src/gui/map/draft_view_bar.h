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

#ifndef OPENORIENTEERING_DRAFT_VIEW_BAR_H
#define OPENORIENTEERING_DRAFT_VIEW_BAR_H

#include <QObject>
#include <QToolBar>

class QAction;
class QSlider;
class QWidget;

namespace OpenOrienteering {

class MapEditorController;
class MapView;


/**
 * The toolbar of the draft view: the switch and how much the map and the
 * background maps are dimmed, in percent.
 *
 * See the design notes, "Draft view".
 */
class DraftViewBar : public QToolBar
{
Q_OBJECT
public:
	DraftViewBar(MapEditorController* editor, QAction* draft_view_action, QWidget* parent = nullptr);
	~DraftViewBar() override;

	QSlider* mapSlider() const { return map_slider; }
	QSlider* templateSlider() const { return template_slider; }

	/** Shows the dims of the main view of the editor. */
	void updateFromView();

private:
	MapView* view() const;

	MapEditorController* editor;
	QSlider* map_slider;
	QSlider* template_slider;
};


}  // namespace OpenOrienteering

#endif
