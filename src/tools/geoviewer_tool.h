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

#ifndef FOOM_GEOVIEWER_TOOL_H
#define FOOM_GEOVIEWER_TOOL_H

#include <QObject>
#include <QUrl>

#include "core/map_coord.h"
#include "tools/tool_base.h"

class QAction;
class QKeyEvent;
class QPainter;
class QRectF;

namespace OpenOrienteering {

class MapEditorController;
class MapWidget;


/**
 * The geoviewer tool: a click opens the geoviewer at that place looking
 * north, a drag at its start looking towards its end.
 *
 * The URL is emitted; the editor gives it to the system's browser.
 * See the design notes, "Online map and geoviewer".
 */
class GeoviewerTool : public MapEditorToolBase
{
Q_OBJECT
public:
	GeoviewerTool(MapEditorController* editor, QAction* tool_action);
	~GeoviewerTool() override;

signals:
	/** The geoviewer's URL for the place and direction chosen. */
	void urlChosen(const QUrl& url);

protected:
	void updateStatusText() override;
	void objectSelectionChangedImpl() override;

	void clickRelease() override;
	void dragStart() override;
	void dragMove() override;
	void dragFinish() override;
	void dragCanceled() override;
	bool keyPress(QKeyEvent* event) override;

	void drawImpl(QPainter* painter, MapWidget* widget) override;
	int updateDirtyRectImpl(QRectF& rect) override;

private:
	void open(const MapCoordF& from, const MapCoordF& to);

	MapCoordF start;
	MapCoordF current;
	bool pointing = false;
};


}  // namespace OpenOrienteering

#endif
