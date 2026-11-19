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


#ifndef FOOM_RANGEFINDER_TOOL_H
#define FOOM_RANGEFINDER_TOOL_H

#include <QObject>
#include <QPointer>
#include <QString>

#include "tools/tool_base.h"

class QAction;
class QKeyEvent;
class QLabel;

namespace OpenOrienteering {

class MapEditorController;


/**
 * Tool to set the reference point of the rangefinder by a click on the
 * map. The tool ends with the click.
 *
 * See the design notes, "Rangefinder dock: detail".
 */
class RangefinderReferenceTool : public MapEditorToolBase
{
Q_OBJECT
public:
	RangefinderReferenceTool(MapEditorController* editor, QAction* tool_action);
	~RangefinderReferenceTool() override;
	
protected:
	void initImpl() override;
	void updateStatusText() override;
	void objectSelectionChangedImpl() override;
	
	void clickRelease() override;
	bool keyPress(QKeyEvent* event) override;
	
private:
	QPointer<QLabel> help_label;
};


}  // namespace OpenOrienteering

#endif
