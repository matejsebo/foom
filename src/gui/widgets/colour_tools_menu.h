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

#ifndef FOOM_GUI_COLOUR_TOOLS_MENU_H
#define FOOM_GUI_COLOUR_TOOLS_MENU_H

class QMenu;
class QWidget;

namespace OpenOrienteering {

class Map;
class MainWindow;


/**
 * The menu of the colour dock's *More* button.
 *
 * See the design notes, "Colours".
 */
QMenu* createColourToolsMenu(Map* map, MainWindow* window, QWidget* parent);


}  // namespace OpenOrienteering

#endif
