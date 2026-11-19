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


#ifndef FOOM_SKETCH_DIALOG_H
#define FOOM_SKETCH_DIALOG_H

#include <vector>

#include <QDialog>
#include <QObject>
#include <QRectF>
#include <QString>

#include "sketch/sketch.h"

class QLabel;
class QListWidget;
class QPushButton;
class QWidget;

namespace OpenOrienteering {

class Map;
class SymbolDropDown;


/**
 * The list of the strokes and notes of the sketch part, with the
 * conversion of one of them into a map object of a chosen symbol.
 *
 * See the design notes, "Sketch layer", second change.
 */
class SketchManageDialog : public QDialog
{
Q_OBJECT
public:
	explicit SketchManageDialog(Map& map, QWidget* parent = nullptr);
	~SketchManageDialog() override;
	
signals:
	/** An item was selected: its extent, to be shown in the map. */
	void showExtent(const QRectF& extent);
	
private:
	void refresh();
	void rowChanged(int row);
	void convert();
	
	Map& map;
	std::vector<SketchItem> items;
	QListWidget* list;
	SymbolDropDown* symbol_box;
	QPushButton* convert_button;
	QLabel* message_label;
};


}  // namespace OpenOrienteering

#endif
