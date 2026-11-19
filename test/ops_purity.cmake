#
#    Copyright 2026 Matej Šebo
#
#    This file is part of FOOM.
#
#    FOOM is free software: you can redistribute it and/or modify
#    it under the terms of the GNU General Public License as published by
#    the Free Software Foundation, either version 3 of the License, or
#    (at your option) any later version.
#
#    FOOM is distributed in the hope that it will be useful,
#    but WITHOUT ANY WARRANTY; without even the implied warranty of
#    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#    GNU General Public License for more details.
#
#    You should have received a copy of the GNU General Public License
#    along with FOOM.  If not, see <http://www.gnu.org/licenses/>.

# ops_purity.cmake: fails when a source under OPS_DIR includes editor code.
# the design notes, "Geometry operations", Accept (1):
# no header from src/gui/, src/undo/ or src/tools/, and no QWidget-derived
# Qt class. apply_op.h and apply_op.cpp alone may include src/undo/.
#
# Usage: cmake -DOPS_DIR=<dir> -P ops_purity.cmake

if(NOT OPS_DIR)
	message(FATAL_ERROR "OPS_DIR is not set")
endif()

file(GLOB sources "${OPS_DIR}/*.h" "${OPS_DIR}/*.cpp")
if(NOT sources)
	message(FATAL_ERROR "No sources in ${OPS_DIR}")
endif()

set(widget_classes
  "Abstract(Button|ItemView|ScrollArea|Slider|SpinBox)"
  "Widget" "Dialog" "MainWindow" "PushButton" "ToolButton" "CheckBox" "RadioButton"
  "ComboBox" "LineEdit" "TextEdit" "PlainTextEdit" "SpinBox" "DoubleSpinBox" "Slider"
  "Label" "Menu" "MenuBar" "ToolBar" "StatusBar" "TabWidget" "TabBar" "ListView"
  "ListWidget" "TreeView" "TreeWidget" "TableView" "TableWidget" "ScrollArea" "GroupBox"
  "Frame" "Splitter" "StackedWidget" "DockWidget" "ProgressBar" "MessageBox" "FileDialog"
  "ColorDialog" "FontDialog" "InputDialog" "Wizard" "GraphicsView" "DateTimeEdit"
  "DateEdit" "TimeEdit" "CalendarWidget" "Dial" "LCDNumber" "RubberBand" "SizeGrip"
  "MdiArea" "MdiSubWindow" "OpenGLWidget" "SplashScreen"
)
string(REPLACE ";" "|" widget_classes "${widget_classes}")
set(widget_regex "<(QtWidgets|Q(${widget_classes})[>.])")

set(violations)
foreach(source ${sources})
	get_filename_component(name "${source}" NAME)
	file(STRINGS "${source}" includes REGEX "^[ \t]*#[ \t]*include")
	foreach(include ${includes})
		set(bad FALSE)
		if(include MATCHES "\"(gui|tools)/")
			set(bad TRUE)
		elseif(include MATCHES "\"undo/" AND NOT name MATCHES "^apply_op\\.(h|cpp)$")
			set(bad TRUE)
		elseif(include MATCHES "${widget_regex}")
			set(bad TRUE)
		endif()
		if(bad)
			list(APPEND violations "${name}: ${include}")
		endif()
	endforeach()
endforeach()

if(violations)
	string(REPLACE ";" "\n  " violations "${violations}")
	message(FATAL_ERROR "Editor code included from ${OPS_DIR}:\n  ${violations}")
endif()
list(LENGTH sources count)
message(STATUS "ops purity: ${count} files under ${OPS_DIR} are clean")
