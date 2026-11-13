/*
 *    Copyright 2026 Matej Šebo
 *
 *    This file is part of FOOM, the Fantastic Open Orienteering Mapper,
 *    a fork of OpenOrienteering Mapper.
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


#ifndef OPENORIENTEERING_TOPOLOGY_DIALOGS_H
#define OPENORIENTEERING_TOPOLOGY_DIALOGS_H

#include <vector>

#include <QDialog>
#include <QObject>
#include <QString>

#include "core/ops/topology.h"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QRadioButton;
class QWidget;

namespace OpenOrienteering {

class MapEditorController;
class Object;


/**
 * The scope of a topology command which can act beyond the selection.
 */
enum class TopologyScope
{
	AllObjects,       ///< Every object of the current part.
	SelectedSymbols,  ///< The objects of the symbols selected in the symbol pane.
	Selection,        ///< The selected objects.
};

/**
 * Returns the objects of the current part in the scope.
 */
std::vector<const Object*> topologyScopeObjects(const MapEditorController* editor, TopologyScope scope);


/**
 * Asks for the scope of a topology command.
 *
 * See the design notes, "Topology commands".
 */
class TopologyScopeDialog : public QDialog
{
Q_OBJECT
public:
	TopologyScopeDialog(const QString& title, MapEditorController* editor, QWidget* parent);
	~TopologyScopeDialog() override;
	
	TopologyScope scope() const;
	
private:
	QRadioButton* all_button;
	QRadioButton* symbols_button;
	QRadioButton* selection_button;
};


/**
 * Asks for the parameters of the overshoot command.
 */
class OvershootDialog : public QDialog
{
Q_OBJECT
public:
	OvershootDialog(QWidget* parent);
	~OvershootDialog() override;
	
	ops::OvershootParams params() const;
	
private:
	QCheckBox* overshoots_check;
	QCheckBox* undershoots_check;
	QDoubleSpinBox* tolerance_edit;
};


/**
 * Asks for the parameters of the intersections command.
 */
class IntersectionDialog : public QDialog
{
Q_OBJECT
public:
	IntersectionDialog(MapEditorController* editor, QWidget* parent);
	~IntersectionDialog() override;
	
	ops::IntersectionParams params() const;
	
private:
	MapEditorController* editor;
	QRadioButton* insert_button;
	QRadioButton* cut_button;
	QRadioButton* point_button;
	QComboBox* vertex_combo;
	QCheckBox* touching_check;
};


}  // namespace OpenOrienteering

#endif
