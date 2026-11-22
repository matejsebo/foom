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


#ifndef FOOM_GENERATOR_PLACE_TOOL_H
#define FOOM_GENERATOR_PLACE_TOOL_H

#include <QJsonObject>
#include <QObject>
#include <QRectF>
#include <QString>

#include "tools/tool_base.h"

class QKeyEvent;
class QPainter;

namespace OpenOrienteering {

class MapEditorController;
class MapWidget;


/**
 * Places a generated group by a click: a box of the group's extent follows
 * the pointer, a click builds the group there, Esc cancels.
 *
 * See the design notes, "Generators", third change.
 */
class GeneratorPlaceTool : public MapEditorToolBase
{
Q_OBJECT
public:
	/**
	 * The preview is the extent of the group with the anchor at the origin,
	 * from generatorPreview().
	 */
	GeneratorPlaceTool(MapEditorController* editor, const QString& id, const QJsonObject& params, const QRectF& preview);
	~GeneratorPlaceTool() override;

protected:
	void updateStatusText() override;
	void objectSelectionChangedImpl() override;
	void mouseMove() override;
	void clickRelease() override;
	bool keyPress(QKeyEvent* event) override;
	void drawImpl(QPainter* painter, MapWidget* widget) override;
	int updateDirtyRectImpl(QRectF& rect) override;

private:
	QString id;
	QJsonObject params;
	QRectF preview;
	bool have_position = false;
};


}  // namespace OpenOrienteering

#endif
