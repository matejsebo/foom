/*
 *    Copyright 2012, 2013 Thomas Schöps
 *    Copyright 2012-2021, 2024, 2025 Kai Pastor
 *
 *    This file is part of OpenOrienteering.
 *
 *    OpenOrienteering is free software: you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation, either version 3 of the License, or
 *    (at your option) any later version.
 *
 *    OpenOrienteering is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with OpenOrienteering.  If not, see <http://www.gnu.org/licenses/>.
 */


#include "map_editor.h"
#include "map_editor_p.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <functional>
#include <iterator>
#include <limits>
#include <set>
#include <utility>
#include <vector>
// IWYU pragma: no_include <type_traits>

#include <Qt>
#include <QtGlobal>
#include <QtMath>
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QBuffer>
#include <QByteArray>
#include <QComboBox>
#include <QDate>
#include <QCheckBox>
#include <QClipboard>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>  // foom
#include <QFileDialog>  // foom
#include <QDoubleSpinBox>
#include <QDir>
#include <QDockWidget>
#include <QEvent>
#include <QFileInfo>
#include <QFlags>
#include <QFont>
#include <QFontMetrics>
#include <QFormLayout>  // foom
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QImage> // IWYU pragma: keep
#include <QInputDialog>
#include <QIODevice>
#include <QKeyEvent> // IWYU pragma: keep
#include <QKeySequence>
#include <QLabel>
#include <QRadioButton>
#include <QLatin1Char>
#include <QLatin1String>
#include <QLineEdit>
#include <QList>
#include <QLocale>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
// IWYU pragma: no_include <QMetaObject>
#include <QMimeData>
#include <QPainter>
#include <QPlainTextEdit>  // foom
#include <QColor>
#include <QPen>
#include <QPolygonF>
#include <QPixmap>
#include <QPoint>
#include <QPointer>
#include <QPushButton>  // foom
#include <QPointF>
#include <QRect>
#include <QRectF>
#include <QSettings>
#include <QSignalBlocker>
#include <QSignalMapper>
#include <QSize>
#include <QSizeGrip> // IWYU pragma: keep
#include <QSizePolicy>
#include <QSplitter>
#include <QStatusBar>
#include <QStringList>
#include <QToolBar>
#include <QToolButton>
#include <QVariant>
#include <QVBoxLayout>
#include <QWidget>

#ifdef Q_OS_ANDROID
#include <QtAndroidExtras/QAndroidJniObject>
#endif

#include "settings.h"
#include "core/georeferencing.h"
#include "core/map.h"
#include "core/map_coord.h"
#include "core/map_part.h"
#include "core/map_printer.h"  // foom
#include "core/map_view.h"
#include "core/objects/boolean_tool.h"
#include "core/objects/image_object.h"  // foom
#include "core/objects/object.h"
#include "core/objects/object_operations.h"
#include "core/objects/text_object.h"  // foom
#include "elevation/contours.h"  // foom
#include "elevation/elevation_model.h"  // foom
#include "elevation/elevation_set.h"  // foom
#include "elevation/cliffs.h"  // foom
#include "elevation/landforms.h"  // foom
#include "elevation/profile.h"  // foom
#include "elevation/trees.h"  // foom
#include "elevation/point_cloud.h"  // foom
#include "core/map_transform.h"  // foom
#include "gui/map/map_transform_dialogs.h"  // foom
#include "tools/affine_transform_tool.h"  // foom
#include "tools/local_warp_tool.h"  // foom
#include "tools/rubbersheet_tool.h"  // foom
#include "elevation/raster_products.h"  // foom
#include "core/ops/align.h"
#include "core/ops/apply_op.h"
#include "core/ops/offset.h"
#include "core/ops/vertex_type.h"
#include "core/ops/group.h"
#include "core/object_groups.h"
#include "core/ops/flatten.h"
#include "core/ops/convert.h"  // foom
#include "core/ops/interpolate.h"  // foom
#include "core/ops/mirror.h"
#include "core/ops/transform.h"
#include "core/symbols/line_symbol.h"  // foom
#include "core/symbols/private_symbol.h"  // foom
#include "core/symbols/symbol.h"
#include "core/symbols/symbol_icon_decorator.h"
#include "fileformats/file_format.h"
#include "fileformats/file_format_registry.h"
#include "fileformats/file_import_export.h"
#include "fileformats/simple_course_export.h"
#include "gui/configure_grid_dialog.h"
#include "gui/file_dialog.h"
#include "gui/georeferencing_dialog.h"
#include "gui/main_window.h"
#include "gui/print_widget.h"
#include "gui/simple_course_dialog.h"
#include "gui/text_browser_dialog.h"
#include "gui/util_gui.h"
#include "gui/map/map_dialog_scale.h"
#include "gui/map/map_editor_activity.h"
#include "gui/map/map_find_feature.h"
#include "gui/map/map_information_dialog.h"
#include "gui/map/map_notes.h"
#include "gui/map/map_widget.h"
#include "gui/map/generator_dialog.h"  // foom
#include "gui/map/field_copy_dialog.h"  // foom
#include "gui/map/merge_dialog.h"  // foom
#include "gui/map/merge_overlay.h"  // foom
#include "gui/map/sketch_dialog.h"  // foom
#include "gui/map/image_properties_dialog.h"  // foom
#include "gui/map/import_points_dialog.h"  // foom
#include "gui/symbols/symbol_setting_dialog.h"  // foom
#include "layout/generator.h"  // foom
#include "sketch/sketch.h"  // foom
#include "sync/field_copy.h"  // foom
#include "sync/merge.h"  // foom
#include "sync/merge_decisions.h"  // foom
#include <QDateTime>  // foom
#include <QInputDialog>  // foom
#include "layout/layout_part.h"  // foom
#include "gui/map/move_parallel_dialog.h"
#include "gui/map/numeric_entry_bar.h"
#include "gui/map/bookmark_dialog.h"
#include "gui/map/course_bulk_dialog.h"  // foom
#include "gui/map/course_canvases_dialog.h"  // foom
#include "gui/map/course_maps_dialog.h"  // foom
#include "gui/map/course_punch_dialog.h"  // foom
#include "gui/map/route_settings_dialog.h"  // foom
#include "gui/map/course_statistics_dialog.h"  // foom
#include "check/issue.h"
#include "check/legibility.h"
#include "check/map_check.h"
#include "gui/map/extent_dialog.h"
#include "gui/map/legibility_dialog.h"
#include "gui/map/elevation_dialogs.h"  // foom
#include "gui/map/selection_dialogs.h"  // foom
#include "core/selection_queries.h"  // foom
#include "core/object_information.h"  // foom
#include "core/status_sets.h"  // foom
#include "core/symbol_compare.h"  // foom
#include "core/symbol_descriptions.h"  // foom
#include "core/symbol_tools.h"  // foom
#include "core/symbol_update.h"  // foom
#include "gui/widgets/symbol_groups_widget.h"  // foom
#include "gui/widgets/symbol_tools_dialogs.h"  // foom
#include "gui/map/rectangle_box_dialog.h"
#include "gui/map/lengthen_line_text_dialog.h"  // foom
#include "gui/map/topology_dialogs.h"
#include "gui/map/to_curve_dialog.h"  // foom
#include "gui/widgets/issues_widget.h"
#include "gui/map/find_replace_text_dialog.h"
#include "undo/map_data_undo.h"
#include "gui/map/numeric_transform_dialogs.h"
#include "gui/map/rotate_map_dialog.h"
#include "gui/symbols/symbol_replacement.h"
#include "gui/symbols/symbol_report_feature.h"
#include "gui/widgets/action_grid_bar.h"
#include "gui/widgets/color_list_widget.h"
#include "gui/widgets/compass_display.h"
#include "gui/widgets/key_button_bar.h" // IWYU pragma: keep
#include "gui/widgets/measure_widget.h"
#include "course/course_description.h"  // foom
#include "course/course_interchange.h"  // foom
#include "course/course_relay.h"  // foom
#include "course/course_render.h"  // foom
#include "course/course_report.h"  // foom
#include "course/course_storage.h"  // foom
#include "course/route_course.h"  // foom
#include "course/route_search.h"  // foom
#include "course/route_store.h"  // foom
#include "course/route_terrain.h"  // foom
#include "course/course_symbols.h"  // foom
#include "gui/widgets/course_widget.h"  // foom
#include "gui/widgets/rangefinder_widget.h"  // foom
#include "gui/widgets/shortcut_settings.h"  // foom
#include "gui/widgets/symbol_widget.h"
#include "gui/widgets/tags_widget.h"
#include "gui/widgets/template_list_widget.h"
#include "sensors/compass.h"
#include "sensors/gps_display.h"
#include "sensors/gps_path_recorder.h"  // foom
#include "sensors/gps_temporary_markers.h"
#include "sensors/gps_track_recorder.h"
#include "templates/paint_on_template_feature.h"
#include "templates/template.h"
#include "templates/template_dialog_reopen.h"
#include "templates/template_image.h"  // foom
#include "templates/template_relink.h"  // foom
#include "templates/template_track.h"
#include "tools/box_zoom_tool.h"
#include "tools/compose_course_tool.h"  // foom
#include "tools/route_tool.h"  // foom
#include "tools/cut_tool.h"
#include "tools/cut_hole_tool.h"
#include "tools/lasso_tool.h"  // foom
#include "tools/sketch_tools.h"  // foom
#include "tools/move_parallel_tool.h"  // foom
#include "tools/reshape_tool.h"  // foom
#include "tools/cutout_tool.h"
#include "tools/distribute_points_tool.h"
#include "tools/draw_circle_tool.h"
#include "tools/draw_freehand_tool.h"
#include "tools/draw_path_tool.h"
#include "tools/draw_point_tool.h"
#include "tools/draw_point_gps_tool.h"
#include "tools/draw_rectangle_tool.h"
#include "tools/rectangle_box_tool.h"
#include "tools/stairway_tool.h"  // foom
#include "tools/points_along_tool.h"  // foom
#include "tools/sized_shape_tool.h"  // foom
#include "tools/generator_place_tool.h"  // foom
#include "tools/drawing_settings.h"  // foom
#include "gui/map/sized_shape_dialog.h"  // foom
#include "core/ops/frame.h"  // foom
#include "tools/draw_text_tool.h"
#include "tools/draw_line_text_tool.h"  // foom
#include "core/symbols/text_symbol.h"  // foom
#include "tools/edit_point_tool.h"
#include "tools/edit_line_tool.h"
#include "tools/fill_tool.h"
#include "tools/gps_offset_tool.h"  // foom
#include "tools/pan_tool.h"
#include "tools/rangefinder_tool.h"  // foom
#include "tools/rotate_pattern_tool.h"
#include "tools/rotate_tool.h"
#include "tools/scale_tool.h"
#include "tools/tool.h"
#include "undo/map_part_undo.h"
#include "undo/object_undo.h"
#include "undo/undo.h"
#include "undo/undo_manager.h"
#include "util/backports.h" // IWYU pragma: keep

#ifdef MAPPER_USE_GDAL
#include "gdal/ogr_template.h"
#include "gdal/geodata_import.h"  // foom
#include "gdal/geodata_import_dialog.h"  // foom
#include "gdal/map_service_dialog.h"  // foom
#include "gdal/tile_service_template.h"  // foom
#endif


#ifdef __clang_analyzer__
#define singleShot(A, B, C) singleShot(A, B, #C) // NOLINT 
#endif


namespace OpenOrienteering {

class PointSymbol;


namespace {
	
	/**
	 * Creates a partial, resizable widget overlay over the main window.
	 * 
	 * This widget is meant to be used as a dock widget replacement in the
	 * mobile app. In landscape mode, the child widget is placed on the right
	 * side, spanning the full height. In portrait mode, the child is placed
	 * on the top, spanning the full width.
	 */
	QSplitter* createDockWidgetSubstitute(MainWindow* window, QWidget* child)
	{
		auto* splitter = new QSplitter(window);
		splitter->setChildrenCollapsible(false);
		
		auto* placeholder = new QWidget();
		
		splitter->setAttribute(Qt::WA_NoSystemBackground, true);
		placeholder->setAttribute(Qt::WA_NoSystemBackground, true);
		child->setAutoFillBackground(true);
		
		auto geometry = window->geometry();
		splitter->setGeometry(geometry);
		if (geometry.height() > geometry.width())
		{
			splitter->setOrientation(Qt::Vertical);
			splitter->addWidget(child);
			splitter->addWidget(placeholder);
		}
		else
		{
			splitter->setOrientation(Qt::Horizontal);
			splitter->addWidget(placeholder);
			splitter->addWidget(child);
		}
		
		return splitter;
	}
	
	template<class T>
	bool containsPathObject(const T& container)
	{
		return std::any_of(begin(container), end(container), [](const auto* object) {
			return object->getType() == Object::Path;
		});
	}
	
	
}  // namespace



namespace MimeType {

/// The MIME type of Mapper data
QString OpenOrienteeringObjects()
{
	return QStringLiteral("openorienteering/objects");
}


}  // namespace MimeType



// ### MapEditorController ###

MapEditorController::MapEditorController(OperatingMode mode, Map* map, MapView* map_view)
: MainWindowController()
, mobile_mode(Settings::getInstance().touchModeEnabled())
, active_symbol(nullptr)
, template_list_widget(nullptr)
, mappart_remove_act(nullptr)
, mappart_merge_act(nullptr)
, mappart_merge_menu(nullptr)
, mappart_move_menu(nullptr)
, mappart_selector_box(nullptr)
, mappart_merge_mapper(new QSignalMapper(this))
, mappart_move_mapper(new QSignalMapper(this))
{
	this->mode = mode;
	this->map = nullptr;
	main_view = nullptr;
	symbol_widget = nullptr;
	window = nullptr;
	editing_in_progress = false;
	
	cut_hole_menu = nullptr;
	
	if (map)
		setMapAndView(map, map_view ? map_view : new MapView(this, map));
	
	editor_activity = nullptr;
	current_tool = nullptr;
	override_tool = nullptr;
	
	paste_act = nullptr;
	reopen_template_act = nullptr;
	overprinting_simulation_act = nullptr;
	
	toolbar_view = nullptr;
	toolbar_mapparts = nullptr;
	toolbar_drawing = nullptr;
	toolbar_editing = nullptr;
	toolbar_advanced_editing = nullptr;
	print_dock_widget = nullptr;
	measure_dock_widget = nullptr;
	symbol_dock_widget = nullptr;
	
	statusbar_zoom_frame = nullptr;
	statusbar_cursorpos_label = nullptr;
	
	gps_display = nullptr;
	gps_track_recorder = nullptr;
	compass_display = nullptr;
	gps_marker_display = nullptr;
	
	actionsById[""] = new QAction(this); // dummy action
	
	connect(mappart_merge_mapper, QOverload<int>::of(&QSignalMapper::mapped), this, &MapEditorController::mergeCurrentMapPartTo);
	connect(mappart_move_mapper, QOverload<int>::of(&QSignalMapper::mapped), this, &MapEditorController::reassignObjectsToMapPart);
}

MapEditorController::~MapEditorController()
{
	paste_act = nullptr;
	delete current_tool;
	delete override_tool;
	delete editor_activity;
	delete toolbar_view;
	delete toolbar_drawing;
	delete toolbar_editing;
	delete toolbar_advanced_editing;
	delete toolbar_mapparts;
	delete print_dock_widget;
	delete measure_dock_widget;
	delete local_warp_dialog;  // foom
	delete rangefinder_container.data();  // foom
	delete course_dock_widget.data();  // foom
	delete symbol_groups_dock_widget.data();  // foom
	if (color_dock_widget)
		delete color_dock_widget;
	delete symbol_dock_widget;
	if (template_dock_widget)
		delete template_dock_widget;
	if (tags_dock_widget)
		delete tags_dock_widget;
	delete cut_hole_menu;
	delete mappart_merge_act;
	delete mappart_merge_menu;
	delete mappart_move_menu;
	if (mappart_selector_box)
		delete mappart_selector_box;
	delete gps_display;
	delete gps_track_recorder;
	delete compass_display;
	delete gps_marker_display;
	delete map;
}

bool MapEditorController::menuBarVisible()
{
	if (mode == SymbolEditor)
		return false;
	
	return MainWindowController::menuBarVisible();
}

bool MapEditorController::isInMobileMode() const
{
	return mobile_mode;
}

void MapEditorController::setTool(MapEditorTool* new_tool)
{
	if (current_tool)
	{
		if (current_tool->editingInProgress())
		{
			current_tool->finishEditing();
		}
		current_tool->deleteLater();
	}
	
	if (!override_tool)
	{
		map->clearDrawingBoundingBox();
		window->setStatusBarText(QString{});
	}
	
	current_tool = new_tool;
	if (current_tool && !override_tool)
	{
		current_tool->init();
	}
	
	if (!override_tool)
		map_widget->setTool(current_tool);
	
	emit toolChanged(current_tool);  // foom
}

void MapEditorController::setEditTool()
{
	if (!current_tool || current_tool->toolType() != MapEditorTool::EditPoint)
		setTool(new EditPointTool(this, edit_tool_act));
}

void MapEditorController::setOverrideTool(MapEditorTool* new_override_tool)
{
	if (override_tool == new_override_tool)
		return;
	
	if (override_tool)
	{
		if (override_tool->editingInProgress())
		{
			override_tool->finishEditing();
		}
		delete override_tool;
	}
	
	map->clearDrawingBoundingBox();
	window->setStatusBarText(QString{});
	
	override_tool = new_override_tool;
	if (override_tool)
	{
		override_tool->init();
	}
	else if (current_tool)
	{
		current_tool->init();
	}
	
	map_widget->setTool(override_tool ? override_tool : current_tool);
}

MapEditorTool* MapEditorController::getDefaultDrawToolForSymbol(const Symbol* symbol)
{
	if (!symbol)
		return new EditPointTool(this, edit_tool_act);
	else if (symbol->getType() == Symbol::Point)
		return new DrawPointTool(this, draw_point_act);
	else if (symbol->getType() == Symbol::Line || symbol->getType() == Symbol::Area || symbol->getType() == Symbol::Combined)
		return new DrawPathTool(this, draw_path_act, false, true);
	else if (symbol->getType() == Symbol::Text && symbol->asText()->followsPath())  // foom
		return new DrawLineTextTool(this, draw_text_act);
	else if (symbol->getType() == Symbol::Text)
		return new DrawTextTool(this, draw_text_act);
	else
		Q_ASSERT(false);
	return nullptr;
}


void MapEditorController::setEditingInProgress(bool value)
{
	if (value != editing_in_progress)
	{
		editing_in_progress = value;
		if (!editing_in_progress && gps_record_act && gps_record_act->isChecked())  // foom: the path is done
		{
			gps_record_act->setChecked(false);
			gpsRecordClicked(false);
		}
		if (!editing_in_progress && gps_position_average)  // foom: the averaging is given up with the path
		{
			gps_position_average.reset();
			gps_average_vertex_act->setChecked(false);
		}
		
		// Widgets
		map_widget->setGesturesEnabled(!editing_in_progress);
		Q_ASSERT(symbol_widget);
		symbol_widget->setEnabled(!editing_in_progress);
		if (isInMobileMode())
			mobile_symbol_selector_action->setEnabled(!editing_in_progress);
		if (color_dock_widget)
			color_dock_widget->widget()->setEnabled(!editing_in_progress);
		if (mappart_selector_box)
			mappart_selector_box->setEnabled(!editing_in_progress);
		
		// Edit menu
		undo_act->setEnabled(!editing_in_progress && map->undoManager().canUndo());
		redo_act->setEnabled(!editing_in_progress && map->undoManager().canRedo());
		updatePasteAvailability();
		select_all_act->setEnabled(!editing_in_progress);
		select_nothing_act->setEnabled(!editing_in_progress);
		invert_selection_act->setEnabled(!editing_in_progress);
		find_feature->setEnabled(!editing_in_progress);
		
		// Map menu
		georeferencing_act->setEnabled(!editing_in_progress);
		scale_map_act->setEnabled(!editing_in_progress);
		rotate_map_act->setEnabled(!editing_in_progress);
		map_move_act->setEnabled(!editing_in_progress);  // foom
		map_stretch_act->setEnabled(!editing_in_progress);  // foom
		map_mirror_act->setEnabled(!editing_in_progress);  // foom
		map_affine_act->setEnabled(!editing_in_progress);  // foom
		map_rubbersheet_act->setEnabled(!editing_in_progress);  // foom
		map_local_warp_act->setEnabled(!editing_in_progress);  // foom
		map_reproject_act->setEnabled(!editing_in_progress);  // foom
		map_rotate_magnetic_act->setEnabled(!editing_in_progress);  // foom
		map_center_act->setEnabled(!editing_in_progress);  // foom
		map_notes_act->setEnabled(!editing_in_progress);
		map_info_act->setEnabled(!editing_in_progress);
		
		// Map menu, continued
		const int num_parts = map->getNumParts();
		mappart_add_act->setEnabled(!editing_in_progress);
		mappart_rename_act->setEnabled(!editing_in_progress && num_parts > 0);
		mappart_remove_act->setEnabled(!editing_in_progress && num_parts > 1);
		mappart_move_menu->setEnabled(!editing_in_progress && num_parts > 1);
		mappart_merge_act->setEnabled(!editing_in_progress && num_parts > 1);
		mappart_merge_menu->setEnabled(!editing_in_progress && num_parts > 1);
		updateLayoutActions();  // foom
		
		// Symbol menu
		symbol_set_id_act->setEnabled(!editing_in_progress);
		scale_all_symbols_act->setEnabled(!editing_in_progress);
		load_symbols_from_act->setEnabled(!editing_in_progress);
		load_crt_act->setEnabled(!editing_in_progress);
		symbol_report_feature->setEnabled(!editing_in_progress);
		
		// Templates menu
		paint_feature->setEnabled(!editing_in_progress);
		
		updateObjectDependentActions();
		updateSymbolDependentActions();
		updateSymbolAndObjectDependentActions();
	}
}

bool MapEditorController::isEditingInProgress() const
{
	return editing_in_progress;
}


void MapEditorController::setEditorActivity(MapEditorActivity* new_activity)
{
	if (auto* last_activity = editor_activity)
	{
		editor_activity = nullptr;
		map_widget->setActivity(nullptr);
		map->clearActivityBoundingBox();
		delete last_activity;
	}
	
	editor_activity = new_activity;
	if (editor_activity)
	{
		editor_activity->init();
		map_widget->setActivity(editor_activity);
	}
}


void MapEditorController::showPopupWidget(QWidget* child_widget, const QString& title, PopupLocation location)
{
	auto const make_mobile_popup = [this, child_widget]() -> QWidget* {
		// Binding child_widget lifetime directly to map_widget
		child_widget->setParent(map_widget);
		
		// Not being part of the layout, widgets must explicitly draw the background.
		// But for KeyButtonBar, it is enough that the buttons draw their background.
		if (!qobject_cast<KeyButtonBar*>(child_widget))
			child_widget->setAutoFillBackground(true);
		return child_widget;
	};
		
	auto const make_desktop_popup = [this, child_widget, &title]() -> QWidget* {
		auto* dock_widget = new QDockWidget(title, window);
		dock_widget->setFeatures(dock_widget->features() & ~QDockWidget::DockWidgetClosable);
		dock_widget->setWidget(child_widget);
		
		// Show dock in floating state
		dock_widget->setFloating(true);
		
		// Binding child_widget lifetime to map_widget via deletion of dock_widget
		connect(map_widget, &QObject::destroyed, dock_widget, [dock_widget]() { delete dock_widget; });
		
		return dock_widget;
	};
	
	auto const calculate_geometry = [this, location](const QSize& size_hint) -> QRect {
		auto const map_widget_rect = map_widget->rect();
		auto const w = qMin(size_hint.width(), map_widget_rect.width());
		auto const h = qMin(size_hint.height(), map_widget_rect.height());
		auto x = map_widget->mapToGlobal(map_widget_rect.center()).x() - w / 2;
		auto y = map_widget->mapToGlobal(map_widget_rect.topLeft()).y();
		switch (location)
		{
		case PopupLocationTop:
			if (top_action_bar && top_action_bar->isVisible())
				y += top_action_bar->height();
			break;
		case PopupLocationBottom:
			y = map_widget->mapToGlobal(map_widget_rect.bottomRight()).y() - h;
			break;
		}
		return {x, y, w, h};
	};
	
	auto* popup = mobile_mode ? make_mobile_popup() : make_desktop_popup();
	popup->setGeometry(calculate_geometry(popup->sizeHint()));
	popup->show();
}

void MapEditorController::deletePopupWidget(QWidget* child_widget)
{
	if (mobile_mode)
	{
		delete child_widget;
	}
	else
	{
		// Delete the dock widget
		delete child_widget->parentWidget();
	}
}


bool MapEditorController::saveTo(const QString& path, const FileFormat& format)
{
	if (editing_in_progress)
	{
		QMessageBox::warning(window,
		                     tr("Editing in progress"),
		                     tr("The map is currently being edited. "
		                        "Please finish the edit operation before saving.") );
		return false;
	}
	
	purgePrivateSymbols(*map);  // foom
	if (!exportTo(path, format))
		return false;
	
	map->setHasUnsavedChanges(false);
	map->undoManager().setClean();
	window->showStatusBarMessage(tr("Map saved"), 1000);
	return true;
}


bool MapEditorController::exportTo(const QString& path, const FileFormat& format)
{
	if (!map || editing_in_progress)
		return false;
	
	auto exporter = format.makeExporter(path, map, main_view);
	if (!exporter)
	{
		auto message =
		        tr("Cannot export the map as\n"
		           "\"%1\"\n"
		           "because saving as %2 (.%3) is not supported.").
		        arg(path,
		            format.description(),
		            format.fileExtensions().join(QLatin1String(", ")) );
		QMessageBox::warning(nullptr, tr("Error"), message);
		return false;
	}
	
	if (!exporter->doExport())
	{
		auto message = tr("Cannot save file\n%1:\n%2")
		               .arg(path, exporter->warnings().back());
		QMessageBox::warning(nullptr, tr("Error"), message);
		return false;
	}
	
	if (!exporter->warnings().empty())
	{
		MainWindow::showMessageBox(nullptr,
		                           tr("Warning"),
		                           tr("The map export generated warnings."),
		                           exporter->warnings() );
	}
	
	return true;
}


bool MapEditorController::loadFrom(const QString& path, const FileFormat& format, QWidget* dialog_parent)
{
	if (!dialog_parent)
		dialog_parent = window;
	
	if (!map)
	{
		map = new Map();
		main_view = new MapView(this, map);
	}
#ifdef __clang_analyzer__
	// clang-analyzer-core.CallAndMessage seems to be unable to realize
	// that map != null from the previous block.
	if (!map) { return false; }
#endif
	
	auto importer = format.makeImporter(path, map, main_view);
	if (!importer)
	{
		QMessageBox::warning(dialog_parent, tr("Error"),
		                     ::OpenOrienteering::MainWindow::tr("Cannot open file:\n%1\n\n%2")
		                     .arg(path, ::OpenOrienteering::MainWindow::tr("Invalid file type.")));
		return false;
	}
	
	if (!importer->doImport())
	{
		delete map;
		map = nullptr;
		main_view = nullptr;
		
		Q_ASSERT(!importer->warnings().empty());
		QMessageBox::warning(dialog_parent, tr("Error"), importer->warnings().back());
		return false;
	}
	
	map->loadTemplateFilesAsync(*main_view, [controller = QPointer<MapEditorController>(this)](const QString& message) {
		auto* window = controller ? controller->getWindow() : nullptr;
		if (!window)
			return;
		if (message.isEmpty())
			window->clearStatusBarMessage();
		else
			window->showStatusBarMessageImmediately(message);
	});
	setMapAndView(map, main_view);
	map->setHasUnsavedChanges(false);
	if (!importer->warnings().empty())
	{
		// Display warnings asynchronously, so that map and templates get visible.
		auto warnings = importer->warnings();
		auto show_warnings = [dialog_parent, warnings]() {
			MainWindow::showMessageBox(dialog_parent,
			                           tr("Warning"),
			                           tr("The map import generated warnings."),
			                           warnings);
		};
		QTimer::singleShot(0, dialog_parent, show_warnings);
	}
	return true;
}


void MapEditorController::attach(MainWindow* window)
{
	print_dock_widget = nullptr;
	measure_dock_widget = nullptr;
	color_dock_widget = nullptr;
	symbol_dock_widget = nullptr;
	std::function<void(const QString&)> zoom_display_function;
	
	this->window = window;
	if (mode == MapEditor)
	{
		window->setHasUnsavedChanges(map->hasUnsavedChanges());
	}
	connect(map, &Map::hasUnsavedChanged, window, &MainWindow::setHasUnsavedChanges);
	
#ifdef Q_OS_ANDROID
	QAndroidJniObject::callStaticMethod<void>("org/openorienteering/mapper/MapperActivity",
                                       "lockOrientation",
                                       "()V");
#endif
	if (mobile_mode)
	{
		window->setWindowState(window->windowState() | Qt::WindowFullScreen);
		zoom_display_function = [window](const QString& text) {
			window->showStatusBarMessage(text, 1000);
		};
	}
	else
	{
		// Add zoom / cursor position field to status bar
		auto* statusbar_zoom_icon = new QLabel();
		auto fontmetrics = statusbar_zoom_icon->fontMetrics();
		auto pixmap = QPixmap(QLatin1String(":/images/magnifying-glass.png"));
		auto scale = qreal(fontmetrics.height()) / pixmap.height();
		if (scale < 0.9 || scale > 1.1)
			pixmap = pixmap.scaledToHeight(qRound(scale * pixmap.height()), Qt::SmoothTransformation);
		statusbar_zoom_icon->setPixmap(pixmap);
		
		auto* statusbar_zoom_label = new QLabel();
		statusbar_zoom_label->setMinimumWidth(fontmetrics.boundingRect(QLatin1String("0.333x")).width());
		statusbar_zoom_label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
		statusbar_zoom_label->setFrameShape(QFrame::NoFrame);
		
		zoom_display_function = [statusbar_zoom_label](const QString& text) {
			statusbar_zoom_label->setText(text);
		};
		
		statusbar_zoom_frame = new QFrame();
#ifdef Q_OS_WIN
		statusbar_zoom_frame->setFrameShape(QFrame::NoFrame);
#else
		statusbar_zoom_frame->setFrameStyle(QFrame::StyledPanel | QFrame::Sunken);
#endif
		auto* statusbar_zoom_frame_layout = new QHBoxLayout();
		statusbar_zoom_frame_layout->setMargin(0);
		statusbar_zoom_frame_layout->setSpacing(0);
		statusbar_zoom_frame_layout->addSpacing(1);
		statusbar_zoom_frame_layout->addWidget(statusbar_zoom_icon);
		statusbar_zoom_frame_layout->addWidget(statusbar_zoom_label);
		statusbar_zoom_frame->setLayout(statusbar_zoom_frame_layout);
		
		statusbar_cursorpos_label = new QLabel();
#ifdef Q_OS_WIN
		statusbar_cursorpos_label->setFrameShape(QFrame::NoFrame);
#else
		statusbar_cursorpos_label->setFrameStyle(QFrame::StyledPanel | QFrame::Sunken);
#endif
		statusbar_cursorpos_label->setMinimumWidth(fontmetrics.boundingRect(QLatin1String("-3,333.33 -333.33 (mm)")).width());
		statusbar_cursorpos_label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
		
		window->statusBar()->addPermanentWidget(statusbar_zoom_frame);
		window->statusBar()->addPermanentWidget(statusbar_cursorpos_label);
	}
	
	// Create map widget
	map_widget = new MapWidget(mode == MapEditor, mode == SymbolEditor);
	map_widget->setMapView(main_view);
	map_widget->setZoomDisplay(zoom_display_function);
	recordView();  // foom: the opening view is the first entry of the view history
	map_widget->setOverlay([this](QPainter* painter, MapWidget* widget) { drawActiveExtent(painter, widget); drawTemplateAttributions(painter, widget); drawRangefinder(painter, widget); drawCoursePreview(painter, widget); });  // foom
	
	// Create menu and toolbar together, so actions can be inserted into one or both
	if (mode == MapEditor)
	{
		gps_display = new GPSDisplay(map_widget, map->getGeoreferencing());
		connect(gps_display, &GPSDisplay::fixChecked, this, [this](const FixInfo& fix, int verdict) {  // foom: the quality gate
			if (verdict != FixGate::Accepted)
				this->window->showStatusBarMessage(FixGate::verdictText(FixGate::Verdict(verdict), fix, gps_display->fixGate()), 3000);
		});
		connect(gps_display, &GPSDisplay::sentenceLogChanged, this, [this](const QString& file, bool writing) {  // foom: the sentence log
			auto const name = QDir::toNativeSeparators(file);
			this->window->showStatusBarMessage(writing ? tr("Received sentences are written to %1").arg(name) : tr("Received sentences cannot be written to %1").arg(name), 5000);
		});
		gps_marker_display = new GPSTemporaryMarkers(map_widget, gps_display);
		
		createActions();
		if (mobile_mode)
		{
			createMobileGUI();
			compass_display = new CompassDisplay(map_widget->parentWidget());
			compass_display->setVisible(false);
		}
		else
		{
			map_widget->setCursorposLabel(statusbar_cursorpos_label);
			window->setCentralWidget(map_widget);
			
			createMenuAndToolbars();
			restoreWindowState();
		}
	}
	else if (mode == SymbolEditor)
	{
		map_widget->setCursorposLabel(statusbar_cursorpos_label);
		window->setCentralWidget(map_widget);
	}
	
	// Update enabled/disabled state for the tools ...
	updateWidgets();
	templateAvailabilityChanged();
	// ... and make sure it is kept up to date for copy/paste
	connect(QApplication::clipboard(), &QClipboard::changed, this, &MapEditorController::clipboardChanged);
	clipboardChanged(QClipboard::Clipboard);
	
	if (mode == MapEditor)
	{
		// Create/show the dock widgets
		if (mobile_mode)
		{
			createSymbolWidget(window);

			// Activate the map view pan tool
			pan_act->setChecked(true);
			pan();
		}
		else
		{
			symbol_window_act->trigger();
			createColorWindow();
			createTemplateWindow();
			createTagEditor();
			
			if (map->getNumColors() == 0)
				QTimer::singleShot(0, color_dock_widget, &QWidget::show);

			// Activate the edit tool
			edit_tool_act->setChecked(true);
			setEditTool();
		}
		
		// Set the coordinates display mode
		coordsDisplayChanged();
	}
	else
	{
		// Set the coordinates display mode
		map_widget->setCoordsDisplay(MapWidget::MAP_COORDS);
	}
}

QAction* MapEditorController::newAction(const char* id, const QString &tr_text, QObject* receiver, const char* slot, const char* icon, const QString& tr_tip, const char* whats_this_link)
{
	Q_ASSERT(window); // Qt documentation recommends that actions are created as children of the window they are used in.
	QAction* action = new QAction(icon ? QIcon(QLatin1String(":/images/") + QLatin1String(icon)) : QIcon(), tr_text, window);
	if (!tr_tip.isEmpty()) action->setStatusTip(tr_tip);
	if (whats_this_link) action->setWhatsThis(Util::makeWhatThis(whats_this_link));
	if (receiver) QObject::connect(action, SIGNAL(triggered()), receiver, slot);
	actionsById[id] = action;
	action->setObjectName(QString::fromLatin1(id));  // foom: the shortcut editor addresses actions by id
	action->setMenuRole(QAction::NoRole);
	return action;
}

QAction* MapEditorController::newCheckAction(const char* id, const QString &tr_text, QObject* receiver, const char* slot, const char* icon, const QString& tr_tip, const char* whats_this_link)
{
	auto* action = newAction(id, tr_text, nullptr, nullptr, icon, tr_tip, whats_this_link);
	action->setCheckable(true);
	if (receiver) QObject::connect(action, SIGNAL(triggered(bool)), receiver, slot);
	return action;
}

QAction* MapEditorController::newToolAction(const char* id, const QString &tr_text, QObject* receiver, const char* slot, const char* icon, const QString& tr_tip, const char* whats_this_link)
{
	Q_ASSERT(window); // Qt documentation recommends that actions are created as children of the window they are used in.
	QAction* action = new MapEditorToolAction(icon ? QIcon(QLatin1String(":/images/") + QLatin1String(icon)) : QIcon(), tr_text, window);
	if (!tr_tip.isEmpty()) action->setStatusTip(tr_tip);
	if (whats_this_link) action->setWhatsThis(Util::makeWhatThis(whats_this_link));
	if (receiver) QObject::connect(action, SIGNAL(activated()), receiver, slot);
	actionsById[id] = action;
	action->setObjectName(QString::fromLatin1(id));  // foom
	return action;
}

QAction* MapEditorController::findAction(const char* id)
{
	if (!actionsById.contains(id)) return actionsById[""];
	else return actionsById[id];
}

QAction* MapEditorController::getAction(const char* id)
{
	return actionsById.contains(id) ? actionsById[id] : nullptr;
}

void MapEditorController::assignKeyboardShortcuts()
{
	// Standard keyboard shortcuts
	findAction("print")->setShortcut(QKeySequence::Print);
	findAction("undo")->setShortcut(QKeySequence::Undo);
	findAction("redo")->setShortcut(QKeySequence::Redo);
	findAction("cut")->setShortcut(QKeySequence::Cut);
	findAction("copy")->setShortcut(QKeySequence::Copy);
	findAction("paste")->setShortcut(QKeySequence::Paste);
	
	// Custom keyboard shortcuts
	
	// QKeySequence::Deselect is empty for Windows, so be explicit all select-*
	findAction("select-all")->setShortcut(QKeySequence(tr("Ctrl+A")));
	findAction("select-nothing")->setShortcut(QKeySequence(tr("Ctrl+Shift+A")));
	findAction("invert-selection")->setShortcut(QKeySequence(tr("Ctrl+I")));
	
	findAction("showgrid")->setShortcut(QKeySequence(tr("G")));
	findAction("zoomin")->setShortcuts(QList<QKeySequence>() << QKeySequence(Qt::Key_F7) << QKeySequence(Qt::Key_Plus) << QKeySequence(Qt::KeypadModifier + Qt::Key_Plus));
	findAction("zoomout")->setShortcuts(QList<QKeySequence>() << QKeySequence(Qt::Key_F8) << QKeySequence(Qt::Key_Minus) << QKeySequence(Qt::KeypadModifier + Qt::Key_Minus));
	findAction("hatchareasview")->setShortcut(QKeySequence(Qt::Key_F2));
	findAction("baselineview")->setShortcut(QKeySequence(Qt::Key_F3));
	findAction("hidealltemplates")->setShortcut(QKeySequence(Qt::Key_F10));
	findAction("overprintsimulation")->setShortcut(QKeySequence(Qt::Key_F4));
	findAction("fullscreen")->setShortcut(QKeySequence(Qt::Key_F11));
	tags_window_act->setShortcut(QKeySequence(Qt::CTRL + Qt::SHIFT + Qt::Key_6));
	color_window_act->setShortcut(QKeySequence(Qt::CTRL + Qt::SHIFT + Qt::Key_7));
	symbol_window_act->setShortcut(QKeySequence(Qt::CTRL + Qt::SHIFT + Qt::Key_8));
	template_window_act->setShortcut(QKeySequence(Qt::CTRL + Qt::SHIFT + Qt::Key_9));
	
	findAction("editobjects")->setShortcut(QKeySequence(tr("E")));
	findAction("editlines")->setShortcut(QKeySequence(tr("L")));
	findAction("drawpoint")->setShortcut(QKeySequence(tr("S")));
	findAction("drawpath")->setShortcut(QKeySequence(tr("P")));
	findAction("drawcircle")->setShortcut(QKeySequence(tr("O")));
	findAction("drawrectangle")->setShortcut(QKeySequence(tr("Ctrl+R")));
	findAction("drawfill")->setShortcut(QKeySequence(tr("F")));
	findAction("drawtext")->setShortcut(QKeySequence(tr("T")));
	
	findAction("duplicate")->setShortcut(QKeySequence(tr("D")));
	findAction("switchsymbol")->setShortcut(QKeySequence(tr("Ctrl+G")));
	findAction("fillborder")->setShortcut(QKeySequence(tr("Ctrl+F")));
	findAction("switchdashes")->setShortcut(QKeySequence(tr("Ctrl+D")));
	findAction("connectpaths")->setShortcut(QKeySequence(tr("C")));
	findAction("rotateobjects")->setShortcut(QKeySequence(tr("R")));
	findAction("scaleobjects")->setShortcut(QKeySequence(tr("Z")));
	findAction("cutobject")->setShortcut(QKeySequence(tr("K")));
	findAction("cuthole")->setShortcut(QKeySequence(tr("H")));
	findAction("measure")->setShortcut(QKeySequence(tr("M")));
	findAction("booleanunion")->setShortcut(QKeySequence(tr("U")));
	findAction("booleandifference")->setShortcut(QKeySequence(tr("X")));
	findAction("converttocurves")->setShortcut(QKeySequence(tr("N")));
	findAction("simplify")->setShortcut(QKeySequence(tr("Ctrl+M")));
}

void MapEditorController::createActions()
{
	// Define all the actions, saving them into variables as necessary. Can also get them by ID.
#ifdef QT_PRINTSUPPORT_LIB
	auto* print_act_mapper = new QSignalMapper(this);
	connect(print_act_mapper, QOverload<int>::of(&QSignalMapper::mapped), this, QOverload<int>::of(&MapEditorController::printClicked));
	print_act = newAction("print", tr("Print..."), print_act_mapper, SLOT(map()), "print.png", QString{}, "file_menu.html");
	print_act_mapper->setMapping(print_act, PrintWidget::PRINT_TASK);
	export_image_act = newAction("export-image", tr("&Image"), print_act_mapper, SLOT(map()), nullptr, QString{}, "file_menu.html");
	print_act_mapper->setMapping(export_image_act, PrintWidget::EXPORT_IMAGE_TASK);
	export_kmz_act = newAction("export-kmz", tr("&KMZ"), print_act_mapper, SLOT(map()), nullptr, QString{}, "file_menu.html");
	print_act_mapper->setMapping(export_kmz_act, PrintWidget::EXPORT_KMZ_TASK);
#ifndef MAPPER_USE_GDAL
	export_kmz_act->setVisible(false);
#endif
	export_simple_course_act = newAction("export-simple-course", tr("Simple &course"), this, SLOT(exportSimpleCourse()), nullptr, QString{}, "file_menu.html");
	export_pdf_act = newAction("export-pdf", tr("&PDF"), print_act_mapper, SLOT(map()), nullptr, QString{}, "file_menu.html");
	print_act_mapper->setMapping(export_pdf_act, PrintWidget::EXPORT_PDF_TASK);
#ifdef MAPPER_USE_GDAL
	export_vector_act = newAction("export-vector", ::OpenOrienteering::ImportExport::tr("Geospatial vector data"), this, SLOT(exportVector()), nullptr, {}, "edit_menu.html");
#else
	export_vector_act = nullptr;
#endif
	
#else
	print_act = nullptr;
	export_image_act = nullptr;
	export_kmz_act = nullptr;
	export_simple_course_act = nullptr;
	export_pdf_act = nullptr;
#endif
	
	undo_act = newAction("undo", tr("Undo"), this, SLOT(undo()), "undo.png", tr("Undo the last step"), "edit_menu.html");
	redo_act = newAction("redo", tr("Redo"), this, SLOT(redo()), "redo.png", tr("Redo the last step"), "edit_menu.html");
	cut_act = newAction("cut", tr("Cu&t"), this, SLOT(cut()), "cut.png", QString{}, "edit_menu.html");
	cut_act->setMenuRole(QAction::TextHeuristicRole);
	copy_act = newAction("copy", tr("C&opy"), this, SLOT(copy()), "copy.png", QString{}, "edit_menu.html");
	copy_act->setMenuRole(QAction::TextHeuristicRole);
	auto* paste_act_mapper = new QSignalMapper(this);
	connect(paste_act_mapper, QOverload<int>::of(&QSignalMapper::mapped), this, QOverload<int>::of(&MapEditorController::paste));
	paste_act = newAction("paste", tr("&Paste"), paste_act_mapper, SLOT(map()), "paste", QString{}, "edit_menu.html");
	paste_act->setMenuRole(QAction::TextHeuristicRole);
	paste_act_mapper->setMapping(paste_act, 1);
	paste_original_act = newAction("paste-original", tr("Paste at original location"), paste_act_mapper, SLOT(map()), "paste-at-location.png", QString{}, "edit_menu.html");
	paste_original_act->setMenuRole(QAction::TextHeuristicRole);
	paste_act_mapper->setMapping(paste_original_act, 0);
	delete_act = newAction("delete", tr("Delete"), this, SLOT(deleteClicked()), "delete.png", QString{}, "toolbars.html#delete");
	select_all_act = newAction("select-all", tr("Select all"), this, SLOT(selectAll()), nullptr, QString{}, "edit_menu.html");
	select_nothing_act = newAction("select-nothing", tr("Select nothing"), this, SLOT(selectNothing()), nullptr, QString{}, "edit_menu.html");
	invert_selection_act = newAction("invert-selection", tr("Invert selection"), this, SLOT(invertSelection()), nullptr, QString{}, "edit_menu.html");
	select_by_current_symbol_act = newAction("select-by-symbol", QApplication::translate("OpenOrienteering::SymbolRenderWidget", "Select all objects with selected symbols"), this, SLOT(selectByCurrentSymbols()), nullptr, QString{}, "edit_menu.html");
	find_feature = std::make_unique<MapFindFeature>(*this);
	
	clear_undo_redo_history_act = newAction("clearundoredohistory", tr("Clear undo / redo history"), this, SLOT(clearUndoRedoHistory()), nullptr, tr("Clear the undo / redo history to reduce map file size."), "edit_menu.html");
	
	show_grid_act = newCheckAction("showgrid", tr("Show grid"), this, SLOT(showGrid()), "grid.png", QString{}, "grid.html");
	configure_grid_act = newAction("configuregrid", tr("Configure grid..."), this, SLOT(configureGrid()), "grid.png", QString{}, "grid.html");
	pan_act = newToolAction("panmap", tr("Pan"), this, SLOT(pan()), "move.png", QString{}, "view_menu.html");
	move_to_gps_pos_act = newAction("movegps", tr("Move to my location"), this, SLOT(moveToGpsPos()), "move-to-gps.png", QString{}, "view_menu.html");
	move_to_gps_pos_act->setEnabled(false);
	follow_position_act = newCheckAction("follow-position", tr("Keep my location on screen"), this, SLOT(followPositionClicked(bool)), nullptr, QString{}, "view_menu.html");
	zoom_in_act = newAction("zoomin", tr("Zoom in"), this, SLOT(zoomIn()), "view-zoom-in.png", QString{}, "view_menu.html");
	zoom_out_act = newAction("zoomout", tr("Zoom out"), this, SLOT(zoomOut()), "view-zoom-out.png", QString{}, "view_menu.html");
	box_zoom_act = newCheckAction("boxzoom", tr("Zoom to box"), this, SLOT(boxZoom(bool)), "view-box-zoom.png", QString{}, "view_menu.html");
	show_all_act = newAction("showall", tr("Show whole map"), this, SLOT(showWholeMap()), "view-show-all.png", QString{}, "view_menu.html");
	fullscreen_act = newAction("fullscreen", tr("Toggle fullscreen mode"), window, SLOT(toggleFullscreenMode()), nullptr, QString{}, "view_menu.html");
	custom_zoom_act = newAction("setzoom", tr("Set custom zoom factor..."), this, SLOT(setCustomZoomFactorClicked()), nullptr, QString{}, "view_menu.html");
	view_back_act = newAction("view-back", tr("Back"), this, SLOT(viewBack()));  // foom
	view_forward_act = newAction("view-forward", tr("Forward"), this, SLOT(viewForward()));  // foom
	bookmark_add_act = newAction("bookmark-add", tr("Add bookmark..."), this, SLOT(bookmarkAddClicked()));  // foom
	find_replace_text_act = newAction("find-replace-text", tr("Find and replace text..."), this, SLOT(findReplaceTextClicked()));  // foom
	extent_define_act = newAction("extent-define", tr("Define extent..."), this, SLOT(extentDefineClicked()));  // foom
	check_map_act = newAction("check-map", tr("Check map"), this, SLOT(runMapCheck()));  // foom
	check_legibility_act = newAction("check-legibility", tr("Check legibility..."), this, SLOT(checkLegibilityClicked()));  // foom
	topo_join_act = newAction("topo-join", tr("Join lines..."), this, SLOT(joinLinesClicked()));  // foom
	close_areas_act = newAction("close-areas", tr("Close areas..."), this, SLOT(closeAreasClicked()));  // foom
	areas_ccw_act = newAction("areas-ccw", tr("Areas anticlockwise..."), this, SLOT(areasAnticlockwiseClicked()));  // foom
	fix_overshoots_act = newAction("fix-overshoots", tr("Remove overshoots and undershoots..."), this, SLOT(fixOvershootsClicked()));  // foom
	intersections_act = newAction("intersections", tr("Intersections..."), this, SLOT(intersectionsClicked()));  // foom
	remove_duplicate_vertices_act = newAction("remove-duplicate-vertices", tr("Remove duplicate vertices"), this, SLOT(removeDuplicateVertices()));  // foom
	lengthen_line_text_act = newAction("lengthen-line-text", tr("Lengthen line text..."), this, SLOT(lengthenLineTextClicked()));  // foom
	topo_smooth_act = newAction("topo-smooth", tr("Smooth..."), this, SLOT(smoothClicked()));  // foom
	make_rectangular_act = newAction("make-rectangular", tr("Make rectangular..."), this, SLOT(makeRectangularClicked()));  // foom
	generalize_buildings_act = newAction("generalize-buildings", tr("Generalize buildings..."), this, SLOT(generalizeBuildingsClicked()));  // foom
	dem_open_act = newAction("dem-open", tr("Import elevation data..."), this, SLOT(elevationOpenClicked()));  // foom
	if (PointCloud::supported())
		dem_point_cloud_act = newAction("dem-point-cloud", tr("Import point cloud..."), this, SLOT(elevationPointCloudClicked()));  // foom
	dem_info_act = newAction("dem-info", tr("Elevation information..."), this, SLOT(elevationInfoClicked()));  // foom
	dem_contours_act = newAction("dem-contours", tr("Contours..."), this, SLOT(elevationContoursClicked()));  // foom
	dem_hillshade_act = newAction("dem-hillshade", tr("Hill shading..."), this, SLOT(elevationHillShadeClicked()));  // foom
	dem_slope_act = newAction("dem-slope", tr("Slope gradient..."), this, SLOT(elevationSlopeClicked()));  // foom
	dem_vegetation_height_act = newAction("dem-vegetation-height", tr("Vegetation height..."), this, SLOT(elevationVegetationHeightClicked()));  // foom
	dem_merge_contours_act = newAction("dem-merge-contours", tr("Merge contour lines..."), this, SLOT(elevationMergeContoursClicked()));  // foom
	dem_profile_act = newAction("dem-profile", tr("Profile..."), this, SLOT(elevationProfileClicked()));  // foom
	dem_hypsometric_act = newAction("dem-hypsometric", tr("Hypsometric map..."), this, SLOT(elevationHypsometricClicked()));  // foom
	dem_features_act = newAction("dem-features", tr("Extract features..."), this, SLOT(elevationFeaturesClicked()));  // foom
	dem_export_act = newAction("dem-export", tr("Export grid..."), this, SLOT(elevationExportClicked()));  // foom
	dem_merge_act = newAction("dem-merge", tr("Merge elevation grids..."), this, SLOT(elevationMergeClicked()));  // foom
	dem_difference_act = newAction("dem-difference", tr("Surface less terrain"), this, SLOT(elevationDifferenceClicked()));  // foom
	dem_resize_act = newAction("dem-resize", tr("Crop elevation grid..."), this, SLOT(elevationResizeClicked()));  // foom
	dem_smooth_act = newAction("dem-smooth", tr("Smooth elevation grid..."), this, SLOT(elevationSmoothClicked()));  // foom
	dem_close_act = newAction("dem-close", tr("Close elevation grids"), this, SLOT(elevationCloseClicked()));  // foom
	issues_window_act = newCheckAction("issueswindow", tr("Issues window"), this, SLOT(showIssuesWindow(bool)));  // foom
	issue_model = new IssueModel(this);
	connect(issue_model, &IssueModel::issueActivated, this, &MapEditorController::issueActivated);
	extent_delete_act = newAction("extent-delete", tr("Delete extent..."), this, SLOT(extentDeleteClicked()));  // foom
	extent_import_act = newAction("extent-import", tr("Import extents..."), this, SLOT(extentImportClicked()));  // foom
	bookmark_manage_act = newAction("bookmark-manage", tr("Manage bookmarks..."), this, SLOT(bookmarkManageClicked()));  // foom
	view_history_timer.setSingleShot(true);  // foom
	view_history_timer.setInterval(500);
	connect(&view_history_timer, &QTimer::timeout, this, &MapEditorController::recordView);
	view_back_act->setEnabled(view_history.canGoBack());
	view_forward_act->setEnabled(view_history.canGoForward());
	
	hatch_areas_view_act = newCheckAction("hatchareasview", tr("Hatch areas"), this, SLOT(hatchAreas(bool)), "view-hatch-areas.png", QString{}, "view_menu.html");
	baseline_view_act = newCheckAction("baselineview", tr("Baseline view"), this, SLOT(baselineView(bool)), "view-baseline.png", QString{}, "view_menu.html");
	hide_all_templates_act = newCheckAction("hidealltemplates", tr("Hide all templates"), this, SLOT(hideAllTemplates(bool)), nullptr, QString{}, "view_menu.html");
	favourite_templates_act = newCheckAction("view-favourites-only", tr("Favourite templates only"), this, SLOT(favouriteTemplatesOnly(bool)));  // foom
	overprinting_simulation_act = newCheckAction("overprintsimulation", tr("Overprinting simulation"), this, SLOT(overprintingSimulation(bool)), nullptr, QString{}, "view_menu.html");
	
	symbol_window_act = newCheckAction("symbolwindow", tr("Symbol window"), this, SLOT(showSymbolWindow(bool)), "symbols.png", tr("Show/Hide the symbol window"), "symbol_dock_widget.html");
	color_window_act = newCheckAction("colorwindow", tr("Color window"), this, SLOT(showColorWindow(bool)), "colors.png", tr("Show/Hide the color window"), "color_dock_widget.html");
	symbol_set_id_act = newAction("symbol-set-id", tr("Symbol set ID..."), this, SLOT(symbolSetIdClicked()), nullptr, tr("Edit the symbol set ID"), nullptr);
	load_symbols_from_act = newAction("loadsymbols", tr("Replace symbol set..."), this, SLOT(loadSymbolsFromClicked()), nullptr, tr("Replace the symbols with those from another map file"), "symbol_replace_dialog.html");
	load_crt_act = newAction("loadcrt", tr("Load CRT file..."), this, SLOT(loadCrtClicked()), nullptr, tr("Assign new symbols by cross-reference table"), "symbol_replace_dialog.html");
	/*QAction* load_colors_from_act = newAction("loadcolors", tr("Load colors from..."), this, SLOT(loadColorsFromClicked()), nullptr, tr("Replace the colors with those from another map file"));*/
	symbol_report_feature = std::make_unique<SymbolReportFeature>(*this);
	
	scale_all_symbols_act = newAction("scaleall", tr("Scale all symbols..."), this, SLOT(scaleAllSymbolsClicked()), nullptr, tr("Scale the whole symbol set"), "map_menu.html");
	georeferencing_act = newAction("georef", tr("Georeferencing..."), this, SLOT(editGeoreferencing()), nullptr, QString{}, "georeferencing.html");
	scale_map_act = newAction("scalemap", tr("Change map scale..."), this, SLOT(scaleMapClicked()), "tool-scale.png", tr("Change the map scale and adjust map objects and symbol sizes"), "map_menu.html");
	rotate_map_act = newAction("rotatemap", tr("Rotate map..."), this, SLOT(rotateMapClicked()), "tool-rotate.png", tr("Rotate the whole map"), "map_menu.html");
	map_move_act = newAction("map-move", tr("Move map..."), this, SLOT(mapMoveClicked()));  // foom
	map_stretch_act = newAction("map-stretch", tr("Stretch / shrink map..."), this, SLOT(mapStretchClicked()));  // foom
	map_mirror_act = newAction("map-mirror", tr("Mirror map..."), this, SLOT(mapMirrorClicked()));  // foom
	map_rotate_magnetic_act = newAction("map-rotate-magnetic", tr("Rotate map to magnetic north..."), this, SLOT(mapRotateMagneticClicked()));  // foom
	map_center_act = newAction("map-center", tr("Center map in drawing area..."), this, SLOT(mapCenterClicked()));  // foom
	map_reproject_act = newAction("map-reproject", tr("Change coordinate system..."), this, SLOT(mapReprojectClicked()));  // foom
	map_local_warp_act = newToolAction("map-local-warp", tr("Local transformation..."), this, SLOT(mapLocalWarpClicked()), nullptr, tr("Opens the dialog of the local transformation; then drag from a centre to a target to warp the map within a radius."));  // foom
	map_rubbersheet_act = newToolAction("map-rubbersheet", tr("Rubbersheeting..."), this, SLOT(mapRubbersheetClicked()), nullptr, tr("Click a point of the map, then the same point on the reference; the window beside the map holds the points, the perimeter and the options. The selected closed paths are the perimeter."));  // foom
	map_affine_act = newToolAction("map-affine", tr("Affine..."), this, SLOT(mapAffineClicked()), nullptr, tr("Click a point of the map, then the same point on the reference; Enter transforms the map."));  // foom
	map_notes_act = newAction("mapnotes", tr("Map notes..."), this, SLOT(mapNotesClicked()), nullptr, QString{}, "map_menu.html");
	map_info_act = newAction("mapinfo", tr("Map information..."), this, SLOT(mapInfoClicked()), "map-information.png", QString{}, "map_menu.html");
	
	template_window_act = newCheckAction("templatewindow", tr("Template setup window"), this, SLOT(showTemplateWindow(bool)), "templates.png", tr("Show/Hide the template window"), "templates_menu.html");
	//QAction* template_config_window_act = newCheckAction("templateconfigwindow", tr("Template configurations window"), this, SLOT(showTemplateConfigurationsWindow(bool)), "window-new", tr("Show/Hide the template configurations window"));
	//QAction* template_visibilities_window_act = newCheckAction("templatevisibilitieswindow", tr("Template visibilities window"), this, SLOT(showTemplateVisbilitiesWindow(bool)), "window-new", tr("Show/Hide the template visibilities window"));
	open_template_act = newAction("opentemplate", tr("Open template..."), this, SLOT(openTemplateClicked()), nullptr, QString{}, "templates_menu.html");
	reopen_template_act = newAction("reopentemplate", tr("Reopen template..."), this, SLOT(reopenTemplateClicked()), nullptr, QString{}, "templates_menu.html");
	add_map_service_act = newAction("template-add-service", tr("Add map service..."), this, SLOT(addMapServiceClicked()));  // foom
	relink_templates_act = newAction("templates-relink", tr("Relink by folder..."), this, SLOT(relinkTemplatesClicked()));  // foom
	updateMapServiceAction();  // foom
	
	tags_window_act = newCheckAction("tagswindow", tr("Tag editor"), this, SLOT(showTagsWindow(bool)), "tag-editor.png", tr("Show/Hide the tag editor window"), "tag_editor.html");
	
	edit_tool_act = newToolAction("editobjects", tr("Edit objects"), this, SLOT(editToolClicked()), "tool-edit.png", QString{}, "toolbars.html#tool_edit_point");
	edit_line_tool_act = newToolAction("editlines", tr("Edit lines"), this, SLOT(editLineToolClicked()), "tool-edit-line.png", QString{}, "toolbars.html#tool_edit_line");
	lasso_act = newToolAction("select-lasso", tr("Lasso selection"), this, SLOT(lassoClicked()));  // foom
	// foom: the design notes, "Sketch layer"
	sketch_pen_act = newToolAction("sketch-pen", tr("Sketch pen"), this, SLOT(sketchPenClicked()));
	sketch_eraser_act = newToolAction("sketch-eraser", tr("Sketch eraser"), this, SLOT(sketchEraserClicked()));
	sketch_note_act = newToolAction("sketch-note", tr("Sketch note"), this, SLOT(sketchNoteClicked()));
	sketch_manage_act = newAction("sketch-manage", tr("Manage sketch..."), this, SLOT(sketchManageClicked()));
	sketch_flatten_act = newAction("sketch-flatten", tr("Flatten sketch to template..."), this, SLOT(sketchFlattenClicked()));
	draw_point_act = newToolAction("drawpoint", tr("Set point objects"), this, SLOT(drawPointClicked()), "draw-point.png", QString{}, "toolbars.html#tool_draw_point");
	draw_path_act = newToolAction("drawpath", tr("Draw paths"), this, SLOT(drawPathClicked()), "draw-path.png", QString{}, "toolbars.html#tool_draw_path");
	draw_circle_act = newToolAction("drawcircle", tr("Draw circles and ellipses"), this, SLOT(drawCircleClicked()), "draw-circle.png", QString{}, "toolbars.html#tool_draw_circle");
	draw_rectangle_act = newToolAction("drawrectangle", tr("Draw rectangles"), this, SLOT(drawRectangleClicked()), "draw-rectangle.png", QString{}, "toolbars.html#tool_draw_rectangle");
	draw_rectangle_box_act = newToolAction("draw-rectangle-box", tr("Rectangle box..."), this, SLOT(drawRectangleBoxClicked()), "draw-rectangle.png");  // foom
	draw_stairway_act = newToolAction("draw-stairway", tr("Stairway..."), this, SLOT(drawStairwayClicked()), "draw-rectangle.png");  // foom
	draw_rectangle_open_act = newCheckAction("draw-rectangle-open", tr("Leave rectangles open"), nullptr, nullptr);  // foom
	draw_frame_act = newAction("draw-frame", tr("Frame around map..."), this, SLOT(drawFrameClicked()));  // foom
	draw_points_along_act = newToolAction("draw-points-along", tr("Points along a line..."), this, SLOT(drawPointsAlongClicked()), "draw-point.png");  // foom
	draw_sized_shape_act = newToolAction("draw-sized-shape", tr("Shape of a given size..."), this, SLOT(drawSizedShapeClicked()), "draw-circle.png");  // foom
	{
		// foom: the design notes, "Freehand smoothing"
		auto* group = new QActionGroup(this);
		const char* const ids[4] = { "freehand-smoothing-off", "freehand-smoothing-slight", "freehand-smoothing-medium", "freehand-smoothing-strong" };
		const QString names[4] = { tr("Off"), tr("Slight"), tr("Medium"), tr("Strong") };
		for (int i = 0; i < 4; ++i)
		{
			freehand_smoothing_acts[i] = newCheckAction(ids[i], names[i], this, SLOT(freehandSmoothingChosen()));
			group->addAction(freehand_smoothing_acts[i]);
		}
		freehand_smoothing_acts[freehandSmoothingLevel()]->setChecked(true);
	}
	auto_join_act = newCheckAction("auto-join", tr("Join while drawing"), this, SLOT(autoJoinToggled(bool)));  // foom
	auto_join_act->setChecked(autoJoinEnabled());
	draw_freehand_act = newToolAction("drawfreehand", tr("Draw free-handedly"), this, SLOT(drawFreehandClicked()), "draw-freehand.png", QString{}, "toolbars.html#tool_draw_freehand"); // TODO: documentation
	draw_fill_act = newToolAction("drawfill", tr("Fill bounded areas"), this, SLOT(drawFillClicked()), "tool-fill.png", QString{}, "toolbars.html#tool_draw_fill"); // TODO: documentation
	draw_text_act = newToolAction("drawtext", tr("Write text"), this, SLOT(drawTextClicked()), "draw-text.png", QString{}, "toolbars.html#tool_draw_text");
	duplicate_act = newAction("duplicate", tr("Duplicate"), this, SLOT(duplicateClicked()), "tool-duplicate.png", QString{}, "toolbars.html#duplicate");
	switch_symbol_act = newAction("switchsymbol", tr("Switch symbol"), this, SLOT(switchSymbolClicked()), "tool-switch-symbol.png", QString{}, "toolbars.html#switch_symbol");
	fill_border_act = newAction("fillborder", tr("Fill / Create border"), this, SLOT(fillBorderClicked()), "tool-fill-border.png", QString{}, "toolbars.html#fill_create_border");
	switch_dashes_act = newAction("switchdashes", tr("Switch dash direction"), this, SLOT(switchDashesClicked()), "tool-switch-dashes.png", QString{}, "toolbars.html#switch_dashes");
	connect_paths_act = newAction("connectpaths", tr("Connect paths"), this, SLOT(connectPathsClicked()), "tool-connect-paths.png", QString{}, "toolbars.html#connect");
	
	cut_tool_act = newToolAction("cutobject", tr("Cut object"), this, SLOT(cutClicked()), "tool-cut.png", QString{}, "toolbars.html#cut_tool");
	reshape_act = newToolAction("reshape", tr("Reshape"), this, SLOT(reshapeClicked()));  // foom
	move_parallel_act = newToolAction("move-parallel", tr("Move parallel"), this, SLOT(moveParallelClicked()));  // foom
	move_parallel_by_act = newAction("move-parallel-by", tr("Move parallel by..."), this, SLOT(moveParallelByClicked()));  // foom
	vertex_type_normal_act = newAction("set-vertex-type-normal", tr("Normal"), this, SLOT(setVertexTypeClicked()));  // foom
	vertex_type_normal_act->setData(false);
	vertex_type_dash_act = newAction("set-vertex-type-dash", tr("Dash point"), this, SLOT(setVertexTypeClicked()));  // foom
	vertex_type_dash_act->setData(true);
	vertex_type_keep_ends_act = newCheckAction("set-vertex-type-keep-ends", tr("Keep first and last vertex"), nullptr, nullptr);  // foom
	vertex_type_keep_ends_act->setChecked(true);
	group_act = newAction("group", tr("Group..."), this, SLOT(groupClicked()));  // foom
	ungroup_act = newAction("ungroup", tr("Ungroup"), this, SLOT(ungroupSelection()));  // foom
	select_group_act = newAction("select-group", tr("Select group"), this, SLOT(selectGroup()));  // foom
	select_duplicates_act = newAction("select-duplicates", tr("Select duplicate objects"), this, SLOT(selectDuplicates()));  // foom
	select_self_intersecting_act = newAction("select-self-intersecting", tr("Select self-intersecting objects"), this, SLOT(selectSelfIntersecting()));  // foom
	select_invalid_act = newAction("select-invalid", tr("Select objects with invalid geometry"), this, SLOT(selectInvalid()));  // foom
	select_by_date_act = newAction("select-by-date", tr("Select objects by date..."), this, SLOT(selectByDateClicked()));  // foom
	select_by_property_act = newAction("select-by-property", tr("Select objects by property..."), this, SLOT(selectByPropertyClicked()));  // foom
	select_by_index_act = newAction("select-by-index", tr("Select object by index..."), this, SLOT(selectByIndexClicked()));  // foom
	select_next_act = newAction("select-next", tr("Select next object"), this, SLOT(selectNext()));  // foom
	selection_save_act = newAction("selection-save", tr("Save selection..."), this, SLOT(saveSelectionClicked()));  // foom
	selection_load_act = newAction("selection-load", tr("Reload selection..."), this, SLOT(loadSavedSelectionClicked()));  // foom
	selection_delete_act = newAction("selection-delete", tr("Delete saved selection..."), this, SLOT(deleteSavedSelectionClicked()));  // foom
	cut_hole_act = newToolAction("cuthole", tr("Cut free form hole"), this, SLOT(cutHoleClicked()), "tool-cut-hole.png", QString{}, "toolbars.html#cut_hole"); // cut hole using a path
	cut_hole_circle_act = newToolAction("cutholecircle", tr("Cut round hole"), this, SLOT(cutHoleCircleClicked()), "tool-cut-hole.png", QString{}, "toolbars.html#cut_hole");
	cut_hole_rectangle_act = newToolAction("cutholerectangle", tr("Cut rectangular hole"), this, SLOT(cutHoleRectangleClicked()), "tool-cut-hole.png", QString{}, "toolbars.html#cut_hole");
	cut_hole_menu = new QMenu(tr("Cut hole"));
	cut_hole_menu->menuAction()->setMenuRole(QAction::NoRole);
	cut_hole_menu->setIcon(QIcon(QString::fromLatin1(":/images/tool-cut-hole.png")));
	cut_hole_menu->addAction(cut_hole_act);
	cut_hole_menu->addAction(cut_hole_circle_act);
	cut_hole_menu->addAction(cut_hole_rectangle_act);
	
	rotate_act = newToolAction("rotateobjects", tr("Rotate objects"), this, SLOT(rotateClicked()), "tool-rotate.png", QString{}, "toolbars.html#rotate");
	rotate_pattern_act = newToolAction("rotatepatterns", tr("Rotate pattern"), this, SLOT(rotatePatternClicked()), "tool-rotate-pattern.png", QString{}, "toolbars.html#tool_rotate_pattern");
	scale_act = newToolAction("scaleobjects", tr("Scale objects"), this, SLOT(scaleClicked()), "tool-scale.png", QString{}, "toolbars.html#scale");
	measure_act = newCheckAction("measure", tr("Measure lengths and areas"), this, SLOT(measureClicked(bool)), "tool-measure.png", QString{}, "toolbars.html#measure");
	boolean_union_act = newAction("booleanunion", tr("Unify areas"), this, SLOT(booleanUnionClicked()), "tool-boolean-union.png", QString{}, "toolbars.html#unify_areas");
	boolean_intersection_act = newAction("booleanintersection", tr("Intersect areas"), this, SLOT(booleanIntersectionClicked()), "tool-boolean-intersection.png", QString{}, "toolbars.html#intersect_areas");
	boolean_difference_act = newAction("booleandifference", tr("Cut away from area"), this, SLOT(booleanDifferenceClicked()), "tool-boolean-difference.png", QString{}, "toolbars.html#area_difference");
	boolean_xor_act = newAction("booleanxor", tr("Area XOr"), this, SLOT(booleanXOrClicked()), "tool-boolean-xor.png", QString{}, "toolbars.html#area_xor");
	boolean_merge_holes_act = newAction("booleanmergeholes", tr("Merge area holes"), this, SLOT(booleanMergeHolesClicked()), "tool-boolean-merge-holes.png", QString{}, "toolbars.html#area_merge_holes"); // TODO:documentation
	convert_to_curves_act = newAction("converttocurves", tr("Convert to curves"), this, SLOT(convertToCurvesClicked()), "tool-convert-to-curves.png", QString{}, "toolbars.html#convert_to_curves");
	simplify_path_act = newAction("simplify", tr("Simplify path"), this, SLOT(simplifyPathClicked()), "tool-simplify-path.png", QString{}, "toolbars.html#simplify_path");
	to_polyline_act = newAction("to-polyline", tr("To polyline..."), this, SLOT(toPolylineClicked()));  // foom
	to_curve_act = newAction("to-curve", tr("To curve..."), this, SLOT(toCurveClicked()));  // foom
	mirror_act = newAction("mirror", tr("Mirror..."), this, SLOT(mirrorClicked()));  // foom
	convert_to_point_act = newAction("convert-to-point", tr("Convert to point objects..."), this, SLOT(convertToPointsClicked()));  // foom
	interpolate_act = newAction("interpolate", tr("Interpolate..."), this, SLOT(interpolateClicked()));  // foom
	export_descriptions_act = newAction("symbols-descriptions-export", tr("Export symbol descriptions..."), this, SLOT(exportSymbolDescriptionsClicked()));  // foom
	import_descriptions_act = newAction("symbols-descriptions-import", tr("Import symbol descriptions..."), this, SLOT(importSymbolDescriptionsClicked()));  // foom
	update_symbols_act = newAction("symbols-update", tr("Update from file..."), this, SLOT(updateSymbolsClicked()));  // foom
	compare_symbols_act = newAction("symbols-compare", tr("Compare with file..."), this, SLOT(compareSymbolsClicked()));  // foom
	symbol_groups_act = newCheckAction("symbols-groups", tr("Symbol groups"), this, SLOT(showSymbolGroups(bool)));  // foom
	status_save_act = newAction("symbols-status-save", tr("Save status set..."), this, SLOT(saveStatusSetClicked()));  // foom
	status_apply_act = newAction("symbols-status-apply", tr("Apply status set..."), this, SLOT(applyStatusSetClicked()));  // foom
	status_delete_act = newAction("symbols-status-delete", tr("Delete status set..."), this, SLOT(deleteStatusSetClicked()));  // foom
	replace_font_act = newAction("symbols-replace-font", tr("Replace font..."), this, SLOT(replaceFontClicked()));  // foom
	replace_color_act = newAction("symbols-replace-color", tr("Replace colour..."), this, SLOT(replaceColorClicked()));  // foom
	renumber_symbols_act = newAction("symbols-renumber", tr("Renumber symbols..."), this, SLOT(renumberSymbolsClicked()));  // foom
	object_info_act = newAction("object-info", tr("Object information..."), this, SLOT(objectInformationClicked()));  // foom
	move_by_act = newAction("move-by", tr("Move by..."), this, SLOT(moveByClicked()));  // foom
	rotate_by_act = newAction("rotate-by", tr("Rotate by..."), this, SLOT(rotateByClicked()));  // foom
	stretch_act = newAction("stretch", tr("Stretch..."), this, SLOT(stretchClicked()));  // foom
	align_acts[ops::AlignParams::Left] = newAction("align-left", tr("Align left"), this, SLOT(alignClicked()));  // foom
	align_acts[ops::AlignParams::HCentre] = newAction("align-hcenter", tr("Align centers horizontally"), this, SLOT(alignClicked()));  // foom
	align_acts[ops::AlignParams::Right] = newAction("align-right", tr("Align right"), this, SLOT(alignClicked()));  // foom
	align_acts[ops::AlignParams::Top] = newAction("align-top", tr("Align top"), this, SLOT(alignClicked()));  // foom
	align_acts[ops::AlignParams::VCentre] = newAction("align-vcenter", tr("Align centers vertically"), this, SLOT(alignClicked()));  // foom
	align_acts[ops::AlignParams::Bottom] = newAction("align-bottom", tr("Align bottom"), this, SLOT(alignClicked()));  // foom
	for (int edge = ops::AlignParams::Left; edge <= ops::AlignParams::Bottom; ++edge)  // foom
		align_acts[edge]->setData(edge);
	distribute_h_act = newAction("distribute-h", tr("Distribute horizontally"), this, SLOT(distributeClicked()));  // foom
	distribute_h_act->setData(true);
	distribute_v_act = newAction("distribute-v", tr("Distribute vertically"), this, SLOT(distributeClicked()));  // foom
	distribute_v_act->setData(false);
	clip_area_act = newToolAction("cliparea", tr("Clip area"), this, SLOT(clipAreaClicked()), "tool-clip.png", QString{}, "toolbars.html#clip_area");
	erase_area_act = newToolAction("erasearea", tr("Erase area"), this, SLOT(eraseAreaClicked()), "tool-erase.png", QString{}, "toolbars.html#erase_area");
	distribute_points_act = newAction("distributepoints", tr("Distribute points along path"), this, SLOT(distributePointsClicked()), "tool-distribute-points.png", QString{}, "toolbars.html#distribute_points"); // TODO: write documentation
	
	paint_feature = std::make_unique<PaintOnTemplateFeature>(*this);
	
	touch_cursor_action = newCheckAction("touchcursor", tr("Enable touch cursor"), map_widget, SLOT(enableTouchCursor(bool)), "tool-touch-cursor.png", QString{}, "toolbars.html#touch_cursor"); // TODO: write documentation
	gps_display_action = newCheckAction("gpsdisplay", tr("Enable GPS display"), this, SLOT(enableGPSDisplay(bool)), "tool-gps-display.png", QString{}, "toolbars.html#gps_display"); // TODO: write documentation
	gps_display_action->setEnabled(map->getGeoreferencing().getState() == Georeferencing::Geospatial);
	gps_distance_rings_action = newCheckAction("gpsdistancerings", tr("Enable GPS distance rings"), this, SLOT(enableGPSDistanceRings(bool)), "gps-distance-rings.png", QString{}, "toolbars.html#gps_distance_rings"); // TODO: write documentation
	gps_distance_rings_action->setEnabled(false);
	draw_point_gps_act = newToolAction("drawpointgps", tr("Set point object at GPS position"), this, SLOT(drawPointGPSClicked()), "draw-point-gps.png", QString{}, "toolbars.html#tool_draw_point_gps"); // TODO: write documentation
	draw_point_gps_act->setEnabled(false);
	gps_temporary_point_act = newAction("gpstemporarypoint", tr("Set temporary marker at GPS position"), this, SLOT(gpsTemporaryPointClicked()), "gps-temporary-point.png", QString{}, "toolbars.html#gps_temporary_point"); // TODO: write documentation
	gps_temporary_point_act->setEnabled(false);
	gps_temporary_path_act = newCheckAction("gpstemporarypath", tr("Create temporary path at GPS position"), this, SLOT(gpsTemporaryPathClicked(bool)), "gps-temporary-path.png", QString{}, "toolbars.html#gps_temporary_path"); // TODO: write documentation
	gps_temporary_path_act->setEnabled(false);
	gps_temporary_clear_act = newAction("gpstemporaryclear", tr("Clear temporary GPS markers"), this, SLOT(gpsTemporaryClearClicked()), "gps-temporary-clear.png", QString{}, "toolbars.html#gps_temporary_clear"); // TODO: write documentation
	gps_temporary_clear_act->setEnabled(false);
	// foom: the design notes, "Recording into a symbol"
	gps_record_act = newCheckAction("gps-record", tr("Record GPS positions into the path"), this, SLOT(gpsRecordClicked(bool)), nullptr, tr("While a path is drawn, adds a vertex for every GPS position which is far enough from the last one."));
	gps_record_act->setEnabled(false);
	gps_record_pause_act = newCheckAction("gps-record-pause", tr("Pause recording"), this, SLOT(gpsRecordPauseClicked(bool)));
	gps_record_pause_act->setEnabled(false);
	gps_add_vertex_act = newAction("gps-add-vertex", tr("Add vertex at GPS position"), this, SLOT(gpsAddVertexClicked()));
	gps_add_vertex_act->setEnabled(false);
	gps_average_vertex_act = newCheckAction("gps-average-vertex", tr("Average GPS positions for a vertex"), this, SLOT(gpsAverageVertexClicked(bool)), nullptr, tr("While it is on, the GPS positions are averaged. Switching it off adds the mean as a vertex."));
	gps_average_vertex_act->setEnabled(false);
	// foom: the offset of the GPS position; the design notes, "Position offset and sentence log"
	gps_set_offset_act = newToolAction("gps-set-offset", tr("Set GPS position offset"), this, SLOT(gpsSetOffsetClicked()), nullptr, tr("Click the place on the map where the GPS receiver is. The difference is added to the GPS positions which follow."));
	gps_set_offset_act->setEnabled(false);
	gps_clear_offset_act = newAction("gps-clear-offset", tr("Remove GPS position offset"), this, SLOT(gpsClearOffsetClicked()));
	gps_clear_offset_act->setEnabled(false);
	// foom: the course dock; the design notes, "Course dock"
	course_dock_act = newCheckAction("course-dock", tr("Course setting"), this, SLOT(courseDockClicked(bool)), nullptr, tr("Show or hide the course objects, the courses and their classes."));
	// foom: the course symbols; the design notes, "FOOM's course symbols"
	course_symbols_act = newAction("course-symbols", tr("Add course symbols"), this, SLOT(addCourseSymbolsClicked()), nullptr, tr("Add the symbols of course setting to this map: start, control and finish, and the symbols of the control descriptions."));
	// foom: the design notes, "Composing a course and the bulk commands"
	compose_course_act = newToolAction("compose-course", tr("Compose course"), this, SLOT(composeCourseClicked()), "draw-point.png", tr("Compose the course of the course dock by clicks in the map: start, controls and finish."));
	course_insert_act = newAction("course-insert-into-courses", tr("Insert course object into courses..."), this, SLOT(insertIntoCoursesClicked()), nullptr, tr("Put a course object into many courses at once."));
	course_delete_act = newAction("course-delete-from-courses", tr("Delete course object from courses..."), this, SLOT(deleteFromCoursesClicked()), nullptr, tr("Take a course object out of many courses at once. The object stays in the map."));
	course_renumber_act = newAction("course-renumber", tr("Renumber all controls..."), this, SLOT(renumberControlsClicked()), nullptr, tr("Give the controls new codes, from a new first code on."));
	// foom: the design notes, "Punching unit ids"
	course_punch_ids_act = newAction("course-punch-ids", tr("Punching unit IDs..."), this, SLOT(punchIdsClicked()), nullptr, tr("Give the controls the IDs of their punching units."));
	// foom: the design notes, "Statistics and the consistency check"
	course_statistics_act = newAction("course-statistics", tr("Statistics..."), this, SLOT(courseStatisticsClicked()), nullptr, tr("Show the statistics of the courses, their legs and the event."));
	course_check_act = newAction("course-check", tr("Consistency check"), this, SLOT(runCourseCheck()), nullptr, tr("Check the courses, the controls and their descriptions, and list what is found in the issues window."));
	// foom: the design notes, "Interchange: IOF XML, GPX and KML"
	course_export_iof_act = newAction("course-export-iof", tr("Export courses (IOF XML 3.0)..."), this, SLOT(exportCoursesIofClicked()), nullptr, tr("Write the course objects, the courses and the classes as course data of the IOF Data Standard 3.0."));
	course_export_gpx_act = newAction("course-export-gpx", tr("Export courses (GPX)..."), this, SLOT(exportCoursesGpxClicked()), nullptr, tr("Write start, controls and finish as waypoints and the courses as tracks."));
	course_export_kml_act = newAction("course-export-kml", tr("Export courses (KML)..."), this, SLOT(exportCoursesKmlClicked()), nullptr, tr("Write start, controls and finish as placemarks and the courses as lines."));
	course_export_kmz_act = newAction("course-export-kmz", tr("Export courses (KMZ)..."), this, SLOT(exportCoursesKmzClicked()), nullptr, tr("Write the placemarks and lines of the courses together with a picture of the map, or of the map of the course."));
	course_import_geodata_act = newAction("course-import-gpx-kml", tr("Import GPX or KML..."), this, SLOT(importGeodataCoursesClicked()), nullptr, tr("Take waypoints as course objects, and tracks which start and end at course objects as courses."));
	course_import_iof_act = newAction("course-import-iof", tr("Import IOF XML..."), this, SLOT(importIofCoursesClicked()), nullptr, tr("Take course objects, courses and classes from course data of the IOF Data Standard 3.0."));
	// foom: the design notes, "Relays: variations, variants and their allocation"
	course_canvases_act = newAction("course-canvases", tr("Canvases..."), this, SLOT(courseCanvasesClicked()), nullptr, tr("Say for which courses which extent of the map is printed, at which scale and with which course symbols."));
	course_routes_analyse_act = newAction("course-routes-analyse", tr("Analyse routes of the course"), this, SLOT(analyseCourseRoutesClicked()), nullptr, tr("Search the fastest route of every leg of the course which is chosen in the course dock, and draw the routes over the map."));
	course_routes_analyse_all_act = newAction("course-routes-analyse-all", tr("Analyse routes of all courses"), this, SLOT(analyseAllCourseRoutesClicked()), nullptr, tr("Search the fastest route of every leg of all courses, and keep the routes in the map."));
	course_routes_show_act = newAction("course-routes-show", tr("Show routes"), this, SLOT(showCourseRoutesClicked()), nullptr, tr("Draw the routes which the map keeps for the course which is chosen in the course dock."));
	course_routes_hide_act = newAction("course-routes-hide", tr("Hide routes"), this, SLOT(hideCourseRoutesClicked()), nullptr, tr("Do not draw the routes of the analysis over the map. They stay in the map."));
	course_routes_via_act = newToolAction("course-routes-via", tr("Add route via points"), this, SLOT(routeViaPointsClicked()), "draw-point.png", tr("Lead the route of a leg over places of your choice: drag from the route to the place."));
	course_routes_leg_act = newToolAction("course-routes-leg", tr("Draw leg and analyse route"), this, SLOT(routeFreeLegClicked()), "draw-point.png", tr("Search the fastest route between two places: drag from the one to the other."));
	course_routes_delete_act = newAction("course-routes-delete", tr("Delete routes"), this, SLOT(deleteCourseRoutesClicked()), nullptr, tr("Take the routes of the analysis out of the map."));
	course_routes_export_act = newAction("course-routes-export", tr("Export routes..."), this, SLOT(exportCourseRoutesClicked()), nullptr, tr("Write the lengths and the times of the routes of all courses as a text file."));
	course_routes_settings_act = newAction("course-routes-settings", tr("Route settings..."), this, SLOT(routeSettingsClicked()), nullptr, tr("The base pace of the route analysis, whether it uses runnability and elevation, and the runnability of the symbols."));
	course_export_maps_act = newAction("course-export-maps", tr("Export course maps (PDF)..."), this, SLOT(exportCourseMapsClicked()), nullptr, tr("Write a PDF file with the map of every course, of every part of a course with a map exchange, and of every variant or runner of a relay."));
	course_relay_variations_act = newAction("course-relay-variations", tr("Export relay variations..."), this, SLOT(exportRelayVariationsClicked()), nullptr, tr("Write which team runs which variant of the relays, as a text file."));
	// foom: the rangefinder; the design notes, "Rangefinder dock: detail"
	rangefinder_act = newCheckAction("rangefinder", tr("Rangefinder"), this, SLOT(rangefinderClicked(bool)), nullptr, tr("Draw from a reference point with distance and azimuth."));
	rangefinder_reference_act = newToolAction("rangefinder-reference", tr("Set rangefinder reference"), this, SLOT(rangefinderReferenceClicked()), nullptr, tr("Click the reference point of the rangefinder on the map."));
	connect(this, &MapEditorController::toolChanged, this, &MapEditorController::updateGpsRecordActions);
	if (gps_display)
	{
		connect(gps_display, &GPSDisplay::mapPositionUpdated, this, &MapEditorController::gpsRecordPosition);
		connect(gps_display, &GPSDisplay::positionOffsetChanged, this, [this]() {
			gps_clear_offset_act->setEnabled(gps_display->hasPositionOffset());
		});
	}
	
	compass_action = newCheckAction("compassdisplay", tr("Enable compass display"), this, SLOT(enableCompassDisplay(bool)), "compass.png", QString{}, "toolbars.html#compass_display"); // TODO: write documentation
	align_map_with_north_act = newCheckAction("alignmapwithnorth", tr("Align map with north"), this, SLOT(alignMapWithNorth(bool)), "rotate-map.png", QString{}, "toolbars.html#align_map_with_north"); // TODO: write documentation
	
	mappart_add_act = newAction("addmappart", tr("Add new part..."), this, SLOT(addMapPart()));
	mappart_rename_act = newAction("renamemappart", tr("Rename current part..."), this, SLOT(renameMapPart()));
	mappart_remove_act = newAction("removemappart", tr("Remove current part"), this, SLOT(removeMapPart()));
	mappart_merge_act = newAction("mergemapparts", tr("Merge all parts"), this, SLOT(mergeAllMapParts()));
	
	import_act = newAction("import", tr("Import..."), this, SLOT(importClicked()), nullptr, QString{}, "file_menu.html");
#ifdef MAPPER_USE_GDAL
	import_geodata_act = newAction("import-geodata", tr("Import geodata..."), this, SLOT(importGeodataClicked()), nullptr, QString{}, "file_menu.html");  // foom
#endif
	import_points_act = newAction("import-points", tr("Import points..."), this, SLOT(importPointsClicked()), nullptr, QString{}, "file_menu.html");  // foom
	// foom: the design notes, "Layout part commands"
	layout_create_act = newAction("layout-create", tr("Create layout part"), this, SLOT(createLayoutClicked()), nullptr, QString{}, "map_menu.html");
	layout_hide_act = newCheckAction("layout-hide", tr("Hide layout"), this, SLOT(hideLayoutClicked(bool)), nullptr, QString{}, "view_menu.html");
	to_layout_act = newAction("to-layout", tr("Move to layout"), this, SLOT(moveToLayoutClicked()), nullptr, tr("Moves the selected objects to the layout part."), "edit_menu.html");
	to_map_act = newAction("to-map", tr("Move to map"), this, SLOT(moveToMapClicked()), nullptr, tr("Moves the selected objects of the layout part to the map."), "edit_menu.html");
	layout_save_act = newAction("layout-save", tr("Save layout..."), this, SLOT(saveLayoutClicked()), nullptr, QString{}, "file_menu.html");
	// foom: the design notes, "Merge changes from file"
	field_copy_create_act = newAction("field-copy-create", tr("Create field copy..."), this, SLOT(fieldCopyCreateClicked()), nullptr, QString{}, "file_menu.html");
	merge_from_file_act = newAction("merge-from-file", tr("Merge changes..."), this, SLOT(mergeFromFileClicked()), nullptr, QString{}, "file_menu.html");
	field_copy_close_act = newAction("field-copy-close", tr("Close field copy..."), this, SLOT(fieldCopyCloseClicked()), nullptr, QString{}, "file_menu.html");
	// foom: the design notes, "Private symbols"
	to_graphic_act = newAction("to-graphic", tr("Convert to graphic"), this, SLOT(convertToGraphicClicked()), nullptr, tr("Gives the selected objects their own copies of their symbols."), "edit_menu.html");
	object_style_act = newAction("object-style", tr("Object style..."), this, SLOT(objectStyleClicked()), nullptr, tr("Edits the style of the selected graphic objects."), "edit_menu.html");
	// foom: the design notes, "Image object"
	image_add_act = newAction("image-add", tr("Add image..."), this, SLOT(addImageClicked()), nullptr, tr("Adds a PNG or JPEG image to the map."), "map_menu.html");
	image_embed_act = newAction("image-embed", tr("Embed image"), this, SLOT(embedImageClicked()), nullptr, tr("Stores the selected linked images in the map file."), "map_menu.html");
	image_properties_act = newAction("image-properties", tr("Image properties..."), this, SLOT(imagePropertiesClicked()), nullptr, tr("Sets the opacity and the position of the selected images."), "map_menu.html");
	// foom: the design notes, "Generators"
	gen_scale_bar_act = newAction("gen-scale-bar", tr("Scale bar..."), this, SLOT(generateScaleBarClicked()), nullptr, QString{}, "map_menu.html");
	gen_north_lines_act = newAction("gen-north-lines", tr("North lines..."), this, SLOT(generateNorthLinesClicked()), nullptr, QString{}, "map_menu.html");
	gen_legend_act = newAction("gen-legend", tr("Legend..."), this, SLOT(generateLegendClicked()), nullptr, QString{}, "map_menu.html");
	{
		const std::pair<const char*, QString> more_generators[] = {
			{ "gen-north-arrow", tr("North arrow...") },
			{ "gen-grid", tr("Grid...") },
			{ "gen-graticule", tr("Graticule...") },
			{ "gen-name-index", tr("Name index...") },
			{ "gen-trim-marks", tr("Trim marks...") },
			{ "gen-qr-code", tr("QR code...") },
		};
		for (auto const& generator : more_generators)
		{
			auto* action = newAction(generator.first, generator.second, this, SLOT(generateClicked()), nullptr, QString{}, "map_menu.html");
			action->setData(QString::fromLatin1(generator.first));
			more_generator_acts.push_back(action);
		}
	}
	regenerate_act = newAction("regenerate", tr("Regenerate"), this, SLOT(regenerateClicked()), nullptr, tr("Rebuilds the selected generated groups from their parameters."), "map_menu.html");
	layout_import_act = newAction("layout-import", tr("Import layout..."), this, SLOT(importLayoutClicked()), nullptr, QString{}, "file_menu.html");
	
	map_coordinates_act = new QAction(tr("Map coordinates"), this);
	map_coordinates_act->setCheckable(true);
	projected_coordinates_act = new QAction(tr("Projected coordinates"), this);
	projected_coordinates_act->setCheckable(true);
	geographic_coordinates_act = new QAction(tr("Latitude/Longitude (Dec)"), this);
	geographic_coordinates_act->setCheckable(true);
	geographic_coordinates_dms_act = new QAction(tr("Latitude/Longitude (DMS)"), this);
	geographic_coordinates_dms_act->setCheckable(true);
	auto* coordinates_group = new QActionGroup(this);
	coordinates_group->addAction(map_coordinates_act);
	coordinates_group->addAction(projected_coordinates_act);
	coordinates_group->addAction(geographic_coordinates_act);
	coordinates_group->addAction(geographic_coordinates_dms_act);
	QObject::connect(coordinates_group, &QActionGroup::triggered, this, &MapEditorController::coordsDisplayChanged);
	map_coordinates_act->setChecked(true);
	QObject::connect(&map->getGeoreferencing(), &Georeferencing::projectionChanged, this, &MapEditorController::projectionChanged);
	projectionChanged();
	
	copy_coords_act = newAction("copy-coords", tr("Copy position"), this, SLOT(copyDisplayedCoords()), "copy-coords.png", tr("Copy position to clipboard."));
}

void MapEditorController::createMenuAndToolbars()
{
	statusbar_cursorpos_label->setContextMenuPolicy(Qt::ActionsContextMenu);
	statusbar_cursorpos_label->addAction(map_coordinates_act);
	statusbar_cursorpos_label->addAction(projected_coordinates_act);
	statusbar_cursorpos_label->addAction(geographic_coordinates_act);
	statusbar_cursorpos_label->addAction(geographic_coordinates_dms_act);
	
	// Refactored so we can do custom key bindings in the future
	assignKeyboardShortcuts();
	// foom: remember the defaults, then apply the stored shortcuts.
	for (auto it = actionsById.constBegin(); it != actionsById.constEnd(); ++it)
	{
		if (it.value())
			default_shortcuts.insert(it.key(), it.value()->shortcut());
	}
	QSettings shortcut_settings;
	ShortcutSettings::apply(shortcut_settings, actionsById);
	
	// Extend file menu
	QMenu* file_menu = window->getFileMenu();
	QAction* insertion_act = window->getFileMenuExtensionAct();
#ifdef QT_PRINTSUPPORT_LIB
	file_menu->insertAction(insertion_act, print_act);
	file_menu->insertSeparator(insertion_act);
	insertion_act = print_act;
#endif
	file_menu->insertAction(insertion_act, import_act);
	if (import_geodata_act)  // foom
		file_menu->insertAction(insertion_act, import_geodata_act);
	file_menu->insertAction(insertion_act, import_points_act);  // foom
	auto* layout_file_menu = new QMenu(tr("Layout"), file_menu);  // foom
	layout_file_menu->menuAction()->setMenuRole(QAction::NoRole);
	layout_file_menu->addAction(layout_save_act);
	layout_file_menu->addAction(layout_import_act);
	file_menu->insertMenu(insertion_act, layout_file_menu);
	auto* field_copy_menu = new QMenu(tr("Field copy"), file_menu);  // foom
	field_copy_menu->menuAction()->setMenuRole(QAction::NoRole);
	field_copy_menu->addAction(field_copy_create_act);
	field_copy_menu->addAction(merge_from_file_act);
	field_copy_menu->addAction(field_copy_close_act);
	file_menu->insertMenu(insertion_act, field_copy_menu);
#ifdef QT_PRINTSUPPORT_LIB
	QMenu* export_menu = new QMenu(tr("&Export as..."), file_menu);
	export_menu->menuAction()->setMenuRole(QAction::NoRole);
	export_menu->addAction(export_image_act);
	export_menu->addAction(export_pdf_act);
	export_menu->addAction(export_kmz_act);
	export_menu->addAction(export_simple_course_act);
	if (export_vector_act)
		export_menu->addAction(export_vector_act);
	file_menu->insertMenu(insertion_act, export_menu);
#endif
	file_menu->insertSeparator(insertion_act);
		
	// Edit menu
	QMenu* edit_menu = window->menuBar()->addMenu(tr("&Edit"));
	edit_menu->setWhatsThis(Util::makeWhatThis("edit_menu.html"));
	edit_menu->addAction(undo_act);
	edit_menu->addAction(redo_act);
	edit_menu->addSeparator();
	edit_menu->addAction(cut_act);
	edit_menu->addAction(copy_act);
	edit_menu->addAction(paste_act);
	edit_menu->addAction(paste_original_act);
	edit_menu->addAction(delete_act);
	edit_menu->addAction(to_layout_act);  // foom
	edit_menu->addAction(to_map_act);  // foom
	edit_menu->addAction(to_graphic_act);  // foom
	edit_menu->addAction(object_style_act);  // foom
	edit_menu->addSeparator();
	edit_menu->addAction(select_all_act);
	edit_menu->addAction(select_nothing_act);
	edit_menu->addAction(invert_selection_act);
	edit_menu->addAction(select_next_act);  // foom: the design notes, "Selection queries"
	edit_menu->addAction(select_by_index_act);  // foom
	edit_menu->addAction(select_by_property_act);  // foom
	edit_menu->addAction(select_by_date_act);  // foom
	edit_menu->addAction(select_duplicates_act);  // foom
	edit_menu->addAction(select_self_intersecting_act);  // foom
	edit_menu->addAction(select_invalid_act);  // foom
	edit_menu->addAction(selection_save_act);  // foom
	edit_menu->addAction(selection_load_act);  // foom
	edit_menu->addAction(selection_delete_act);  // foom
	edit_menu->addAction(select_by_current_symbol_act);
	edit_menu->addSeparator();
	edit_menu->addAction(find_feature->showDialogAction());
	edit_menu->addAction(find_feature->findNextAction());
	edit_menu->addAction(find_replace_text_act);  // foom
	edit_menu->addSeparator();  // foom: numeric entry bar
	numeric_entry_bar = new NumericEntryBar(this, window);
	auto* numeric_entry_act = numeric_entry_bar->toggleViewAction();
	numeric_entry_act->setText(tr("Numeric entry"));
	actionsById["numeric-entry"] = numeric_entry_act;
	edit_menu->addAction(numeric_entry_act);
	edit_menu->addSeparator();
	edit_menu->addAction(clear_undo_redo_history_act);
	
	// View menu
	QMenu* view_menu = window->menuBar()->addMenu(tr("&View"));
	view_menu->setWhatsThis(Util::makeWhatThis("view_menu.html"));
	view_menu->addAction(pan_act);
	view_menu->addAction(zoom_in_act);
	view_menu->addAction(zoom_out_act);
	view_menu->addAction(box_zoom_act);
	view_menu->addAction(show_all_act);
	view_menu->addAction(custom_zoom_act);
	view_menu->addSeparator();  // foom: view history and bookmarks
	view_menu->addAction(view_back_act);
	view_menu->addAction(view_forward_act);
	bookmarks_menu = view_menu->addMenu(tr("Bookmarks"));
	connect(bookmarks_menu, &QMenu::aboutToShow, this, &MapEditorController::updateBookmarksMenu);
	updateBookmarksMenu();
	active_extent_menu = view_menu->addMenu(tr("Active extent"));  // foom
	connect(active_extent_menu, &QMenu::aboutToShow, this, &MapEditorController::updateExtentMenus);
	updateExtentMenus();
	view_menu->addSeparator();
	view_menu->addAction(show_grid_act);
	view_menu->addAction(hatch_areas_view_act);
	view_menu->addAction(baseline_view_act);
	view_menu->addAction(overprinting_simulation_act);
	view_menu->addAction(hide_all_templates_act);
	view_menu->addAction(favourite_templates_act);  // foom
	view_menu->addAction(layout_hide_act);  // foom
	view_menu->addSeparator();
	QMenu* coordinates_menu = new QMenu(tr("Display coordinates as..."), view_menu);
	coordinates_menu->menuAction()->setMenuRole(QAction::NoRole);
	coordinates_menu->addAction(map_coordinates_act);
	coordinates_menu->addAction(projected_coordinates_act);
	coordinates_menu->addAction(geographic_coordinates_act);
	coordinates_menu->addAction(geographic_coordinates_dms_act);
	view_menu->addMenu(coordinates_menu);
	view_menu->addSeparator();
	view_menu->addAction(fullscreen_act);
	view_menu->addSeparator();
	toolbars_menu = view_menu->addMenu(tr("Toolbars"));
	view_menu->addAction(tags_window_act);
	view_menu->addAction(color_window_act);
	view_menu->addAction(symbol_window_act);
	view_menu->addAction(template_window_act);
	view_menu->addAction(issues_window_act);  // foom
	
	// Tools menu
	QMenu *tools_menu = window->menuBar()->addMenu(tr("&Tools"));
	tools_menu->setWhatsThis(Util::makeWhatThis("tools_menu.html"));
	tools_menu->addAction(edit_tool_act);
	tools_menu->addAction(edit_line_tool_act);
	tools_menu->addAction(lasso_act);  // foom
	{
		// foom: the design notes, "Sketch layer"
		auto* sketch_menu = tools_menu->addMenu(tr("Sketch"));
		sketch_menu->menuAction()->setMenuRole(QAction::NoRole);
		sketch_menu->addAction(sketch_pen_act);
		sketch_menu->addAction(sketch_eraser_act);
		sketch_menu->addAction(sketch_note_act);
		auto* color_menu = sketch_menu->addMenu(tr("Pen colour"));
		auto* color_group = new QActionGroup(color_menu);
		for (int i = 0; i < sketch_color_count; ++i)
		{
			auto* action = color_menu->addAction(sketchColorName(i));
			action->setCheckable(true);
			action->setChecked(i == SketchPenTool::penColor());
			action->setData(i);
			color_group->addAction(action);
		}
		connect(color_group, &QActionGroup::triggered, this, [](QAction* action) {
			SketchPenTool::setPen(action->data().toInt(), SketchPenTool::penWidth());
		});
		auto* width_menu = sketch_menu->addMenu(tr("Pen width"));
		auto* width_group = new QActionGroup(width_menu);
		for (int i = 0; i < sketch_width_count; ++i)
		{
			auto* action = width_menu->addAction(tr("%1 mm").arg(sketchWidth(i)));
			action->setCheckable(true);
			action->setChecked(i == SketchPenTool::penWidth());
			action->setData(i);
			width_group->addAction(action);
		}
		connect(width_group, &QActionGroup::triggered, this, [](QAction* action) {
			SketchPenTool::setPen(SketchPenTool::penColor(), action->data().toInt());
		});
		sketch_menu->addSeparator();
		sketch_menu->addAction(sketch_manage_act);
		sketch_menu->addAction(sketch_flatten_act);
	}
	{
		// foom: the design notes, "Recording into a symbol".
		// Upstream offers the GPS display in the mobile interface only.
		auto* gps_menu = tools_menu->addMenu(tr("GPS"));
		gps_menu->menuAction()->setMenuRole(QAction::NoRole);
		gps_menu->addAction(gps_display_action);
		gps_menu->addSeparator();
		gps_menu->addAction(gps_record_act);
		gps_menu->addAction(gps_record_pause_act);
		gps_menu->addAction(gps_add_vertex_act);
		gps_menu->addAction(gps_average_vertex_act);
		gps_menu->addSeparator();
		gps_menu->addAction(gps_set_offset_act);
		gps_menu->addAction(gps_clear_offset_act);
	}
	tools_menu->addAction(rangefinder_act);  // foom
	tools_menu->addAction(draw_point_act);
	tools_menu->addAction(draw_path_act);
	tools_menu->addAction(draw_circle_act);
	tools_menu->addAction(draw_rectangle_act);
	tools_menu->addAction(draw_rectangle_box_act);  // foom
	tools_menu->addAction(draw_stairway_act);  // foom
	tools_menu->addAction(draw_rectangle_open_act);  // foom
	tools_menu->addAction(draw_frame_act);  // foom
	tools_menu->addAction(draw_points_along_act);  // foom
	tools_menu->addAction(draw_sized_shape_act);  // foom
	{
		auto* smoothing_menu = tools_menu->addMenu(tr("Freehand smoothing"));  // foom
		for (auto* action : freehand_smoothing_acts)
			smoothing_menu->addAction(action);
	}
	tools_menu->addAction(auto_join_act);  // foom
	tools_menu->addAction(draw_freehand_act);
	tools_menu->addAction(draw_fill_act);
	tools_menu->addAction(draw_text_act);
	tools_menu->addAction(duplicate_act);
	tools_menu->addAction(switch_symbol_act);
	tools_menu->addAction(fill_border_act);
	tools_menu->addAction(switch_dashes_act);
	tools_menu->addAction(connect_paths_act);
	tools_menu->addAction(boolean_union_act);
	tools_menu->addAction(boolean_intersection_act);
	tools_menu->addAction(boolean_difference_act);
	tools_menu->addAction(boolean_xor_act);
	tools_menu->addAction(boolean_merge_holes_act);
	tools_menu->addAction(cut_tool_act);
	tools_menu->addAction(reshape_act);  // foom
	tools_menu->addMenu(cut_hole_menu);
	tools_menu->addAction(rotate_act);
	tools_menu->addAction(rotate_pattern_act);
	tools_menu->addAction(scale_act);
	tools_menu->addAction(measure_act);
	tools_menu->addAction(convert_to_curves_act);
	tools_menu->addAction(simplify_path_act);
	tools_menu->addAction(to_polyline_act);  // foom
	tools_menu->addAction(to_curve_act);  // foom
	tools_menu->addAction(mirror_act);  // foom
	tools_menu->addAction(interpolate_act);  // foom
	tools_menu->addAction(object_info_act);  // foom
	tools_menu->addAction(convert_to_point_act);  // foom
	tools_menu->addAction(move_by_act);  // foom
	tools_menu->addAction(rotate_by_act);  // foom
	tools_menu->addAction(stretch_act);  // foom
	tools_menu->addAction(move_parallel_act);  // foom
	tools_menu->addAction(move_parallel_by_act);  // foom
	auto* align_menu = tools_menu->addMenu(tr("Align"));  // foom
	for (auto* action : align_acts)
		align_menu->addAction(action);
	align_menu->addSeparator();
	align_menu->addAction(distribute_h_act);
	align_menu->addAction(distribute_v_act);
	auto* vertex_type_menu = tools_menu->addMenu(tr("Vertex type"));  // foom
	vertex_type_menu->addAction(vertex_type_normal_act);
	vertex_type_menu->addAction(vertex_type_dash_act);
	vertex_type_menu->addSeparator();
	vertex_type_menu->addAction(vertex_type_keep_ends_act);
	tools_menu->addAction(group_act);  // foom
	tools_menu->addAction(ungroup_act);  // foom
	tools_menu->addAction(select_group_act);  // foom
	tools_menu->addAction(clip_area_act);
	tools_menu->addAction(erase_area_act);
	tools_menu->addAction(distribute_points_act);
	tools_menu->addAction(touch_cursor_action);
	
	// Map menu
	QMenu* map_menu = window->menuBar()->addMenu(tr("M&ap"));
	map_menu->setWhatsThis(Util::makeWhatThis("map_menu.html"));
	map_menu->addAction(georeferencing_act);
	map_menu->addAction(configure_grid_act);
	map_menu->addSeparator();
	map_menu->addAction(scale_map_act);
	map_menu->addAction(rotate_map_act);
	auto* transform_menu = map_menu->addMenu(tr("Transform"));  // foom: the design notes, "Menu placement"
	transform_menu->addAction(map_move_act);
	transform_menu->addAction(map_stretch_act);
	transform_menu->addAction(map_mirror_act);
	transform_menu->addAction(rotate_map_act);
	transform_menu->addAction(map_rotate_magnetic_act);
	transform_menu->addAction(map_reproject_act);
	transform_menu->addAction(map_affine_act);
	transform_menu->addAction(map_rubbersheet_act);
	transform_menu->addAction(map_local_warp_act);
	transform_menu->addAction(map_center_act);
	map_menu->addAction(map_notes_act);
	map_menu->addAction(map_info_act);
	map_menu->addSeparator();
	updateMapPartsUI();
	map_menu->addAction(mappart_add_act);
	map_menu->addAction(layout_create_act);  // foom
	map_menu->addAction(image_add_act);  // foom
	map_menu->addAction(image_embed_act);  // foom
	map_menu->addAction(image_properties_act);  // foom
	auto* generate_menu = map_menu->addMenu(tr("Generate"));  // foom
	generate_menu->menuAction()->setMenuRole(QAction::NoRole);
	generate_menu->addAction(gen_scale_bar_act);
	generate_menu->addAction(gen_north_lines_act);
	generate_menu->addAction(gen_legend_act);
	for (auto* action : more_generator_acts)
		generate_menu->addAction(action);
	map_menu->addAction(regenerate_act);  // foom
	map_menu->addAction(mappart_rename_act);
	map_menu->addAction(mappart_remove_act);
	map_menu->addMenu(mappart_move_menu);
	map_menu->addMenu(mappart_merge_menu);
	map_menu->addAction(mappart_merge_act);
	auto* topology_menu = map_menu->addMenu(tr("Topology"));  // foom
	topology_menu->addAction(topo_join_act);
	topology_menu->addAction(topo_smooth_act);
	topology_menu->addAction(make_rectangular_act);
	topology_menu->addAction(generalize_buildings_act);
	topology_menu->addAction(close_areas_act);
	topology_menu->addAction(areas_ccw_act);
	topology_menu->addAction(fix_overshoots_act);
	topology_menu->addAction(intersections_act);
	topology_menu->addAction(remove_duplicate_vertices_act);
	topology_menu->addAction(lengthen_line_text_act);  // foom
	map_menu->addAction(check_legibility_act);  // foom
	map_menu->addAction(check_map_act);  // foom
	map_menu->addSeparator();  // foom: named extents
	auto* extents_menu = map_menu->addMenu(tr("Extents"));
	// Extents also come by undo, by import and from the canvas dialog.
	connect(extents_menu, &QMenu::aboutToShow, this, &MapEditorController::updateExtentMenus);
	extents_menu->addAction(extent_define_act);
	extents_menu->addAction(extent_delete_act);
	extents_menu->addAction(extent_import_act);
	
	// Symbols menu
	QMenu* symbols_menu = window->menuBar()->addMenu(tr("Sy&mbols"));
	symbols_menu->setWhatsThis(Util::makeWhatThis("symbols_menu.html"));
	symbols_menu->addAction(symbol_window_act);
	symbols_menu->addAction(color_window_act);
	symbols_menu->addAction(symbol_groups_act);  // foom
	symbols_menu->addSeparator();
	symbols_menu->addAction(symbol_set_id_act);
	symbols_menu->addAction(scale_all_symbols_act);
	symbols_menu->addAction(load_symbols_from_act);
	symbols_menu->addAction(load_crt_act);
	symbols_menu->addAction(replace_font_act);  // foom
	symbols_menu->addAction(replace_color_act);  // foom
	symbols_menu->addAction(renumber_symbols_act);  // foom
	symbols_menu->addAction(compare_symbols_act);  // foom
	symbols_menu->addAction(update_symbols_act);  // foom
	symbols_menu->addAction(export_descriptions_act);  // foom
	symbols_menu->addAction(import_descriptions_act);  // foom
	auto* status_menu = symbols_menu->addMenu(tr("Status sets"));  // foom: the design notes, "Status sets"
	status_menu->addAction(status_save_act);
	status_menu->addAction(status_apply_act);
	status_menu->addAction(status_delete_act);
	/*symbols_menu->addAction(load_colors_from_act);*/
	symbols_menu->addSeparator();
	symbols_menu->addAction(symbol_report_feature->showDialogAction());
	
	// Templates menu
	QMenu* template_menu = window->menuBar()->addMenu(tr("&Templates"));
	template_menu->setWhatsThis(Util::makeWhatThis("templates_menu.html"));
	template_menu->addAction(template_window_act);
	/*template_menu->addAction(template_config_window_act);
	template_menu->addAction(template_visibilities_window_act);*/
	template_menu->addSeparator();
	template_menu->addAction(open_template_act);
	template_menu->addAction(reopen_template_act);
	template_menu->addAction(add_map_service_act);  // foom
	template_menu->addAction(relink_templates_act);  // foom
	
	QMenu* elevation_menu = window->menuBar()->addMenu(tr("&Elevation"));  // foom: the design notes, "Menu placement"
	elevation_menu->addAction(dem_open_act);
	if (dem_point_cloud_act)
		elevation_menu->addAction(dem_point_cloud_act);  // foom
	elevation_menu->addAction(dem_info_act);
	elevation_menu->addAction(dem_export_act);
	elevation_menu->addAction(dem_close_act);
	elevation_menu->addSeparator();
	elevation_menu->addAction(dem_merge_act);
	elevation_menu->addAction(dem_difference_act);
	elevation_menu->addAction(dem_resize_act);
	elevation_menu->addAction(dem_smooth_act);
	elevation_menu->addSeparator();
	elevation_menu->addAction(dem_contours_act);
	elevation_menu->addAction(dem_merge_contours_act);
	elevation_menu->addAction(dem_hillshade_act);
	elevation_menu->addAction(dem_slope_act);
	elevation_menu->addAction(dem_hypsometric_act);
	elevation_menu->addAction(dem_vegetation_height_act);
	elevation_menu->addAction(dem_profile_act);
	elevation_menu->addAction(dem_features_act);
	
	QMenu* courses_menu = window->menuBar()->addMenu(tr("&Courses"));  // foom: the design notes, "Menu placement"
	courses_menu->addAction(course_dock_act);
	courses_menu->addAction(course_symbols_act);
	courses_menu->addSeparator();
	courses_menu->addAction(compose_course_act);
	courses_menu->addAction(course_insert_act);
	courses_menu->addAction(course_delete_act);
	courses_menu->addAction(course_renumber_act);
	courses_menu->addAction(course_punch_ids_act);  // foom
	courses_menu->addSeparator();
	courses_menu->addAction(course_statistics_act);
	courses_menu->addAction(course_check_act);
	courses_menu->addSeparator();
	courses_menu->addAction(course_export_iof_act);
	courses_menu->addAction(course_export_gpx_act);
	courses_menu->addAction(course_export_kml_act);
	courses_menu->addAction(course_export_kmz_act);
	courses_menu->addAction(course_relay_variations_act);
	courses_menu->addAction(course_export_maps_act);
	courses_menu->addAction(course_canvases_act);
	courses_menu->addSeparator();
	courses_menu->addAction(course_routes_analyse_act);
	courses_menu->addAction(course_routes_analyse_all_act);
	courses_menu->addAction(course_routes_show_act);
	courses_menu->addAction(course_routes_hide_act);
	courses_menu->addAction(course_routes_via_act);
	courses_menu->addAction(course_routes_leg_act);
	courses_menu->addAction(course_routes_delete_act);
	courses_menu->addAction(course_routes_export_act);
	courses_menu->addAction(course_routes_settings_act);
	courses_menu->addAction(course_import_iof_act);
	courses_menu->addAction(course_import_geodata_act);
	
	// Extend and activate general toolbar
	QToolBar* main_toolbar = window->getGeneralToolBar();
#ifdef QT_PRINTSUPPORT_LIB
	main_toolbar->addAction(print_act);
	main_toolbar->addSeparator();
#endif
	main_toolbar->addAction(cut_act);
	main_toolbar->addAction(copy_act);
	main_toolbar->addAction(paste_act);
	main_toolbar->addSeparator();
	main_toolbar->addAction(undo_act);
	main_toolbar->addAction(redo_act);
	window->addToolBar(main_toolbar);
	
	// View toolbar
	toolbar_view = window->addToolBar(tr("View"));
	toolbar_view->setObjectName(QString::fromLatin1("View toolbar"));
	auto* grid_button = new QToolButton();
	grid_button->setCheckable(true);
	grid_button->setDefaultAction(show_grid_act);
	grid_button->setPopupMode(QToolButton::MenuButtonPopup);
	auto* grid_menu = new QMenu(grid_button);
	grid_menu->addAction(tr("Configure grid..."));
	grid_button->setMenu(grid_menu);
	connect(grid_menu, &QMenu::triggered, this, &MapEditorController::configureGrid);
	toolbar_view->addWidget(grid_button);
	toolbar_view->addSeparator();
	toolbar_view->addAction(pan_act);
	toolbar_view->addAction(zoom_in_act);
	toolbar_view->addAction(zoom_out_act);
	toolbar_view->addAction(box_zoom_act);
	toolbar_view->addAction(show_all_act);
	toolbar_view->addAction(template_window_act);
	
	// MapParts toolbar
	toolbar_mapparts = window->addToolBar(tr("Map parts"));
	toolbar_mapparts->setObjectName(QString::fromLatin1("Map parts toolbar"));
	if (!mappart_selector_box)
	{
		mappart_selector_box = new QComboBox(toolbar_mapparts);
		mappart_selector_box->setToolTip(tr("Map parts"));
	}
	mappart_selector_box->setSizeAdjustPolicy(QComboBox::AdjustToContents);
	connect(mappart_selector_box, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MapEditorController::changeMapPart);
	toolbar_mapparts->addWidget(mappart_selector_box);
	
	window->addToolBarBreak();
	
	// Drawing toolbar
	toolbar_drawing = window->addToolBar(tr("Drawing"));
	toolbar_drawing->setObjectName(QString::fromLatin1("Drawing toolbar"));
	toolbar_drawing->addAction(edit_tool_act);
	toolbar_drawing->addAction(edit_line_tool_act);
	toolbar_drawing->addAction(draw_point_act);
	toolbar_drawing->addAction(draw_path_act);
	toolbar_drawing->addAction(draw_circle_act);
	toolbar_drawing->addAction(draw_rectangle_act);
	toolbar_drawing->addAction(draw_freehand_act);
	toolbar_drawing->addAction(draw_fill_act);
	toolbar_drawing->addAction(draw_text_act);
	toolbar_drawing->addSeparator();
	
	auto* paint_action = paint_feature->paintAction();
	toolbar_drawing->addAction(paint_action);
	if (auto* button = qobject_cast<QToolButton*>(toolbar_drawing->widgetForAction(paint_action)))
		button->setPopupMode(QToolButton::MenuButtonPopup);
	
	// Editing toolbar
	toolbar_editing = window->addToolBar(tr("Editing"));
	toolbar_editing->setObjectName(QString::fromLatin1("Editing toolbar"));
	toolbar_editing->addAction(delete_act);
	toolbar_editing->addAction(duplicate_act);
	toolbar_editing->addAction(switch_symbol_act);
	toolbar_editing->addAction(fill_border_act);
	toolbar_editing->addAction(switch_dashes_act);
	toolbar_editing->addAction(connect_paths_act);
	toolbar_editing->addAction(boolean_union_act);
	toolbar_editing->addAction(cut_tool_act);
	
	auto* cut_hole_button = new QToolButton();
	cut_hole_button->setCheckable(true);
	cut_hole_button->setToolButtonStyle(Qt::ToolButtonIconOnly);
	cut_hole_button->setDefaultAction(cut_hole_act);
	cut_hole_button->setPopupMode(QToolButton::MenuButtonPopup);
	cut_hole_button->setMenu(cut_hole_menu);
	toolbar_editing->addWidget(cut_hole_button);
	
	toolbar_editing->addAction(rotate_act);
	toolbar_editing->addAction(rotate_pattern_act);
	toolbar_editing->addAction(scale_act);
	toolbar_editing->addAction(measure_act);
	
	// Advanced editing toolbar
	toolbar_advanced_editing = window->addToolBar(tr("Advanced editing"));
	toolbar_advanced_editing->setObjectName(QString::fromLatin1("Advanced editing toolbar"));
	toolbar_advanced_editing->addAction(clip_area_act);
	toolbar_advanced_editing->addAction(erase_area_act);
	toolbar_advanced_editing->addAction(convert_to_curves_act);
	toolbar_advanced_editing->addAction(simplify_path_act);
	toolbar_advanced_editing->addAction(distribute_points_act);
	toolbar_advanced_editing->addAction(boolean_intersection_act);
	toolbar_advanced_editing->addAction(boolean_difference_act);
	toolbar_advanced_editing->addAction(boolean_xor_act);
	toolbar_advanced_editing->addAction(boolean_merge_holes_act);
	
	toolbars_menu->addAction(main_toolbar->toggleViewAction());
	toolbars_menu->addAction(toolbar_view->toggleViewAction());
	toolbars_menu->addAction(toolbar_mapparts->toggleViewAction());
	toolbars_menu->addAction(toolbar_drawing->toggleViewAction());
	toolbars_menu->addAction(toolbar_editing->toggleViewAction());
	toolbars_menu->addAction(toolbar_advanced_editing->toggleViewAction());
	window->addToolBarBreak();  // foom: the numeric entry bar has its own row and starts hidden
	window->addToolBar(numeric_entry_bar);
	numeric_entry_bar->hide();
	toolbars_menu->addAction(numeric_entry_act);
	
	QWidget* context_menu = map_widget->getContextMenu();
	context_menu->addAction(edit_tool_act);
	context_menu->addAction(draw_point_act);
	context_menu->addAction(draw_path_act);
	context_menu->addAction(draw_rectangle_act);
	context_menu->addAction(cut_tool_act);
	context_menu->addAction(cut_hole_act);
	context_menu->addAction(switch_dashes_act);
	context_menu->addAction(connect_paths_act);
	context_menu->addAction(copy_coords_act);
	
	// Show keyboard shortcuts in toolbar tooltips
	for (auto* toolbar : {main_toolbar, toolbar_view, toolbar_mapparts, toolbar_drawing, toolbar_editing, toolbar_advanced_editing})
	{
		for (auto* action : toolbar->actions())
		{
			if (action && !action->isSeparator() && !action->shortcut().isEmpty())
			{
				auto label = action->text();
				// Remove unescaped '&', cf. qt_strippedText in qtbase/src/widgets/kernel/qaction.cpp
				for (int i = 0; i < label.size(); ++i) {
					if (label.at(i) == QLatin1Char('&'))
						label.remove(i, 1);
				}
				action->setToolTip(tr("%1 (%2)").arg(label, action->shortcut().toString()));
			}
		}
	}
}

void MapEditorController::createMobileGUI()
{
	// Create mobile-specific actions
	mobile_symbol_selector_action = new QAction(tr("Select symbol"), this);
	connect(mobile_symbol_selector_action, &QAction::triggered, this, &MapEditorController::mobileSymbolSelectorClicked);
	
	mobile_symbol_button_menu = new QMenu(window);
	mobile_symbol_button_menu->addAction(QString{}); // reserved for symbol name
	auto* description_action = mobile_symbol_button_menu->addAction(QApplication::translate("OpenOrienteering::SymbolPropertiesWidget", "Description"));
	connect(description_action, &QAction::triggered, this, [this]() {
		auto* symbol = symbol_widget->getSingleSelectedSymbol();
		auto document = QString{ symbol->getNumberAsString() + QLatin1Char(' ')
		                         + QLatin1String("<b>") + symbol->getName() + QLatin1String("</b>\n\n")
		                         + symbol->getDescription() };
		// Cf. SymbolToolTip::scheduleShow
		document.replace(QLatin1Char('\n'), QStringLiteral("<br>"));
		document.remove(QLatin1Char('\r'));
		TextBrowserDialog description_dialog(document, window);
		description_dialog.exec();
	});
	mobile_symbol_button_menu->addSeparator();
	auto* hide_symbol_action = mobile_symbol_button_menu->addAction(QApplication::translate("OpenOrienteering::SymbolRenderWidget", "Hide objects with this symbol"));
	hide_symbol_action->setCheckable(true);
	connect(hide_symbol_action, &QAction::triggered, this, [this](bool value) {
		auto* symbol = symbol_widget->getSingleSelectedSymbol();
		symbol->setHidden(value);
		if (!value && map->removeSymbolFromSelection(symbol, false))
		    map->emitSelectionChanged();
		map->updateAllMapWidgets();
		map->setSymbolsDirty();
		selectedSymbolsChanged();
	});
	auto* protected_symbol_action = mobile_symbol_button_menu->addAction(QApplication::translate("OpenOrienteering::SymbolRenderWidget", "Protect objects with this symbol"));
	protected_symbol_action->setCheckable(true);
	connect(protected_symbol_action, &QAction::triggered, this, [this](bool value) {
		auto* symbol = symbol_widget->getSingleSelectedSymbol();
		symbol->setProtected(value);
		if (!value && map->removeSymbolFromSelection(symbol, false))
		    map->emitSelectionChanged();
		map->setSymbolsDirty();
		selectedSymbolsChanged();
	});
	
	QAction* hide_top_bar_action = new QAction(QIcon(QString::fromLatin1(":/images/arrow-thin-upleft.png")), tr("Hide top bar"), this);
 	connect(hide_top_bar_action, &QAction::triggered, this, &MapEditorController::hideTopActionBar);
	
	QAction* show_top_bar_action = new QAction(QIcon(QString::fromLatin1(":/images/arrow-thin-downright.png")), tr("Show top bar"), this);
 	connect(show_top_bar_action, &QAction::triggered, this, &MapEditorController::showTopActionBar);
	
	QAction* mappart_action = new QAction(QIcon(QString::fromLatin1(":/images/map-parts.png")), tr("Map parts"), this);
	auto* mappart_group = new QActionGroup(window);
	auto* mappart_menu = new QMenu(window);
	mappart_action->setMenu(mappart_menu);
	
	// Don't use QMenu::aboutToShow because it causes menu mispositioning near
	// lower screen border when triggered from QToolButton.
	connect(mappart_action, &QAction::hovered, this, [this, mappart_menu, mappart_group]() {
		mappart_menu->clear();
		Q_ASSERT(mappart_group->actions().isEmpty());
		for (auto i = 0; i < map->getNumParts(); ++i)
		{
			auto* part = map->getPart(i);
			auto* action = mappart_menu->addAction(part->getName());
			action->setCheckable(true);
			action->setActionGroup(mappart_group);
			if (part == map->getCurrentPart())
				action->setChecked(true);
			connect(action, &QAction::triggered, this, [this, i]() { changeMapPart(i); });
		}
	});
	
	// Create button for showing the top bar again after hiding it
	const auto button_size_px = qRound(Util::mmToPixelPhysical(Settings::getInstance().getSetting(Settings::ActionGridBar_ButtonSizeMM).toReal()));
	const auto icon_size_px = button_size_px - 12;
	const auto icon_size = QSize{icon_size_px, icon_size_px};
	
	QIcon icon = show_top_bar_action->icon();
	QPixmap pixmap = icon.pixmap(icon_size, QIcon::Normal, QIcon::Off);
	if (pixmap.width() < icon_size_px)
	{
		pixmap = pixmap.scaled(icon_size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
		icon.addPixmap(pixmap);
		show_top_bar_action->setIcon(icon);
	}
	
	show_top_bar_button = new QToolButton(window);
	show_top_bar_button->setDefaultAction(show_top_bar_action);
	show_top_bar_button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
	show_top_bar_button->setAutoRaise(true);
	show_top_bar_button->setIconSize(icon_size);
	show_top_bar_button->setGeometry(0, 0, button_size_px, button_size_px);
	
	
	// Create bottom action bar
	bottom_action_bar = new ActionGridBar(ActionGridBar::Horizontal, 2);
	
	// Left side
	int col = 0;
	bottom_action_bar->addAction(zoom_in_act, 0, col);
	bottom_action_bar->addAction(pan_act, 1, col++);
	
	bottom_action_bar->addAction(zoom_out_act, 0, col);
	auto* zoom_out_button = bottom_action_bar->getButtonForAction(zoom_out_act);
	auto* mobile_zoom_out_menu = new QMenu(zoom_out_button);
	auto* zoom_1x_action = mobile_zoom_out_menu->addAction(tr("1x zoom"));
	connect(zoom_1x_action, &QAction::triggered, this, [this]() {
		main_view->setZoom(1);
	});
	auto* zoom_2x_action = mobile_zoom_out_menu->addAction(tr("2x zoom"));
	connect(zoom_2x_action, &QAction::triggered, this, [this]() {
		main_view->setZoom(2);
	});
	zoom_out_button->setMenu(mobile_zoom_out_menu);

	auto* move_to_gps_pos_menu = new QMenu(bottom_action_bar);
	move_to_gps_pos_menu->addAction(follow_position_act);
	move_to_gps_pos_act->setMenu(move_to_gps_pos_menu);
	bottom_action_bar->addAction(move_to_gps_pos_act, 1, col++);
	if (auto* button = bottom_action_bar->getButtonForAction(move_to_gps_pos_act))
		button->setPopupMode(QToolButton::DelayedPopup);
	
	bottom_action_bar->addAction(hatch_areas_view_act, 0, col);
	bottom_action_bar->addAction(baseline_view_act, 1, col++);	

	bottom_action_bar->addAction(gps_temporary_path_act, 0, col);
	auto* temp_path_button = bottom_action_bar->getButtonForAction(gps_temporary_path_act);
	auto* mobile_gps_temp_path_menu = new QMenu(temp_path_button);
	mobile_gps_temp_path_menu->addAction(gps_temporary_clear_act);
	mobile_gps_temp_path_menu->addSeparator();  // foom: recording into a path
	mobile_gps_temp_path_menu->addAction(gps_record_act);
	mobile_gps_temp_path_menu->addAction(gps_record_pause_act);
	mobile_gps_temp_path_menu->addAction(gps_add_vertex_act);
	mobile_gps_temp_path_menu->addAction(gps_average_vertex_act);
	mobile_gps_temp_path_menu->addSeparator();
	mobile_gps_temp_path_menu->addAction(gps_set_offset_act);
	mobile_gps_temp_path_menu->addAction(gps_clear_offset_act);
	mobile_gps_temp_path_menu->addSeparator();
	mobile_gps_temp_path_menu->addAction(rangefinder_act);
	temp_path_button->setMenu(mobile_gps_temp_path_menu);
	
	bottom_action_bar->addAction(gps_temporary_point_act, 1, col++);

	auto* paint_action = paint_feature->paintAction();
	bottom_action_bar->addAction(paint_action, 0, col);
	if (auto* button = bottom_action_bar->getButtonForAction(paint_action))
		button->setPopupMode(QToolButton::DelayedPopup);
	
	// Right side
	bottom_action_bar->addActionAtEnd(mobile_symbol_selector_action, 0, 1, 2, 2);
	auto* button = bottom_action_bar->getButtonForAction(mobile_symbol_selector_action);
	button->setPopupMode(QToolButton::DelayedPopup);
	
	col = 2;
	bottom_action_bar->addActionAtEnd(draw_point_act, 0, col);
	bottom_action_bar->addActionAtEnd(draw_point_gps_act, 1, col++);
	
	bottom_action_bar->addActionAtEnd(draw_path_act, 0, col);
	bottom_action_bar->addActionAtEnd(draw_freehand_act, 1, col++);
	
	bottom_action_bar->addActionAtEnd(draw_rectangle_act, 0, col);
	bottom_action_bar->addActionAtEnd(draw_circle_act, 1, col++);
	
	//bottom_action_bar->addActionAtEnd(draw_fill_act, 0, col);
	bottom_action_bar->addActionAtEnd(draw_text_act, 1, col++);
	
	
	// Create top action bar
	top_action_bar = new ActionGridBar(ActionGridBar::Horizontal, 2);
	
	// Left side
	col = 0;
	top_action_bar->addAction(hide_top_bar_action, 0, col);
	top_action_bar->addAction(window->getSaveAct(), 1, col++);
	
	top_action_bar->addAction(compass_action, 0, col);
	top_action_bar->addAction(gps_display_action, 1, col++);
	
	top_action_bar->addAction(gps_distance_rings_action, 0, col);
	top_action_bar->addAction(align_map_with_north_act, 1, col++);
	
	top_action_bar->addAction(show_grid_act, 0, col);
	top_action_bar->addAction(show_all_act, 1, col++);
	
	// Right side
	col = 0;
	top_action_bar->addActionAtEnd(window->getCloseAct(), 0, col);
	top_action_bar->addActionAtEnd(top_action_bar->getOverflowAction(), 1, col++);
	
	top_action_bar->addActionAtEnd(redo_act, 0, col);
	top_action_bar->addActionAtEnd(undo_act, 1, col++);
	
	top_action_bar->addActionAtEnd(touch_cursor_action, 0, col);
	top_action_bar->addActionAtEnd(template_window_act, 1, col++);
	
	top_action_bar->addActionAtEnd(edit_tool_act, 0, col);
	top_action_bar->addActionAtEnd(edit_line_tool_act, 1, col++);
	
	top_action_bar->addActionAtEnd(delete_act, 0, col);
	top_action_bar->addActionAtEnd(duplicate_act, 1, col++);
	
	top_action_bar->addActionAtEnd(switch_symbol_act, 0, col);
	top_action_bar->addActionAtEnd(fill_border_act, 1, col++);
	
	top_action_bar->addActionAtEnd(switch_dashes_act, 0, col);
	top_action_bar->addActionAtEnd(boolean_union_act, 1, col++);
	
	top_action_bar->addActionAtEnd(cut_tool_act, 0, col);
	top_action_bar->addActionAtEnd(connect_paths_act, 1, col++);
	
	top_action_bar->addActionAtEnd(rotate_act, 0, col);
	top_action_bar->addActionAtEnd(cut_hole_act, 1, col++);
	
	top_action_bar->addActionAtEnd(scale_act, 0, col);
	top_action_bar->addActionAtEnd(rotate_pattern_act, 1, col++);
	
	top_action_bar->addActionAtEnd(convert_to_curves_act, 0, col);
	top_action_bar->addActionAtEnd(simplify_path_act, 1, col++);
	
	top_action_bar->addActionAtEnd(distribute_points_act, 0, col);
	top_action_bar->addActionAtEnd(boolean_difference_act, 1, col++);
	
	top_action_bar->addActionAtEnd(measure_act, 0, col);
	top_action_bar->addActionAtEnd(boolean_merge_holes_act, 1, col++);
	
	top_action_bar->addActionAtEnd(mappart_action, 1, col++);
	if (auto* mappart_button = top_action_bar->getButtonForAction(mappart_action))
		mappart_button->setPopupMode(QToolButton::InstantPopup);
	
	bottom_action_bar->setToUseOverflowActionFrom(top_action_bar);
	
	top_action_bar->setParent(map_widget);
	
	auto* container_widget = new QWidget();
	auto* layout = new QVBoxLayout();
	layout->setMargin(0);
	layout->setSpacing(0);
	layout->addWidget(map_widget, 1);
	layout->addWidget(bottom_action_bar);
	container_widget->setLayout(layout);
	window->setCentralWidget(container_widget);
}

void MapEditorController::detach()
{
	// Terminate all editing
	setTool(nullptr);
	setOverrideTool(nullptr);
	
	saveWindowState();
	
	// Avoid a crash triggered by pressing Ctrl-W during loading.
	if (nullptr != symbol_dock_widget)
		window->removeDockWidget(symbol_dock_widget);
	else if (nullptr != symbol_widget)
		delete symbol_widget;
	
	delete gps_display;
	gps_display = nullptr;
	delete gps_track_recorder;
	gps_track_recorder = nullptr;
	delete compass_display;
	compass_display = nullptr;
	delete gps_marker_display;
	gps_marker_display = nullptr;
	
	find_feature.reset(nullptr);
	paint_feature.reset(nullptr);
	
	window->setCentralWidget(nullptr);
	delete map_widget;
	
	delete statusbar_zoom_frame;
	delete statusbar_cursorpos_label;
	
#ifdef Q_OS_ANDROID
	QAndroidJniObject::callStaticMethod<void>("org/openorienteering/mapper/MapperActivity",
                                       "unlockOrientation",
                                       "()V");
#endif
	
	if (mobile_mode)
		window->setWindowState(window->windowState() & ~Qt::WindowFullScreen);
}


void MapEditorController::setWindowStateChanged()
{
	if (!window_state_changed)
	{
		window_state_changed = true;
		QTimer::singleShot(10, this, &MapEditorController::saveWindowState);
	}
}

void MapEditorController::saveWindowState()
{
	if (!mobile_mode && mode != SymbolEditor)
	{
		QSettings settings;
		settings.beginGroup(QString::fromUtf8(metaObject()->className()));
		settings.setValue(QString::fromLatin1("state"), window->saveState());
		window_state_changed = false;
	}
}

void MapEditorController::restoreWindowState()
{
	if (!mobile_mode && mode != SymbolEditor)
	{
		QSettings settings;
		settings.beginGroup(QString::fromUtf8(metaObject()->className()));
		auto const key = QString::fromLatin1("state");
		auto const state = settings.value(key).toByteArray();
		settings.remove(key);  // Avoid repeated crash from invalid data, GH-1366.
		settings.sync();
		
		window->restoreState(state);
		if (toolbar_mapparts && mappart_selector_box)
			toolbar_mapparts->setVisible(mappart_selector_box->count() > 1);
		
		settings.setValue(key, state);  // Save valid state again.
		window_state_changed = false;
	}
}

bool MapEditorController::keyPressEventFilter(QKeyEvent* event)
{
#if defined(Q_OS_ANDROID)
	if (event->key() == Qt::Key_Back)
	{
		if (symbol_widget && symbol_widget->isVisible())
		{
			mobileSymbolSelectorFinished();
			return true;
		}
		
		if (isEditingInProgress() && !map_widget->findChild<KeyButtonBar*>())
		{
			QKeyEvent escape_pressed{ QEvent::KeyPress, Qt::Key_Escape, event->modifiers() };
			QCoreApplication::sendEvent(window, &escape_pressed);
			QKeyEvent escape_released{ QEvent::KeyRelease, Qt::Key_Escape, event->modifiers() };
			QCoreApplication::sendEvent(window, &escape_released);
			return true;
		}
	}
#endif
	
	return map_widget->keyPressEventFilter(event);
}

bool MapEditorController::keyReleaseEventFilter(QKeyEvent* event)
{
	return map_widget->keyReleaseEventFilter(event);
}



void MapEditorController::exportVectorData(int file_types, const QString& format_settings_key)
{
	QSettings settings;
	QString const import_directory = settings.value(QString::fromLatin1("importFileDirectory"), QDir::homePath()).toString();
	QString selected_filter = settings.value(format_settings_key).toString();
	
	// Build the list of supported file filters based on the file format registry
	QStringList filters;
	for (auto format : FileFormats.formats())
	{
		if (format->supportsFileExport() && format->fileType() & file_types)
			filters.append(format->filter());
	}
	filters.sort();
	
	QString filename = FileDialog::getSaveFileName(
	                       window,
	                       tr("Export"),
	                       import_directory,
	                       filters.join(QLatin1String(";;")),
	                       &selected_filter);
	if (filename.isEmpty())
		return;
	
	settings.setValue(QString::fromLatin1("importFileDirectory"), QFileInfo(filename).canonicalPath());
	settings.setValue(format_settings_key, selected_filter);
	
	auto const* format = FileFormats.findFormatByFilter(selected_filter, &FileFormat::supportsFileExport);
	if (!format)
	{
		QMessageBox::warning(window, tr("Error"),
		                     ::OpenOrienteering::MainWindow::tr("File could not be saved:") + QLatin1Char('\n')
		                     + ::OpenOrienteering::ImportExport::tr("Cannot find a vector data export driver named '%1'")
		                       .arg(selected_filter) + QLatin1Char('\n') + QLatin1Char('\n')
		                     + ::OpenOrienteering::MainWindow::tr("Please report this as a bug.") );
		return;
	}
	
	filename = format->fixupExtension(filename);
	exportTo(filename, *format);
}



// slot
void MapEditorController::exportSimpleCourse()
{
	auto simple_export = SimpleCourseExport(*map);
	if (!simple_export.canExport())
	{
		QMessageBox::warning(window, tr("Error"), simple_export.errorString());
		return;
	}
	
	SimpleCourseDialog course_dialog(simple_export, window);
	if (course_dialog.exec() != QDialog::Accepted)
		return;
	
	// Ideally, the dialog results should be passed to the exporter via options.
	// However, the exporter is created only much later. So here, we can neither
	// discover the supported options nor set them directly. We would have to pass
	// them through the call stack as parameter. Instead of this intrusive approach,
	// we rely on transient map properties handled by SimpleCourseExport.
	simple_export.setProperties(*map,
	                            course_dialog.eventName(),
	                            course_dialog.courseName(),
	                            course_dialog.firstCodeNumber());
	exportVectorData(FileFormat::SimpleCourseFile, QStringLiteral("Export/lastSimpleCourseFormat"));
}


// slot
void MapEditorController::exportVector()
{
	exportVectorData(FileFormat::MapFile | FileFormat::OgrFile, QStringLiteral("Export/lastVectorFormat"));
}


void MapEditorController::printClicked(int task)
{
#ifdef QT_PRINTSUPPORT_LIB
	if (editing_in_progress)
	{
		QMessageBox::warning(window,
		                     tr("Editing in progress"),
		                     tr("The map is currently being edited. "
		                        "Please finish the edit operation first.") );
		return;
	}
	if (!print_dock_widget)
	{
		print_dock_widget = new EditorDockWidget(QString{}, nullptr, this, window);
		print_dock_widget->setAllowedAreas(Qt::NoDockWidgetArea);
		print_dock_widget->toggleViewAction()->setVisible(false);
		print_widget = new PrintWidget(map, window, main_view, this, print_dock_widget);
		connect(print_dock_widget, &QDockWidget::visibilityChanged, this, [this]() {
			print_widget->setActive(print_dock_widget->isVisible());
		} );
		connect(print_widget, &PrintWidget::closeClicked, print_dock_widget, &QWidget::close);
		connect(print_widget, &PrintWidget::finished, print_dock_widget, &QWidget::close);
		connect(print_widget, &PrintWidget::taskChanged, print_dock_widget, &QWidget::setWindowTitle);
		print_dock_widget->setWidget(print_widget);
		print_dock_widget->setObjectName(QString::fromLatin1("Print dock widget"));
		addFloatingDockWidget(print_dock_widget);
	}
	
	print_widget->setActive(true); // Make sure to save the state before setting the task.
	print_widget->setTask((PrintWidget::TaskFlags)task);
	print_dock_widget->show();
	print_dock_widget->raise();
#else
	Q_UNUSED(task)
	QMessageBox::warning(window, tr("Error"), tr("Print / Export is not available in this program version!"));
#endif
}

void MapEditorController::undo()
{
	doUndo(false);
}

void MapEditorController::redo()
{
	doUndo(true);
}

void MapEditorController::doUndo(bool redo)
{
	if ((!redo && !map->undoManager().canUndo()) ||
		(redo && !map->undoManager().canRedo()))
	{
		// This should not happen as the action should be deactivated in this case!
		QMessageBox::critical(window, tr("Error"), tr("No undo steps available."));
		return;
	}
	
	if (redo)
		map->undoManager().redo(window);
	else
		map->undoManager().undo(window);
}

void MapEditorController::cut()
{
	copy();
	//: Past tense. Displayed when an Edit > Cut operation is completed.
	window->showStatusBarMessage(tr("Cut %n object(s)", nullptr, map->getNumSelectedObjects()), 2000);
	map->deleteSelectedObjects();
}

void MapEditorController::copy()
{
	if (map->getNumSelectedObjects() == 0)
		return;
	
	// Create map containing required objects and their symbol and color dependencies
	Map copy_map;
	copy_map.setScaleDenominator(map->getScaleDenominator());
	
	std::vector<bool> symbol_filter;
	symbol_filter.assign(map->getNumSymbols(), false);
	for (const auto* object : map->selectedObjects())
	{
		int symbol_index = map->findSymbolIndex(object->getSymbol());
		if (symbol_index >= 0)
			symbol_filter[symbol_index] = true;
	}
	
	// Copy all colors. This improves preservation of relative order during paste.
	copy_map.importMap(*map, Map::ColorImport);
	
	// Export symbols and colors into copy_map
	auto symbol_map = copy_map.importMap(*map, Map::MinimalSymbolImport, &symbol_filter, -1, true);
	
	// Duplicate all selected objects into copy map
	for (const auto* object : map->selectedObjects())
	{
		auto* new_object = object->duplicate();
		if (symbol_map.contains(new_object->getSymbol()))
			new_object->setSymbol(symbol_map.value(new_object->getSymbol()), true);
		
		copy_map.addObject(new_object);
	}
	
	// Save map to memory
	QBuffer buffer;
	if (!copy_map.exportToIODevice(buffer))
	{
		QMessageBox::warning(nullptr, tr("Error"), tr("An internal error occurred, sorry!"));
		return;
	}
	
	// Put buffer into clipboard
	auto* mime_data = new QMimeData();
	mime_data->setData(MimeType::OpenOrienteeringObjects(), buffer.data());
	QApplication::clipboard()->setMimeData(mime_data);
	
	// Show message
	window->showStatusBarMessage(tr("Copied %n object(s)", nullptr, map->getNumSelectedObjects()), 2000);
}


void MapEditorController::paste(int paste_at_center)
{
	if (editing_in_progress)
		return;
	if (!QApplication::clipboard()->mimeData()->hasFormat(MimeType::OpenOrienteeringObjects()))
	{
		QMessageBox::warning(nullptr, tr("Error"), tr("There are no objects in clipboard which could be pasted!"));
		return;
	}
	
	// Get buffer from clipboard
	QByteArray byte_array = QApplication::clipboard()->mimeData()->data(MimeType::OpenOrienteeringObjects());
	QBuffer buffer(&byte_array);
	buffer.open(QIODevice::ReadOnly);
	
	// Create map from buffer
	Map paste_map;
	if (!paste_map.importFromIODevice(buffer))
	{
		QMessageBox::warning(nullptr, tr("Error"), tr("An internal error occurred, sorry!"));
		return;
	}
	
	if (paste_at_center)
	{
		// Move objects in paste_map so their bounding box center is at this map's viewport center.
		// This makes the pasted objects appear at the center of the viewport.
		QRectF paste_extent = paste_map.calculateExtent(true, false, nullptr);
		auto offset = main_view->center() - paste_extent.center();
		
		MapPart* part = paste_map.getCurrentPart();
		for (int i = 0; i < part->getNumObjects(); ++i)
			part->getObject(i)->move(offset);
	}
	
	// foom: a pasted object is modified now (the design notes, "Object identity and timestamps").
	auto const now = Object::currentTime();
	for (int p = 0; p < paste_map.getNumParts(); ++p)
	{
		auto* part = paste_map.getPart(p);
		for (int i = 0; i < part->getNumObjects(); ++i)
			part->getObject(i)->touch(now);
	}

	// Import pasted map. Do not blindly import all colors.
	importMap(paste_map, Map::MinimalObjectImport, window);
	
	// Show message
	window->showStatusBarMessage(tr("Pasted %n object(s)", nullptr, paste_map.getNumObjects()), 2000);
}


void MapEditorController::clearUndoRedoHistory()
{
	map->undoManager().clear();
	map->setOtherDirty();
}

void MapEditorController::spotColorPresenceChanged(bool has_spot_colors)
{
	if (overprinting_simulation_act)
	{
		if (has_spot_colors)
		{
			overprinting_simulation_act->setEnabled(true);
		}
		else
		{
			if (overprinting_simulation_act->isChecked())
				overprinting_simulation_act->trigger();
			overprinting_simulation_act->setEnabled(false);
		}
	}
}

void MapEditorController::showGrid()
{
	main_view->setGridVisible(show_grid_act->isChecked());
}

void MapEditorController::configureGrid()
{
	ConfigureGridDialog dialog(window, *map, show_grid_act->isChecked());
	dialog.setWindowModality(Qt::WindowModal);
	if (dialog.exec() == QDialog::Accepted)
	{
		map->setGrid(dialog.resultGrid());
		if (dialog.gridVisible() != show_grid_act->isChecked())
			show_grid_act->trigger();
	}
}

void MapEditorController::pan()
{
	setTool(new PanTool(this, pan_act));
}

void MapEditorController::moveToGpsPos()
{
	if (!gps_display->hasValidPosition())
		return;
	auto cur_gps_pos = gps_display->getLatestGPSCoord();
	main_view->setCenter({ cur_gps_pos.x(), cur_gps_pos.y() });
	gps_display->startBlinking(3);
}

void MapEditorController::followPositionClicked(bool enable)
{
	if (enable)
		connect(gps_display, &GPSDisplay::mapPositionUpdated, this, &MapEditorController::followPositionUpdate);
	else
		disconnect(gps_display, &GPSDisplay::mapPositionUpdated, this, &MapEditorController::followPositionUpdate);
}

void MapEditorController::followPositionUpdate(MapCoordF position)
{
	// When the given position is out of the half-width half-height rectangle
	// in the center of the view, push the position's coordinate to the center
	// of the view.
	auto const map_view_rect = main_view->calculateViewedRect(map_widget->viewportToView(map_widget->rect()));
	auto center = map_view_rect.center();
	bool update = false;
	if (position.x() < map_view_rect.left() + map_view_rect.width() / 4
	    || position.x() > map_view_rect.right() - map_view_rect.width() / 4)
	{
		center.setX(position.x());
		update = true;
	}
	if (position.y() < map_view_rect.top() + map_view_rect.height() / 4
	    || position.y() > map_view_rect.bottom() - map_view_rect.height() / 4)
	{
		center.setY(position.y());
		update = true;
	}
	
	if (update)
		main_view->setCenter(MapCoord(center));
}

void MapEditorController::zoomIn()
{
	main_view->zoomSteps(1);
}

void MapEditorController::zoomOut()
{
	main_view->zoomSteps(-1);
}

void MapEditorController::boxZoom(bool checked)
{
	if (checked)
		setTool(new BoxZoomTool(this, box_zoom_act));
	else
		setTool(nullptr);
}

void MapEditorController::setCustomZoomFactorClicked()
{
	bool ok;
	double factor = QInputDialog::getDouble(window, tr("Set custom zoom factor"), tr("Zoom factor:"), main_view->getZoom(), MapView::zoom_out_limit, MapView::zoom_in_limit, 3, &ok);
	if (!ok || factor == main_view->getZoom())
		return;
	
	main_view->setZoom(factor);
}

void MapEditorController::hatchAreas(bool checked)
{
	map->setAreaHatchingEnabled(checked);
	// Update all areas
	map->applyOnMatchingObjects(&Object::forceUpdate, ObjectOp::ContainsSymbolType{Symbol::Area});
}

void MapEditorController::baselineView(bool checked)
{
	map->setBaselineViewEnabled(checked);
	map->updateAllObjects();
}

void MapEditorController::hideAllTemplates(bool checked)
{
	hide_all_templates_act->setChecked(checked);
	main_view->setAllTemplatesHidden(checked);
}

// foom: the design notes, "Favourite templates"
void MapEditorController::favouriteTemplatesOnly(bool checked)
{
	favourite_templates_act->setChecked(checked);
	main_view->setFavouriteTemplatesOnly(checked);
}

void MapEditorController::overprintingSimulation(bool checked)
{
	if (checked && !map->hasSpotColors())
	{
		checked = false;
	}
	main_view->setOverprintingSimulationEnabled(checked);
}

void MapEditorController::coordsDisplayChanged()
{
	if (geographic_coordinates_dms_act->isChecked())
		map_widget->setCoordsDisplay(MapWidget::GEOGRAPHIC_COORDS_DMS);
	else if (geographic_coordinates_act->isChecked())
		map_widget->setCoordsDisplay(MapWidget::GEOGRAPHIC_COORDS);
	else if (projected_coordinates_act->isChecked())
		map_widget->setCoordsDisplay(MapWidget::PROJECTED_COORDS);	
	else
		map_widget->setCoordsDisplay(MapWidget::MAP_COORDS);
}

void MapEditorController::copyDisplayedCoords()
{
	QApplication::clipboard()->setText(statusbar_cursorpos_label->text());
}

void MapEditorController::projectionChanged()
{
	updateMapServiceAction();  // foom
	const Georeferencing& geo(map->getGeoreferencing());
	
	projected_coordinates_act->setText(geo.getProjectedCoordinatesName());
	
	bool enable_geographic = geo.getState() == Georeferencing::Geospatial;
	geographic_coordinates_act->setEnabled(enable_geographic);
	geographic_coordinates_dms_act->setEnabled(enable_geographic);
	if (!enable_geographic &&
		(map_widget->getCoordsDisplay() == MapWidget::GEOGRAPHIC_COORDS ||
		map_widget->getCoordsDisplay() == MapWidget::GEOGRAPHIC_COORDS_DMS))
	{
		map_widget->setCoordsDisplay(MapWidget::MAP_COORDS);
		map_coordinates_act->setChecked(true);
	}
}

void MapEditorController::showSymbolWindow(bool show)
{
	if (!symbol_dock_widget)
	{
		symbol_dock_widget = new EditorDockWidget(tr("Symbols"), symbol_window_act, this, window);
		createSymbolWidget();
		symbol_dock_widget->setObjectName(QString::fromLatin1("Symbol dock widget"));
		if (!window->restoreDockWidget(symbol_dock_widget))
			window->addDockWidget(Qt::RightDockWidgetArea, symbol_dock_widget, Qt::Vertical);
	}
	
	symbol_dock_widget->setVisible(show);
}

void MapEditorController::createColorWindow()
{
	Q_ASSERT(!color_dock_widget);
	
	color_dock_widget = new EditorDockWidget(tr("Colors"), color_window_act, this, window);
	color_dock_widget->setWidget(new ColorListWidget(map, window, color_dock_widget));
	color_dock_widget->widget()->setEnabled(!editing_in_progress);
	color_dock_widget->setObjectName(QString::fromLatin1("Color dock widget"));
	if (!window->restoreDockWidget(color_dock_widget))
		window->addDockWidget(Qt::LeftDockWidgetArea, color_dock_widget, Qt::Vertical);
	color_dock_widget->setVisible(false);
}

void MapEditorController::showColorWindow(bool show)
{
	if (!color_dock_widget)
		createColorWindow();
	
	color_dock_widget->setVisible(show);
}


void MapEditorController::symbolSetIdClicked()
{
	bool ok;
	auto id = QInputDialog::getText(window, tr("Symbol set ID"),
	                                tr("Edit the symbol set ID:"), QLineEdit::Normal,
	                                map->symbolSetId(), &ok);
	if (ok)
		map->setSymbolSetId(id);
}


void MapEditorController::loadSymbolsFromClicked()
{
	SymbolReplacement(*map).withSymbolSetFileDialog(window);
}


void MapEditorController::loadCrtClicked()
{
	SymbolReplacement(*map, *map).withCrtFileDialog(window);
}


void MapEditorController::loadColorsFromClicked()
{
	// TODO
}

void MapEditorController::scaleAllSymbolsClicked()
{
	bool ok;
	double percent = QInputDialog::getDouble(window, tr("Scale all symbols"), tr("Scale to percentage:"), 100, 0, 999999, 6, &ok);
	if (!ok || percent == 100)
		return;
	
	map->scaleAllSymbols(percent / 100.0);
}

void MapEditorController::scaleMapClicked()
{
	auto const scale_before = map->getScaleDenominator();  // foom
	ScaleMapDialog dialog(window, map);
	dialog.setWindowModality(Qt::WindowModal);
	dialog.exec();
	
	// foom: scale bars show the old scale now. the design notes, "Generators"
	auto const scale_bars = scaleDependentGroups(*map);
	if (map->getScaleDenominator() != scale_before && !scale_bars.empty()
	    && QMessageBox::question(window, tr("Change map scale"), tr("The map has %n scale bar(s). Rebuild them for the new scale?", nullptr, int(scale_bars.size()))) == QMessageBox::Yes)
	{
		QString error;
		regenerateGroups(*map, scale_bars, &error);
	}
}

void MapEditorController::rotateMapClicked()
{
	RotateMapDialog dialog(*map, window);
	dialog.setWindowModality(Qt::WindowModal);
	if (dialog.exec() == QDialog::Accepted)
		dialog.rotate(*map);
}

void MapEditorController::mapNotesClicked()
{
	if (map)
	{
		MapNotesDialog dialog(window, *map);
		dialog.setWindowModality(Qt::WindowModal);
		dialog.exec();
	}
}

void MapEditorController::mapInfoClicked()
{
	MapInformationDialog dialog(window, map);
	dialog.setWindowModality(Qt::WindowModal);
	dialog.exec();
}

void MapEditorController::createTemplateWindow()
{
	Q_ASSERT(!template_dock_widget);
	
	template_list_widget = new TemplateListWidget(*map, *main_view, *this);
	
	if (isInMobileMode())
	{
		template_dock_widget = createDockWidgetSubstitute(window, template_list_widget);
		connect(template_list_widget, &TemplateListWidget::closeClicked, this, [this]() { showTemplateWindow(false); });
	}
	else
	{
		auto* dock_widget = new EditorDockWidget(tr("Templates"), template_window_act, this, window);
		dock_widget->setWidget(template_list_widget);
		dock_widget->setObjectName(QString::fromLatin1("Templates dock widget"));
		if (!window->restoreDockWidget(dock_widget))
			window->addDockWidget(Qt::RightDockWidgetArea, dock_widget, Qt::Vertical);
		dock_widget->setVisible(false);
		
		template_dock_widget = dock_widget;
	}
}

void MapEditorController::showTemplateWindow(bool show)
{
	if (!template_dock_widget)
		createTemplateWindow();
	
	template_window_act->setChecked(show);
	template_dock_widget->setVisible(show);
}

void MapEditorController::openTemplateClicked()
{
	auto new_template = TemplateListWidget::showOpenTemplateDialog(window, *this);
	if (new_template)
	{
		map->addTemplate(-1, std::move(new_template));
		hideAllTemplates(false);
		showTemplateWindow(true);
	}
}

// foom
// foom: the design notes, "Relink by folder"
void MapEditorController::relinkTemplatesClicked()
{
	auto const title = tr("Relink by folder");
	bool ok = false;
	auto const old_folder = QInputDialog::getText(window, title, tr("The folder in which the templates were:"), QLineEdit::Normal,
	                                              QDir::toNativeSeparators(missingTemplatesFolder(*map)), &ok);
	if (!ok || old_folder.trimmed().isEmpty())
		return;
	auto const new_folder = QFileDialog::getExistingDirectory(window, tr("The folder in which they are now"),
	                                                          relinkStartFolder(old_folder.trimmed(), window->currentPath()));
	if (new_folder.isEmpty())
		return;
	auto const report = OpenOrienteering::relinkTemplates(*map, old_folder.trimmed(), new_folder);
	QStringList lines;
	lines << tr("%n template(s) relinked.", nullptr, report.relinked.size());
	if (!report.not_found.isEmpty())
		lines << tr("Not in the new folder: %1").arg(report.not_found.join(QLatin1String(", ")));
	if (!report.failed.isEmpty())
		lines << tr("Found but not loaded: %1").arg(report.failed.join(QLatin1String(", ")));
	QMessageBox::information(window, title, lines.join(QLatin1Char('\n')));
}

// foom: the design notes, "Templates on a spot colour plate"
bool MapEditorController::setTemplateSeparation(Template* temp, const MapColor* color, QString* error)
{
	if (!temp || !temp->isRasterGraphics())
	{
		if (error)
			*error = tr("Only raster templates can be given a spot colour plate.");
		return false;
	}
	if (color && (map->findColorIndex(color) < 0 || color->getSpotColorMethod() != MapColor::SpotColor))
	{
		if (error)
			*error = tr("Only a spot colour of the map has a plate.");
		return false;
	}
	temp->setSeparationColor(color);
	return true;
}

// foom: the design notes, "Embedded raster templates"
bool MapEditorController::setTemplateEmbedded(Template* temp, bool embedded, QString* error)
{
	auto* image = qobject_cast<TemplateImage*>(temp);
	if (!image)
	{
		if (error)
			*error = tr("Only image templates can be embedded.");
		return false;
	}
	return embedded ? image->embed(TemplateImage::embedLimitBytes(), error) : image->unembed(error);
}

// foom
int MapEditorController::relinkTemplates(const QString& old_folder, const QString& new_folder)
{
	auto const report = OpenOrienteering::relinkTemplates(*map, old_folder, new_folder);
	return report.relinked.size();
}

void MapEditorController::addMapServiceClicked()
{
	// the design notes, "Tile and map service templates".
#ifdef MAPPER_USE_GDAL
	MapServiceDialog dialog(this, window);
	if (dialog.exec() != QDialog::Accepted)
		return;
	auto new_template = std::make_unique<TileServiceTemplate>(dialog.service(), map);
	if (!new_template->setupAndLoad(window, main_view))
	{
		QMessageBox::warning(window, tr("Add map service"), tr("Cannot open the map service:\n%1").arg(new_template->errorString()));
		return;
	}
	map->addTemplate(-1, std::move(new_template));
	hideAllTemplates(false);
	showTemplateWindow(true);
#endif
}

// foom
void MapEditorController::updateMapServiceAction()
{
	if (!add_map_service_act)
		return;
	QString reason;
#ifdef MAPPER_USE_GDAL
	if (!TileServiceTemplate::driversAvailable(&reason))
		;  // reason is set
	else if (map->getGeoreferencing().getState() != Georeferencing::Geospatial)
		reason = tr("The map must be georeferenced with a projected coordinate system (Map > Georeferencing...).");
#else
	reason = tr("This build has no GDAL support.");
#endif
	add_map_service_act->setEnabled(reason.isEmpty());
	add_map_service_act->setToolTip(reason);
	add_map_service_act->setStatusTip(reason);
}

// foom
void MapEditorController::drawTemplateAttributions(QPainter* painter, MapWidget* widget) const
{
	// the design notes, "Attribution and fair use".
#ifdef MAPPER_USE_GDAL
	QStringList attributions;
	auto const* view = widget->getMapView();
	for (int i = 0; i < map->getNumTemplates(); ++i)
	{
		auto const* temp = qobject_cast<const TileServiceTemplate*>(map->getTemplate(i));
		if (!temp || temp->getTemplateState() != Template::Loaded || temp->service().attribution.isEmpty())
			continue;
		if (view->areAllTemplatesHidden() || !view->isTemplateVisible(temp))
			continue;
		if (!attributions.contains(temp->service().attribution))
			attributions.append(temp->service().attribution);
	}
	if (attributions.isEmpty())
		return;
	auto const text = attributions.join(QLatin1String(" | "));
	painter->save();
	auto font = painter->font();
	font.setPointSizeF(qMax(6.0, font.pointSizeF() * 0.8));
	painter->setFont(font);
	auto const metrics = painter->fontMetrics();
	auto const rect = QRectF(widget->width() - metrics.horizontalAdvance(text) - 10, widget->height() - metrics.height() - 4,
	                         metrics.horizontalAdvance(text) + 8, metrics.height() + 2);
	painter->fillRect(rect, QColor(255, 255, 255, 180));
	painter->setPen(QColor(40, 40, 40));
	painter->drawText(rect, Qt::AlignCenter, text);
	painter->restore();
#else
	Q_UNUSED(painter)
	Q_UNUSED(widget)
#endif
}

void MapEditorController::reopenTemplateClicked()
{
	hideAllTemplates(false);
	
	QString map_directory = window->currentPath();
	if (!map_directory.isEmpty())
		map_directory = QFileInfo(map_directory).canonicalPath();
	auto* dialog = new ReopenTemplateDialog(window, map, map_directory); 
	dialog->setWindowModality(Qt::WindowModal);
	dialog->exec();
	delete dialog;
}

void MapEditorController::templateAvailabilityChanged()
{
	// Nothing
}

void MapEditorController::closedTemplateAvailabilityChanged()
{
	if (reopen_template_act)
		reopen_template_act->setEnabled(map->getNumClosedTemplates() > 0);
}

void MapEditorController::createTagEditor()
{
	Q_ASSERT(!tags_dock_widget);
	
	auto* tags_widget = new TagsWidget(map, main_view, this);
	tags_dock_widget = new EditorDockWidget(tr("Tag Editor"), tags_window_act, this, window);
	tags_dock_widget->setWidget(tags_widget);
	tags_dock_widget->setObjectName(QString::fromLatin1("Tag editor dock widget"));
	if (!window->restoreDockWidget(tags_dock_widget))
		window->addDockWidget(Qt::RightDockWidgetArea, tags_dock_widget, Qt::Vertical);
	tags_dock_widget->setVisible(false);
}

void MapEditorController::showTagsWindow(bool show)
{
	if (!tags_dock_widget)
		createTagEditor();
	
	tags_window_act->setChecked(show);
	tags_dock_widget->setVisible(show);
}


void MapEditorController::editGeoreferencing()
{
	if (georeferencing_dialog.isNull())
	{
		auto* dialog = new GeoreferencingDialog(this); 
		georeferencing_dialog.reset(dialog);
		connect(dialog, &QDialog::finished, this, &MapEditorController::georeferencingDialogFinished);
	}
	georeferencing_dialog->exec();
}

void MapEditorController::georeferencingDialogFinished()
{
	georeferencing_dialog.take()->deleteLater();
	map->updateAllMapWidgets();
	
	bool gps_display_possible = map->getGeoreferencing().getState() == Georeferencing::Geospatial;
	if (!gps_display_possible)
	{
		gps_display_action->setChecked(false);
		if (gps_display)
			enableGPSDisplay(false);
	}
	gps_display_action->setEnabled(gps_display_possible);
}

void MapEditorController::selectedSymbolsChanged()
{
	Symbol* symbol = symbol_widget->getSingleSelectedSymbol();
	
	if (mobile_mode)
	{
		auto* symbol_button = bottom_action_bar->getButtonForAction(mobile_symbol_selector_action);
		        
		// (Re-)create the mobile_symbol_selector_action icon
		QSize icon_size = bottom_action_bar->getIconSize(2, 2);
		QPixmap pixmap(icon_size);
		pixmap.fill(Qt::white);
		if (symbol_widget->selectedSymbolsCount() != 1)
		{
			QFont font(window->font());
			font.setPixelSize(icon_size.height() / 5);
			QPainter painter(&pixmap);
			painter.setFont(font);
			QString text = (symbol_widget->selectedSymbolsCount() == 0) ?
				//: Keep it short. Should not be much longer per line than the longest word in the original.
				tr("No\nsymbol\nselected") :
				//: Keep it short. Should not be much longer per line than the longest word in the original.
				tr("Multiple\nsymbols\nselected");
			painter.drawText(pixmap.rect(), Qt::AlignCenter, text);
			
			symbol_button->setMenu(nullptr);
		}
		else //if (symbol_widget->getNumSelectedSymbols() == 1)
		{
			auto image = symbol->getCustomIcon();
			if (image.isNull())
				image = symbol->createIcon(*map, qMin(icon_size.width(), icon_size.height()));
			else
				image = image.scaled(icon_size.width(), icon_size.height(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
			if (symbol->isHidden() || symbol->isProtected() || symbol->isHelperSymbol())
			{
				QPainter p(&image);
				if (symbol->isHidden())
					HiddenSymbolDecorator(icon_size.width()).draw(p);
				if (symbol->isProtected())
					ProtectedSymbolDecorator(icon_size.width()).draw(p);
				if (symbol->isHelperSymbol())
					HelperSymbolDecorator(icon_size.width()).draw(p);
			}
			pixmap = QPixmap::fromImage(image);
			
			symbol_button->setMenu(mobile_symbol_button_menu);
			const auto actions = mobile_symbol_button_menu->actions();
			int i = 0;
			actions[i]->setText(symbol->getNumberAndPlainTextName());
			actions[++i]->setVisible(!symbol->getDescription().isEmpty());
			++i;  // separator
			actions[++i]->setChecked(symbol->isHidden());
			actions[++i]->setChecked(symbol->isProtected());
		}
		mobile_symbol_selector_action->setIcon(QIcon(pixmap));
	}
	
	// FIXME: Postpone switch of active symbol while editing is progress
	bool const symbol_changed = active_symbol != symbol;  // foom
	if (active_symbol != symbol)
	{
		active_symbol = symbol;
		
		if (symbol && !symbol->isHidden() && !symbol->isProtected() && current_tool)
		{
			// Auto-switch to a draw tool when selecting a symbol under certain conditions
			if (current_tool->toolType() == MapEditorTool::Pan || current_tool->toolType() == MapEditorTool::BoxZoom || current_tool->toolType() == MapEditorTool::Scribble
			    || ((current_tool->toolType() == MapEditorTool::EditLine || current_tool->toolType() == MapEditorTool::EditPoint) && map->getNumSelectedObjects() == 0))
			{
				current_tool->switchToDefaultDrawTool(active_symbol);
			}
		}
		
		if (symbol)
			window->showStatusBarMessage(symbol->getNumberAndPlainTextName(), 1000);
	}
	
	// Even when the symbol (pointer) hasn't changed,
	// the symbol's visibility may constitute a change.
	emit activeSymbolChanged(active_symbol);
	
	updateSymbolDependentActions();
	updateSymbolAndObjectDependentActions();
	
	// foom: the symbol's preferred tool, see the design notes, "Line symbol properties".
	if (symbol_changed && symbol && symbol->getType() == Symbol::Line && !editing_in_progress
	    && !symbol->isHidden() && !symbol->isProtected() && !symbol->asLine()->getPreferredTool().isEmpty())
	{
		auto const id = symbol->asLine()->getPreferredTool().toLatin1();
		if (auto* action = findAction(id.constData()))
		{
			if (action->isEnabled() && (!current_tool || current_tool->toolAction() != action))
				action->trigger();
		}
	}
}

void MapEditorController::objectSelectionChanged()
{
	if (mode != MapEditor)
		return;
	
	updateObjectDependentActions();
	
	// Automatic symbol selection of selected objects
	if (symbol_widget && !editing_in_progress)
	{
		bool uniform_symbol_selected = true;
		const Symbol* uniform_symbol = nullptr;
		for (const auto* object : map->selectedObjects())
		{
			const auto* symbol = object->getSymbol();
			if (!uniform_symbol)
			{
				uniform_symbol = symbol;
			}
			else if (uniform_symbol != symbol)
			{
				uniform_symbol_selected = false;
				break;
			}
		}
		if (uniform_symbol_selected && Settings::getInstance().getSettingCached(Settings::MapEditor_ChangeSymbolWhenSelecting).toBool())
			symbol_widget->selectSingleSymbol(uniform_symbol);
	}
	
	updateSymbolAndObjectDependentActions();
}

void MapEditorController::updateSymbolDependentActions()
{
	const Symbol* symbol = activeSymbol();
	const Symbol::Type type = (symbol && !editing_in_progress) ? symbol->getType() : Symbol::NoSymbol;
	
	select_by_current_symbol_act->setEnabled(!editing_in_progress && symbol_widget && symbol_widget->selectedSymbolsCount());
	
	updateDrawPointGPSAvailability();
	draw_point_act->setEnabled(type == Symbol::Point && !symbol->isHidden());
	draw_point_act->setStatusTip(tr("Place point objects on the map.") + (draw_point_act->isEnabled() ? QString{} : QString(QLatin1Char(' ') + tr("Select a point symbol to be able to use this tool."))));
	draw_path_act->setEnabled((type == Symbol::Line || type == Symbol::Area || type == Symbol::Combined) && !symbol->isHidden());
	draw_path_act->setStatusTip(tr("Draw polygonal and curved lines.") + (draw_path_act->isEnabled() ? QString{} : QString(QLatin1Char(' ') + tr("Select a line, area or combined symbol to be able to use this tool."))));
	draw_circle_act->setEnabled(draw_path_act->isEnabled());
	draw_circle_act->setStatusTip(tr("Draw circles and ellipses.") + (draw_circle_act->isEnabled() ? QString{} : QString(QLatin1Char(' ') + tr("Select a line, area or combined symbol to be able to use this tool."))));
	draw_rectangle_act->setEnabled(draw_path_act->isEnabled());
	draw_rectangle_act->setStatusTip(tr("Draw rectangles.") + (draw_rectangle_act->isEnabled() ? QString{} : QString(QLatin1Char(' ') + tr("Select a line, area or combined symbol to be able to use this tool."))));
	draw_rectangle_box_act->setEnabled(draw_rectangle_act->isEnabled());  // foom
	draw_stairway_act->setEnabled(draw_rectangle_act->isEnabled() && (symbol->getContainedTypes() & Symbol::Line));  // foom
	draw_frame_act->setEnabled(draw_rectangle_act->isEnabled());  // foom
	draw_points_along_act->setEnabled(draw_point_act->isEnabled());  // foom
	draw_sized_shape_act->setEnabled(draw_rectangle_act->isEnabled());  // foom
	draw_rectangle_box_act->setStatusTip(tr("Draw a frame with an optional numbered grid.") + (draw_rectangle_box_act->isEnabled() ? QString{} : QString(QLatin1Char(' ') + tr("Select a line, area or combined symbol to be able to use this tool."))));
	draw_freehand_act->setEnabled(draw_path_act->isEnabled());
	draw_freehand_act->setStatusTip(tr("Draw paths free-handedly.") + (draw_freehand_act->isEnabled() ? QString{} : QString(QLatin1Char(' ') + tr("Select a line, area or combined symbol to be able to use this tool."))));
	draw_fill_act->setEnabled(draw_path_act->isEnabled());
	draw_fill_act->setStatusTip(tr("Fill bounded areas.") + (draw_fill_act->isEnabled() ? QString{} : QString(QLatin1Char(' ') + tr("Select a line, area or combined symbol to be able to use this tool."))));
	draw_text_act->setEnabled(type == Symbol::Text && !symbol->isHidden());
	draw_text_act->setStatusTip(tr("Write text on the map.") + (draw_text_act->isEnabled() ? QString{} : QString(QLatin1Char(' ') + tr("Select a text symbol to be able to use this tool."))));
}

void MapEditorController::updateObjectDependentActions()
{
	bool have_multiple_parts     = map->getNumParts() > 1;
	bool have_selection          = map->getNumSelectedObjects() > 0 && !editing_in_progress;
	bool single_object_selected  = map->getNumSelectedObjects() == 1 && !editing_in_progress;
	bool have_line               = false;
	bool have_area               = false;
	bool have_area_with_holes    = false;
	bool have_rotatable_pattern  = false;
	bool have_rotatable_object   = false;
	int  num_selected_paths      = 0;
	bool first_selected_is_path  = have_selection && map->getFirstSelectedObject()->getType() == Object::Path;
	bool uniform_symbol_selected = true;
	const Symbol* uniform_symbol = nullptr;
	const Symbol* first_selected_symbol= have_selection ? map->getFirstSelectedObject()->getSymbol() : nullptr;
	std::vector< bool > symbols_in_selection(map->getNumSymbols(), false);
	
	if (!editing_in_progress)
	{
		for (const auto* object : map->selectedObjects())
		{
			const auto* symbol = object->getSymbol();
			int symbol_index = map->findSymbolIndex(symbol);
			if (symbol_index >= 0 && symbol_index < (int)symbols_in_selection.size())
				symbols_in_selection[symbol_index] = true;
			
			if (uniform_symbol_selected)
			{
				if (!uniform_symbol)
				{
					uniform_symbol = symbol;
				}
				else if (uniform_symbol != symbol)
				{
					uniform_symbol = nullptr;
					uniform_symbol_selected = false;
				}
			}
			
			have_rotatable_object |= symbol->isRotatable();
			have_rotatable_pattern |= symbol->hasRotatableFillPattern();
			
			if (Symbol::areTypesCompatible(symbol->getType(), Symbol::Area))
			{
				++num_selected_paths;
				
				int const contained_types = symbol->getContainedTypes();
				
				if (contained_types & Symbol::Line)
				{
					have_line = true;
				}
				
				if (contained_types & Symbol::Area)
				{
					have_area = true;
					have_area_with_holes |= object->asPath()->parts().size() > 1;
				}
			}
		}
	}
	
	// have_selection
	cut_act->setEnabled(have_selection);
	copy_act->setEnabled(have_selection);
	delete_act->setEnabled(have_selection);
	delete_act->setStatusTip(tr("Deletes the selected objects.") + (delete_act->isEnabled() ? QString{} : QString(QLatin1Char(' ') + tr("Select at least one object to activate this tool."))));
	duplicate_act->setEnabled(have_selection);
	duplicate_act->setStatusTip(tr("Duplicate the selected objects.") + (duplicate_act->isEnabled() ? QString{} : QString(QLatin1Char(' ') + tr("Select at least one object to activate this tool."))));
	rotate_act->setEnabled(have_selection);
	rotate_act->setStatusTip(tr("Rotate the selected objects.") + (rotate_act->isEnabled() ? QString{} : QString(QLatin1Char(' ') + tr("Select at least one object to activate this tool."))));
	scale_act->setEnabled(have_selection);
	scale_act->setStatusTip(tr("Scale the selected objects.") + (scale_act->isEnabled() ? QString{} : QString(QLatin1Char(' ') + tr("Select at least one object to activate this tool."))));
	mappart_move_menu->setEnabled(have_selection && have_multiple_parts);
	updateLayoutActions();  // foom
	
	// have_rotatable_pattern || have_rotatable_point
	rotate_pattern_act->setEnabled(have_rotatable_pattern || have_rotatable_object);
	rotate_pattern_act->setStatusTip(tr("Set the direction of area fill patterns or point objects.") + (rotate_pattern_act->isEnabled() ? QString{} : QString(QLatin1Char(' ') + tr("Select an area object with rotatable fill pattern or a rotatable point object to activate this tool."))));
	
	// have_line
	switch_dashes_act->setEnabled(have_line);
	switch_dashes_act->setStatusTip(tr("Switch the direction of symbols on line objects.") + (switch_dashes_act->isEnabled() ? QString{} : QString(QLatin1Char(' ') + tr("Select at least one line object to activate this tool."))));
	connect_paths_act->setEnabled(have_line);
	connect_paths_act->setStatusTip(tr("Connect endpoints of paths which are close together.") + (connect_paths_act->isEnabled() ? QString{} : QString(QLatin1Char(' ') + tr("Select at least one line object to activate this tool."))));
	
	// have_are || have_line
	cut_tool_act->setEnabled(have_area || have_line);
	cut_tool_act->setStatusTip(tr("Cut the selected objects into smaller parts.") + (cut_tool_act->isEnabled() ? QString{} : QString(QLatin1Char(' ') + tr("Select at least one line or area object to activate this tool."))));
	convert_to_curves_act->setEnabled(have_area || have_line);
	convert_to_curves_act->setStatusTip(tr("Turn paths made of straight segments into smooth bezier splines.") + (convert_to_curves_act->isEnabled() ? QString{} : QString(QLatin1Char(' ') + tr("Select a path object to activate this tool."))));
	simplify_path_act->setEnabled(have_area || have_line);
	simplify_path_act->setStatusTip(tr("Reduce the number of points in path objects while trying to retain their shape.") + (simplify_path_act->isEnabled() ? QString{} : QString(QLatin1Char(' ') + tr("Select a path object to activate this tool."))));
	to_polyline_act->setEnabled(have_area || have_line);  // foom
	to_polyline_act->setStatusTip(tr("Replace the curves of path objects by straight segments.") + (to_polyline_act->isEnabled() ? QString{} : QString(QLatin1Char(' ') + tr("Select a path object to activate this tool."))));
	to_curve_act->setEnabled(have_area || have_line);  // foom
	to_curve_act->setStatusTip(tr("Turn the straight segments of path objects into curves through the same vertices.") + (to_curve_act->isEnabled() ? QString{} : QString(QLatin1Char(' ') + tr("Select a path object to activate this tool."))));
	mirror_act->setEnabled(have_selection);  // foom
	object_info_act->setEnabled(map->getNumSelectedObjects() == 1);  // foom
	convert_to_point_act->setEnabled(have_selection);  // foom
	interpolate_act->setEnabled(map->getNumSelectedObjects() == 2 && std::all_of(map->selectedObjects().begin(), map->selectedObjects().end(), [](const Object* o) { return o->getType() == Object::Path; }));  // foom
	mirror_act->setStatusTip(tr("Mirror the selected objects about an axis through their center.") + (mirror_act->isEnabled() ? QString{} : QString(QLatin1Char(' ') + tr("Select at least one object to activate this tool."))));
	for (auto* action : { move_by_act, rotate_by_act, stretch_act })  // foom
	{
		action->setEnabled(have_selection);
		action->setStatusTip(action->isEnabled() ? QString{} : tr("Select at least one object to activate this tool."));
	}
	for (auto* action : { move_parallel_act, move_parallel_by_act, vertex_type_normal_act, vertex_type_dash_act })  // foom
	{
		action->setEnabled(have_line || have_area);
		action->setStatusTip(action->isEnabled() ? QString{} : tr("Select a line or area object to activate this tool."));
	}
	auto const num_selected = have_selection ? map->getNumSelectedObjects() : 0;  // foom
	for (auto* action : align_acts)
	{
		action->setEnabled(num_selected >= 2);
		action->setStatusTip(action->isEnabled() ? QString{} : tr("Select at least two objects to activate this tool."));
	}
	for (auto* action : { distribute_h_act, distribute_v_act })
	{
		action->setEnabled(num_selected >= 3);
		action->setStatusTip(action->isEnabled() ? QString{} : tr("Select at least three objects to activate this tool."));
	}
	for (auto* action : { topo_join_act, fix_overshoots_act, intersections_act })  // foom
	{
		action->setEnabled(num_selected >= 2 && (have_line || have_area));
		action->setStatusTip(action->isEnabled() ? QString{} : tr("Select at least two path objects to activate this tool."));
	}
	remove_duplicate_vertices_act->setEnabled(have_line || have_area);  // foom
	lengthen_line_text_act->setEnabled(have_selection && std::any_of(map->selectedObjects().begin(), map->selectedObjects().end(), [](const Object* o) { return o->getType() == Object::Text && o->asText()->followsPath(); }));  // foom
	lengthen_line_text_act->setStatusTip(lengthen_line_text_act->isEnabled() ? QString{} : tr("Select a line text object to activate this tool."));
	for (auto* action : { make_rectangular_act, generalize_buildings_act })  // foom
	{
		action->setEnabled(have_line || have_area);
		action->setStatusTip(action->isEnabled() ? QString{} : tr("Select closed line or area objects to activate this tool."));
	}
	topo_smooth_act->setEnabled(have_line || have_area);  // foom
	topo_smooth_act->setStatusTip(topo_smooth_act->isEnabled() ? QString{} : tr("Select a line or area object to activate this tool."));
	group_act->setEnabled(num_selected >= 2);  // foom
	group_act->setStatusTip(group_act->isEnabled() ? QString{} : tr("Select at least two objects to activate this tool."));
	for (auto* action : { ungroup_act, select_group_act })  // foom
	{
		action->setEnabled(have_selection);
		action->setStatusTip(action->isEnabled() ? QString{} : tr("Select at least one object to activate this tool."));
	}
	
	// cut_hole_enabled
	const bool cut_hole_enabled = single_object_selected && have_area;
	cut_hole_act->setEnabled(cut_hole_enabled);
	cut_hole_act->setStatusTip(tr("Cut a hole into the selected area object.") + (cut_hole_act->isEnabled() ? QString{} : QString(QLatin1Char(' ') + tr("Select a single area object to activate this tool."))));
	cut_hole_circle_act->setEnabled(cut_hole_enabled);
	cut_hole_circle_act->setStatusTip(cut_hole_act->statusTip());
	cut_hole_rectangle_act->setEnabled(cut_hole_enabled);
	cut_hole_rectangle_act->setStatusTip(cut_hole_act->statusTip());
	cut_hole_menu->setEnabled(cut_hole_enabled);
	
	// boolean_prerequisite [&& x]
	bool const boolean_prerequisite = first_selected_is_path && num_selected_paths >= 2;
	QString const extra_status_tip = QLatin1Char(' ') +
	                                 ( boolean_prerequisite
	                                 ? tr("Resulting symbol: %1.").arg(first_selected_symbol->getNumberAndPlainTextName())
	                                 : tr("Select at least two area or path objects activate this tool.") );
	boolean_union_act->setEnabled(boolean_prerequisite);
	boolean_union_act->setStatusTip(tr("Unify overlapping objects.") + extra_status_tip);
	boolean_intersection_act->setEnabled(boolean_prerequisite);
	boolean_intersection_act->setStatusTip(tr("Remove all parts which are not overlaps with the first selected object.") + extra_status_tip);
	boolean_difference_act->setEnabled(boolean_prerequisite);
	boolean_difference_act->setStatusTip(tr("Remove overlapped parts of the first selected object.") + (boolean_prerequisite ? QString{} : extra_status_tip));
	boolean_xor_act->setEnabled(boolean_prerequisite);
	boolean_xor_act->setStatusTip(tr("Remove all parts which overlap the first selected object.") + extra_status_tip);
	
	// special
	boolean_merge_holes_act->setEnabled(single_object_selected && have_area_with_holes);
	boolean_merge_holes_act->setStatusTip(tr("Merge area holes together, or merge holes with the object boundary to cut out this part.") + (boolean_merge_holes_act->isEnabled() ? QString{} : QString(QLatin1Char(' ') + tr("Select one area object with holes to activate this tool."))));
	
	// clip_erase_enabled
	bool const clip_erase_enabled = single_object_selected && (have_area || have_line) && !have_area_with_holes && (*(map->selectedObjectsBegin()))->asPath()->parts().front().isClosed();
	clip_area_act->setEnabled(clip_erase_enabled);
	clip_area_act->setStatusTip(tr("Create a cutout of some objects or the whole map.") + (clip_area_act->isEnabled() ? QString{} : QString(QLatin1Char(' ') + tr("Select a closed path object as cutout shape to activate this tool."))));
	erase_area_act->setEnabled(clip_erase_enabled);
	erase_area_act->setStatusTip(tr("Cut away some objects or everything in a limited area.") + (erase_area_act->isEnabled() ? QString{} : QString(QLatin1Char(' ') + tr("Select a closed path object as cutout shape to activate this tool."))));
}

void MapEditorController::updateSymbolAndObjectDependentActions()
{
	const Symbol* single_symbol = activeSymbol();
	bool single_symbol_compatible = false;
	bool single_symbol_different  = false;
	if (!editing_in_progress)
	{
		map->getSelectionToSymbolCompatibility(single_symbol, single_symbol_compatible, single_symbol_different);
	}
	
	switch_symbol_act->setEnabled(single_symbol_compatible && single_symbol_different);
	switch_symbol_act->setStatusTip(tr("Switches the symbol of the selected objects to the selected symbol.") + (switch_symbol_act->isEnabled() ? QString{} : QString(QLatin1Char(' ') + tr("Select at least one object and a fitting, different symbol to activate this tool."))));
	fill_border_act->setEnabled(single_symbol_compatible && single_symbol_different);
	fill_border_act->setStatusTip(tr("Fill the selected lines or create a border for the selected areas.") + (fill_border_act->isEnabled() ? QString{} : QString(QLatin1Char(' ') + tr("Select at least one object and a fitting, different symbol to activate this tool."))));
	distribute_points_act->setEnabled(single_symbol && single_symbol->getType() == Symbol::Point
	                                  && containsPathObject(map->selectedObjects()));
	distribute_points_act->setStatusTip(tr("Places evenly spaced point objects along an existing path object") + (distribute_points_act->isEnabled() ? QString{} : QString(QLatin1Char(' ') + tr("Select at least one path object and a single point symbol to activate this tool."))));
}

void MapEditorController::undoStepAvailabilityChanged()
{
	if (mode != MapEditor)
		return;
	
	undo_act->setEnabled(map->undoManager().canUndo());
	redo_act->setEnabled(map->undoManager().canRedo());
	clear_undo_redo_history_act->setEnabled(undo_act->isEnabled() || redo_act->isEnabled());
}

void MapEditorController::clipboardChanged(QClipboard::Mode mode)
{
	if (mode == QClipboard::Clipboard)
		updatePasteAvailability();
}

void MapEditorController::updatePasteAvailability()
{
	const bool paste_available = QApplication::clipboard()->mimeData()
		&& QApplication::clipboard()->mimeData()->hasFormat(MimeType::OpenOrienteeringObjects())
		&& !editing_in_progress;
	
	if (paste_act)
		paste_act->setEnabled(paste_available);
	if (paste_original_act)
		paste_original_act->setEnabled(paste_available);
}

void MapEditorController::showWholeMap()
{
	QRectF map_extent = map->calculateExtent(true, !main_view->areAllTemplatesHidden(), main_view);
	map_widget->adjustViewToRect(map_extent, MapWidget::ContinuousZoom);
}

void MapEditorController::editToolClicked()
{
	setEditTool();
}

void MapEditorController::editLineToolClicked()
{
	if (!current_tool || current_tool->toolType() != MapEditorTool::EditLine)
		setTool(new EditLineTool(this, edit_line_tool_act));
}

void MapEditorController::drawPointClicked()
{
	setTool(new DrawPointTool(this, draw_point_act));
}

void MapEditorController::drawPathClicked()
{
	setTool(new DrawPathTool(this, draw_path_act, false, true));
}

void MapEditorController::drawCircleClicked()
{
	setTool(new DrawCircleTool(this, draw_circle_act, false));
}

void MapEditorController::drawRectangleClicked()
{
	setTool(new DrawRectangleTool(this, draw_rectangle_act, false));
}

// foom
void MapEditorController::drawRectangleBoxClicked()
{
	RectangleBoxDialog dialog(map, window);
	if (dialog.exec() == QDialog::Accepted)
		drawRectangleBox(dialog.params());
}

// foom: the design notes, "Stairway"
void MapEditorController::drawStairwayClicked()
{
	bool ok = false;
	auto const tread = QInputDialog::getDouble(window, tr("Stairway"), tr("Tread depth (mm):"), stairway_tread_mm, 0.05, 20.0, 2, &ok);
	if (ok)
	{
		stairway_tread_mm = tread;
		drawStairway(tread);
	}
}

// foom
void MapEditorController::drawStairway(double tread_mm)
{
	setTool(new StairwayTool(this, draw_stairway_act, tread_mm));
}

// foom: the design notes, "Frame around the map"
void MapEditorController::drawFrameClicked()
{
	bool ok = false;
	auto const margin = QInputDialog::getDouble(window, tr("Frame around map"), tr("Make a frame around the entire map, at a margin of (mm):"), frame_margin_mm, 0.0, 100.0, 2, &ok);
	if (ok)
	{
		frame_margin_mm = margin;
		drawFrame(margin);
	}
}

// foom
void MapEditorController::drawFrame(double margin_mm)
{
	ops::FrameParams params;
	params.symbol = activeSymbol();
	params.margin_mm = margin_mm;
	applyOpResult(ops::frameAroundMap(*map, params));
}

// foom: the design notes, "Points along a drawn path"
void MapEditorController::drawPointsAlongClicked()
{
	auto* point = activeSymbol() && activeSymbol()->getType() == Symbol::Point ? const_cast<PointSymbol*>(activeSymbol()->asPoint()) : nullptr;
	if (!point)
		return;
	if (DistributePointsTool::showSettingsDialog(window, point, points_along_settings))
		drawPointsAlong(points_along_settings);
}

// foom
void MapEditorController::drawPointsAlong(const DistributePointsTool::Settings& settings)
{
	auto* point = activeSymbol() && activeSymbol()->getType() == Symbol::Point ? const_cast<PointSymbol*>(activeSymbol()->asPoint()) : nullptr;
	if (point)
		setTool(new PointsAlongTool(this, draw_points_along_act, point, settings));
}

// foom: the design notes, "Shapes of a given size"
void MapEditorController::drawSizedShapeClicked()
{
	SizedShapeDialog dialog(sized_shape_params, window);
	if (dialog.exec() == QDialog::Accepted)
	{
		sized_shape_params = dialog.params();
		drawSizedShape(sized_shape_params);
	}
}

// foom
void MapEditorController::drawSizedShape(const ops::SizedShapeParams& params)
{
	setTool(new SizedShapeTool(this, draw_sized_shape_act, params));
}

// foom: the design notes, "Freehand smoothing"
void MapEditorController::freehandSmoothingChosen()
{
	for (int i = 0; i < 4; ++i)
	{
		if (freehand_smoothing_acts[i] && freehand_smoothing_acts[i]->isChecked())
			setFreehandSmoothingLevel(i);
	}
}

// foom: the design notes, "Join while drawing"
void MapEditorController::autoJoinToggled(bool checked)
{
	setAutoJoinEnabled(checked);
}

// foom: the design notes, "Open rectangles"
bool MapEditorController::leavesRectanglesOpen() const
{
	return draw_rectangle_open_act && draw_rectangle_open_act->isChecked();
}

// foom
void MapEditorController::drawRectangleBox(const ops::RectangleBoxParams& params)
{
	// the design notes, "Rectangle objects".
	setTool(new RectangleBoxTool(this, draw_rectangle_box_act, params));
}

void MapEditorController::drawFreehandClicked()
{
	setTool(new DrawFreehandTool(this, draw_freehand_act, false));
}

void MapEditorController::drawFillClicked()
{
	setTool(new FillTool(this, draw_fill_act));
}

void MapEditorController::drawTextClicked()
{
	if (activeSymbol() && activeSymbol()->getType() == Symbol::Text && activeSymbol()->asText()->followsPath())  // foom
		setTool(new DrawLineTextTool(this, draw_text_act));
	else
		setTool(new DrawTextTool(this, draw_text_act));
}

void MapEditorController::deleteClicked()
{
	if (editing_in_progress)
		return;
	map->deleteSelectedObjects();
}

void MapEditorController::duplicateClicked()
{
	Q_ASSERT(!map->selectedObjects().empty());
	
	std::vector<Object*> new_objects;
	new_objects.reserve(map->getNumSelectedObjects());
	
	for (const auto* object : map->selectedObjects())
	{
		Object* duplicate = object->duplicate();
		map->addObject(duplicate);
		new_objects.push_back(duplicate);
	}
	
	auto* undo_step = new DeleteObjectsUndoStep(map);
	MapPart* part = map->getCurrentPart();
	
	map->clearObjectSelection(false);
	for (auto* object : new_objects)
	{
		undo_step->addObject(part->findObjectIndex(object));
		map->addObjectToSelection(object, object == new_objects.back());
	}
	
	map->setObjectsDirty();
	map->push(undo_step);
	setEditTool();
	window->showStatusBarMessage(tr("Duplicated %n object(s)", nullptr, int(new_objects.size())), 2000);
}

void MapEditorController::switchSymbolClicked()
{
	SwitchSymbolUndoStep* switch_step = nullptr;
	ReplaceObjectsUndoStep* replace_step = nullptr;
	AddObjectsUndoStep* add_step = nullptr;
	DeleteObjectsUndoStep* delete_step = nullptr;
	std::vector<Object*> old_objects;
	std::vector<Object*> new_objects;
	MapPart* part = map->getCurrentPart();
	Symbol* symbol = activeSymbol();
	
	bool close_paths = false, split_up = false;
	auto contained_types = symbol->getContainedTypes();
	if (contained_types & Symbol::Area && !(contained_types & Symbol::Line))
		close_paths = true;
	else if (contained_types & Symbol::Line && !(contained_types & Symbol::Area))
		split_up = true;
	
	if (close_paths)
	{
		replace_step = new ReplaceObjectsUndoStep(map);
	}
	else if (split_up)
	{
		add_step = new AddObjectsUndoStep(map);
		delete_step = new DeleteObjectsUndoStep(map);
	}
	else
	{
		switch_step = new SwitchSymbolUndoStep(map);
	}
	
	for (auto* object : map->selectedObjects())
	{
		if (close_paths)
		{
			replace_step->addObject(part->findObjectIndex(object), object->duplicate());
		}
		else if (split_up)
		{
			add_step->addObject(part->findObjectIndex(object), object);
			old_objects.push_back(object);
		}
		else
		{
			switch_step->addObject(part->findObjectIndex(object), object->getSymbol());
		}
		
		if (!split_up)
		{
			object->setSymbol(symbol, true);
		}
		
		if (object->getType() == Object::Path)
		{
			PathObject* path_object = object->asPath();
			if (close_paths)
			{
				path_object->closeAllParts();
			}
			else if (split_up)
			{
				for (const auto& part : path_object->parts())
				{
					auto* new_object = new PathObject { part };
					new_object->setSymbol(symbol, true);
					new_objects.push_back(new_object);
				}
				
				Q_ASSERT(!new_objects.empty());
				if (!new_objects.empty())
				{
					new_objects.front()->setTags(path_object->tags());
				}
			}
		}
		
		object->update();
	}
	
	if (split_up)	
	{
		map->clearObjectSelection(false);
		for (auto* object : old_objects)
		{
			map->releaseObject(object);
		}
		for (auto* object : new_objects)
		{
			map->addObject(object);
			map->addObjectToSelection(object, false);
		}
		map->emitSelectionChanged();
		// Do not merge this loop into the upper one;
		// theoretically undo step indices could be wrong this way.
		for (auto* object : new_objects)
		{
			delete_step->addObject(part->findObjectIndex(object));
		}
	}
	
	map->setObjectsDirty();
	if (close_paths)
	{
		map->push(replace_step);
	}
	else if (split_up)
	{
		auto* combined_step = new CombinedUndoStep(map);
		combined_step->push(add_step);
		combined_step->push(delete_step);
		map->push(combined_step);
	}
	else
	{
		map->push(switch_step);
	}
	map->emitSelectionEdited();
	// Also emit selectionChanged, as symbols of selected objects changed
	map->emitSelectionChanged();
}

void MapEditorController::fillBorderClicked()
{
	Symbol* symbol = activeSymbol();
	std::vector<Object*> new_objects;
	new_objects.reserve(map->getNumSelectedObjects());
	
	bool close_paths = false, split_up = false;
	auto contained_types = symbol->getContainedTypes();
	if (contained_types & Symbol::Area && !(contained_types & Symbol::Line))
		close_paths = true;
	else if (contained_types & Symbol::Line && !(contained_types & Symbol::Area))
		split_up = true;
	
	auto* undo_step = new DeleteObjectsUndoStep(map);
	MapPart* part = map->getCurrentPart();
	
	for (const auto* object : map->selectedObjects())
	{
		if (split_up && object->getType() == Object::Path)
		{
			const auto* path_object = object->asPath();
			for (const auto& part : path_object->parts())
			{
				auto* new_object = new PathObject { part };
				new_object->setSymbol(symbol, true);
				map->addObject(new_object);
				new_objects.push_back(new_object);
			}
		}
		else
		{
			Object* duplicate = object->duplicate();
			duplicate->setSymbol(symbol, true);
			if (close_paths && duplicate->getType() == Object::Path)
			{
				PathObject* path_object = duplicate->asPath();
				path_object->closeAllParts();
			}
			map->addObject(duplicate);
			new_objects.push_back(duplicate);
		}
	}
	
	map->setObjectsDirty();
	map->clearObjectSelection(false);
	for (int i = 0; i < (int)new_objects.size(); ++i)
	{
		map->addObjectToSelection(new_objects[i], i == (int)new_objects.size() - 1);
		undo_step->addObject(part->findObjectIndex(new_objects[i]));
	}
	map->push(undo_step);
}
void MapEditorController::selectObjectsClicked(bool select_exclusively)
{
	bool selection_changed = false;
	if (select_exclusively)
	{
		if (map->getNumSelectedObjects() > 0)
			selection_changed = true;
		map->clearObjectSelection(false);
	}

	bool object_selected = false;	
	MapPart* part = map->getCurrentPart();
	for (int i = 0, size = part->getNumObjects(); i < size; ++i)
	{
		Object* object = part->getObject(i);
		if (symbol_widget->isSymbolSelected(object->getSymbol()) && !(!select_exclusively && map->isObjectSelected(object)))
		{
			map->addObjectToSelection(object, false);
			object_selected = true;
		}
	}
	
	selection_changed |= object_selected;
	if (selection_changed)
		map->emitSelectionChanged();
	
	if (object_selected)
	{
		if (current_tool && current_tool->isDrawTool())
			setEditTool();
	}
	else
	{
		QMessageBox::warning(window, tr("Object selection"), tr("No objects were selected because there are no objects with the selected symbols."));
	}
}

void MapEditorController::deselectObjectsClicked()
{
	bool selection_changed = false;
	
	MapPart* part = map->getCurrentPart();
	for (int i = 0, size = part->getNumObjects(); i < size; ++i)
	{
		Object* object = part->getObject(i);
		if (symbol_widget->isSymbolSelected(object->getSymbol()) && map->isObjectSelected(object))
		{
			map->removeObjectFromSelection(object, false);
			selection_changed = true;
		}
	}
	
	if (selection_changed)
	{
		map->emitSelectionChanged();
		
		if (current_tool && current_tool->isDrawTool())
			setEditTool();
	}
}

void MapEditorController::selectAll()
{
	auto num_selected_objects = map->getNumSelectedObjects();
	map->clearObjectSelection(false);
	map->getCurrentPart()->applyOnAllObjects([this](Object* object) {
		map->addObjectToSelection(object, false);
	});
	
	if (map->getNumSelectedObjects() != num_selected_objects)
	{
		map->emitSelectionChanged();
		if (current_tool && current_tool->isDrawTool())
			setEditTool();
	}
}

void MapEditorController::selectNothing()
{
	if (map->getNumSelectedObjects() > 0)
		map->clearObjectSelection(true);
}

void MapEditorController::invertSelection()
{
	auto selection = Map::ObjectSelection{ map->selectedObjects() };
	map->clearObjectSelection(false);
	map->getCurrentPart()->applyOnAllObjects([this, &selection](Object* object) {
		if (selection.find(object) == end(selection))
			map->addObjectToSelection(object, false);
	});
	
	if (map->getCurrentPart()->getNumObjects() > 0)
	{
		map->emitSelectionChanged();
		if (current_tool && current_tool->isDrawTool())
			setEditTool();
	}
}

void MapEditorController::selectByCurrentSymbols()
{
	selectObjectsClicked(true);
}

void MapEditorController::switchDashesClicked()
{
	auto* undo_step = new SwitchDashesUndoStep(map);
	MapPart* part = map->getCurrentPart();
	
	for (auto* object : map->selectedObjects())
	{
		if (object->getSymbol()->getContainedTypes() & Symbol::Line)
		{
			PathObject* path = reinterpret_cast<PathObject*>(object);
			path->reverse();
			object->update();
			
			undo_step->addObject(part->findObjectIndex(object));
		}
	}
	
	map->setObjectsDirty();
	map->push(undo_step);
	map->emitSelectionEdited();
}

/// \todo Review use of container API
float connectPaths_FindClosestEnd(const std::vector<Object*>& objects, const PathObject* a, int a_index, PathPartVector::size_type path_part_a, bool path_part_a_begin, PathObject** out_b, int* out_b_index, int* out_path_part_b, bool* out_path_part_b_begin)
{
	float best_dist_sq = std::numeric_limits<float>::max();
	for (int i = a_index; i < (int)objects.size(); ++i)
	{
		const auto* b = static_cast<const PathObject*>(objects[i]);
		if (b->getSymbol() != a->getSymbol())
			continue;
		
		auto num_parts = b->parts().size();
		for (PathPartVector::size_type path_part_b = (a == b) ? path_part_a : 0; path_part_b < num_parts; ++path_part_b)
		{
			const PathPart& part = b->parts()[path_part_b];
			if (!part.isClosed())
			{
				for (int begin = 0; begin < 2; ++begin)
				{
					bool path_part_b_begin = (begin == 0);
					if (a == b && path_part_a == path_part_b && path_part_a_begin == path_part_b_begin)
						continue;
					
					const MapCoord coord_a = a->getCoordinate(path_part_a_begin ? a->parts()[path_part_a].first_index : (a->parts()[path_part_a].last_index));
					const MapCoord coord_b = b->getCoordinate(path_part_b_begin ? b->parts()[path_part_b].first_index : (b->parts()[path_part_b].last_index));
					float distance_sq = coord_a.distanceSquaredTo(coord_b);
					if (distance_sq < best_dist_sq)
					{
						best_dist_sq = distance_sq;
						*out_b = const_cast<PathObject*>(b);  // = /* non-const */ object[i]
						*out_b_index = i;
						*out_path_part_b = path_part_b;
						*out_path_part_b_begin = path_part_b_begin;
						if (distance_sq == 0)
							return 0;
					}
				}
			}
		}
	}
	return best_dist_sq;
}

void MapEditorController::connectPathsClicked()
{
	std::vector<Object*> objects;
	std::vector<Object*> undo_objects;
	std::vector<Object*> deleted_objects;
	
	// Collect all objects in question
	objects.reserve(map->getNumSelectedObjects());
	undo_objects.reserve(map->getNumSelectedObjects());
	for (auto* object : map->selectedObjects())
	{
		if (object->getSymbol()->getContainedTypes() & Symbol::Line && object->getType() == Object::Path)
		{
			object->update();
			objects.push_back(object);
			undo_objects.push_back(nullptr);
		}
	}
	
	AddObjectsUndoStep* add_step = nullptr;
	MapPart* part = map->getCurrentPart();
	
/// \todo Fix connectPathsClicked()
#ifndef __clang_analyzer__
	while (true)
	{
		// Find the closest pair of open ends of objects with the same symbol,
		// which is closer than a threshold
		PathObject* best_object_a = nullptr;
		PathObject* best_object_b = nullptr;
		int best_object_a_index = 0;
		int best_object_b_index = 0;
		auto best_part_a = PathPartVector::size_type { 0 };
		auto best_part_b = PathPartVector::size_type { 0 };
		bool best_part_a_begin = false;
		bool best_part_b_begin = false;
		float best_dist_sq = std::numeric_limits<float>::max();
		
		for (int i = 0; i < (int)objects.size(); ++i)
		{
			PathObject* a = reinterpret_cast<PathObject*>(objects[i]);
			
			// Choose connection threshold as maximum of 0.35mm, 1.5 * largest line extent, and 6 pixels
			// TODO: instead of 6 pixels, use a physical size as soon as screen dpi is in the settings
			auto close_distance_sq = qMax(0.35, 1.5 * a->getSymbol()->calculateLargestLineExtent());
			close_distance_sq = qMax(close_distance_sq, 0.001 * main_view->pixelToLength(6));
			close_distance_sq = qPow(close_distance_sq, 2);
			
			auto num_parts = a->parts().size();
			for (PathPartVector::size_type path_part_a = 0; path_part_a < num_parts; ++path_part_a)
			{
				PathPart& part = a->parts()[path_part_a];
				if (!part.isClosed())
				{
					PathObject* b;
					int b_index;
					int path_part_b;
					bool path_part_b_begin;
					float distance_sq;
					
					for (int begin = 0; begin < 2; ++begin)
					{
						bool path_part_a_begin = (begin == 0);
						distance_sq = connectPaths_FindClosestEnd(objects, a, i, path_part_a, path_part_a_begin, &b, &b_index, &path_part_b, &path_part_b_begin);
						if (distance_sq <= close_distance_sq && distance_sq < best_dist_sq)
						{
							best_dist_sq = distance_sq;
							best_object_a = a;
							best_object_b = b;
							best_object_a_index = i;
							best_object_b_index = b_index;
							best_part_a = path_part_a;
							best_part_b = path_part_b;
							best_part_a_begin = path_part_a_begin;
							best_part_b_begin = path_part_b_begin;
						}
					}
				}
			}
		}
		
		// Abort if no possible connections found
		if (best_dist_sq == std::numeric_limits<float>::max())
			break;
		
		// Create undo objects for a and b
		if (!undo_objects[best_object_a_index])
			undo_objects[best_object_a_index] = best_object_a->duplicate();
		if (!undo_objects[best_object_b_index])
			undo_objects[best_object_b_index] = best_object_b->duplicate();
		
		// Connect the best parts
		if (best_part_a_begin && best_part_b_begin)
		{
			best_object_b->parts()[best_part_b].reverse();
			best_object_a->connectPathParts(best_part_a, best_object_b, best_part_b, true);
		}
		else if (best_part_a_begin && !best_part_b_begin)
		{
			if (best_object_a == best_object_b && best_part_a == best_part_b)
				best_object_a->parts()[best_part_a].connectEnds();
			else
				best_object_a->connectPathParts(best_part_a, best_object_b, best_part_b, true);
		}
		else if (!best_part_a_begin && best_part_b_begin)
		{
			if (best_object_a == best_object_b && best_part_a == best_part_b)
				best_object_a->parts()[best_part_a].connectEnds();
			else
				best_object_a->connectPathParts(best_part_a, best_object_b, best_part_b, false);
		}
		else //if (!best_part_a_begin && !best_part_b_begin)
		{
			best_object_b->parts()[best_part_b].reverse();
			best_object_a->connectPathParts(best_part_a, best_object_b, best_part_b, false);
		}
		
		if (best_object_a != best_object_b)
		{
			// Copy all remaining parts of object b over to a
			best_object_a->getCoordinateRef(best_object_a->getCoordinateCount() - 1).setHolePoint(true);
			for (auto i = PathPartVector::size_type { 0 }; i < best_object_b->parts().size(); ++i)
			{
				if (i != best_part_b)
					best_object_a->appendPathPart(best_object_b->parts()[i]);
			}
			
			// Create an add step for b
			int b_index_in_part = part->findObjectIndex(best_object_b);
			deleted_objects.push_back(best_object_b);
			if (!add_step)
				add_step = new AddObjectsUndoStep(map);
			add_step->addObject(b_index_in_part, undo_objects[best_object_b_index]);
			undo_objects[best_object_b_index] = nullptr;
			
			// Delete b from the active list
			objects.erase(objects.begin() + best_object_b_index);
			undo_objects.erase(undo_objects.begin() + best_object_b_index);
		}
		else
		{
			// Delete the part which has been merged with another part
			if (best_part_a != best_part_b)
				best_object_a->deletePart(best_part_b);
		}
	}
#endif
	
	// Create undo step?
	ReplaceObjectsUndoStep* replace_step = nullptr;
	for (int i = 0; i < (int)undo_objects.size(); ++i)
	{
		if (undo_objects[i])
		{
			// The object was changed, update the new version
			objects[i]->forceUpdate(); /// @todo get rid of force if possible
			
			// Add the old version to the undo step
			if (!replace_step)
				replace_step = new ReplaceObjectsUndoStep(map);
			replace_step->addObject(part->findObjectIndex(objects[i]), undo_objects[i]);
		}
	}
	
	if (add_step)
	{
		for (auto* object : deleted_objects)
		{
			map->removeObjectFromSelection(object, false);
			map->getCurrentPart()->deleteObject(object);
		}
	}
	
	if (add_step || replace_step)
	{
		auto* undo_step = new CombinedUndoStep(map);
		if (replace_step)
			undo_step->push(replace_step);
		if (add_step)
			undo_step->push(add_step);
		map->push(undo_step);
		map->setObjectsDirty();
	}
	
	map->emitSelectionChanged();
	map->emitSelectionEdited();
}
void MapEditorController::cutClicked()
{
	setTool(new CutTool(this, cut_tool_act));
}
// foom
void MapEditorController::reshapeClicked()
{
	setTool(new ReshapeTool(this, reshape_act));
}

// foom
void MapEditorController::moveParallelClicked()
{
	setTool(new MoveParallelTool(this, move_parallel_act));
}

// foom
void MapEditorController::moveParallelByClicked()
{
	if (!move_parallel_dialog)
		move_parallel_dialog = new MoveParallelDialog(this, window);
	move_parallel_dialog->show();
	move_parallel_dialog->raise();
	move_parallel_dialog->activateWindow();
}

// foom
void MapEditorController::moveParallelBy(double distance, bool duplicate)
{
	// the design notes, "Parallel offset", editor paragraph.
	auto const& selection = map->selectedObjects();
	std::vector<const Object*> in(begin(selection), end(selection));
	ops::OffsetParams params;
	params.distance = distance;
	params.duplicate = duplicate;
	applyOpResult(ops::offset(*map, in, params));
}

// foom
void MapEditorController::setVertexTypeClicked()
{
	if (auto* action = qobject_cast<QAction*>(sender()))
		setVertexType(action->data().toBool(), vertex_type_keep_ends_act->isChecked());
}

// foom
void MapEditorController::setVertexType(bool dash, bool keep_ends)
{
	// the design notes, "Vertex types".
	auto const& selection = map->selectedObjects();
	std::vector<const Object*> in(begin(selection), end(selection));
	ops::VertexTypeParams params;
	params.dash = dash;
	params.keep_ends = keep_ends;
	applyOpResult(ops::setVertexType(*map, in, params));
}

// foom: the design notes, "Sketch layer"
void MapEditorController::sketchPenClicked()
{
	setTool(new SketchPenTool(this, sketch_pen_act));
}

void MapEditorController::sketchEraserClicked()
{
	setTool(new SketchEraserTool(this, sketch_eraser_act));
}

void MapEditorController::sketchNoteClicked()
{
	setTool(new SketchNoteTool(this, sketch_note_act));
}

void MapEditorController::sketchManageClicked()
{
	SketchManageDialog dialog(*map, window);
	connect(&dialog, &SketchManageDialog::showExtent, this, [this](const QRectF& extent) {
		map_widget->ensureVisibilityOfRect(extent, MapWidget::ContinuousZoom);
	});
	dialog.exec();
}

void MapEditorController::sketchFlattenClicked()
{
	if (sketchPartIndex(*map) < 0)
	{
		QMessageBox::information(window, tr("Flatten sketch"), tr("The map has no sketch."));
		return;
	}
	QString filter = tr("PNG");
	filter += QLatin1String(" (*.png)");
	auto path = QFileDialog::getSaveFileName(window, tr("Flatten sketch"), QFileInfo(window->currentPath()).path(), filter);
	if (path.isEmpty())
		return;
	if (!path.endsWith(QLatin1String(".png"), Qt::CaseInsensitive))
		path += QLatin1String(".png");
	auto ok = false;
	auto const dpi = QInputDialog::getInt(window, tr("Flatten sketch"), tr("Resolution (dpi):"), 300, 50, 1200, 50, &ok);
	if (!ok)
		return;
	
	QString error;
	if (!flattenSketch(*map, path, dpi, &error))
	{
		QMessageBox::warning(window, tr("Flatten sketch"), error);
		return;
	}
	auto new_template = Template::templateForPath(path, map);
	if (!new_template || !new_template->setupAndLoad(window, main_view))
	{
		QMessageBox::warning(window, tr("Flatten sketch"), tr("The image is written to %1, but it could not be opened as a template.").arg(path));
		return;
	}
	// On top of the map, and seen: a position below zero would mean behind the map.
	map->addTemplate(map->getNumTemplates(), std::move(new_template));
	hideAllTemplates(false);
	showTemplateWindow(true);
	window->showStatusBarMessage(tr("The sketch is flattened to %1 and opened as a template.").arg(path), 4000);
}

// foom
void MapEditorController::lassoClicked()
{
	setTool(new LassoTool(this, lasso_act));
}

// foom
void MapEditorController::joinLinesClicked()
{
	bool ok = false;
	auto const tolerance = QInputDialog::getDouble(window, tr("Join lines"), tr("Join ends closer than:"), ops::JoinParams().tolerance, 0.0, 100.0, 2, &ok);
	if (ok)
		joinLines(tolerance);
}

// foom
void MapEditorController::joinLines(double tolerance)
{
	// the design notes, "Topology commands".
	auto const& selection = map->selectedObjects();
	std::vector<const Object*> in(begin(selection), end(selection));
	ops::JoinParams params;
	params.tolerance = tolerance;
	applyOpResult(ops::joinLines(*map, in, params));
}

// foom
void MapEditorController::closeAreasClicked()
{
	TopologyScopeDialog dialog(tr("Close areas"), this, window);
	if (dialog.exec() == QDialog::Accepted)
		closeAreas(int(dialog.scope()));
}

// foom
void MapEditorController::closeAreas(int scope)
{
	applyOpResult(ops::closeAreas(*map, topologyScopeObjects(this, TopologyScope(scope)), ops::OpParams()));
}

// foom
void MapEditorController::areasAnticlockwiseClicked()
{
	TopologyScopeDialog dialog(tr("Areas anticlockwise"), this, window);
	if (dialog.exec() == QDialog::Accepted)
		areasAnticlockwise(int(dialog.scope()));
}

// foom
void MapEditorController::areasAnticlockwise(int scope)
{
	applyOpResult(ops::areasAnticlockwise(*map, topologyScopeObjects(this, TopologyScope(scope)), ops::OpParams()));
}

// foom
void MapEditorController::fixOvershootsClicked()
{
	OvershootDialog dialog(window);
	if (dialog.exec() == QDialog::Accepted)
		fixOvershoots(dialog.params());
}

// foom
void MapEditorController::fixOvershoots(const ops::OvershootParams& params)
{
	auto const& selection = map->selectedObjects();
	std::vector<const Object*> in(begin(selection), end(selection));
	applyOpResult(ops::fixOvershoots(*map, in, params));
}

// foom: the design notes, "Lengthen line text"
void MapEditorController::lengthenLineTextClicked()
{
	LengthenLineTextDialog dialog(lengthen_line_text_params, window);
	if (dialog.exec() == QDialog::Accepted)
	{
		lengthen_line_text_params = dialog.params();
		lengthenLineText(lengthen_line_text_params);
	}
}

// foom
void MapEditorController::lengthenLineText(const ops::LengthenLineTextParams& params)
{
	auto const& selection = map->selectedObjects();
	std::vector<const Object*> in(begin(selection), end(selection));
	applyOpResult(ops::lengthenLineText(*map, in, params));
}

// foom
void MapEditorController::intersectionsClicked()
{
	IntersectionDialog dialog(this, window);
	if (dialog.exec() == QDialog::Accepted)
		intersections(dialog.params());
}

// foom
void MapEditorController::intersections(const ops::IntersectionParams& params)
{
	auto const& selection = map->selectedObjects();
	std::vector<const Object*> in(begin(selection), end(selection));
	applyOpResult(ops::intersections(*map, in, params));
}

// foom
void MapEditorController::removeDuplicateVertices()
{
	auto const& selection = map->selectedObjects();
	std::vector<const Object*> in(begin(selection), end(selection));
	applyOpResult(ops::removeDuplicateVertices(*map, in, ops::OpParams()));
}

// foom
void MapEditorController::smoothClicked()
{
	SmoothDialog dialog(window);
	if (dialog.exec() == QDialog::Accepted)
		smoothPaths(dialog.params());
}

// foom
void MapEditorController::smoothPaths(const ops::SmoothParams& params)
{
	// the design notes, "Smooth".
	auto const& selection = map->selectedObjects();
	std::vector<const Object*> in(begin(selection), end(selection));
	applyOpResult(ops::smoothPaths(*map, in, params));
}

// foom
void MapEditorController::makeRectangularClicked()
{
	MakeRectangularDialog dialog(this, window);
	if (dialog.exec() == QDialog::Accepted)
		makeRectangular(dialog.params());
}

// foom
void MapEditorController::makeRectangular(const ops::RectangularParams& params)
{
	// the design notes, "Make rectangular".
	auto const& selection = map->selectedObjects();
	std::vector<const Object*> in(begin(selection), end(selection));
	applyOpResult(ops::makeRectangular(*map, in, params));
}

// foom
void MapEditorController::generalizeBuildingsClicked()
{
	GeneralizeBuildingsDialog dialog(window);
	if (dialog.exec() != QDialog::Accepted)
		return;
	if (dialog.simplify())
		simplifyBuildings(dialog.simplifyParams());
	else
		buildingRectangles(dialog.rectangleParams());
}

// foom
void MapEditorController::simplifyBuildings(const ops::SimplifyBuildingParams& params)
{
	auto const& selection = map->selectedObjects();
	std::vector<const Object*> in(begin(selection), end(selection));
	applyOpResult(ops::simplifyBuildings(*map, in, params));
}

// foom
void MapEditorController::buildingRectangles(const ops::RectangleParams& params)
{
	auto const& selection = map->selectedObjects();
	std::vector<const Object*> in(begin(selection), end(selection));
	applyOpResult(ops::buildingRectangles(*map, in, params));
}

// foom
void MapEditorController::groupClicked()
{
	bool ok = false;
	auto const name = QInputDialog::getText(window, tr("Group"), tr("Name of the group (may stay empty):"), QLineEdit::Normal, QString{}, &ok);
	if (ok)
		groupSelection(name);
}

// foom
void MapEditorController::groupSelection(const QString& name)
{
	// the design notes, "Groups".
	auto const& selection = map->selectedObjects();
	std::vector<const Object*> in(begin(selection), end(selection));
	ops::GroupParams params;
	params.group = nextObjectGroupId(*map);
	auto result = ops::setGroup(*map, in, params);
	if (result.error.isEmpty() && !result.out.empty())
		map->setObjectGroupName(params.group, name);
	applyOpResult(std::move(result));
}

// foom
void MapEditorController::ungroupSelection()
{
	auto const& selection = map->selectedObjects();
	std::vector<const Object*> in(begin(selection), end(selection));
	ops::GroupParams params;
	params.group = 0;
	applyOpResult(ops::setGroup(*map, in, params));
}

// foom
void MapEditorController::selectGroup()
{
	expandSelectionToGroups(*map);
}

// foom: the design notes, "Selection queries".
int MapEditorController::selectFound(const std::vector<Object*>& found)
{
	map->clearObjectSelection(false);
	for (auto* object : found)
		map->addObjectToSelection(object, false);
	map->emitSelectionChanged();
	if (window)
		window->showStatusBarMessage(tr("%1 object(s) selected").arg(int(found.size())), 5000);
	return int(found.size());
}

int MapEditorController::selectDuplicates()
{
	return selectFound(findDuplicates(*map->getCurrentPart()));
}

int MapEditorController::selectSelfIntersecting()
{
	return selectFound(findSelfIntersecting(*map->getCurrentPart()));
}

int MapEditorController::selectInvalid()
{
	return selectFound(findInvalid(*map->getCurrentPart()));
}

int MapEditorController::selectByDate(int kind, qint64 from, qint64 to)
{
	return selectFound(findByDate(*map->getCurrentPart(), DateKind(kind), from, to));
}

int MapEditorController::selectByProperty(const PropertyQuery& query)
{
	return selectFound(findByProperty(*map->getCurrentPart(), query));
}

int MapEditorController::selectByIndex(int number)
{
	auto* part = map->getCurrentPart();
	if (number < 1 || number > part->getNumObjects())
		return selectFound({});
	auto const count = selectFound({ part->getObject(number - 1) });
	showSelectedObject();
	return count;
}

void MapEditorController::selectNext()
{
	auto* part = map->getCurrentPart();
	auto const count = part->getNumObjects();
	if (count == 0)
		return;
	int last = -1;
	for (int i = 0; i < count; ++i)
	{
		if (map->isObjectSelected(part->getObject(i)))
			last = i;
	}
	selectFound({ part->getObject((last + 1) % count) });
	showSelectedObject();
}

// foom: the one object found by index or as the next lies in the view.
void MapEditorController::showSelectedObject()
{
	auto* object = map->getFirstSelectedObject();
	if (!object || !map_widget)
		return;
	object->update();
	map_widget->ensureVisibilityOfRect(object->getExtent(), MapWidget::ContinuousZoom);
}

void MapEditorController::selectByDateClicked()
{
	SelectByDateDialog dialog(window);
	if (dialog.exec() == QDialog::Accepted)
		selectByDate(int(dialog.kind()), dialog.from(), dialog.to());
}

void MapEditorController::selectByPropertyClicked()
{
	SelectByPropertyDialog dialog(window);
	if (dialog.exec() != QDialog::Accepted)
		return;
	auto query = dialog.query();
	if (dialog.onlySelectedSymbols() && symbol_widget)
	{
		for (int i = 0; i < map->getNumSymbols(); ++i)
		{
			if (symbol_widget->isSymbolSelected(map->getSymbol(i)))
				query.symbols.push_back(map->getSymbol(i));
		}
		if (query.symbols.empty())
		{
			selectFound({});
			return;
		}
	}
	selectByProperty(query);
}

void MapEditorController::selectByIndexClicked()
{
	bool ok = false;
	auto const count = map->getCurrentPart()->getNumObjects();
	if (count == 0)
		return;
	auto const number = QInputDialog::getInt(window, tr("Select object by index"), tr("Object number:"), 1, 1, count, 1, &ok);
	if (ok)
		selectByIndex(number);
}

// foom: the design notes, "Saved selections".
void MapEditorController::saveSelection(const QString& name)
{
	SavedSelection selection;
	selection.name = name;
	for (auto const* object : map->selectedObjects())
		selection.uuids.push_back(object->uuid());
	auto selections = map->savedSelections();
	auto existing = std::find_if(selections.begin(), selections.end(), [&name](const SavedSelection& s) { return s.name == name; });
	if (existing != selections.end())
		*existing = std::move(selection);
	else
		selections.push_back(std::move(selection));
	map->setSavedSelections(std::move(selections));
	map->setHasUnsavedChanges(true);
}

int MapEditorController::loadSavedSelection(const QString& name)
{
	auto const& selections = map->savedSelections();
	auto const found = std::find_if(selections.begin(), selections.end(), [&name](const SavedSelection& s) { return s.name == name; });
	if (found == selections.end())
		return selectFound({});
	std::vector<Object*> objects;
	int elsewhere = 0;
	auto const* part = map->getCurrentPart();
	for (auto const& uuid : found->uuids)
	{
		auto* object = map->findObject(uuid);
		if (!object)
			continue;
		if (part->contains(object))
			objects.push_back(object);
		else
			++elsewhere;
	}
	auto const count = selectFound(objects);
	if (elsewhere > 0 && window)
		window->showStatusBarMessage(tr("%1 object(s) selected; %2 of the saved selection lie in other parts").arg(count).arg(elsewhere), 5000);
	return count;
}

void MapEditorController::deleteSavedSelection(const QString& name)
{
	auto selections = map->savedSelections();
	auto const before = selections.size();
	selections.erase(std::remove_if(selections.begin(), selections.end(), [&name](const SavedSelection& s) { return s.name == name; }), selections.end());
	if (selections.size() == before)
		return;
	map->setSavedSelections(std::move(selections));
	map->setHasUnsavedChanges(true);
}

namespace {

/// Asks for one of the saved selections' names; empty when cancelled or there is none.
QString chooseSavedSelection(QWidget* parent, const Map& map, const QString& title)
{
	QStringList names;
	for (auto const& selection : map.savedSelections())
		names.push_back(selection.name);
	if (names.isEmpty())
	{
		QMessageBox::information(parent, title, MapEditorController::tr("The map has no saved selection."));
		return {};
	}
	bool ok = false;
	auto const name = QInputDialog::getItem(parent, title, MapEditorController::tr("Selection:"), names, 0, false, &ok);
	return ok ? name : QString{};
}

}  // namespace

void MapEditorController::saveSelectionClicked()
{
	if (map->getNumSelectedObjects() == 0)
	{
		QMessageBox::information(window, tr("Save selection"), tr("Select the objects first."));
		return;
	}
	bool ok = false;
	auto const name = QInputDialog::getText(window, tr("Save selection"), tr("Name:"), QLineEdit::Normal,
	                                        tr("Selection %1").arg(map->savedSelections().size() + 1), &ok);
	if (ok && !name.trimmed().isEmpty())
		saveSelection(name.trimmed());
}

void MapEditorController::loadSavedSelectionClicked()
{
	auto const name = chooseSavedSelection(window, *map, tr("Reload selection"));
	if (!name.isEmpty())
		loadSavedSelection(name);
}

void MapEditorController::deleteSavedSelectionClicked()
{
	auto const name = chooseSavedSelection(window, *map, tr("Delete saved selection"));
	if (!name.isEmpty())
		deleteSavedSelection(name);
}

// foom
void MapEditorController::bookmarkAddClicked()
{
	bool ok = false;
	auto const name = QInputDialog::getText(window, tr("Add bookmark"), tr("Name:"), QLineEdit::Normal, tr("Bookmark %1").arg(map->bookmarks().size() + 1), &ok);
	if (ok && !name.isEmpty())
		addBookmark(name);
}

// foom
void MapEditorController::addBookmark(const QString& name)
{
	// the design notes, "Bookmarks and view history".
	Bookmark bookmark;
	bookmark.name = name;
	bookmark.center = main_view->center();
	bookmark.zoom = main_view->getZoom();
	map->addBookmark(bookmark);
	map->setHasUnsavedChanges(true);
}

// foom
void MapEditorController::bookmarkManageClicked()
{
	BookmarkDialog dialog(this, window);
	dialog.exec();
}

// foom
void MapEditorController::jumpToBookmark(int index)
{
	auto const& bookmarks = map->bookmarks();
	if (index < 0 || std::size_t(index) >= bookmarks.size())
		return;
	main_view->setCenter(bookmarks[index].center);
	main_view->setZoom(bookmarks[index].zoom);
}

// foom
void MapEditorController::bookmarkJumpClicked()
{
	if (auto* action = qobject_cast<QAction*>(sender()))
		jumpToBookmark(action->data().toInt());
}

// foom
void MapEditorController::updateBookmarksMenu()
{
	if (!bookmarks_menu)
		return;
	bookmarks_menu->clear();
	bookmarks_menu->addAction(bookmark_add_act);
	bookmarks_menu->addAction(bookmark_manage_act);
	auto const& bookmarks = map->bookmarks();
	if (!bookmarks.empty())
		bookmarks_menu->addSeparator();
	for (std::size_t i = 0; i < bookmarks.size(); ++i)
	{
		auto* action = bookmarks_menu->addAction(bookmarks[i].name, this, SLOT(bookmarkJumpClicked()));
		action->setData(int(i));
	}
}

// foom
void MapEditorController::recordView()
{
	if (!main_view)
		return;
	view_history.record({ main_view->center(), main_view->getZoom() });
	if (view_back_act && view_forward_act)  // not yet created while attaching, absent in the symbol editor
	{
		view_back_act->setEnabled(view_history.canGoBack());
		view_forward_act->setEnabled(view_history.canGoForward());
	}
}

// foom
void MapEditorController::viewChangedForHistory(MapView::ChangeFlags change)
{
	if (!applying_view_history && (change & (MapView::CenterChange | MapView::ZoomChange)))
		view_history_timer.start();
}

// foom
void MapEditorController::viewBack()
{
	if (auto const* entry = view_history.back())
	{
		applying_view_history = true;
		main_view->setCenter(entry->center);
		main_view->setZoom(entry->zoom);
		applying_view_history = false;
		view_history_timer.stop();
	}
	view_back_act->setEnabled(view_history.canGoBack());
	view_forward_act->setEnabled(view_history.canGoForward());
}

// foom
void MapEditorController::viewForward()
{
	if (auto const* entry = view_history.forward())
	{
		applying_view_history = true;
		main_view->setCenter(entry->center);
		main_view->setZoom(entry->zoom);
		applying_view_history = false;
		view_history_timer.stop();
	}
	view_back_act->setEnabled(view_history.canGoBack());
	view_forward_act->setEnabled(view_history.canGoForward());
}

// foom
void MapEditorController::findReplaceTextClicked()
{
	if (!find_replace_text_dialog)
		find_replace_text_dialog = new FindReplaceTextDialog(this, window);
	find_replace_text_dialog->show();
	find_replace_text_dialog->raise();
	find_replace_text_dialog->activateWindow();
}

namespace {
	
// foom: the text objects of the part, all or those of the symbols selected in the pane.
std::vector<const Object*> textObjectsInScope(const MapPart& part, const SymbolWidget* symbol_widget, bool selected_symbols_only, const Map::ObjectSelection* selection)
{
	std::vector<const Object*> objects;
	for (int i = 0; i < part.getNumObjects(); ++i)
	{
		auto const* object = part.getObject(i);
		if (object->getType() != Object::Text)
			continue;
		if (selected_symbols_only && !(symbol_widget && symbol_widget->isSymbolSelected(object->getSymbol())))
			continue;
		if (selection && !selection->count(const_cast<Object*>(object)))
			continue;
		objects.push_back(object);
	}
	return objects;
}

}  // namespace

// foom
bool MapEditorController::findNextText(const ops::ReplaceTextParams& params, bool selected_symbols_only)
{
	// the design notes, "Find and replace text".
	auto const candidates = textObjectsInScope(*map->getCurrentPart(), symbol_widget, selected_symbols_only, nullptr);
	auto const* pivot = map->getFirstSelectedObject();
	const Object* first_match = nullptr;
	const Object* next_match = nullptr;
	auto after_pivot = pivot == nullptr;
	for (auto const* object : candidates)
	{
		if (object == pivot)
		{
			after_pivot = true;
			continue;
		}
		if (!ops::textMatches(object->asText()->getText(), params))
			continue;
		if (after_pivot)
		{
			next_match = object;
			break;
		}
		if (!first_match)
			first_match = object;
	}
	if (!next_match)
		next_match = first_match;
	if (!next_match)
		return false;
	map->clearObjectSelection(false);
	map->addObjectToSelection(const_cast<Object*>(next_match), true);
	map->ensureVisibilityOfSelectedObjects(Map::FullVisibility);
	return true;
}

// foom
int MapEditorController::replaceText(const ops::ReplaceTextParams& params, bool selected_symbols_only, bool selected_objects_only)
{
	auto const in = textObjectsInScope(*map->getCurrentPart(), symbol_widget, selected_symbols_only, selected_objects_only ? &map->selectedObjects() : nullptr);
	auto result = ops::replaceText(*map, in, params);
	auto const count = int(result.out.size());
	applyOpResult(std::move(result));
	return count;
}

// foom
int MapEditorController::deleteMatchingText(const ops::ReplaceTextParams& params, bool selected_symbols_only, bool selected_objects_only)
{
	auto const in = textObjectsInScope(*map->getCurrentPart(), symbol_widget, selected_symbols_only, selected_objects_only ? &map->selectedObjects() : nullptr);
	auto result = ops::deleteMatchingText(*map, in, params);
	auto const count = int(result.consumed.size());
	applyOpResult(std::move(result));
	return count;
}

// foom
void MapEditorController::extentDefineClicked()
{
	ExtentDialog dialog(this, newExtent(), window);
	if (dialog.exec() == QDialog::Accepted && !dialog.extent().name.isEmpty())
		defineExtent(dialog.extent());
}

// foom
Extent MapEditorController::newExtent() const
{
	Extent extent;
	extent.name = tr("Extent %1").arg(map->extents().size() + 1);
	// The const overload, which does not make a print area for the map.
	extent.rect_mm = static_cast<const Map&>(*map).printerConfig().print_area;
	return extent;
}

// foom
void MapEditorController::defineExtent(const Extent& extent)
{
	// the design notes, "Named extents and the paper frame".
	auto* step = new MapDataUndoStep(map, MapDataUndoStep::Extents, MapDataUndoStep::currentXml(*map, MapDataUndoStep::Extents));
	auto extents = map->extents();
	auto const existing = std::find_if(begin(extents), end(extents), [&extent](const Extent& other) { return other.name == extent.name; });
	if (existing != end(extents))
		*existing = extent;
	else
		extents.push_back(extent);
	map->setExtents(std::move(extents));
	map->push(step);
	updateExtentMenus();
	map_widget->update();
}

// foom
void MapEditorController::extentImportClicked()
{
	QSettings settings;
	auto const directory = settings.value(QString::fromLatin1("importFileDirectory"), QDir::homePath()).toString();
	QString const filter = tr("Map files") + QLatin1String(" (*.foom *.omap *.xmap *.ocd);;") + tr("All files") + QLatin1String(" (*.*)");
	auto const filename = QFileDialog::getOpenFileName(window, tr("Import extents"), directory, filter);
	if (filename.isEmpty())
		return;
	settings.setValue(QString::fromLatin1("importFileDirectory"), QFileInfo(filename).canonicalPath());
	QString error;
	auto const count = importExtentsFromFile(*map, filename, &error);
	if (count < 0)
	{
		QMessageBox::warning(window, tr("Import extents"), error);
		return;
	}
	updateExtentMenus();
	map_widget->update();
	window->showStatusBarMessage(tr("Imported %n extent(s).", nullptr, count), 2000);
}

// foom
void MapEditorController::extentDeleteClicked()
{
	QStringList names;
	for (auto const& extent : map->extents())
		names << extent.name;
	if (names.isEmpty())
		return;
	bool ok = false;
	auto const name = QInputDialog::getItem(window, tr("Delete extent"), tr("Extent:"), names, 0, false, &ok);
	if (ok)
		deleteExtent(name);
}

// foom
bool MapEditorController::deleteExtent(const QString& name)
{
	auto extents = map->extents();
	auto const existing = std::find_if(begin(extents), end(extents), [&name](const Extent& other) { return other.name == name; });
	if (existing == end(extents))
		return false;
	auto* step = new MapDataUndoStep(map, MapDataUndoStep::Extents, MapDataUndoStep::currentXml(*map, MapDataUndoStep::Extents));
	extents.erase(existing);
	map->setExtents(std::move(extents));
	map->push(step);
	if (active_extent_name == name)
		active_extent_name.clear();
	updateExtentMenus();
	map_widget->update();
	return true;
}

// foom
void MapEditorController::setActiveExtent(const QString& name)
{
	active_extent_name = name;
	updateExtentMenus();
	if (map_widget)
		map_widget->update();
}

// foom
void MapEditorController::activeExtentClicked()
{
	if (auto* action = qobject_cast<QAction*>(sender()))
		setActiveExtent(action->data().toString());
}

// foom
void MapEditorController::updateExtentMenus()
{
	if (!active_extent_menu)
		return;
	active_extent_menu->clear();
	auto* none = active_extent_menu->addAction(tr("None"), this, SLOT(activeExtentClicked()));
	none->setData(QString{});
	none->setCheckable(true);
	none->setChecked(active_extent_name.isEmpty());
	for (auto const& extent : map->extents())
	{
		auto* action = active_extent_menu->addAction(extent.name, this, SLOT(activeExtentClicked()));
		action->setData(extent.name);
		action->setCheckable(true);
		action->setChecked(extent.name == active_extent_name);
	}
	extent_delete_act->setEnabled(!map->extents().empty());
}

// foom
void MapEditorController::drawActiveExtent(QPainter* painter, MapWidget* widget) const
{
	if (active_extent_name.isEmpty())
		return;
	for (auto const& extent : map->extents())
	{
		if (extent.name != active_extent_name)
			continue;
		auto const rect = widget->mapToViewport(extent.rect_mm);
		painter->save();
		painter->setBrush(Qt::NoBrush);
		painter->setPen(QPen(QColor(80, 80, 80), 2));
		painter->drawRect(rect);
		if (extent.margin_mm > 0)
		{
			auto const m = extent.margin_mm;
			painter->setPen(QPen(QColor(80, 80, 80), 1, Qt::DashLine));
			painter->drawRect(widget->mapToViewport(extent.rect_mm.adjusted(m, m, -m, -m)));
		}
		painter->setPen(QPen(QColor(80, 80, 80)));
		painter->drawText(rect.topLeft() + QPointF(4.0, -4.0), extent.name);
		painter->restore();
		return;
	}
}

// foom
int MapEditorController::runMapCheck()
{
	// the design notes, "Map check".
	last_check = [this]() { return checkMap(*map); };
	checkAgain();
	return int(issue_model->issues().size());
}

// foom
void MapEditorController::checkLegibilityClicked()
{
	LegibilityDialog dialog(this, window);
	if (dialog.exec() == QDialog::Accepted)
		runLegibilityCheck(dialog.params());
}

// foom
int MapEditorController::runLegibilityCheck(const LegibilityParams& params)
{
	// the design notes, "Legibility".
	last_check = [this, params]() { return checkLegibility(*map, params); };
	checkAgain();
	return int(issue_model->issues().size());
}

// foom
void MapEditorController::checkAgain()
{
	if (!last_check)
		last_check = [this]() { return checkMap(*map); };
	issue_model->setIssues(last_check());
	showIssuesWindow(true);
}

// foom
void MapEditorController::showIssuesWindow(bool show)
{
	if (!issues_dock_widget)
	{
		issues_dock_widget = new EditorDockWidget(tr("Issues"), issues_window_act, this, window);
		issues_dock_widget->setWidget(new IssuesWidget(this, issue_model, issues_dock_widget));
		issues_dock_widget->setObjectName(QString::fromLatin1("Issues dock widget"));
		if (!window->restoreDockWidget(issues_dock_widget))
			window->addDockWidget(Qt::BottomDockWidgetArea, issues_dock_widget, Qt::Horizontal);
	}
	issues_window_act->setChecked(show);
	issues_dock_widget->setVisible(show);
}

// foom
bool MapEditorController::fixIssueRow(int row)
{
	if (row < 0 || row >= issue_model->rowCount())
		return false;
	auto const fixed = fixIssue(*map, issue_model->issueAt(row));
	checkAgain();
	return fixed;
}

// foom
int MapEditorController::fixShownIssues()
{
	std::vector<Issue> shown;
	for (int row = 0; row < issue_model->rowCount(); ++row)
		shown.push_back(issue_model->issueAt(row));
	auto count = 0;
	for (auto const& issue : shown)
	{
		if (fixIssue(*map, issue))
			++count;
	}
	checkAgain();
	return count;
}

// foom
void MapEditorController::issueActivated(const QVector<QUuid>& objects, const MapCoordF& where)
{
	map->clearObjectSelection(false);
	auto found = false;
	for (auto const& uuid : objects)
	{
		if (auto* object = map->findObject(uuid))
		{
			map->addObjectToSelection(object, false);
			found = true;
		}
	}
	map->emitSelectionChanged();
	if (found)
		map->ensureVisibilityOfSelectedObjects(Map::FullVisibility);
	else if (main_view && !where.isNull())
		main_view->setCenter(MapCoord(where));
}

void MapEditorController::cutHoleClicked()
{
	setTool(new CutHoleTool(this, cut_hole_act, CutHoleTool::Path));
}

void MapEditorController::cutHoleCircleClicked()
{
	setTool(new CutHoleTool(this, cut_hole_circle_act, CutHoleTool::Circle));
}

void MapEditorController::cutHoleRectangleClicked()
{
	setTool(new CutHoleTool(this, cut_hole_rectangle_act, CutHoleTool::Rect));
}

void MapEditorController::rotateClicked()
{
	setTool(new RotateTool(this, rotate_act));
}

void MapEditorController::rotatePatternClicked()
{
	setTool(new RotatePatternTool(this, rotate_pattern_act));
}

void MapEditorController::scaleClicked()
{
	setTool(new ScaleTool(this, scale_act));
}

void MapEditorController::measureClicked(bool checked)
{
	if (!measure_dock_widget)
	{
		measure_dock_widget = new EditorDockWidget(tr("Measure"), measure_act, this, window);
		measure_dock_widget->toggleViewAction()->setVisible(false);
		auto* measure_widget = new MeasureWidget(map);
		measure_dock_widget->setWidget(measure_widget);
		measure_dock_widget->setObjectName(QString::fromLatin1("Measure dock widget"));
		addFloatingDockWidget(measure_dock_widget);
	}
	
	measure_dock_widget->setVisible(checked);
}

// foom: The course dock.
// the design notes, "Course dock".

void MapEditorController::courseDockClicked(bool checked)
{
	if (mobile_mode)
		return;
	if (!course_dock_widget)
	{
		auto* dock_widget = new EditorDockWidget(tr("Course setting"), course_dock_act, this, window);
		dock_widget->toggleViewAction()->setVisible(false);
		course_widget = new CourseWidget(map);
		dock_widget->setWidget(course_widget);
		connect(course_widget, &CourseWidget::currentCourseChanged, map_widget, QOverload<>::of(&QWidget::update));
		dock_widget->setObjectName(QString::fromLatin1("Course dock widget"));
		if (!window->restoreDockWidget(dock_widget))
			window->addDockWidget(Qt::LeftDockWidgetArea, dock_widget, Qt::Vertical);
		course_dock_widget = dock_widget;
	}
	course_dock_widget->setVisible(checked);
	map_widget->update();
}

// foom: The course symbols.
// the design notes, "FOOM's course symbols".

void MapEditorController::addCourseSymbolsClicked()
{
	auto const report = addCourseSymbols(*map);
	if (!report.loaded)
	{
		QMessageBox::warning(window, tr("Add course symbols"), tr("The symbol set \"Course Design\", of which the course symbols are made, was not found among the symbol sets of the program."));
		return;
	}
	QString message = report.added > 0 ? tr("%n course symbol(s) were added.", nullptr, report.added)
	                                    : tr("The map has the course symbols already.");
	if (report.added > 0 && report.taken > 0)
		message += QLatin1Char('\n') + tr("%n number(s) were in use. The symbols which have them were left as they are.", nullptr, report.taken);
	if (report.roles_given > 0)
		message += QLatin1Char('\n') + tr("%n symbol(s) of the map (start, control, finish, marked route) were given their course role.", nullptr, report.roles_given);
	QMessageBox::information(window, tr("Add course symbols"), message);
}

// foom: Composing a course, and the commands for many courses.
// the design notes, "Composing a course and the bulk commands".

void MapEditorController::composeCourseClicked()
{
	auto const title = tr("Compose course");
	// Without the tool, the action of the tool which stays is the checked one.
	auto const give_up = [this]() {
		compose_course_act->setChecked(false);
		if (current_tool && current_tool->toolAction())
			current_tool->toolAction()->setChecked(true);
	};
	
	if (!course_dock_act->isChecked())
		course_dock_act->trigger();
	if (!course_widget)
	{
		give_up();
		return;
	}
	
	if (course_widget->currentCourse().isEmpty())
	{
		auto ok = false;
		auto const& courses = map->courseData().courses;
		if (courses.empty())
		{
			auto const name = QInputDialog::getText(window, title, tr("There is no course yet. Name of the new course:"), QLineEdit::Normal, QString{}, &ok);
			if (ok && !course_widget->addCourse(name))
			{
				QMessageBox::warning(window, title, tr("This is no name for a course."));
				ok = false;
			}
		}
		else
		{
			QStringList names;
			for (auto const& course : courses)
				names.push_back(course.name);
			auto const name = QInputDialog::getItem(window, title, tr("Course:"), names, 0, false, &ok);
			if (ok)
				course_widget->setCurrentCourse(name);
		}
		if (!ok || course_widget->currentCourse().isEmpty())
		{
			give_up();
			return;
		}
	}
	setTool(new ComposeCourseTool(this, compose_course_act, course_widget));
}

/// foom: The code of the course object which is selected in the map; empty when there is none.
static QString selectedCourseCode(const Map& map)
{
	if (auto const* object = map.getFirstSelectedObject())
	{
		for (auto const& course_object : map.courseData().objects)
		{
			if (course_object.object == object->uuid())
				return course_object.code;
		}
	}
	return {};
}

void MapEditorController::insertIntoCoursesClicked()
{
	if (map->courseData().courses.empty())
	{
		QMessageBox::information(window, tr("Insert course object into courses"), tr("The map has no courses."));
		return;
	}
	CourseBulkDialog dialog(map, CourseBulkDialog::Insert, selectedCourseCode(*map), window);
	if (dialog.exec() == QDialog::Accepted)
		window->showStatusBarMessage(dialog.report(), 10000);
}

void MapEditorController::deleteFromCoursesClicked()
{
	if (map->courseData().courses.empty())
	{
		QMessageBox::information(window, tr("Delete course object from courses"), tr("The map has no courses."));
		return;
	}
	CourseBulkDialog dialog(map, CourseBulkDialog::Remove, selectedCourseCode(*map), window);
	if (dialog.exec() == QDialog::Accepted)
		window->showStatusBarMessage(dialog.report(), 10000);
}

void MapEditorController::renumberControlsClicked()
{
	if (firstControlCode(map->courseData()) == 0)
	{
		QMessageBox::information(window, tr("Renumber all controls"), tr("The map has no control whose code is a number."));
		return;
	}
	CourseRenumberDialog dialog(map, window);
	if (dialog.exec() == QDialog::Accepted)
		window->showStatusBarMessage(dialog.report(), 10000);
}

// foom: the design notes, "Punching unit ids".
void MapEditorController::punchIdsClicked()
{
	auto const& objects = map->courseData().objects;
	if (std::none_of(begin(objects), end(objects), [](const CourseObject& object) { return object.kind == CourseObject::Control; }))
	{
		QMessageBox::information(window, tr("Punching unit IDs"), tr("The map has no controls."));
		return;
	}
	CoursePunchDialog dialog(map, window);
	if (dialog.exec() == QDialog::Accepted)
		window->showStatusBarMessage(dialog.report(), 10000);
}

// foom: The statistics and the consistency check of the courses.
// the design notes, "Statistics and the consistency check".

void MapEditorController::courseStatisticsClicked()
{
	if (map->courseData().isEmpty())
	{
		QMessageBox::information(window, tr("Course and event statistics"), tr("The map has no course objects and no courses."));
		return;
	}
	CourseStatisticsDialog dialog(map, window);
	dialog.exec();
}

int MapEditorController::runCourseCheck()
{
	last_check = [this]() { return checkCourses(*map, map->courseData()); };
	checkAgain();
	return int(issue_model->issues().size());
}

// foom: The interchange of courses.
// the design notes, "Interchange: IOF XML, GPX and KML".

/// foom: Asks for a file and writes the courses into it, or into one file for every course.
/// A KMZ has the templates which the view shows, as the maps of the courses.
static void exportCourses(MainWindow* window, Map* map, CourseFileKind kind, const MapView* view = nullptr)
{
	QString title;
	QString filter;
	QString suffix;
	switch (kind)
	{
	case CourseFileKind::IofXml:
		title = MapEditorController::tr("Export courses (IOF XML 3.0)");
		filter = MapEditorController::tr("IOF XML files (*.xml)");
		suffix = QStringLiteral("xml");
		break;
	case CourseFileKind::Gpx:
		title = MapEditorController::tr("Export courses (GPX)");
		filter = MapEditorController::tr("GPX files (*.gpx)");
		suffix = QStringLiteral("gpx");
		break;
	case CourseFileKind::Kml:
		title = MapEditorController::tr("Export courses (KML)");
		filter = MapEditorController::tr("KML files (*.kml)");
		suffix = QStringLiteral("kml");
		break;
	case CourseFileKind::Kmz:
		title = MapEditorController::tr("Export courses (KMZ)");
		filter = MapEditorController::tr("KMZ files (*.kmz)");
		suffix = QStringLiteral("kmz");
		break;
	}
	if (map->courseData().isEmpty())
	{
		QMessageBox::information(window, title, MapEditorController::tr("The map has no course objects and no courses."));
		return;
	}
	
	auto path = QFileDialog::getSaveFileName(window, title, QFileInfo(window->currentPath()).path(), filter);
	if (path.isEmpty())
		return;
	if (QFileInfo(path).suffix().isEmpty())
		path += QLatin1Char('.') + suffix;
	
	auto per_course = false;
	if (kind != CourseFileKind::IofXml && map->courseData().courses.size() > 1)
	{
		auto const answer = QMessageBox::question(window, title, MapEditorController::tr("Write one file for every course?"),
		                                          QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel, QMessageBox::No);
		if (answer == QMessageBox::Cancel)
			return;
		per_course = answer == QMessageBox::Yes;
	}
	
	if (kind == CourseFileKind::Kmz)
		QApplication::setOverrideCursor(Qt::WaitCursor);
	CourseKmzOptions kmz;
	kmz.view = view;
	auto const report = writeCourseFiles(*map, path, kind, per_course, kmz);
	if (kind == CourseFileKind::Kmz)
		QApplication::restoreOverrideCursor();
	if (!report.ok)
	{
		QMessageBox::warning(window, title, report.error);
		return;
	}
	QString message = MapEditorController::tr("%n file(s) were written.", nullptr, report.files.size());
	if (!report.skipped_courses.isEmpty())
	{
		message += QLatin1Char('\n') + (kind == CourseFileKind::IofXml
		                                ? MapEditorController::tr("Left out, for a fault of their variations or for having fewer than two controls: %1.")
		                                : MapEditorController::tr("Left out for a fault of their variations: %1.")).arg(report.skipped_courses.join(QLatin1String(", ")));
	}
	QMessageBox::information(window, title, message);
}

void MapEditorController::exportCoursesIofClicked()
{
	exportCourses(window, map, CourseFileKind::IofXml);
}

void MapEditorController::exportCoursesGpxClicked()
{
	exportCourses(window, map, CourseFileKind::Gpx);
}

void MapEditorController::exportCoursesKmlClicked()
{
	exportCourses(window, map, CourseFileKind::Kml);
}

void MapEditorController::exportCoursesKmzClicked()
{
	exportCourses(window, map, CourseFileKind::Kmz, main_view);
}

// the design notes, "Interchange: GPX and KML in, and a KMZ with the map"
void MapEditorController::importGeodataCoursesClicked()
{
	auto const title = tr("Import GPX or KML");
	auto const filter = QString(tr("GPX, KML and KMZ files (*.gpx *.kml *.kmz)") + QLatin1String(";;") + tr("All files (*.*)"));
	auto const path = QFileDialog::getOpenFileName(window, title, QFileInfo(window->currentPath()).path(), filter);
	if (path.isEmpty())
		return;

	QDialog dialog(window, Qt::WindowSystemMenuHint | Qt::WindowTitleHint);
	dialog.setWindowTitle(title);
	auto* file_label = new QLabel(tr("From %1:").arg(QFileInfo(path).fileName()));
	auto* objects_check = new QCheckBox(tr("Course objects (start, controls, finish)"));
	auto* courses_check = new QCheckBox(tr("Courses"));
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	auto* layout = new QVBoxLayout(&dialog);
	layout->addWidget(file_label);
	for (auto* check : { objects_check, courses_check })
	{
		check->setChecked(true);
		layout->addWidget(check);
	}
	layout->addWidget(buttons);
	connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	// Wide enough for the whole title and the name of the program which the window manager adds to it.
	dialog.setMinimumWidth(dialog.fontMetrics().horizontalAdvance(title) * 2 + 150);
	if (dialog.exec() != QDialog::Accepted)
		return;

	CourseGeodataImportOptions options;
	options.objects = objects_check->isChecked();
	options.courses = courses_check->isChecked();
	auto const report = importCourseFile(*map, path, options);
	if (!report.ok)
	{
		QMessageBox::warning(window, title, report.error);
		return;
	}

	auto const separator = QStringLiteral(", ");
	QStringList lines;
	lines << tr("Course objects made: %1; in the map already: %2.").arg(report.objects_added).arg(report.objects_found);
	lines << tr("Courses added: %1.").arg(report.courses_added);
	if (!report.objects_not_made.isEmpty())
		lines << tr("Waypoints not made, for want of a code or of a symbol: %1.").arg(report.objects_not_made.join(separator));
	if (!report.courses_existing.isEmpty())
		lines << tr("Courses which the map has and which were not touched: %1.").arg(report.courses_existing.join(separator));
	if (!report.tracks_not_courses.isEmpty())
		lines << tr("Tracks which do not start and end at course objects, and are no courses: %1.").arg(report.tracks_not_courses.join(separator));
	if (report.relay_variants > 0)
		lines << tr("Variants of relays which the map has, not imported as courses: %1.").arg(report.relay_variants);
	QMessageBox::information(window, title, lines.join(QLatin1Char('\n')));
}

void MapEditorController::importIofCoursesClicked()
{
	auto const title = tr("Import IOF XML");
	auto const filter = QString(tr("IOF XML files (*.xml)") + QLatin1String(";;") + tr("All files (*.*)"));
	auto const path = QFileDialog::getOpenFileName(window, title, QFileInfo(window->currentPath()).path(), filter);
	if (path.isEmpty())
		return;
	
	QDialog dialog(window, Qt::WindowSystemMenuHint | Qt::WindowTitleHint);
	dialog.setWindowTitle(title);
	auto* objects_check = new QCheckBox(tr("Course objects (start, controls, finish)"));
	auto* courses_check = new QCheckBox(tr("Courses"));
	auto* classes_check = new QCheckBox(tr("Classes"));
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	auto* layout = new QVBoxLayout(&dialog);
	for (auto* check : { objects_check, courses_check, classes_check })
	{
		check->setChecked(true);
		layout->addWidget(check);
	}
	layout->addWidget(buttons);
	connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	if (dialog.exec() != QDialog::Accepted)
		return;
	
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly))
	{
		QMessageBox::warning(window, title, tr("The file cannot be opened."));
		return;
	}
	IofImportOptions options;
	options.objects = objects_check->isChecked();
	options.courses = courses_check->isChecked();
	options.classes = classes_check->isChecked();
	auto const report = importIofCourseData(*map, file, options);
	if (!report.ok)
	{
		QMessageBox::warning(window, title, report.error);
		return;
	}
	
	auto const separator = QStringLiteral(", ");
	QStringList lines;
	lines << tr("Course objects made: %1; in the map already: %2.").arg(report.objects_added).arg(report.objects_found);
	lines << tr("Courses added: %1. Classes added: %2.").arg(report.courses_added).arg(report.classes_added);
	if (!report.objects_not_made.isEmpty())
		lines << tr("Not made, for want of a place or of a symbol: %1.").arg(report.objects_not_made.join(separator));
	if (!report.courses_existing.isEmpty())
		lines << tr("Courses which the map has and which were not touched: %1.").arg(report.courses_existing.join(separator));
	if (!report.codes_missing.isEmpty())
		lines << tr("Used in the courses but not found in the map, and left out: %1.").arg(report.codes_missing.join(separator));
	if (!report.classes_skipped.isEmpty())
		lines << tr("Classes not added: %1.").arg(report.classes_skipped.join(separator));
	if (report.relay_variants > 0)
		lines << tr("Variants of relays which the map has, not imported as courses: %1.").arg(report.relay_variants);
	if (report.taped_routes > 0)
		lines << tr("Taped routes of the file, which have no objects and were left out: %1.").arg(report.taped_routes);
	QMessageBox::information(window, title, lines.join(QLatin1Char('\n')));
}

// foom: The relays.
// the design notes, "Relays: variations, variants and their allocation".

void MapEditorController::exportRelayVariationsClicked()
{
	auto const title = tr("Export relay variations");
	auto const& data = map->courseData();
	QStringList written;
	QStringList faulty;
	for (auto const& course : data.courses)
	{
		if (course.type == Course::Normal)
			continue;
		if (relayStructure(course).isValid())
			written.push_back(course.name);
		else
			faulty.push_back(course.name);
	}
	if (written.isEmpty())
	{
		QMessageBox::information(window, title, faulty.isEmpty() ? tr("The map has no relay.")
		                                                         : tr("The variations of the relays have faults; the consistency check says which."));
		return;
	}
	auto path = QFileDialog::getSaveFileName(window, title, QFileInfo(window->currentPath()).path(), tr("Text files (*.txt)"));
	if (path.isEmpty())
		return;
	if (QFileInfo(path).suffix().isEmpty())
		path += QLatin1String(".txt");
	QFile file(path);
	auto const bytes = relayAllocationText(data).toUtf8();
	if (!file.open(QIODevice::WriteOnly | QIODevice::Text) || file.write(bytes) != bytes.size())
	{
		QMessageBox::warning(window, title, tr("The file could not be written."));
		return;
	}
	QString message = tr("Written for: %1.").arg(written.join(QLatin1String(", ")));
	if (!faulty.isEmpty())
		message += QLatin1Char('\n') + tr("Left out for the faults of their variations: %1.").arg(faulty.join(QLatin1String(", ")));
	QMessageBox::information(window, title, message);
}

// the design notes, "Route analysis: the routes of a course"
void MapEditorController::analyseCourseRoutesClicked()
{
	auto const title = tr("Analyse routes");
	auto const course = course_widget && course_dock_widget && !course_dock_widget->isHidden() ? course_widget->currentCourse() : QString{};
	if (course.isEmpty())
	{
		QMessageBox::information(window, title, tr("Choose a course in the course dock first."));
		return;
	}
	QApplication::setOverrideCursor(Qt::WaitCursor);
	auto const settings = map->courseData().route;
	auto const terrain = buildRouteTerrain(*map, settings);
	RouteSearch search(terrain, settings, map->getScaleDenominator());
	setRouteHeights(search, *map);
	// Every leg of the course is searched anew.
	StoredRoutes fresh;
	auto routes = analyseCourseRoutes(*map, course_widget->previewData(), course, course_widget->previewPart(), search, fresh);
	// The table and the window name the variant and the part which were searched.
	routes.variant = course_widget->previewVariant();
	auto course_name = routes.variant.isEmpty() ? course : QString(course + QLatin1Char('-') + routes.variant);
	if (routes.part >= 0)
		course_name += QLatin1String(" (") + QString::number(routes.part + 1) + QLatin1Char(')');
	QApplication::restoreOverrideCursor();
	if (!routes.analysed)
	{
		QMessageBox::information(window, title, tr("The course has no leg which can be analysed: a relay is analysed by the variant which is chosen, and a course needs two items with a place."));
		return;
	}
	// The routes are kept in the map, in the place of those of the same legs.
	auto data = map->courseData();
	data.routes.take(fresh);
	if (data != map->courseData())
		changeCourseData(*map, std::move(data));
	show_course_routes = true;
	map_widget->update();
	
	QString text = courseRoutesText(routes);
	if (routes.missing > 0)
		text += QLatin1Char('\n') + tr("%n leg(s) have no route: something impassable lies between their ends. They count with their straight length.", nullptr, routes.missing);
	QMessageBox box(QMessageBox::Information, title, tr("The fastest routes of the course %1 take %2 over %3 m; the course itself is %4 m long.")
	                .arg(course_name, formatRouteTime(routes.time_s)).arg(qRound(routes.length_m)).arg(qRound(routes.course_length_m)),
	                QMessageBox::Ok, window);
	box.setDetailedText(text);
	box.exec();
}


// the design notes, "Route analysis: all courses, and the routes in the file"
void MapEditorController::analyseAllCourseRoutesClicked()
{
	auto const title = tr("Analyse routes");
	if (map->courseData().courses.empty())
	{
		QMessageBox::information(window, title, tr("The map has no course."));
		return;
	}
	QApplication::setOverrideCursor(Qt::WaitCursor);
	StoredRoutes fresh;
	auto const all = analyseAllRoutesOfMap(*map, fresh);
	QApplication::restoreOverrideCursor();
	if (all.tables.empty())
	{
		QMessageBox::information(window, title, tr("No course has a leg which can be analysed."));
		return;
	}
	auto missing = 0;
	for (auto const& route : fresh.routes)
		missing += route.found ? 0 : 1;
	auto const text = allRoutesText(map->courseData(), fresh, all);
	auto data = map->courseData();
	// The routes over via points stay with the legs whose ends are where they were.
	fresh.takeViaRoutes(data.routes);
	data.routes = fresh;
	if (data != map->courseData())
		changeCourseData(*map, std::move(data));
	show_course_routes = true;
	map_widget->update();
	
	QString message = tr("%n route(s) were searched and are kept in the map.", nullptr, int(fresh.routes.size()))
	                  + QLatin1Char('\n') + tr("They are the routes of %n course(s) and variant(s) of relays.", nullptr, int(all.tables.size()));
	if (missing > 0)
		message += QLatin1Char('\n') + tr("%n leg(s) have no route: something impassable lies between their ends.", nullptr, missing);
	if (!all.skipped.isEmpty())
		message += QLatin1Char('\n') + tr("Left out for the faults of their variations: %1.").arg(all.skipped.join(QLatin1String(", ")));
	QMessageBox box(QMessageBox::Information, title, message, QMessageBox::Ok, window);
	box.setDetailedText(text);
	box.exec();
}


void MapEditorController::showCourseRoutesClicked()
{
	if (map->courseData().routes.isEmpty())
	{
		QMessageBox::information(window, tr("Show routes"), tr("The map keeps no routes. Analyse the routes of a course or of all courses first."));
		return;
	}
	show_course_routes = true;
	map_widget->update();
}


void MapEditorController::deleteCourseRoutesClicked()
{
	auto data = map->courseData();
	if (data.routes.isEmpty())
		return;
	data.routes = {};
	changeCourseData(*map, std::move(data));
	map_widget->update();
}


void MapEditorController::exportCourseRoutesClicked()
{
	auto const title = tr("Export routes");
	auto const& data = map->courseData();
	if (data.routes.isEmpty())
	{
		QMessageBox::information(window, title, tr("The map keeps no routes. Analyse the routes of a course or of all courses first."));
		return;
	}
	auto const map_path = window->currentPath();
	QString proposal;
	if (!map_path.isEmpty())
		proposal = QDir(QFileInfo(map_path).path()).filePath(QFileInfo(map_path).completeBaseName() + QLatin1String(".AllLegsRouteLengths.txt"));
	auto path = QFileDialog::getSaveFileName(window, title, proposal, tr("Text files (*.txt)"));
	if (path.isEmpty())
		return;
	if (QFileInfo(path).suffix().isEmpty())
		path += QLatin1String(".txt");
	auto const all = storedAllRoutes(*map, data);
	// With a byte order mark, as the format has it.
	QByteArray bytes("\xef\xbb\xbf");
	bytes += allRoutesText(data, data.routes, all).toUtf8();
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) || file.write(bytes) != bytes.size())
	{
		QMessageBox::warning(window, title, tr("The file could not be written."));
		return;
	}
	auto unknown = 0;
	for (auto const& table : all.tables)
		unknown += table.unknown;
	QString message = tr("The routes of %n course(s) and variant(s) of relays were written.", nullptr, int(all.tables.size()));
	if (unknown > 0)
		message += QLatin1Char('\n') + tr("%n leg(s) of them have not been analysed, or not since an object of theirs was moved.", nullptr, unknown);
	if (!all.skipped.isEmpty())
		message += QLatin1Char('\n') + tr("Left out for the faults of their variations: %1.").arg(all.skipped.join(QLatin1String(", ")));
	QMessageBox::information(window, title, message);
}


// the design notes, "Route analysis: via points and the leg by hand"
void MapEditorController::routeViaPointsClicked()
{
	// Without the tool, the action of the tool which stays is the checked one.
	auto const give_up = [this](const QString& why) {
		course_routes_via_act->setChecked(false);
		if (current_tool && current_tool->toolAction())
			current_tool->toolAction()->setChecked(true);
		if (!why.isEmpty())
			QMessageBox::information(window, tr("Add route via points"), why);
	};
	if (!course_dock_act->isChecked())
		course_dock_act->trigger();
	if (!course_widget)
	{
		give_up({});
		return;
	}
	if (course_widget->currentCourse().isEmpty())
	{
		give_up(tr("Choose a course in the course dock first."));
		return;
	}
	if (map->courseData().routes.isEmpty())
	{
		give_up(tr("The map keeps no routes. Analyse the routes of a course or of all courses first."));
		return;
	}
	// The routes are what the tool is used on.
	show_course_routes = true;
	map_widget->update();
	setTool(new RouteTool(this, course_routes_via_act, course_widget, RouteTool::ViaPoints));
}


void MapEditorController::routeFreeLegClicked()
{
	setTool(new RouteTool(this, course_routes_leg_act, course_widget, RouteTool::FreeLeg));
}


// the design notes, "Route analysis: the settings and the elevation"
void MapEditorController::routeSettingsClicked()
{
	auto const has_elevation = bool(routeHeightsOf(*map));
	RouteSettingsDialog dialog(map, has_elevation, window);
	if (dialog.exec() != QDialog::Accepted)
		return;
	auto data = map->courseData();
	if (data.route == dialog.settings())
		return;
	data.route = dialog.settings();
	// The routes which are kept are no longer those of the settings.
	data.routes = {};
	changeCourseData(*map, std::move(data));
	map_widget->update();
}


void MapEditorController::hideCourseRoutesClicked()
{
	show_course_routes = false;
	map_widget->update();
}


// the design notes, "Canvases: the dialog"
void MapEditorController::courseCanvasesClicked()
{
	CourseCanvasesDialog dialog(map, window);
	if (dialog.exec() != QDialog::Accepted)
		return;
	// foom: the design notes, "Canvases: load extent from background map"
	importExtents(*map, dialog.loadedExtents());
	auto data = map->courseData();
	if (data.canvases == dialog.canvases())
		return;
	data.canvases = dialog.canvases();
	changeCourseData(*map, std::move(data));
}


// the design notes, "The maps of the courses as PDF files"
void MapEditorController::exportCourseMapsClicked()
{
	auto const title = tr("Export course maps");
	if (map->courseData().courses.empty())
	{
		QMessageBox::information(window, title, tr("The map has no course."));
		return;
	}
	auto const map_path = window->currentPath();
	CourseMapsDialog dialog(map, QFileInfo(map_path).path(), window);
	if (dialog.exec() != QDialog::Accepted)
		return;
	auto options = dialog.options();
	options.base_name = map_path.isEmpty() ? tr("map") : QFileInfo(map_path).completeBaseName();
	options.view = main_view;
	
	QApplication::setOverrideCursor(Qt::WaitCursor);
	auto const report = exportCourseMaps(*map, options);
	QApplication::restoreOverrideCursor();
	map_widget->update();
	if (!report.ok)
	{
		QMessageBox::warning(window, title, report.error);
		return;
	}
	QString message = tr("%n file(s) were written into %1.", nullptr, report.files.size()).arg(QDir::toNativeSeparators(options.folder));
	if (!report.skipped.isEmpty())
		message += QLatin1Char('\n') + (options.use_canvases ? tr("Left out, as no canvas is used for them: %1.")
		                                                      : tr("Left out, relays whose variations have a fault: %1.")).arg(report.skipped.join(QLatin1String(", ")));
	QMessageBox::information(window, title, message);
}


void MapEditorController::drawCoursePreview(QPainter* painter, MapWidget* widget) const
{
	if (!course_widget || !course_dock_widget || course_dock_widget->isHidden())
		return;
	auto const course = course_widget->currentCourse();
	if (course.isEmpty())
		return;
	// Drawn anew at every painting, so that it follows an object which is being moved.
	// A relay is drawn by the variant which is chosen in the dock.
	auto const preview_data = course_widget->previewData();
	// A course with a map exchange or flip by the part which is chosen there.
	auto const part = course_widget->previewPart();
	auto const drawing = drawCourse(*map, preview_data, course, part);
	auto const descriptions = drawCourseDescriptions(*map, preview_data, course, part);
	if (!drawing.drawn && !descriptions.drawn)
		return;
	
	const MapView* map_view = widget->getMapView();
	painter->save();
	painter->translate(widget->width() / 2.0 + map_view->panOffset().x(),
	                   widget->height() / 2.0 + map_view->panOffset().y());
	painter->setWorldTransform(map_view->worldTransform(), true);
	auto const viewed_rect = map_view->calculateViewedRect(widget->viewportToView(widget->rect()));
	// The descriptions first: their white ground lies below the course.
	paintCourseDescriptions(painter, map, descriptions, viewed_rect, map_view->calculateFinalZoomFactor());
	paintCourseDrawing(painter, map, drawing, viewed_rect, map_view->calculateFinalZoomFactor());
	// The routes which the map keeps, for the course, the part and the variant which are chosen.
	if (show_course_routes && !preview_data.routes.isEmpty())
		paintCourseRoutes(painter, storedCourseRoutes(*map, preview_data, course, part));
	painter->restore();
}



// foom: The rangefinder.
// the design notes, "Rangefinder dock: detail".

void MapEditorController::rangefinderClicked(bool checked)
{
	if (!rangefinder_widget)
	{
		rangefinder_widget = new RangefinderWidget(map, mobile_mode);
		rangefinder_widget->setReferenceClickAction(rangefinder_reference_act);
		rangefinder_widget->setAddFunction([this](const MapCoordF& target) { return rangefinderAdd(target); });
		connect(rangefinder_widget, &RangefinderWidget::changed, map_widget, QOverload<>::of(&QWidget::update));
		connect(rangefinder_widget, &RangefinderWidget::referenceFromGpsRequested, this, &MapEditorController::rangefinderReferenceFromGps);
		connect(rangefinder_widget, &RangefinderWidget::finishRequested, this, &MapEditorController::rangefinderFinish);
		if (mobile_mode)
		{
			// A sheet over the full width at the bottom of the map.
			rangefinder_widget->setParent(map_widget);
			rangefinder_widget->setAutoFillBackground(true);
			connect(rangefinder_widget, &RangefinderWidget::closeClicked, this, [this]() {
				rangefinder_act->setChecked(false);
				rangefinderClicked(false);
			});
			rangefinder_container = rangefinder_widget;
		}
		else
		{
			auto* dock_widget = new EditorDockWidget(tr("Rangefinder"), rangefinder_act, this, window);
			dock_widget->toggleViewAction()->setVisible(false);
			dock_widget->setWidget(rangefinder_widget);
			dock_widget->setObjectName(QString::fromLatin1("Rangefinder dock widget"));
			addFloatingDockWidget(dock_widget);
			rangefinder_container = dock_widget;
		}
	}
	
	if (mobile_mode && checked)
	{
		auto const height = qMin(rangefinder_widget->sizeHint().height(), map_widget->height());
		rangefinder_container->setGeometry(0, map_widget->height() - height, map_widget->width(), height);
		rangefinder_container->raise();
	}
	rangefinder_container->setVisible(checked);
	if (!checked && qobject_cast<RangefinderReferenceTool*>(current_tool))
		setEditTool();
	map_widget->update();
}

void MapEditorController::rangefinderReferenceClicked()
{
	setTool(new RangefinderReferenceTool(this, rangefinder_reference_act));
}

void MapEditorController::rangefinderFinish()
{
	if (!current_tool || !current_tool->editingInProgress())
		return;
	// As Ctrl+Return in the map: the path ends at its last vertex and is not closed.
	QKeyEvent event(QEvent::KeyPress, Qt::Key_Return, Qt::ControlModifier);
	current_tool->keyPressEvent(&event);
}

void MapEditorController::rangefinderReferenceFromGps()
{
	if (gps_display && gps_display->hasValidPosition())
		rangefinderSetReference(gps_display->getLatestGPSCoord());
	else
		window->showStatusBarMessage(tr("There is no GPS position which can be used."), 3000);
}

void MapEditorController::rangefinderSetReference(const MapCoordF& position)
{
	if (rangefinder_widget)
		rangefinder_widget->setReference(position);
}

bool MapEditorController::rangefinderAdd(const MapCoordF& target)
{
	if (current_tool && current_tool->acceptsNumericVertices())
	{
		current_tool->acceptNumericVertex(target);
		return true;
	}
	
	auto const* symbol = activeSymbol();
	if (symbol && !symbol->isHidden())
	{
		switch (symbol->getType())
		{
		case Symbol::Point:
			{
				auto* object = new PointObject(symbol);
				object->setPosition(target);
				auto const index = map->addObject(object);
				map->setObjectsDirty();
				auto* undo_step = new DeleteObjectsUndoStep(map);
				undo_step->addObject(index);
				map->push(undo_step);
				return true;
			}
		case Symbol::Line:
		case Symbol::Area:
		case Symbol::Combined:
			// The path tool takes the target as its first vertex.
			if (draw_path_act->isEnabled())
			{
				draw_path_act->trigger();
				if (current_tool && current_tool->acceptsNumericVertices())
				{
					current_tool->acceptNumericVertex(target);
					return true;
				}
			}
			break;
		default:
			break;
		}
	}
	window->showStatusBarMessage(tr("Choose a point, line or area symbol to draw the reading."), 3000);
	return false;
}

void MapEditorController::drawRangefinder(QPainter* painter, MapWidget* widget) const
{
	if (!rangefinder_widget || !rangefinder_container || rangefinder_container->isHidden())
		return;
	auto const& survey = rangefinder_widget->survey();
	if (!survey.hasReference())
		return;
	
	auto const reference = widget->mapToViewport(survey.reference());
	MapCoordF target_coord;
	auto const has_target = rangefinder_widget->previewTarget(&target_coord);
	auto const target = has_target ? widget->mapToViewport(target_coord) : reference;
	auto const radius = Util::mmToPixelLogical(2.5);
	auto const arm = Util::mmToPixelLogical(1.8);
	
	painter->save();
	painter->setRenderHint(QPainter::Antialiasing);
	painter->setBrush(Qt::NoBrush);
	// First a white casing, then the colour, so that it shows on every ground.
	for (auto const& pen : { QPen(Qt::white, Util::mmToPixelLogical(1.2)), QPen(QColor(220, 0, 220), Util::mmToPixelLogical(0.6)) })
	{
		painter->setPen(pen);
		painter->drawEllipse(reference, radius, radius);
		painter->drawLine(reference - QPointF(1.4 * radius, 0), reference + QPointF(1.4 * radius, 0));
		painter->drawLine(reference - QPointF(0, 1.4 * radius), reference + QPointF(0, 1.4 * radius));
		if (has_target)
		{
			painter->drawLine(reference, target);
			painter->drawLine(target - QPointF(arm, arm), target + QPointF(arm, arm));
			painter->drawLine(target - QPointF(arm, -arm), target + QPointF(arm, -arm));
		}
	}
	painter->restore();
}

void MapEditorController::booleanUnionClicked()
{
	if (!BooleanTool(BooleanTool::Union, map).executePerSymbol())
		QMessageBox::warning(window, tr("Error"), tr("Unification failed."));
}

void MapEditorController::booleanIntersectionClicked()
{
	if (!BooleanTool(BooleanTool::Intersection, map).execute())
		QMessageBox::warning(window, tr("Error"), tr("Intersection failed."));
}

void MapEditorController::booleanDifferenceClicked()
{
	if (!BooleanTool(BooleanTool::Difference, map).execute())
		QMessageBox::warning(window, tr("Error"), tr("Difference failed."));
}

void MapEditorController::booleanXOrClicked()
{
	if (!BooleanTool(BooleanTool::XOr, map).execute())
		QMessageBox::warning(window, tr("Error"), tr("XOr failed."));
}

void MapEditorController::booleanMergeHolesClicked()
{
	if (map->getNumSelectedObjects() != 1)
		return;
	
	if (!BooleanTool(BooleanTool::MergeHoles, map).execute())
		QMessageBox::warning(window, tr("Error"), tr("Merging holes failed."));
}

void MapEditorController::convertToCurvesClicked()
{
	auto* undo_step = new ReplaceObjectsUndoStep(map);
	MapPart* part = map->getCurrentPart();
	
	for (auto* object : map->selectedObjects())
	{
		if (object->getType() != Object::Path)
			continue;
		
		PathObject* path = object->asPath();
		PathObject* undo_duplicate = nullptr;
		if (path->convertToCurves(&undo_duplicate))
		{
			undo_step->addObject(part->findObjectIndex(path), undo_duplicate);
			// TODO: make threshold configurable?
			const auto threshold = 0.08;
			path->simplify(nullptr, threshold);
		}
		path->update();
	}
	
	if (undo_step->isEmpty())
		delete undo_step;
	else
	{
		map->setObjectsDirty();
		map->push(undo_step);
		map->emitSelectionEdited();
	}
}

void MapEditorController::simplifyPathClicked()
{
	// TODO: make threshold configurable!
	const auto threshold = 0.1;
	
	auto* undo_step = new ReplaceObjectsUndoStep(map);
	MapPart* part = map->getCurrentPart();
	
	for (auto* object : map->selectedObjects())
	{
		if (object->getType() != Object::Path)
			continue;
		
		PathObject* path = object->asPath();
		PathObject* undo_duplicate = nullptr;
		if (path->simplify(&undo_duplicate, threshold))
			undo_step->addObject(part->findObjectIndex(path), undo_duplicate);
		path->update();
	}
	
	if (undo_step->isEmpty())
		delete undo_step;
	else
	{
		map->setObjectsDirty();
		map->push(undo_step);
		map->emitSelectionEdited();
	}
}

// foom
void MapEditorController::toPolylineClicked()
{
	bool ok = false;
	auto const tolerance = QInputDialog::getDouble(window, tr("To polyline"), tr("Tolerance (mm):"), to_polyline_tolerance, 0.001, 10.0, 3, &ok);
	if (ok)
	{
		to_polyline_tolerance = tolerance;
		toPolyline(tolerance);
	}
}

// foom
void MapEditorController::toPolyline(double tolerance)
{
	// the design notes, "To polyline".
	auto const& selection = map->selectedObjects();
	std::vector<const Object*> in(begin(selection), end(selection));
	ops::FlattenParams params;
	params.tolerance = tolerance;
	applyOpResult(ops::flatten(*map, in, params));
}

// foom
void MapEditorController::toCurveClicked()
{
	ToCurveDialog dialog(to_curve_params, window);
	if (dialog.exec() == QDialog::Accepted)
	{
		to_curve_params = dialog.params();
		toCurve(to_curve_params);
	}
}

// foom
void MapEditorController::toCurve(const ops::ToCurveParams& params)
{
	// the design notes, "Polyline to curve".
	auto const& selection = map->selectedObjects();
	std::vector<const Object*> in(begin(selection), end(selection));
	applyOpResult(ops::toCurve(*map, in, params));
}

// foom
void MapEditorController::mirrorClicked()
{
	QDialog dialog(window);
	dialog.setWindowTitle(tr("Mirror"));
	auto* vertical = new QRadioButton(tr("Vertical axis (swap left and right)"));
	auto* horizontal = new QRadioButton(tr("Horizontal axis (swap top and bottom)"));
	auto* angled = new QRadioButton(tr("Axis at angle:"));
	auto* angle_edit = new QDoubleSpinBox();
	angle_edit->setRange(-180.0, 180.0);
	angle_edit->setDecimals(1);
	angle_edit->setSuffix(tr("\u00b0"));
	angle_edit->setValue(mirror_axis_angle);
	angle_edit->setEnabled(false);
	connect(angled, &QRadioButton::toggled, angle_edit, &QWidget::setEnabled);
	if (qFuzzyCompare(mirror_axis_angle, 90.0))
		vertical->setChecked(true);
	else if (qFuzzyIsNull(mirror_axis_angle))
		horizontal->setChecked(true);
	else
		angled->setChecked(true);
	auto* keep = new QCheckBox(tr("Keep the original"));
	keep->setChecked(mirror_duplicate);
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	auto* angle_row = new QHBoxLayout();
	angle_row->addWidget(angled);
	angle_row->addWidget(angle_edit);
	auto* layout = new QVBoxLayout(&dialog);
	layout->addWidget(vertical);
	layout->addWidget(horizontal);
	layout->addLayout(angle_row);
	layout->addWidget(keep);
	layout->addWidget(buttons);
	if (dialog.exec() != QDialog::Accepted)
		return;
	
	mirror_axis_angle = vertical->isChecked() ? 90.0 : horizontal->isChecked() ? 0.0 : angle_edit->value();
	mirror_duplicate = keep->isChecked();
	mirror(mirror_axis_angle, mirror_duplicate);
}

// foom
void MapEditorController::mirror(double axis_angle, bool duplicate)
{
	// the design notes, "Mirror".
	auto const& selection = map->selectedObjects();
	std::vector<const Object*> in(begin(selection), end(selection));
	ops::MirrorParams params;
	params.axis_angle = axis_angle;
	params.duplicate = duplicate;
	applyOpResult(ops::mirror(*map, in, params));
}

// foom: the design notes, "Descriptions file".
void MapEditorController::exportSymbolDescriptionsClicked()
{
	auto const path = QFileDialog::getSaveFileName(window, tr("Export symbol descriptions"), {}, tr("CSV") + QLatin1String(" (*.csv)"));
	if (path.isEmpty())
		return;
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) || file.write(symbolDescriptionsCsv(*map).toUtf8()) < 0)
		QMessageBox::warning(window, tr("Export symbol descriptions"), file.errorString());
}

void MapEditorController::importSymbolDescriptionsClicked()
{
	auto const path = QFileDialog::getOpenFileName(window, tr("Import symbol descriptions"), {}, tr("CSV") + QLatin1String(" (*.csv);;") + tr("All files") + QLatin1String(" (*.*)"));
	if (path.isEmpty())
		return;
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly))
	{
		QMessageBox::warning(window, tr("Import symbol descriptions"), file.errorString());
		return;
	}
	auto const result = applySymbolDescriptionsCsv(*map, QString::fromUtf8(file.readAll()));
	auto message = tr("%1 symbol(s) changed.").arg(result.changed);
	if (!result.unmatched.isEmpty())
		message += QLatin1Char('\n') + tr("No symbol has the number(s): %1").arg(result.unmatched.join(QStringLiteral(", ")));
	if (result.malformed > 0)
		message += QLatin1Char('\n') + tr("%1 row(s) without three fields were skipped.").arg(result.malformed);
	QMessageBox::information(window, tr("Import symbol descriptions"), message);
}

// foom: the design notes, "Update from file".
void MapEditorController::updateSymbolsClicked()
{
	auto selected = MainWindow::getOpenFileName(window, tr("Update from file"), FileFormat::MapFile);
	if (!selected)
		return;
	Map reference;
	auto importer = selected.fileFormat() ? selected.fileFormat()->makeImporter(selected.filePath(), &reference, nullptr) : nullptr;
	if (!importer || !importer->doImport())
	{
		QMessageBox::warning(window, tr("Update from file"), tr("Cannot read %1.").arg(selected.filePath()));
		return;
	}
	bool ok = false;
	auto const percent = QInputDialog::getDouble(window, tr("Update from file"), tr("Scale the new definitions to:"), 100.0, 1.0, 10000.0, 1, &ok);
	if (!ok)
		return;
	auto const report = updateSymbolsFrom(*map, reference, percent / 100.0);
	auto const list = [](const QStringList& items) { return items.isEmpty() ? tr("none") : items.join(QStringLiteral(", ")); };
	QMessageBox::information(window, tr("Update from file"),
	                         tr("Changed: %1\nAdded: %2\nUnchanged: %3 symbol(s)").arg(list(report.changed), list(report.added)).arg(report.unchanged.size()));
}

// foom: the design notes, "Compare with file".
void MapEditorController::compareSymbolsClicked()
{
	auto selected = MainWindow::getOpenFileName(window, tr("Compare with file"), FileFormat::MapFile);
	if (!selected)
		return;
	Map reference;
	auto importer = selected.fileFormat() ? selected.fileFormat()->makeImporter(selected.filePath(), &reference, nullptr) : nullptr;
	if (!importer || !importer->doImport())
	{
		QMessageBox::warning(window, tr("Compare with file"), tr("Cannot read %1.").arg(selected.filePath()));
		return;
	}
	QDialog options_dialog(window);
	options_dialog.setWindowTitle(tr("Compare with file"));
	auto* used_check = new QCheckBox(tr("Only the symbols used in this map"));
	auto* identical_check = new QCheckBox(tr("List identical symbols"));
	auto* descriptions_check = new QCheckBox(tr("Compare descriptions"));
	descriptions_check->setChecked(true);
	auto* colours_check = new QCheckBox(tr("Compare colours"));
	colours_check->setChecked(true);
	auto* option_buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	connect(option_buttons, &QDialogButtonBox::accepted, &options_dialog, &QDialog::accept);
	connect(option_buttons, &QDialogButtonBox::rejected, &options_dialog, &QDialog::reject);
	auto* options_layout = new QVBoxLayout(&options_dialog);
	for (auto* check : { used_check, identical_check, descriptions_check, colours_check })
		options_layout->addWidget(check);
	options_layout->addWidget(option_buttons);
	if (options_dialog.exec() != QDialog::Accepted)
		return;
	SymbolCompareOptions options;
	options.used_only = used_check->isChecked();
	options.list_identical = identical_check->isChecked();
	options.descriptions = descriptions_check->isChecked();
	options.colours = colours_check->isChecked();
	auto const text = symbolComparisonText(compareSymbolSets(*map, reference, options));

	QDialog dialog(window);
	dialog.setWindowTitle(tr("Compare with %1").arg(QFileInfo(selected.filePath()).fileName()));
	auto* view = new QPlainTextEdit(text);
	view->setReadOnly(true);
	view->setMinimumSize(480, 360);
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close);
	auto* copy = buttons->addButton(tr("Copy"), QDialogButtonBox::ActionRole);
	auto* save = buttons->addButton(tr("Save..."), QDialogButtonBox::ActionRole);
	connect(copy, &QPushButton::clicked, &dialog, [text]() { QApplication::clipboard()->setText(text); });
	connect(save, &QPushButton::clicked, &dialog, [this, text, &dialog]() {
		auto const path = QFileDialog::getSaveFileName(&dialog, tr("Save report"), {}, tr("Text files") + QLatin1String(" (*.txt)"));
		if (path.isEmpty())
			return;
		QFile file(path);
		if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text) || file.write(text.toUtf8()) < 0)
			QMessageBox::warning(&dialog, tr("Save report"), file.errorString());
	});
	connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	auto* layout = new QVBoxLayout(&dialog);
	layout->addWidget(view);
	layout->addWidget(buttons);
	dialog.exec();
}

// foom: the design notes, "Symbol tree".
void MapEditorController::showSymbolGroups(bool show)
{
	if (!symbol_groups_dock_widget)
	{
		symbol_groups_dock_widget = new EditorDockWidget(tr("Symbol groups"), symbol_groups_act, this, window);
		symbol_groups_dock_widget->setWidget(new SymbolGroupsWidget(*map, symbol_widget, symbol_groups_dock_widget));
		symbol_groups_dock_widget->setObjectName(QString::fromLatin1("Symbol groups dock widget"));
		if (!window->restoreDockWidget(symbol_groups_dock_widget))
			window->addDockWidget(Qt::RightDockWidgetArea, symbol_groups_dock_widget, Qt::Vertical);
	}
	symbol_groups_act->setChecked(show);
	symbol_groups_dock_widget->setVisible(show);
}

// foom: the design notes, "Status sets".
void MapEditorController::saveStatusSet(const QString& name)
{
	auto sets = map->statusSets();
	auto set = currentStatusSet(*map, name);
	auto existing = std::find_if(sets.begin(), sets.end(), [&name](const StatusSet& s) { return s.name == name; });
	if (existing != sets.end())
		*existing = std::move(set);
	else
		sets.push_back(std::move(set));
	map->setStatusSets(std::move(sets));
	map->setHasUnsavedChanges(true);
}

int MapEditorController::applyStatusSet(const QString& name)
{
	auto const& sets = map->statusSets();
	auto const found = std::find_if(sets.begin(), sets.end(), [&name](const StatusSet& s) { return s.name == name; });
	if (found == sets.end())
		return 0;
	auto const changed = OpenOrienteering::applyStatusSet(*map, *found);
	if (window)
		window->showStatusBarMessage(tr("%1 symbol(s) changed").arg(changed), 5000);
	return changed;
}

void MapEditorController::deleteStatusSet(const QString& name)
{
	auto sets = map->statusSets();
	auto const before = sets.size();
	sets.erase(std::remove_if(sets.begin(), sets.end(), [&name](const StatusSet& s) { return s.name == name; }), sets.end());
	if (sets.size() == before)
		return;
	map->setStatusSets(std::move(sets));
	map->setHasUnsavedChanges(true);
}

namespace {

QString chooseStatusSet(QWidget* parent, const Map& map, const QString& title)
{
	QStringList names;
	for (auto const& set : map.statusSets())
		names.push_back(set.name);
	if (names.isEmpty())
	{
		QMessageBox::information(parent, title, MapEditorController::tr("The map has no status set."));
		return {};
	}
	bool ok = false;
	auto const name = QInputDialog::getItem(parent, title, MapEditorController::tr("Status set:"), names, 0, false, &ok);
	return ok ? name : QString{};
}

}  // namespace

void MapEditorController::saveStatusSetClicked()
{
	bool ok = false;
	auto const name = QInputDialog::getText(window, tr("Save status set"), tr("Name:"), QLineEdit::Normal,
	                                        tr("Status set %1").arg(map->statusSets().size() + 1), &ok);
	if (ok && !name.trimmed().isEmpty())
		saveStatusSet(name.trimmed());
}

void MapEditorController::applyStatusSetClicked()
{
	auto const name = chooseStatusSet(window, *map, tr("Apply status set"));
	if (!name.isEmpty())
		applyStatusSet(name);
}

void MapEditorController::deleteStatusSetClicked()
{
	auto const name = chooseStatusSet(window, *map, tr("Delete status set"));
	if (!name.isEmpty())
		deleteStatusSet(name);
}

// foom: the design notes, "Selecting symbols, replacing fonts and colours, renumbering".
std::vector<const Symbol*> MapEditorController::selectedSymbolsScope() const
{
	std::vector<const Symbol*> scope;
	if (symbol_widget)
	{
		for (int i = 0; i < map->getNumSymbols(); ++i)
		{
			if (symbol_widget->isSymbolSelected(map->getSymbol(i)))
				scope.push_back(map->getSymbol(i));
		}
	}
	return scope;
}

void MapEditorController::replaceFontClicked()
{
	SymbolReplaceDialog dialog(SymbolReplaceDialog::Font, *map, window);
	if (dialog.exec() != QDialog::Accepted)
		return;
	auto const scope = dialog.onlySelected() ? selectedSymbolsScope() : std::vector<const Symbol*>{};
	if (dialog.onlySelected() && scope.empty())
		return;
	auto const count = replaceFont(*map, dialog.fromFont(), dialog.toFont(), scope);
	window->showStatusBarMessage(tr("%1 symbol(s) changed").arg(count), 5000);
}

void MapEditorController::replaceColorClicked()
{
	SymbolReplaceDialog dialog(SymbolReplaceDialog::Color, *map, window);
	if (dialog.exec() != QDialog::Accepted)
		return;
	auto const scope = dialog.onlySelected() ? selectedSymbolsScope() : std::vector<const Symbol*>{};
	if (dialog.onlySelected() && scope.empty())
		return;
	auto const count = replaceColor(*map, dialog.fromColor(), dialog.toColor(), scope);
	window->showStatusBarMessage(tr("%1 symbol(s) changed").arg(count), 5000);
}

void MapEditorController::renumberSymbolsClicked()
{
	SymbolReplaceDialog dialog(SymbolReplaceDialog::Renumber, *map, window);
	if (dialog.exec() != QDialog::Accepted)
		return;
	auto const scope = dialog.onlySelected() ? selectedSymbolsScope() : std::vector<const Symbol*>{};
	if (dialog.onlySelected() && scope.empty())
		return;
	auto const count = renumberSymbols(*map, dialog.start(), dialog.step(), scope);
	window->showStatusBarMessage(tr("%1 symbol(s) renumbered").arg(count), 5000);
}

// foom: the design notes, "Object information".
void MapEditorController::objectInformationClicked()
{
	if (map->getNumSelectedObjects() != 1)
		return;
	auto const text = objectInformationText(objectInformation(*map, *map->getFirstSelectedObject()));
	QDialog dialog(window);
	dialog.setWindowTitle(tr("Object information"));
	auto* view = new QPlainTextEdit(text);
	view->setReadOnly(true);
	view->setMinimumSize(420, 320);
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close);
	auto* copy = buttons->addButton(tr("Copy"), QDialogButtonBox::ActionRole);
	connect(copy, &QPushButton::clicked, &dialog, [text]() { QApplication::clipboard()->setText(text); });
	connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	auto* layout = new QVBoxLayout(&dialog);
	layout->addWidget(view);
	layout->addWidget(buttons);
	dialog.exec();
}

// foom
void MapEditorController::convertToPointsClicked()
{
	auto const* symbol = symbol_widget ? symbol_widget->getSingleSelectedSymbol() : nullptr;
	if (!symbol || symbol->getType() != Symbol::Point)
	{
		QMessageBox::information(window, tr("Convert to point objects"), tr("Select a point symbol in the symbol pane first."));
		return;
	}
	QDialog dialog(window);
	dialog.setWindowTitle(tr("Convert to point objects"));
	auto* dx_edit = new QDoubleSpinBox();
	auto* dy_edit = new QDoubleSpinBox();
	for (auto* edit : { dx_edit, dy_edit })
	{
		edit->setRange(-1000.0, 1000.0);
		edit->setDecimals(2);
		edit->setSuffix(QStringLiteral(" mm"));
	}
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	auto* form = new QFormLayout();
	form->addRow(tr("Offset of texts, right:"), dx_edit);
	form->addRow(tr("Offset of texts, down:"), dy_edit);
	auto* layout = new QVBoxLayout(&dialog);
	layout->addLayout(form);
	layout->addWidget(buttons);
	if (dialog.exec() == QDialog::Accepted)
		convertToPoints(dx_edit->value(), dy_edit->value());
}

// foom
void MapEditorController::convertToPoints(double dx, double dy)
{
	// the design notes, "Convert objects".
	auto const& selection = map->selectedObjects();
	std::vector<const Object*> in(begin(selection), end(selection));
	ops::ConvertToPointsParams params;
	auto const* symbol = symbol_widget ? symbol_widget->getSingleSelectedSymbol() : nullptr;
	params.symbol = symbol && symbol->getType() == Symbol::Point ? symbol->asPoint() : nullptr;
	params.text_offset = MapCoordF(dx, dy);
	applyOpResult(ops::convertToPoints(*map, in, params));
}

// foom
void MapEditorController::interpolateClicked()
{
	QDialog dialog(window);
	dialog.setWindowTitle(tr("Interpolate"));
	auto* count_edit = new QSpinBox();
	count_edit->setRange(1, 20);
	count_edit->setValue(1);
	auto* symbol_check = new QCheckBox(tr("Use the symbol selected in the symbol pane"));
	auto const* selected = symbol_widget ? symbol_widget->getSingleSelectedSymbol() : nullptr;
	symbol_check->setEnabled(selected && (selected->getType() == Symbol::Line || selected->getType() == Symbol::Area || selected->getType() == Symbol::Combined));
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	auto* form = new QFormLayout();
	form->addRow(tr("Number of new objects:"), count_edit);
	form->addRow(symbol_check);
	auto* layout = new QVBoxLayout(&dialog);
	layout->addLayout(form);
	layout->addWidget(buttons);
	if (dialog.exec() == QDialog::Accepted)
		interpolate(count_edit->value(), symbol_check->isEnabled() && symbol_check->isChecked());
}

// foom
void MapEditorController::interpolate(int count, bool use_selected_symbol)
{
	// the design notes, "Interpolate".
	auto const& selection = map->selectedObjects();
	std::vector<const Object*> in(begin(selection), end(selection));
	ops::InterpolateParams params;
	params.count = count;
	if (use_selected_symbol && symbol_widget)
		params.symbol = symbol_widget->getSingleSelectedSymbol();
	applyOpResult(ops::interpolate(*map, in, params));
}

// foom
void MapEditorController::moveByClicked()
{
	if (NumericTransformDialogs::askMoveBy(window, move_by_params))
		moveBy(move_by_params.dx, move_by_params.dy, move_by_params.duplicate);
}

// foom
void MapEditorController::moveBy(double dx, double dy, bool duplicate)
{
	auto const& selection = map->selectedObjects();
	std::vector<const Object*> in(begin(selection), end(selection));
	ops::MoveParams params;
	params.dx = dx;
	params.dy = dy;
	params.duplicate = duplicate;
	applyOpResult(ops::moveBy(*map, in, params));
}

// foom
void MapEditorController::rotateByClicked()
{
	if (NumericTransformDialogs::askRotateBy(window, rotate_by_params))
		rotateBy(rotate_by_params.angle, rotate_by_params.each_object, rotate_by_params.duplicate);
}

// foom
void MapEditorController::rotateBy(double angle, bool each_object, bool duplicate)
{
	auto const& selection = map->selectedObjects();
	std::vector<const Object*> in(begin(selection), end(selection));
	ops::RotateParams params;
	params.angle = angle;
	params.each_object = each_object;
	params.duplicate = duplicate;
	applyOpResult(ops::rotateBy(*map, in, params));
}

// foom
void MapEditorController::stretchClicked()
{
	if (NumericTransformDialogs::askStretch(window, stretch_params))
		stretch(stretch_params.factor_x, stretch_params.factor_y, stretch_params.duplicate);
}

// foom
void MapEditorController::stretch(double factor_x, double factor_y, bool duplicate)
{
	auto const& selection = map->selectedObjects();
	std::vector<const Object*> in(begin(selection), end(selection));
	ops::StretchParams params;
	params.factor_x = factor_x;
	params.factor_y = factor_y;
	params.duplicate = duplicate;
	applyOpResult(ops::stretch(*map, in, params));
}

// foom
void MapEditorController::alignClicked()
{
	if (auto* action = qobject_cast<QAction*>(sender()))
		align(static_cast<ops::AlignParams::Edge>(action->data().toInt()));
}

// foom
void MapEditorController::align(ops::AlignParams::Edge edge)
{
	// the design notes, "Align and distribute".
	auto const* first = map->getFirstSelectedObject();
	std::vector<const Object*> in;
	in.reserve(map->getNumSelectedObjects());
	if (first)
		in.push_back(first);
	for (auto const* object : map->selectedObjects())
	{
		if (object != first)
			in.push_back(object);
	}
	ops::AlignParams params;
	params.edge = edge;
	applyOpResult(ops::align(*map, in, params));
}

// foom
void MapEditorController::distributeClicked()
{
	if (auto* action = qobject_cast<QAction*>(sender()))
		distribute(action->data().toBool());
}

// foom
void MapEditorController::distribute(bool horizontal)
{
	auto const& selection = map->selectedObjects();
	std::vector<const Object*> in(begin(selection), end(selection));
	ops::DistributeParams params;
	params.direction = horizontal ? ops::DistributeParams::Horizontal : ops::DistributeParams::Vertical;
	applyOpResult(ops::distribute(*map, in, params));
}

// foom
void MapEditorController::applyOpResult(ops::OpResult&& result)
{
	if (!result.error.isEmpty())
	{
		QMessageBox::warning(window, tr("Error"), result.error);
		return;
	}
	auto const warning = result.warning;
	ops::applyOp(*map, std::move(result));
	if (!warning.isEmpty())
		QMessageBox::information(window, tr("Skipped objects"), warning);
}

void MapEditorController::clipAreaClicked()
{
	setTool(new CutoutTool(this, clip_area_act, false));
}

void MapEditorController::eraseAreaClicked()
{
	setTool(new CutoutTool(this, erase_area_act, true));
}

void MapEditorController::distributePointsClicked()
{
	Q_ASSERT(activeSymbol()->getType() == Symbol::Point);
	PointSymbol* point = activeSymbol()->asPoint();
	
	DistributePointsTool::Settings settings;
	if (!DistributePointsTool::showSettingsDialog(window, point, settings))
		return;
	
	// Create points along paths
	std::vector<PointObject*> created_objects;
	for (const auto* object : map->selectedObjects())
	{
		if (object->getType() == Object::Path)
			DistributePointsTool::execute(object->asPath(), point, settings, created_objects);
	}
	if (created_objects.empty())
		return;
	
	// Add points to map
	for (auto* o : created_objects)
		map->addObject(o);
	
	// Create undo step and select new objects
	map->clearObjectSelection(false);
	MapPart* part = map->getCurrentPart();
	auto* delete_step = new DeleteObjectsUndoStep(map);
	for (std::size_t i = 0; i < created_objects.size(); ++i)
	{
		Object* object = created_objects[i];
		delete_step->addObject(part->findObjectIndex(object));
		map->addObjectToSelection(object, i == created_objects.size() - 1);
	}
	map->push(delete_step);
	map->setObjectsDirty();
}

void MapEditorController::addFloatingDockWidget(QDockWidget* dock_widget)
{
	if (!window->restoreDockWidget(dock_widget))
	{
		dock_widget->setFloating(true);
		// We must set the size from the sizeHint() ourselves,
		// otherwise QDockWidget may use the minimum size.
		QRect geometry(window->pos(), dock_widget->sizeHint());
		geometry.translate(window->width() - geometry.width(), window->centralWidget()->y());
		const int max_height = window->centralWidget()->height();
		if (geometry.height() > max_height)
			geometry.setHeight(max_height);
		dock_widget->setGeometry(geometry);
		connect(dock_widget, &QDockWidget::dockLocationChanged, this, &MapEditorController::setWindowStateChanged);
	}
}


void MapEditorController::enableGPSDisplay(bool enable)
{
	if (enable)
	{
		gps_display->startUpdates();
		
		// Create gps_track_recorder if we can determine a template track filename
		constexpr int gps_track_draw_update_interval = 10 * 1000; // in milliseconds
		if (! window->currentPath().isEmpty())
		{
			// Find or create a template for the track with a specific name
			QString gpx_file_path =
				QFileInfo(window->currentPath()).absoluteDir().canonicalPath()
				+ QLatin1Char('/')
				+ QFileInfo(window->currentPath()).completeBaseName()
				+ QLatin1String(" - GPS-")
				+ QDate::currentDate().toString(Qt::ISODate)
				+ QLatin1String(".gpx");
			
			bool new_template = true;  // Indicates that we really add a new template.
			TemplateTrack* track = nullptr;
			int template_index = 0;
			for ( ; template_index < map->getNumTemplates(); ++template_index)
			{
				auto* temp = map->getTemplate(template_index);
				if (temp->getTemplatePath() == gpx_file_path)
				{
					// There is a template for this track.
					new_template = false;
					track = qobject_cast<TemplateTrack*>(temp);
					break;
				}
			}
			
			// Derive new visibility from previous/default one.
			auto visibility = main_view->getTemplateVisibility(track);
			visibility.opacity = std::max(0.5, visibility.opacity);
			visibility.visible = true;
			
			if (!track)
			{
				if (!new_template)
				{
					// Need to replace the template at template_index
					map->deleteTemplate(template_index);
				}
				track = new TemplateTrack(gpx_file_path, map);
				// This will set the state to 'Loaded', so we need to reset it
				// to `Unloaded` to allow for loading the file when it becomes
				// visible for the first time.
				track->configureForGPSTrack();
				if (QFileInfo::exists(gpx_file_path))
				{
					track->unloadTemplateFile();
					track->loadTemplateFile();
				}
				map->addTemplate(template_index, std::unique_ptr<Template>{track});
				// When the map is saved, the new track must be saved even if it is empty.
				track->setHasUnsavedChanges(true);
			}
			
			main_view->setTemplateVisibility(track, visibility);
			
			gps_track_recorder = new GPSTrackRecorder(gps_display, track, gps_track_draw_update_interval, map_widget);
		}
	}
	else
	{
		gps_display->stopUpdates();
		
		delete gps_track_recorder;
		gps_track_recorder = nullptr;
	}
	gps_display->setVisible(enable);
	
	if (! enable)
	{
		gps_marker_display->stopPath();
		gps_temporary_path_act->setChecked(false);
	}
	
	gps_distance_rings_action->setEnabled(enable);
	gps_temporary_point_act->setEnabled(enable);
	gps_temporary_path_act->setEnabled(enable);
	updateDrawPointGPSAvailability();
	updateGpsRecordActions();  // foom
	
	// foom: the offset of the GPS position
	if (gps_set_offset_act)
	{
		gps_set_offset_act->setEnabled(enable);
		if (!enable && qobject_cast<GpsOffsetTool*>(current_tool))
			setEditTool();
		if (enable && gps_display->hasPositionOffset())
			showGpsOffsetMessage();
	}
}


// foom: Recording GPS positions into the path which is drawn.
// the design notes, "Recording into a symbol".

void MapEditorController::updateGpsRecordActions()
{
	if (!gps_record_act || !gps_display)
		return;
	auto const possible = gps_display->isVisible() && current_tool && current_tool->acceptsNumericVertices();
	if (!possible && gps_record_act->isChecked())
	{
		gps_record_act->setChecked(false);
		gpsRecordClicked(false);
	}
	if (!possible && gps_position_average)
	{
		// The averaging is given up without a vertex.
		gps_position_average.reset();
		gps_average_vertex_act->setChecked(false);
	}
	gps_record_act->setEnabled(possible);
	gps_add_vertex_act->setEnabled(possible);
	gps_average_vertex_act->setEnabled(possible);
	gps_record_pause_act->setEnabled(gps_record_act->isChecked());
	if (!gps_record_act->isChecked())
		gps_record_pause_act->setChecked(false);
}

void MapEditorController::gpsRecordClicked(bool checked)
{
	if (checked)
		gps_path_recorder = std::make_unique<GpsPathRecorder>(GpsRecordSettings::load().min_distance, double(map->getScaleDenominator()));
	else
		gps_path_recorder.reset();
	gps_record_pause_act->setChecked(false);
	gps_record_pause_act->setEnabled(checked);
}

void MapEditorController::gpsRecordPauseClicked(bool checked)
{
	if (gps_path_recorder)
		gps_path_recorder->setPaused(checked);
}

void MapEditorController::gpsRecordPosition(const MapCoordF& position, float accuracy)
{
	if (!current_tool || !current_tool->acceptsNumericVertices())
		return;
	if (gps_position_average)
	{
		// While positions are averaged for a vertex, none is recorded.
		gps_position_average->add(position, accuracy);
		window->showStatusBarMessage(tr("Averaging GPS positions: %1").arg(gps_position_average->count()), 5000);
		return;
	}
	if (!gps_path_recorder || !gps_record_act->isChecked())
		return;
	if (gps_path_recorder->accept(position))
		current_tool->acceptNumericVertex(position);
}

void MapEditorController::gpsAddVertexClicked()
{
	if (gps_display->hasValidPosition())
		gpsAddVertexAt(gps_display->getLatestGPSCoord());
	else
		window->showStatusBarMessage(tr("There is no GPS position which can be used."), 3000);
}

void MapEditorController::gpsAverageVertexClicked(bool checked)
{
	if (checked)
	{
		gps_position_average = std::make_unique<GpsPositionAverage>();
		return;
	}
	
	auto const average = std::move(gps_position_average);
	gps_position_average.reset();
	if (!average)
		return;
	if (average->isEmpty())
		window->showStatusBarMessage(tr("No GPS position arrived which can be used."), 3000);
	else
		gpsAddVertexAt(average->mean());
}

void MapEditorController::gpsAddVertexAt(const MapCoordF& position)
{
	if (!gps_add_vertex_act->isEnabled() || !current_tool || !current_tool->acceptsNumericVertices())
		return;
	if (gps_path_recorder)
		gps_path_recorder->take(position);
	current_tool->acceptNumericVertex(position);
}

// foom: The offset of the GPS position.
// the design notes, "Position offset and sentence log".

void MapEditorController::gpsSetOffsetClicked()
{
	setTool(new GpsOffsetTool(this, gps_set_offset_act));
}

bool MapEditorController::gpsSetOffsetAt(const MapCoordF& true_position)
{
	if (!gps_display || !gps_display->setTruePosition(true_position))
	{
		window->showStatusBarMessage(tr("There is no GPS position which can be used."), 3000);
		return false;
	}
	showGpsOffsetMessage();
	return true;
}

void MapEditorController::gpsClearOffsetClicked()
{
	gps_display->clearPositionOffset();
	window->showStatusBarMessage(tr("The GPS position offset is removed."), 3000);
}

void MapEditorController::showGpsOffsetMessage()
{
	auto const offset = gps_display->positionOffset();
	auto const metres = std::hypot(offset.x(), offset.y()) * double(map->getScaleDenominator()) / 1000.0;
	window->showStatusBarMessage(tr("GPS position offset: %1 m").arg(QString::number(metres, 'f', 1)), 5000);
}

void MapEditorController::enableGPSDistanceRings(bool enable)
{
	gps_display->enableDistanceRings(enable);
}

void MapEditorController::updateDrawPointGPSAvailability()
{
	const Symbol* symbol = activeSymbol();
	draw_point_gps_act->setEnabled(symbol && gps_display->isVisible() && symbol->getType() == Symbol::Point && !symbol->isHidden());
	move_to_gps_pos_act->setEnabled(gps_display->isVisible());
}

void MapEditorController::drawPointGPSClicked()
{
	setTool(new DrawPointGPSTool(gps_display, this, draw_point_gps_act));
}

void MapEditorController::gpsTemporaryPointClicked()
{
	if (gps_marker_display->addPoint())
		gps_temporary_clear_act->setEnabled(true);
}

void MapEditorController::gpsTemporaryPathClicked(bool enable)
{
	if (enable)
	{
		gps_marker_display->startPath();
		gps_temporary_clear_act->setEnabled(true);
	}
	else
		gps_marker_display->stopPath();
}

void MapEditorController::gpsTemporaryClearClicked()
{
	if (QMessageBox::question(window, tr("Clear temporary markers"), tr("Are you sure you want to delete all temporary GPS markers? This cannot be undone."), QMessageBox::Yes | QMessageBox::No) == QMessageBox::No)
		return;
	
	gps_marker_display->stopPath();
	gps_temporary_path_act->setChecked(false);
	gps_marker_display->clear();
	gps_temporary_clear_act->setEnabled(false);
}

void MapEditorController::enableCompassDisplay(bool enable)
{
	if (!compass_display || !show_top_bar_button || !top_action_bar)
	{
		Q_ASSERT(!"Prerequisites must be initialized");
	}
	else if (enable)
	{
		auto size = compass_display->sizeHint();
		if (top_action_bar->isVisible())
			compass_display->setGeometry(0, top_action_bar->size().height(), size.width(), size.height());
		else
			compass_display->setGeometry(show_top_bar_button->size().width(), 0, size.width(), size.height());
		connect(&Compass::getInstance(), &Compass::azimuthChanged, compass_display, &CompassDisplay::setAzimuth);
		compass_display->show();
	}
	else
	{
		disconnect(&Compass::getInstance(), &Compass::azimuthChanged, compass_display, &CompassDisplay::setAzimuth);
		compass_display->hide();
	}
}

void MapEditorController::alignMapWithNorth(bool enable)
{
	const int update_interval = 1000; // milliseconds
	
	if (enable)
	{
		Compass::getInstance().startUsage();
		gps_display->enableHeadingIndicator(true);
		
		connect(&align_map_with_north_timer, &QTimer::timeout, this, &MapEditorController::alignMapWithNorthUpdate);
		align_map_with_north_timer.start(update_interval);
		alignMapWithNorthUpdate();
	}
	else
	{
		align_map_with_north_timer.disconnect();
		align_map_with_north_timer.stop();
		
		gps_display->enableHeadingIndicator(false);
		Compass::getInstance().stopUsage();
		
		main_view->setRotation(0);
	}
}

void MapEditorController::alignMapWithNorthUpdate()
{
	// Time in milliseconds for which the rotation should not be updated after
	// the user interacted with the map widget
	const int interaction_time_threshold = 1500;
	if (map_widget->getTimeSinceLastInteraction() < interaction_time_threshold)
		return;
	
	// Set map rotation
	main_view->setRotation(M_PI / -180.0 * Compass::getInstance().getCurrentAzimuth());
}

void MapEditorController::hideTopActionBar()
{
	top_action_bar->hide();
	show_top_bar_button->show();
	show_top_bar_button->raise();
	
	compass_display->move(show_top_bar_button->size().width(), 0);
}

void MapEditorController::showTopActionBar()
{
	show_top_bar_button->hide();
	top_action_bar->show();
	top_action_bar->raise();
	
	compass_display->move(0, top_action_bar->size().height());
}

void MapEditorController::mobileSymbolSelectorClicked()
{
	// NOTE: this does not handle window resizes, however it is assumed that in
	// mobile mode there is no window, instead the application is running fullscreen.
	symbol_widget->setGeometry(window->rect());
	symbol_widget->raise();
	symbol_widget->show();
	connect(symbol_widget, &SymbolWidget::selectedSymbolsChanged, this, &MapEditorController::mobileSymbolSelectorFinished);
}

void MapEditorController::mobileSymbolSelectorFinished()
{
	disconnect(symbol_widget, &SymbolWidget::selectedSymbolsChanged, this, &MapEditorController::mobileSymbolSelectorFinished);
	symbol_widget->hide();
}

void MapEditorController::updateMapPartsUI()
{
	if (!mappart_selector_box)
	{
		mappart_selector_box = new QComboBox();
		mappart_selector_box->setToolTip(tr("Map parts"));
	}
	
	if (!mappart_merge_menu)
	{
		mappart_merge_menu = new QMenu();
		mappart_merge_menu->menuAction()->setMenuRole(QAction::NoRole);
		mappart_merge_menu->setTitle(tr("Merge this part with"));
	}
	
	if (!mappart_move_menu)
	{
		mappart_move_menu = new QMenu();
		mappart_move_menu->menuAction()->setMenuRole(QAction::NoRole);
		mappart_move_menu->setTitle(tr("Move selected objects to"));
	}
	
	const QSignalBlocker selector_box_blocker(mappart_selector_box);
	mappart_selector_box->clear();
	mappart_merge_menu->clear();
	mappart_move_menu->clear();
	
	const int count = map ? map->getNumParts() : 0;
	const bool have_multiple_parts = (count > 1);
	mappart_merge_menu->setEnabled(have_multiple_parts);
	mappart_move_menu->setEnabled(have_multiple_parts && map->getNumSelectedObjects() > 0);
	if (mappart_remove_act)
	{
		mappart_remove_act->setEnabled(have_multiple_parts);
		mappart_merge_act->setEnabled(have_multiple_parts);
	}
	if (toolbar_mapparts && !toolbar_mapparts->isVisible())
	{
		toolbar_mapparts->setVisible(have_multiple_parts);
	}
	
	if (count > 0)
	{
		const int current = map->getCurrentPartIndex();
		for (int i = 0; i < count; ++i)
		{
			QString part_name = map->getPart(i)->getName();
			mappart_selector_box->addItem(part_name);
			
			if (i != current)
			{
				auto* action = new QAction(part_name, this);
				mappart_merge_mapper->setMapping(action, i);
				connect(action, QOverload<bool>::of(&QAction::triggered), mappart_merge_mapper, QOverload<>::of(&QSignalMapper::map));
				mappart_merge_menu->addAction(action);
				
				action = new QAction(part_name, this);
				mappart_move_mapper->setMapping(action, i);
				connect(action, QOverload<bool>::of(&QAction::triggered), mappart_move_mapper, QOverload<>::of(&QSignalMapper::map));
				mappart_move_menu->addAction(action);
			}
		}
		
		mappart_selector_box->setCurrentIndex(current);
	}
	updateLayoutActions();  // foom
}

void MapEditorController::addMapPart()
{
	bool accepted = false;
	QString name = QInputDialog::getText(
	                   window,
	                   tr("Add new part..."),
	                   tr("Enter the name of the map part:"),
	                   QLineEdit::Normal,
	                   QString(),
	                   &accepted );
	if (accepted && !name.isEmpty())
	{
		auto* part = new MapPart(name, map);
		map->addPart(part, map->getCurrentPartIndex() + 1);
		map->setCurrentPart(part);
		map->push(new MapPartUndoStep(map, MapPartUndoStep::RemoveMapPart, part));
	}
}

void MapEditorController::removeMapPart()
{
	auto* part = map->getCurrentPart();
	
	QMessageBox::StandardButton button =
	        QMessageBox::question(
	            window,
                tr("Remove current part"),
                tr("Do you want to remove map part \"%1\" and all its objects?").arg(part->getName()),
                QMessageBox::Yes | QMessageBox::No );
	
	if (button == QMessageBox::Yes)
	{
		auto index = map->getCurrentPartIndex();
		UndoStep* undo_step = new MapPartUndoStep(map, MapPartUndoStep::AddMapPart, index);
		
		auto i = part->getNumObjects();
		if (i > 0)
		{
			auto* add_step = new AddObjectsUndoStep(map);
			do
			{
				--i;
				auto* object = part->getObject(i);
				add_step->addObject(i, object);
				part->releaseObject(object);
			}
			while (i > 0);
			
			auto* combined_step = new CombinedUndoStep(map);
			combined_step->push(add_step);
			combined_step->push(undo_step);
			undo_step = combined_step;
		}
		
		map->push(undo_step);
		map->removePart(index);
	}
}

void MapEditorController::renameMapPart()
{
	bool accepted = false;
	QString name =
	        QInputDialog::getText(
	            window,
	            tr("Rename current part..."),
	            tr("Enter the name of the map part:"),
	            QLineEdit::Normal,
	            map->getCurrentPart()->getName(),
	            &accepted );
	if (accepted && !name.isEmpty())
	{
		map->push(new MapPartUndoStep(map, MapPartUndoStep::ModifyMapPart, map->getCurrentPartIndex()));
		map->getCurrentPart()->setName(name);
	}
}

void MapEditorController::changeMapPart(int index)
{
	if (index >= 0)
	{
		map->setCurrentPartIndex(std::size_t(index));
		window->showStatusBarMessage(tr("Switched to map part '%1'.").arg(map->getCurrentPart()->getName()), 1000);
	}
}

void MapEditorController::reassignObjectsToMapPart(int target)
{
	auto current = map->getCurrentPartIndex();
	auto* current_part = map->getPart(current);
	std::vector<int> objects(map->selectedObjects().size());
	std::transform(map->selectedObjectsBegin(), map->selectedObjectsEnd(), begin(objects), [current_part](auto* object) {
		return current_part->findObjectIndex(object);
	});
	std::sort(objects.rbegin(), objects.rend());
	map->reassignObjectsToMapPart(begin(objects), end(objects), current, target);
	
	auto* undo = new SwitchPartUndoStep(map, target, current);
	for (auto i : objects)
		undo->addObject(i);
	map->push(undo);
}

void MapEditorController::mergeCurrentMapPartTo(int target)
{
	MapPart* const source_part = map->getCurrentPart();
	MapPart* const target_part = map->getPart(target);
	const QMessageBox::StandardButton button =
	        QMessageBox::question(
	            window,
                tr("Merge map parts"),
                tr("Do you want to move all objects from map part \"%1\" to \"%2\", "
	               "and to remove \"%1\"?")
	            .arg(source_part->getName(), target_part->getName()),
                QMessageBox::Yes | QMessageBox::No );
	
	if (button == QMessageBox::Yes)
	{
		// Beware that the source part is removed, and
		// the target part's index might change during merge.
		auto source = map->getCurrentPartIndex();
		UndoStep* add_part_step = new MapPartUndoStep(map, MapPartUndoStep::AddMapPart, source);
		
		auto first  = map->mergeParts(source, target);
		
		auto* switch_part_undo = new SwitchPartUndoStep(map, target, source);
		for (auto i = target_part->getNumObjects(); i > first; --i)
			switch_part_undo->addObject(0);
		
		auto* undo = new CombinedUndoStep(map);
		undo->push(switch_part_undo);
		undo->push(add_part_step);
		map->push(undo);
	}
}

void MapEditorController::mergeAllMapParts()
{
	QString const name = map->getCurrentPart()->getName();
	const QMessageBox::StandardButton button =
	        QMessageBox::question(
	            window,
                tr("Merge map parts"),
                tr("Do you want to move all objects to map part \"%1\", and to remove all other map parts?").arg(name),
                QMessageBox::Yes | QMessageBox::No );
	
	if (button == QMessageBox::Yes)
	{
		auto* undo = new CombinedUndoStep(map);
		
		// For simplicity, we merge to the first part,
		// but keep the properties (i.e. name) of the current part.
		map->setCurrentPartIndex(0);
		MapPart* target_part = map->getPart(0);
		
		for (auto i = map->getNumParts() - 1; i > 0; --i)
		{
			UndoStep* add_part_step = new MapPartUndoStep(map, MapPartUndoStep::AddMapPart, i);
			auto first = map->mergeParts(i, 0);
			auto* switch_part_undo = new SwitchPartUndoStep(map, 0, i);
			for (auto j = target_part->getNumObjects(); j > first; --j)
				switch_part_undo->addObject(0);
			undo->push(switch_part_undo);
			undo->push(add_part_step);
		}
		
		undo->push(new MapPartUndoStep(map, MapPartUndoStep::ModifyMapPart, 0));
		target_part->setName(name);
		
		map->push(undo);
	}
}


void MapEditorController::templateAdded(int /*pos*/, const Template* /*temp*/)
{
	if (map->getNumTemplates() == 1)
		templateAvailabilityChanged();
}

void MapEditorController::templateDeleted(int /*pos*/, const Template* /*temp*/)
{
	if (map->getNumTemplates() == 0)
		templateAvailabilityChanged();
}

void MapEditorController::setMapAndView(Map* map, MapView* map_view)
{
	Q_ASSERT(map);
	Q_ASSERT(map_view);
#ifdef __clang_analyzer__
	if (!map || !map_view) { return; }
#endif
	
	if (this->map)
	{
		this->map->disconnect(this);
		if (symbol_widget)
		{
			this->map->disconnect(symbol_widget);
		}
	}
	
	this->map = map;
	this->main_view = map_view;
	updateMapPartsUI();
	connect(map_view, &MapView::viewChanged, this, &MapEditorController::viewChangedForHistory);  // foom
	
	connect(&map->undoManager(), &UndoManager::canRedoChanged, this, &MapEditorController::undoStepAvailabilityChanged);
	connect(&map->undoManager(), &UndoManager::canUndoChanged, this, &MapEditorController::undoStepAvailabilityChanged);
	connect(map, &Map::objectSelectionChanged, this, &MapEditorController::objectSelectionChanged);
	// foom: the design notes, "Course dock"
	connect(map, &Map::courseObjectsRemovedFromCourses, this, [this](const QStringList& codes, const QStringList& courses) {
		window->showStatusBarMessage(tr("Removed from the courses %1: %2. Undo brings it back.").arg(courses.join(QLatin1String(", ")), codes.join(QLatin1String(", "))), 10000);
	});
	connect(map, &Map::templateAdded, this, &MapEditorController::templateAdded);
	connect(map, &Map::templateDeleted, this, &MapEditorController::templateDeleted);
	connect(map, &Map::closedTemplateAvailabilityChanged, this, &MapEditorController::closedTemplateAvailabilityChanged);
	connect(map, &Map::spotColorPresenceChanged, this, &MapEditorController::spotColorPresenceChanged);
	connect(map, &Map::currentMapPartChanged, this, &MapEditorController::updateMapPartsUI);
	connect(map, &Map::mapPartAdded, this, &MapEditorController::updateMapPartsUI);
	connect(map, &Map::mapPartChanged, this, &MapEditorController::updateMapPartsUI);
	connect(map, &Map::mapPartDeleted, this, &MapEditorController::updateMapPartsUI);
	
	if (symbol_widget)
	{
		delete symbol_widget;
		symbol_widget = nullptr;
		createSymbolWidget();
	}
	
	if (window)
	{
		updateWidgets();
	}
}

void MapEditorController::updateWidgets()
{
	undoStepAvailabilityChanged();
	objectSelectionChanged();
	if (mode == MapEditor)
	{
		if (main_view)
		{
			show_grid_act->setChecked(main_view->isGridVisible());
			hide_all_templates_act->setChecked(main_view->areAllTemplatesHidden());
			favourite_templates_act->setChecked(main_view->areFavouriteTemplatesOnly());  // foom
			overprinting_simulation_act->setChecked(main_view->isOverprintingSimulationEnabled());
		}
		if (map)
		{
			hatch_areas_view_act->setChecked(map->isAreaHatchingEnabled());
			baseline_view_act->setChecked(map->isBaselineViewEnabled());
			closedTemplateAvailabilityChanged();
			spotColorPresenceChanged(map->hasSpotColors());
			updateMapPartsUI();
		}
	}
}

void MapEditorController::createSymbolWidget(QWidget* parent)
{
	if (!symbol_widget)
	{
		symbol_widget = new SymbolWidget(map, mobile_mode, parent);
		connect(symbol_widget, &SymbolWidget::switchSymbolClicked, this, &MapEditorController::switchSymbolClicked);
		connect(symbol_widget, &SymbolWidget::fillBorderClicked, this, &MapEditorController::fillBorderClicked);
		connect(symbol_widget, &SymbolWidget::selectObjectsClicked, this, &MapEditorController::selectObjectsClicked);
		connect(symbol_widget, &SymbolWidget::deselectObjectsClicked, this, &MapEditorController::deselectObjectsClicked);
		connect(symbol_widget, &SymbolWidget::selectedSymbolsChanged, this, &MapEditorController::selectedSymbolsChanged);
		if (symbol_dock_widget)
		{
			Q_ASSERT(!parent);
			symbol_dock_widget->setWidget(symbol_widget);
		}
		
		selectedSymbolsChanged();
	}
}

void MapEditorController::importClicked()
{
	QSettings settings;
	QString import_directory = settings.value(QString::fromLatin1("importFileDirectory"), QDir::homePath()).toString();
	
	QStringList map_extensions;
	for (auto* format : FileFormats.formats())
	{
		if (format->supportsReading())
			map_extensions.append(format->fileExtensions());
	}
	map_extensions.push_back(QLatin1String("gpx"));
	map_extensions.sort(Qt::CaseInsensitive);
	map_extensions.removeDuplicates();
	
	QString filename = FileDialog::getOpenFileName(
	                       window,
	                       tr("Import..."),
	                       import_directory,
	                       QString::fromLatin1("%1 (%2);;%3 (*.*)").arg(
	                           tr("Importable files"), QLatin1String("*.") + map_extensions.join(QString::fromLatin1(" *.")), tr("All files")) );
	if (filename.isEmpty())
		return;
	
	settings.setValue(QString::fromLatin1("importFileDirectory"), QFileInfo(filename).canonicalPath());
	
	/**
	 * Finding the most appropriate import function in the following way:
	 * - If the extensions is ".gpx", the (default) import is via TemplateTrack.
	 * - If the format is understood by OgrFileFormat, it is handled by
	 *   OgrTemplate import. Note that the OgrTemplate import may also handle
	 *   GPX, but it is left to the OgrFileFormat (user configuration) whether
	 *   it wants to report its support for the GPX format.
	 * - Every other recognized map file is imported regularly.
	 */
	char const* format_id = nullptr;
	if (filename.endsWith(QLatin1String(".gpx"), Qt::CaseInsensitive))
		format_id = "GPX";
	
	auto* map_format = FileFormats.findFormatForFilename(filename, &FileFormat::supportsFileImport);
	if (map_format)
		format_id = map_format->id();
	
	if (qstrcmp(format_id, "OGR") == 0)
	{
#ifdef MAPPER_USE_GDAL
		importGeodataFiles({ filename });  // foom: the import options
#else
		importOgrFile(filename);
#endif
		return;
	}
	
	if (qstrcmp(format_id, "GPX") == 0)
	{
		// Legacy GPX file import
		importGpxFile(filename);
		return; // Error reporting in Track::import()
	}
	
	if (format_id)
	{
		importMapFile(filename, false);
		return;
	}
	
	QMessageBox::critical(window, tr("Error"), tr("Cannot import the selected file because its file format is not supported."));
}

bool MapEditorController::importGpxFile(const QString& filename)
{
	Map imported_map;
	imported_map.setGeoreferencing(map->getGeoreferencing());
	
	TemplateTrack temp(filename, &imported_map);
	if (!temp.setupAndLoad(window, main_view) || !temp.import(window))
		return false;
	
	return importMapWithReplacement(imported_map, Map::MinimalObjectImport | Map::GeorefImport, filename);
}

bool MapEditorController::importMapFile(const QString& filename, bool show_errors)
{
	Map imported_map;
	imported_map.setScaleDenominator(map->getScaleDenominator()); // for non-scaled geodata
	
	auto importer = FileFormats.makeImporter(filename, imported_map, nullptr);
	if (!importer)
	{
		if (show_errors)
		{}  /// \todo error message
		return false;
	}
	
	if (!importer->doImport())
	{
		if (show_errors)
			QMessageBox::warning(window, tr("Error"), importer->warnings().back());
		return false;
	}
	
	if (show_errors && !importer->warnings().empty())
	    MainWindow::showMessageBox(window, tr("Warning"), tr("The map import generated warnings."), importer->warnings());
	
	return importMapWithReplacement(imported_map, Map::MinimalObjectImport | Map::GeorefImport, filename);
}

bool MapEditorController::importOgrFile(const QString& filename)
{
#if MAPPER_USE_GDAL
	OgrTemplate ogr_template {filename, map};
	if (!ogr_template.setupAndLoad(window, main_view))
		return false;
	
	auto template_map = ogr_template.takeTemplateMap();
	if (Q_UNLIKELY(!template_map))
		return false;
	
	TemplateTransform transform;
	if (!ogr_template.isTemplateGeoreferenced())
	{
		ogr_template.getTransform(transform);
		template_map->applyOnAllObjects(transform.makeObjectTransform());
		template_map->setGeoreferencing(map->getGeoreferencing());
	}
	auto template_scale = (transform.template_scale_x + transform.template_scale_y) / 2;
	template_scale *= qreal(template_map->getScaleDenominator()) / map->getScaleDenominator();
	if (!qFuzzyCompare(template_scale, 1))
	{
		template_map->scaleAllSymbols(template_scale);
	}
	
	return importMapWithReplacement(*template_map, Map::MinimalObjectImport | Map::GeorefImport, filename);
#else
	return false;
#endif
}


// foom
void MapEditorController::importGeodataClicked()
{
#ifdef MAPPER_USE_GDAL
	// the design notes, "Import options".
	QSettings settings;
	auto const import_directory = settings.value(QString::fromLatin1("importFileDirectory"), QDir::homePath()).toString();
	auto const* format = FileFormats.findFormat("OGR");
	if (!format)
		return;
	auto extensions = format->fileExtensions();
	extensions.sort(Qt::CaseInsensitive);
	QString const filter = tr("Geodata") + QLatin1String(" (*.") + extensions.join(QLatin1String(" *.")) + QLatin1String(");;") + tr("All files") + QLatin1String(" (*.*)");
	auto const filenames = QFileDialog::getOpenFileNames(window, tr("Import geodata"), import_directory, filter);
	if (filenames.isEmpty())
		return;
	settings.setValue(QString::fromLatin1("importFileDirectory"), QFileInfo(filenames.front()).canonicalPath());
	importGeodataFiles(filenames);
#endif
}

// foom
void MapEditorController::convertToGraphicClicked()
{
	auto const count = convertToGraphic(*map);
	window->showStatusBarMessage(tr("Converted %n object(s) to graphic objects.", nullptr, count), 2000);
	updateLayoutActions();
}

void MapEditorController::objectStyleClicked()
{
	auto const index = map->findSymbolIndex(sharedPrivateSymbol(*map));
	if (index < 0)
		return;
	SymbolSettingDialog dialog(map->getSymbol(index), map, window);
	dialog.setWindowModality(Qt::WindowModal);
	if (dialog.exec() == QDialog::Accepted)
	{
		auto symbol = dialog.getNewSymbol();
		symbol->setPrivate(true);
		map->setSymbol(symbol.release(), index);
	}
}

void MapEditorController::addImageClicked()
{
	QSettings settings;
	auto const directory = settings.value(QString::fromLatin1("importFileDirectory"), QDir::homePath()).toString();
	QString const filter = tr("Images") + QLatin1String(" (*.png *.jpg *.jpeg);;") + tr("All files") + QLatin1String(" (*.*)");
	auto const filename = QFileDialog::getOpenFileName(window, tr("Add image"), directory, filter);
	if (filename.isEmpty())
		return;
	settings.setValue(QString::fromLatin1("importFileDirectory"), QFileInfo(filename).canonicalPath());
	
	QMessageBox question(QMessageBox::Question, tr("Add image"),
	                     tr("Embed the image in the map file, or link to the image file?"),
	                     QMessageBox::Cancel, window);
	auto* embed_button = question.addButton(tr("Embed"), QMessageBox::AcceptRole);
	auto* link_button = question.addButton(tr("Link"), QMessageBox::ActionRole);
	question.setDefaultButton(embed_button);
	question.exec();
	if (question.clickedButton() != embed_button && question.clickedButton() != link_button)
		return;
	
	auto const view_rect = main_view->calculateViewedRect(map_widget->viewportToView(map_widget->rect()));
	QString error;
	if (!addImage(*map, filename, question.clickedButton() == embed_button, MapCoordF(view_rect.center()), view_rect.size() / 2, &error))
		QMessageBox::warning(window, tr("Add image"), error);
}

void MapEditorController::generateScaleBarClicked()
{
	runGeneratorDialog(QStringLiteral("gen-scale-bar"));
}

void MapEditorController::generateNorthLinesClicked()
{
	runGeneratorDialog(QStringLiteral("gen-north-lines"));
}

void MapEditorController::generateLegendClicked()
{
	runGeneratorDialog(QStringLiteral("gen-legend"));
}

void MapEditorController::generateClicked()
{
	if (auto const* action = qobject_cast<QAction*>(sender()))
		runGeneratorDialog(action->data().toString());
}

void MapEditorController::runGeneratorDialog(const QString& id)
{
	GeneratorDialog dialog(*map, id, defaultGeneratorParams(id, *map), window);
	dialog.setWindowModality(Qt::WindowModal);
	if (dialog.exec() != QDialog::Accepted)
		return;
	placeGenerator(id, dialog.params());
}

bool MapEditorController::placeGenerator(const QString& id, const QJsonObject& params)
{
	// the design notes, "Generators", third change
	if (generatorIsPlaced(id))
	{
		QString error;
		auto const preview = generatorPreview(*map, id, params, &error);
		if (!preview.isValid() && !error.isEmpty())
		{
			QMessageBox::warning(window, generatorTitle(id), error);
			return false;
		}
		setTool(new GeneratorPlaceTool(this, id, params, preview));
		return true;
	}
	auto const view_rect = main_view->calculateViewedRect(map_widget->viewportToView(map_widget->rect()));
	return placeGeneratorAt(id, params, MapCoordF(view_rect.center()));
}

bool MapEditorController::placeGeneratorAt(const QString& id, const QJsonObject& params, const MapCoordF& anchor)
{
	QString error;
	auto const group = runGenerator(*map, id, params, anchor, &error);
	if (group == 0)
	{
		QMessageBox::warning(window, generatorTitle(id), error);
		return false;
	}
	
	// Show the result selected, in its part, ready to be dragged.
	auto part_index = -1;
	auto const objects = groupObjects(*map, group, &part_index);
	if (part_index >= 0)
	{
		map->setCurrentPartIndex(std::size_t(part_index));
		map->clearObjectSelection(false);
		for (auto* object : objects)
			map->addObjectToSelection(object, false);
		map->emitSelectionChanged();
	}
	window->showStatusBarMessage(tr("%1: %n object(s).", nullptr, int(objects.size())).arg(generatorTitle(id)), 3000);
	return true;
}

void MapEditorController::regenerateClicked()
{
	QString error;
	auto const count = regenerateGroups(*map, generatedGroups(*map, true), &error);
	if (count == 0 && !error.isEmpty())
		QMessageBox::warning(window, tr("Regenerate"), error);
	else
		window->showStatusBarMessage(tr("Rebuilt %n group(s).", nullptr, count), 2000);
}

void MapEditorController::imagePropertiesClicked()
{
	auto const* image = firstSelectedImage(*map);
	if (!image)
		return;
	ImagePropertiesDialog dialog(*image, window);
	dialog.setWindowModality(Qt::WindowModal);
	if (dialog.exec() == QDialog::Accepted)
		setSelectedImageProperties(*map, dialog.opacity(), dialog.layer());
}

void MapEditorController::embedImageClicked()
{
	auto const count = embedSelectedImages(*map);
	window->showStatusBarMessage(tr("Embedded %n image(s).", nullptr, count), 2000);
	updateLayoutActions();
}

void MapEditorController::createLayoutClicked()
{
	addLayoutPart(*map);
}

void MapEditorController::hideLayoutClicked(bool hide)
{
	if (setLayoutVisible(*map, !hide))
		map->updateAllMapWidgets();
	updateLayoutActions();
}

void MapEditorController::moveToLayoutClicked()
{
	moveSelectionToPart(*map, layoutPartIndex(*map));
}

void MapEditorController::moveToMapClicked()
{
	moveSelectionToPart(*map, mapContentPartIndex(*map));
}

// foom: the design notes, "Merge changes from file"
static bool isNativeMapFile(const QString& path)
{
	auto const suffix = QFileInfo(path).suffix().toLower();
	return suffix == QLatin1String("foom") || suffix == QLatin1String("omap") || suffix == QLatin1String("xmap");
}

void MapEditorController::fieldCopyCreateClicked()
{
	auto const map_path = window->currentPath();
	if (!isNativeMapFile(map_path))
	{
		QMessageBox::information(window, tr("Create field copy"), tr("A field copy needs a map which is saved as a .foom, .omap or .xmap file. Save the map first."));
		return;
	}
	auto const visible = map_widget->getMapView()->calculateViewedRect(map_widget->viewportToView(map_widget->rect()));
	QRectF selected;
	map->includeSelectionRect(selected);
	FieldCopyDialog options_dialog(visible, selected, window);
	if (options_dialog.exec() != QDialog::Accepted)
		return;
	const QFileInfo map_file(map_path);
	QString suggestion = map_file.path();
	suggestion += QLatin1Char('/');
	suggestion += map_file.completeBaseName();
	suggestion += QLatin1String("-field.foom");
	QString filter = tr("Map files");
	filter += QLatin1String(" (*.foom *.omap *.xmap)");
	auto filename = QFileDialog::getSaveFileName(window, tr("Create field copy"), suggestion, filter);
	if (filename.isEmpty())
		return;
	if (QFileInfo(filename).suffix().isEmpty())
		filename += QLatin1String(".foom");
	
	QString error;
	auto const id = createFieldCopy(*map, map_path, filename, options_dialog.options(), &error);
	if (id.isEmpty())
	{
		QMessageBox::warning(window, tr("Create field copy"), error);
		return;
	}
	if (!window->save() || map->hasUnsavedChanges())
		QMessageBox::warning(window, tr("Create field copy"), tr("The field copy is written, but this map could not be saved with its record of the copy. Save the map before you close it."));
	else
		window->showStatusBarMessage(tr("Field copy written to %1").arg(filename), 4000);
}

void MapEditorController::mergeFromFileClicked()
{
	auto const map_path = window->currentPath();
	if (!isNativeMapFile(map_path) || map->syncCopies().empty())
	{
		QMessageBox::information(window, tr("Merge changes"), tr("This map has no open field copy. Make one with File > Field copy > Create field copy."));
		return;
	}
	QString filter = tr("Map files");
	filter += QLatin1String(" (*.foom *.omap *.xmap);;");
	filter += tr("All files");
	filter += QLatin1String(" (*.*)");
	auto const filename = QFileDialog::getOpenFileName(window, tr("Merge changes: the returned field copy"), QFileInfo(map_path).path(), filter);
	if (filename.isEmpty())
		return;
	
	QString error;
	MergeInput input;
	auto loaded = loadForMerge(*map, map_path, filename, input, &error);
	if (!loaded && input.other_frame
	    && QMessageBox::question(window, tr("Merge changes"), error + QLatin1String("\n\n") + tr("Transform the objects of the copy?")) == QMessageBox::Yes)
		loaded = loadForMerge(*map, map_path, filename, input, &error, true);
	if (!loaded)
	{
		if (!input.other_frame)
			QMessageBox::warning(window, tr("Merge changes"), error);
		return;
	}
	auto plan = compareMaps(input.base.get(), *map, *input.theirs);
	auto const decisions_path = decisionsFilePath(map_path, input.copy.id);
	loadDecisions(decisions_path, plan);
	auto const differs = !plan.rows.empty() || !plan.symbols.empty() || !plan.colors.empty();
	
	if (differs)
	{
		MergeDialog dialog(*map, *input.theirs, plan, window);
		connect(&dialog, &MergeDialog::showExtent, this, [this](const QRectF& extent) {
			map_widget->ensureVisibilityOfRect(extent, MapWidget::ContinuousZoom);
		});
		// Their version over the map, in a contrast colour, for the selected row.
		auto* overlay = new MergeOverlayActivity(*map);
		setEditorActivity(overlay);
		connect(&dialog, &MergeDialog::showVersions, overlay, [overlay, &input](const QUuid& uuid) {
			overlay->setObject(input.theirs->findObject(uuid));
		});
		auto const code = dialog.exec();
		if (getEditorActivity() == overlay)
			setEditorActivity(nullptr);
		if (code == MergeDialog::Paused)
		{
			if (!saveDecisions(decisions_path, dialog.plan(), &error))
				QMessageBox::warning(window, tr("Merge changes"), error);
			else
				window->showStatusBarMessage(tr("The decisions are kept. Merge the same copy again to go on."), 4000);
			return;
		}
		if (code != QDialog::Accepted)
			return;
		MergeResult result;
		if (!applyMerge(*map, *input.theirs, dialog.plan(), &result, &error))
		{
			QMessageBox::warning(window, tr("Merge changes"), error);
			return;
		}
		QFile::remove(decisions_path);
		map_widget->update();
	}
	
	// The base only after the map is saved: a base ahead of an unsaved merge would hide the merge.
	if (!window->save() || map->hasUnsavedChanges())
	{
		QMessageBox::warning(window, tr("Merge changes"), tr("The changes are merged, but the map could not be saved, so the base snapshot was left as it is. Save the map and merge the same copy again: that merge will find nothing new and bring the base up to date."));
		return;
	}
	if (!refreshBase(map_path, filename, input.copy, &error))
		QMessageBox::warning(window, tr("Merge changes"), error);
	else if (!differs)
		QMessageBox::information(window, tr("Merge changes"), tr("The copy holds nothing which differs from this map."));
	else
		window->showStatusBarMessage(tr("Changes merged."), 4000);
}

void MapEditorController::fieldCopyCloseClicked()
{
	auto const map_path = window->currentPath();
	auto const& copies = map->syncCopies();
	if (!isNativeMapFile(map_path) || copies.empty())
	{
		QMessageBox::information(window, tr("Close field copy"), tr("This map has no open field copy."));
		return;
	}
	auto id = copies.front().id;
	auto created = copies.front().created;
	if (copies.size() > 1)
	{
		QStringList items;
		for (auto const& copy : copies)
		{
			QString item = QDateTime::fromSecsSinceEpoch(copy.created).toString(Qt::ISODate);
			item += QLatin1String("  ");
			item += copy.id;
			items.append(item);
		}
		auto ok = false;
		auto const chosen = QInputDialog::getItem(window, tr("Close field copy"), tr("Field copy:"), items, 0, false, &ok);
		auto const index = items.indexOf(chosen);
		if (!ok || index < 0)
			return;
		id = copies[std::size_t(index)].id;
		created = copies[std::size_t(index)].created;
	}
	auto const question = tr("Close the field copy made on %1? Its base snapshot is deleted, and changes in that copy can then only be merged as conflicts, one by one.")
	                      .arg(QDateTime::fromSecsSinceEpoch(created).toString(Qt::ISODate));
	if (QMessageBox::question(window, tr("Close field copy"), question, QMessageBox::Ok | QMessageBox::Cancel) != QMessageBox::Ok)
		return;
	
	QString error;
	if (!closeFieldCopy(*map, map_path, id, &error))
	{
		QMessageBox::warning(window, tr("Close field copy"), error);
		return;
	}
	window->save();
}

void MapEditorController::saveLayoutClicked()
{
	QSettings settings;
	auto const directory = settings.value(QString::fromLatin1("importFileDirectory"), QDir::homePath()).toString();
	QString const filter = tr("Map files") + QLatin1String(" (*.foom *.omap *.xmap)");
	auto filename = QFileDialog::getSaveFileName(window, tr("Save layout"), directory, filter);
	if (filename.isEmpty())
		return;
	if (QFileInfo(filename).suffix().isEmpty())
		filename += QLatin1String(".foom");
	settings.setValue(QString::fromLatin1("importFileDirectory"), QFileInfo(filename).absolutePath());  // the file does not exist yet
	QString error;
	if (!saveLayout(*map, filename, &error))
		QMessageBox::warning(window, tr("Save layout"), error);
}

void MapEditorController::importLayoutClicked()
{
	QSettings settings;
	auto const directory = settings.value(QString::fromLatin1("importFileDirectory"), QDir::homePath()).toString();
	QString const filter = tr("Map files") + QLatin1String(" (*.foom *.omap *.xmap);;") + tr("All files") + QLatin1String(" (*.*)");
	auto const filename = QFileDialog::getOpenFileName(window, tr("Import layout"), directory, filter);
	if (filename.isEmpty())
		return;
	settings.setValue(QString::fromLatin1("importFileDirectory"), QFileInfo(filename).canonicalPath());
	QString error;
	auto const count = importLayoutFile(*map, filename, &error);
	if (count < 0)
		QMessageBox::warning(window, tr("Import layout"), error);
	else
		window->showStatusBarMessage(tr("Imported %n layout object(s).", nullptr, count), 2000);
}

void MapEditorController::updateLayoutActions()
{
	if (!layout_create_act || !map)
		return;
	auto const layout = layoutPartIndex(*map);
	auto const current = int(map->getCurrentPartIndex());
	auto const have_selection = map->getNumSelectedObjects() > 0;
	layout_create_act->setEnabled(!editing_in_progress && layout < 0);
	layout_hide_act->setEnabled(layout >= 0);
	{
		const QSignalBlocker blocker(layout_hide_act);
		layout_hide_act->setChecked(layout >= 0 && !map->getPart(std::size_t(layout))->isVisible());
	}
	to_layout_act->setEnabled(!editing_in_progress && have_selection && layout >= 0 && current != layout);
	to_map_act->setEnabled(!editing_in_progress && have_selection && layout >= 0 && current == layout && mapContentPartIndex(*map) >= 0);
	layout_save_act->setEnabled(layout >= 0);
	to_graphic_act->setEnabled(!editing_in_progress && have_selection);
	image_add_act->setEnabled(!editing_in_progress);
	image_embed_act->setEnabled(!editing_in_progress && have_selection && isLinkedImageSelected(*map));
	image_properties_act->setEnabled(!editing_in_progress && have_selection && firstSelectedImage(*map));
	gen_scale_bar_act->setEnabled(!editing_in_progress);
	gen_north_lines_act->setEnabled(!editing_in_progress);
	gen_legend_act->setEnabled(!editing_in_progress);
	for (auto* action : more_generator_acts)
		action->setEnabled(!editing_in_progress);
	regenerate_act->setEnabled(!editing_in_progress && have_selection && !generatedGroups(*map, true).empty());
	object_style_act->setEnabled(!editing_in_progress && have_selection && sharedPrivateSymbol(*map));
	layout_import_act->setEnabled(!editing_in_progress);
}

void MapEditorController::importPointsClicked()
{
	// the design notes, "Import options", Delimited text.
	QSettings settings;
	auto const import_directory = settings.value(QString::fromLatin1("importFileDirectory"), QDir::homePath()).toString();
	QString const filter = tr("Delimited text") + QLatin1String(" (*.csv *.txt *.tsv *.xyz);;") + tr("All files") + QLatin1String(" (*.*)");
	auto const filename = QFileDialog::getOpenFileName(window, tr("Import points"), import_directory, filter);
	if (filename.isEmpty())
		return;
	settings.setValue(QString::fromLatin1("importFileDirectory"), QFileInfo(filename).canonicalPath());
	
	QFile file(filename);
	if (!file.open(QIODevice::ReadOnly))
	{
		QMessageBox::warning(window, tr("Import points"), tr("Cannot open file:\n%1\n\n%2").arg(filename, file.errorString()));
		return;
	}
	auto const text = QString::fromUtf8(file.readAll());
	
	ImportPointsDialog dialog(text, *map, activeSymbol(), window);
	if (dialog.exec() != QDialog::Accepted)
		return;
	auto result = ops::importPoints(*map, text, dialog.params());
	auto const warning = result.warning;
	result.warning.clear();
	auto const objects = int(result.out.size());
	auto const failed = !result.error.isEmpty();
	applyOpResult(std::move(result));
	if (failed)
		return;
	if (!warning.isEmpty())
		QMessageBox::information(window, tr("Import points"), warning);
	window->showStatusBarMessage(tr("Imported %n object(s).", nullptr, objects), 4000);
}

// foom
bool MapEditorController::importGeodataFiles(const QStringList& filenames)
{
#ifdef MAPPER_USE_GDAL
	auto const view_rect = main_view->calculateViewedRect(map_widget->viewportToView(map_widget->rect()));
	GeodataImportDialog dialog(filenames, *map, view_rect, window);
	if (dialog.exec() != QDialog::Accepted)
		return false;
	
	auto const result = importGeodata(*map, filenames, dialog.options());
	if (!result.ok)
	{
		QMessageBox::warning(window, tr("Import geodata"), result.error);
		return false;
	}
	if (!result.warnings.isEmpty())
		MainWindow::showMessageBox(window, tr("Warning"), tr("The import generated warnings."), std::vector<QString>(result.warnings.begin(), result.warnings.end()));
	window->showStatusBarMessage(tr("Imported %n object(s).", nullptr, result.objects), 4000);
	return true;
#else
	Q_UNUSED(filenames)
	return false;
#endif
}


bool MapEditorController::importMapWithReplacement(
        Map& imported_map,
        Map::ImportMode mode,
        const QString& crt_file_hint)
{
	if (!SymbolReplacement(imported_map, *map).withAutoCrtFile(window, crt_file_hint))
	{
		auto choice = QMessageBox::question(window,
		                                    ::OpenOrienteering::Map::tr("Import..."),
		                                    ::OpenOrienteering::Map::tr("Symbol replacement was canceled.\n"
		                                                                "Import the data anyway?"),
		                                    QMessageBox::Yes | QMessageBox::No,
		                                    QMessageBox::No);
		if (choice == QMessageBox::No)
			return false;
	}
	
	importMap(imported_map, mode, window);
	return true;
}


QHash<const Symbol*, Symbol*> MapEditorController::importMap(
        Map& other,
        Map::ImportMode mode,
        QWidget* dialog_parent,
        std::vector<bool>* filter,
        int symbol_insert_pos,
        bool merge_duplicate_symbols)
{
	// Check if there is something to import
	if (other.getNumColors() == 0
	    && other.getNumSymbols() == 0
	    && other.getNumObjects() == 0)
	{
		QMessageBox::critical(dialog_parent, tr("Error"), tr("Nothing to import."));
		return {};
	}
	
	// Check scale
	if ((mode & 0x0f) != Map::ColorImport
	    && other.getNumSymbols() > 0
	    && other.getScaleDenominator() != map->getScaleDenominator())
	{
		/// \todo Move message to this context.
		int answer = QMessageBox::question(
		                 dialog_parent,
		                 tr("Question"),
		                 tr("The scale of the imported data is 1:%1 "
		                    "which is different from this map's scale of 1:%2.\n\n"
		                    "Rescale the imported data?")
		                 .arg(QLocale().toString(other.getScaleDenominator()),
		                      QLocale().toString(map->getScaleDenominator())),
		                 QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel, QMessageBox::Yes);
		if (answer == QMessageBox::Yes)
		{
			other.changeScale(map->getScaleDenominator(), 1.0, MapCoord(0, 0), true, true, true, true);
		}
	}
	
	return map->importMap(other, mode, filter, symbol_insert_pos, merge_duplicate_symbols);
}



// slot
void MapEditorController::setViewOptionsEnabled(bool enabled)
{
	pan_act->setEnabled(enabled);
	box_zoom_act->setEnabled(enabled);
	show_grid_act->setEnabled(enabled);
// 	hatch_areas_view_act->setEnabled(enabled);
// 	baseline_view_act->setEnabled(enabled);
	overprinting_simulation_act->setEnabled(enabled);
	hide_all_templates_act->setEnabled(enabled);
	favourite_templates_act->setEnabled(enabled);  // foom
}


// ### EditorDockWidget ###

EditorDockWidget::EditorDockWidget(const QString& title, QAction* action, MapEditorController* editor, QWidget* parent)
: QDockWidget(title, parent)
, action(action)
, editor(editor)
{
	if (editor)
	{
		connect(this, &EditorDockWidget::dockLocationChanged, editor, &MapEditorController::setWindowStateChanged);
	}
	
	if (action)
	{
		connect(toggleViewAction(), &QAction::toggled, action, &QAction::setChecked);
	}
	
#ifdef Q_OS_ANDROID
	size_grip = new QSizeGrip(this);
	size_grip->resize(size_grip->sizeHint());
	size_grip->setVisible(isTopLevel());
	connect(this, &QDockWidget::topLevelChanged, size_grip, &QWidget::setVisible);
#endif
}

bool EditorDockWidget::event(QEvent* event)
{
	switch (event->type())
	{
	case QEvent::ShortcutOverride:
		if (editor->getWindow()->shortcutsBlocked())
			event->accept();
		break;
		
	case QEvent::Show:
		QTimer::singleShot(0, this, &QWidget::raise);
		break;
		
	default:
		; // nothing
	}

	return QDockWidget::event(event);
}

void EditorDockWidget::resizeEvent(QResizeEvent* event)
{
#ifdef Q_OS_ANDROID
	int fw = style()->pixelMetric(QStyle::PM_DockWidgetFrameWidth, 0, this);
	size_grip->move(rect().bottomRight() - size_grip->rect().bottomRight() - QPoint{fw, fw});
	size_grip->raise();
	
	if (isTopLevel())
	{
		auto const frame = frameGeometry();
		auto const old_frame = QRect{pos(), event->oldSize() + frame.size() - size()};
		if (old_frame.united(frame) != frame)
			editor->getMainWidget()->window()->update();
	}
#endif
	
	QDockWidget::resizeEvent(event);
}



// ### MapEditorToolAction ###

MapEditorToolAction::MapEditorToolAction(const QIcon& icon, const QString& text, QObject* parent)
 : QAction(icon, text, parent)
{
	setCheckable(true);
	setMenuRole(QAction::NoRole);
	connect(this, &QAction::triggered, this, &MapEditorToolAction::triggeredImpl);
}

void MapEditorToolAction::triggeredImpl(bool checked)
{
	if (checked)
		emit activated();
	else
		setChecked(true);
}


// foom: the Elevation menu, see the design notes.

namespace {

/** The terrain grid, loaded on demand; warns when there is none. */
const ElevationModel* terrainModel(Map& map, QWidget* parent)
{
	auto const* model = map.elevationSet().model(ElevationSet::Terrain, map.getGeoreferencing().getProjectedCRSSpec());
	if (!model)
	{
		auto const href = map.elevationSet().href(ElevationSet::Terrain);
		QMessageBox::warning(parent, MapEditorController::tr("Elevation"),
		                     href.isEmpty() ? MapEditorController::tr("Import elevation data first (Elevation > Import elevation data).")
		                                    : MapEditorController::tr("The elevation grid %1 cannot be opened: %2").arg(href, ElevationModel::lastError()));
	}
	return model;
}

}  // namespace

void MapEditorController::elevationOpenClicked()
{
	QString const filter = tr("Elevation grids") + QLatin1String(" (*.tif *.tiff *.asc *.xyz *.hgt *.ocdDem *.img *.vrt *.dem);;") + tr("All files") + QLatin1String(" (*.*)");
	auto const start_dir = window->currentPath().isEmpty() ? QString{} : QFileInfo(window->currentPath()).absolutePath();
	auto const path = FileDialog::getOpenFileName(window, tr("Import elevation data"), start_dir, filter);
	if (path.isEmpty())
		return;
	ElevationOpenDialog dialog(window);
	if (dialog.exec() != QDialog::Accepted)
		return;
	auto options = dialog.options();
	options.target_crs = map->getGeoreferencing().getProjectedCRSSpec();
	auto grid = ElevationModel::open(path, options);
	if (!grid)
	{
		QMessageBox::warning(window, tr("Import elevation data"), ElevationModel::lastError());
		return;
	}
	auto href = path;
	if (options.feet_to_metres || options.millimetres_to_metres || options.shift_below_sea_level)
	{
		// The converted grid is kept beside the map, so that it reloads as imported.
		href = productPath(window->currentPath(), path, ElevationSet::kindName(dialog.kind()));
		if (!grid->save(href))
		{
			QMessageBox::warning(window, tr("Import elevation data"), ElevationModel::lastError());
			return;
		}
	}
	auto const info = grid->info();
	map->elevationSet().setModel(dialog.kind(), std::move(grid), href);
	map->setHasUnsavedChanges(true);
	window->showStatusBarMessage(tr("Elevation grid %1: %2 to %3 m").arg(QFileInfo(href).fileName(), QString::number(info.minimum, 'f', 1), QString::number(info.maximum, 'f', 1)), 5000);
}

void MapEditorController::elevationInfoClicked()
{
	auto const* model = terrainModel(*map, window);
	if (!model)
		return;
	auto const info = model->info();
	auto const extent = model->extent();
	QMessageBox::information(window, tr("Elevation information"),
	                         tr("File: %1\nCells: %2 by %3 of %4 m\nEasting: %5 to %6\nNorthing: %7 to %8\nHeight: %9 to %10 m\nVoid cells: %11")
	                         .arg(map->elevationSet().href(ElevationSet::Terrain))
	                         .arg(model->width()).arg(model->height()).arg(model->cellSize())
	                         .arg(extent.left(), 0, 'f', 1).arg(extent.right(), 0, 'f', 1)
	                         .arg(extent.top(), 0, 'f', 1).arg(extent.bottom(), 0, 'f', 1)
	                         .arg(info.minimum, 0, 'f', 2).arg(info.maximum, 0, 'f', 2)
	                         .arg(info.void_count));
}

void MapEditorController::elevationContoursClicked()
{
	auto const* model = terrainModel(*map, window);
	if (!model)
		return;
	auto const info = model->info();
	ContourDialog dialog(info.minimum, info.maximum, window);
	if (dialog.exec() != QDialog::Accepted)
		return;
	std::vector<std::unique_ptr<Object>> objects;
	for (auto const& params : dialog.sets())
	{
		auto const symbols = contourSymbols(*map, params.smoothed);
		auto result = makeContourObjects(*map, *model, params, symbols);
		if (!result.error.isEmpty())
		{
			QMessageBox::warning(window, tr("Contours"), result.error);
			return;
		}
		objects.insert(objects.end(), std::make_move_iterator(result.objects.begin()), std::make_move_iterator(result.objects.end()));
	}
	auto const count = objects.size();
	if (!addContours(*map, std::move(objects)))
	{
		QMessageBox::information(window, tr("Contours"), tr("No contour lies in the chosen range."));
		return;
	}
	window->showStatusBarMessage(tr("%n contour line(s) generated", nullptr, int(count)), 5000);
}

void MapEditorController::elevationHillShadeClicked()
{
	auto const* model = terrainModel(*map, window);
	if (!model)
		return;
	HillShadeDialog dialog(model->cellSize(), window);
	if (dialog.exec() != QDialog::Accepted)
		return;
	auto const product = hillShade(*model, dialog.params());
	auto const path = productPath(window->currentPath(), map->elevationSet().href(ElevationSet::Terrain), QStringLiteral("hillshade"));
	QString error = product.error;
	if (error.isEmpty() && !writeGeoTiff(product, path, &error))
		error = tr("Cannot write %1: %2").arg(path, error);
	if (error.isEmpty() && dialog.attach() && !attachProductTemplate(*map, path, &error))
		error = tr("Cannot attach %1: %2").arg(path, error);
	if (!error.isEmpty())
	{
		QMessageBox::warning(window, tr("Hill shading"), error);
		return;
	}
	if (dialog.attach())
	{
		hideAllTemplates(false);
		showTemplateWindow(true);
	}
	window->showStatusBarMessage(tr("Hill shading written to %1").arg(path), 5000);
}

// foom
void MapEditorController::elevationFeaturesClicked()
{
	if (!terrainModel(*map, window))
		return;
	LandformDialog dialog(window);
	if (dialog.exec() != QDialog::Accepted)
		return;
	auto const count = extractLandforms(dialog.params());
	if (count == 0)
		QMessageBox::information(window, tr("Extract features"), tr("No knoll and no small depression was found."));
}

void MapEditorController::mapRotateMagneticClicked()
{
	MagneticNorthDialog dialog(map->getGeoreferencing(), window);
	if (dialog.exec() == QDialog::Accepted)
		rotateMapToMagneticNorth(dialog.declination(), dialog.keepSelectedSymbols(), dialog.keepLayout());
}

// foom: the design notes, "Rotation to magnetic north and centring".
bool MapEditorController::rotateMapToMagneticNorth(double declination, bool keep_selected_symbols, bool keep_layout)
{
	MapTransformOptions options;
	options.keep_layout = keep_layout;
	if (keep_selected_symbols && symbol_widget)
	{
		for (int i = 0; i < map->getNumSymbols(); ++i)
		{
			if (symbol_widget->isSymbolSelected(map->getSymbol(i)))
				options.fixed_symbols.push_back(map->getSymbol(i));
		}
	}
	if (!OpenOrienteering::rotateMapToMagneticNorth(*map, declination, options))
		return false;
	map->setHasUnsavedChanges(true);
	return true;
}

void MapEditorController::mapCenterClicked()
{
	CenterMapDialog dialog(*map, window);
	if (dialog.exec() == QDialog::Accepted)
		centerMap(dialog.offset());
}

// foom: the design notes, "Rotation to magnetic north and centring".
bool MapEditorController::centerMap(const QPointF& offset)
{
	if (!OpenOrienteering::centerMap(*map, offset))
		return false;
	map->setHasUnsavedChanges(true);
	return true;
}

void MapEditorController::mapReprojectClicked()
{
	if (map->getGeoreferencing().getState() != Georeferencing::Geospatial)
	{
		QMessageBox::information(window, tr("Change coordinate system"), tr("The map has no projected coordinate reference system."));
		return;
	}
	ReprojectDialog dialog(map->getGeoreferencing(), window);
	if (dialog.exec() != QDialog::Accepted)
		return;
	auto const offset = dialog.targetOffset();
	if (!reprojectMap(dialog.crsId(), dialog.crsSpec(), dialog.crsParameters(), &offset))
		QMessageBox::warning(window, tr("Change coordinate system"), tr("The map cannot be changed to this coordinate reference system."));
}

// foom: the design notes, "Change of coordinate system".
bool MapEditorController::reprojectMap(const QString& id, const QString& spec, const std::vector<QString>& params, const QPointF* offset)
{
	if (!OpenOrienteering::reprojectMap(*map, id, spec, params, offset))
		return false;
	map->setHasUnsavedChanges(true);
	return true;
}

void MapEditorController::mapLocalWarpClicked()
{
	// the design notes, "Pair tables, perimeters and the dialogs beside the map":
	// the dialog stays beside the map, and its Start starts the tool.
	if (!local_warp_dialog)
	{
		local_warp_dialog = new LocalWarpDialog(window);
		connect(local_warp_dialog, &LocalWarpDialog::startRequested, this, [this]() {
			if (local_warp_dialog)
				startLocalWarp(local_warp_dialog->settings());
		});
		connect(local_warp_dialog, &LocalWarpDialog::closeRequested, this, [this]() {
			if (auto* tool = qobject_cast<LocalWarpTool*>(getTool()))
				tool->deactivate();
		});
	}
	if (map_local_warp_act)
		map_local_warp_act->setChecked(qobject_cast<LocalWarpTool*>(getTool()) != nullptr);
	local_warp_dialog->show();
	local_warp_dialog->raise();
}

void MapEditorController::startLocalWarp(const LocalWarpParams& settings)
{
	auto* tool = new LocalWarpTool(this, map_local_warp_act, settings);
	tool->setSettingsSource(local_warp_dialog);
	setTool(tool);
}

LocalWarpDialog* MapEditorController::localWarpDialog() const
{
	return local_warp_dialog;
}

// foom: the design notes, "Local transformation".
bool MapEditorController::localWarpMapWith(const LocalWarpParams& params)
{
	if (!localWarpMap(*map, params))
		return false;
	map->setHasUnsavedChanges(true);
	return true;
}

void MapEditorController::mapRubbersheetClicked()
{
	setTool(new RubbersheetTool(this, map_rubbersheet_act));
}

// foom: the design notes, "Rubbersheeting".
bool MapEditorController::rubbersheetMapWith(const RubbersheetParams& params, WarpReport* report)
{
	if (!rubbersheetMap(*map, params, report))
		return false;
	map->setHasUnsavedChanges(true);
	return true;
}

void MapEditorController::mapAffineClicked()
{
	setTool(new AffineTransformTool(this, map_affine_act));
}

// foom: the design notes, "Affine by point pairs".
bool MapEditorController::affineTransformMap(const std::vector<PointPair>& pairs)
{
	auto const fit = fitPointPairs(pairs);
	if (!fit.ok)
		return false;
	MapTransformOptions options;
	options.rotate_orientations = true;
	if (!transformMap(*map, fit.transform, options))
		return false;
	map->setHasUnsavedChanges(true);
	return true;
}

void MapEditorController::mapMoveClicked()
{
	MapMoveDialog dialog(window);
	if (dialog.exec() == QDialog::Accepted)
		moveMap(dialog.east(), dialog.north(), dialog.carry());
}

void MapEditorController::mapStretchClicked()
{
	MapStretchDialog dialog(false, window);
	if (dialog.exec() == QDialog::Accepted)
		stretchMap(dialog.factorX(), dialog.factorY(), dialog.mirrorHorizontally(), dialog.mirrorVertically(), dialog.carry());
}

void MapEditorController::mapMirrorClicked()
{
	MapStretchDialog dialog(true, window);
	if (dialog.exec() == QDialog::Accepted)
		stretchMap(dialog.factorX(), dialog.factorY(), dialog.mirrorHorizontally(), dialog.mirrorVertically(), dialog.carry());
}

// foom: the design notes, "Move, stretch and mirror".
bool MapEditorController::moveMap(double east_m, double north_m, bool carry)
{
	MapTransformOptions options;
	options.carry = carry;
	if (!transformMap(*map, mapMoveTransform(*map, east_m, north_m), options))
		return false;
	map->setHasUnsavedChanges(true);
	return true;
}

bool MapEditorController::stretchMap(double factor_x, double factor_y, bool mirror_horizontally, bool mirror_vertically, bool carry)
{
	if (!(factor_x > 0) || !(factor_y > 0))
		return false;
	MapTransformOptions options;
	options.carry = carry;
	if (!transformMap(*map, mapStretchTransform(factor_x, factor_y, mirror_horizontally, mirror_vertically), options))
		return false;
	map->setHasUnsavedChanges(true);
	return true;
}

void MapEditorController::elevationPointCloudClicked()
{
	QString const filter = tr("Point clouds") + QLatin1String(" (*.las *.laz);;") + tr("All files") + QLatin1String(" (*.*)");
	auto const start_dir = window->currentPath().isEmpty() ? QString{} : QFileInfo(window->currentPath()).absolutePath();
	auto const paths = QFileDialog::getOpenFileNames(window, tr("Import point cloud"), start_dir, filter);
	if (paths.isEmpty())
		return;
	PointCloudDialog dialog(paths, window);
	if (dialog.exec() != QDialog::Accepted)
		return;
	QString error;
	QApplication::setOverrideCursor(Qt::WaitCursor);
	auto const ok = importPointClouds(paths, dialog.params(), dialog.products(), &error);
	QApplication::restoreOverrideCursor();
	if (!ok)
		QMessageBox::warning(window, tr("Import point cloud"), error);
}

// foom: the design notes, "Reading point clouds and gridding them".
bool MapEditorController::importPointClouds(const QStringList& paths, const GriddingParams& params, const PointCloudProducts& products, QString* error)
{
	if (paths.isEmpty())
		return false;
	auto grid_params = params;
	grid_params.dtm = params.dtm || products.vegetation_height || products.vegetation_map;
	grid_params.dsm = params.dsm || products.vegetation_height;
	auto result = PointCloud::grid(paths, grid_params);
	if (!result.error.isEmpty())
	{
		*error = result.error;
		return false;
	}
	auto const map_path = window ? window->currentPath() : QString{};
	auto const target_crs = map->getGeoreferencing().getProjectedCRSSpec();
	QStringList names;
	auto const store = [&](std::unique_ptr<ElevationModel> grid, ElevationSet::Kind kind, const QString& suffix) {
		if (!grid)
			return true;
		auto const href = productPath(map_path, paths.front(), suffix);
		if (!grid->save(href))
		{
			*error = ElevationModel::lastError();
			return false;
		}
		if (!grid->crsSpec().isEmpty() && !target_crs.isEmpty() && !ElevationModel::sameCrs(grid->crsSpec(), target_crs))
		{
			// Warped into the map's system, as an imported grid is.
			ElevationModel::OpenOptions options;
			options.target_crs = target_crs;
			grid = ElevationModel::open(href, options);
			if (!grid)
			{
				*error = ElevationModel::lastError();
				return false;
			}
		}
		map->elevationSet().setModel(kind, std::move(grid), href);
		names.push_back(QFileInfo(href).fileName());
		return true;
	};
	// The products, from the grids in the clouds' system.
	auto const product = [&](const RasterProduct& raster, const QString& suffix) {
		if (!raster.error.isEmpty())
		{
			*error = raster.error;
			return false;
		}
		auto const path = productPath(map_path, paths.front(), suffix);
		if (!writeGeoTiff(raster, path, error) || !attachProductTemplate(*map, path, error))
			return false;
		names.push_back(QFileInfo(path).fileName());
		return true;
	};
	if (products.vegetation_height && result.dtm && result.dsm)
	{
		auto const heights = ElevationModel::difference(*result.dsm, *result.dtm);
		if (!heights || !product(vegetationHeightRaster(*heights, defaultHeightClasses()), QStringLiteral("vegetation_height")))
		{
			if (!heights)
				*error = tr("The surface and terrain models do not overlap.");
			return false;
		}
	}
	if (products.vegetation_map && result.dtm
	    && !product(PointCloud::vegetationBaseMap(paths, *result.dtm, products.vegetation), QStringLiteral("vegetation")))
		return false;
	if (!params.dtm)
		result.dtm.reset();
	if (!params.dsm)
		result.dsm.reset();
	if (!store(std::move(result.dtm), ElevationSet::Terrain, QStringLiteral("DTM"))
	    || !store(std::move(result.dsm), ElevationSet::Surface, QStringLiteral("DSM")))
		return false;
	map->setHasUnsavedChanges(true);
	if (window)
		window->showStatusBarMessage(tr("Point cloud gridded: %1").arg(names.join(QLatin1String(", "))), 5000);
	return true;
}

// foom: the design notes, "Grid commands".
bool MapEditorController::storeElevationGrid(int kind, std::unique_ptr<ElevationModel> grid, QString* error)
{
	auto const set_kind = ElevationSet::Kind(kind);
	auto& set = map->elevationSet();
	auto base = set.href(set_kind);
	if (base.isEmpty())
		base = set.href(ElevationSet::Terrain);
	auto const path = productPath(window ? window->currentPath() : QString{}, base, ElevationSet::kindName(set_kind));
	if (!grid->save(path))
	{
		if (error)
			*error = ElevationModel::lastError();
		return false;
	}
	set.setModel(set_kind, std::move(grid), path);
	map->setHasUnsavedChanges(true);
	return true;
}

bool MapEditorController::mergeElevationFiles(const QStringList& paths, QString* error)
{
	auto const crs = map->getGeoreferencing().getProjectedCRSSpec();
	std::vector<std::unique_ptr<ElevationModel>> opened;
	std::vector<const ElevationModel*> grids;
	if (auto const* terrain = map->elevationSet().model(ElevationSet::Terrain, crs))
		grids.push_back(terrain);
	for (auto const& path : paths)
	{
		ElevationModel::OpenOptions options;
		options.target_crs = crs;
		auto grid = ElevationModel::open(path, options);
		if (!grid)
		{
			if (error)
				*error = ElevationModel::lastError();
			return false;
		}
		grids.push_back(grid.get());
		opened.push_back(std::move(grid));
	}
	auto merged = ElevationModel::merge(grids);
	if (!merged)
	{
		if (error)
			*error = ElevationModel::lastError();
		return false;
	}
	return storeElevationGrid(ElevationSet::Terrain, std::move(merged), error);
}

bool MapEditorController::elevationDifference(QString* error)
{
	auto const crs = map->getGeoreferencing().getProjectedCRSSpec();
	auto const* surface = map->elevationSet().model(ElevationSet::Surface, crs);
	auto const* terrain = map->elevationSet().model(ElevationSet::Terrain, crs);
	if (!surface || !terrain)
	{
		if (error)
			*error = tr("The map needs a surface grid and a terrain grid.");
		return false;
	}
	auto heights = ElevationModel::difference(*surface, *terrain);
	if (!heights)
	{
		if (error)
			*error = ElevationModel::lastError();
		return false;
	}
	return storeElevationGrid(ElevationSet::Canopy, std::move(heights), error);
}

bool MapEditorController::cropElevation(const QRectF& box, QString* error)
{
	auto const* terrain = map->elevationSet().model(ElevationSet::Terrain, map->getGeoreferencing().getProjectedCRSSpec());
	if (!terrain || !terrain->extent().intersects(box))
	{
		if (error)
			*error = terrain ? tr("The box lies outside the elevation grid.") : tr("The map has no terrain grid.");
		return false;
	}
	return storeElevationGrid(ElevationSet::Terrain, terrain->cropped(box), error);
}

bool MapEditorController::smoothElevation(int radius, QString* error)
{
	auto const* terrain = map->elevationSet().model(ElevationSet::Terrain, map->getGeoreferencing().getProjectedCRSSpec());
	if (!terrain)
	{
		if (error)
			*error = tr("The map has no terrain grid.");
		return false;
	}
	return storeElevationGrid(ElevationSet::Terrain, terrain->smoothed(radius), error);
}

void MapEditorController::closeElevation()
{
	map->elevationSet().clear();
	map->setHasUnsavedChanges(true);
}

void MapEditorController::elevationMergeClicked()
{
	QString const filter = tr("Elevation grids") + QLatin1String(" (*.tif *.tiff *.asc *.xyz *.hgt *.ocdDem *.img *.vrt *.dem);;") + tr("All files") + QLatin1String(" (*.*)");
	auto const start_dir = window->currentPath().isEmpty() ? QString{} : QFileInfo(window->currentPath()).absolutePath();
	auto const paths = QFileDialog::getOpenFileNames(window, tr("Merge elevation grids"), start_dir, filter);
	if (paths.isEmpty())
		return;
	QString error;
	if (!mergeElevationFiles(paths, &error))
		QMessageBox::warning(window, tr("Merge elevation grids"), error);
	else
		window->showStatusBarMessage(tr("Elevation grids merged into %1").arg(map->elevationSet().href(ElevationSet::Terrain)), 5000);
}

void MapEditorController::elevationDifferenceClicked()
{
	QString error;
	if (!elevationDifference(&error))
		QMessageBox::warning(window, tr("Surface less terrain"), error);
	else
		window->showStatusBarMessage(tr("Canopy grid written to %1").arg(map->elevationSet().href(ElevationSet::Canopy)), 5000);
}

void MapEditorController::elevationResizeClicked()
{
	auto const* model = terrainModel(*map, window);
	if (!model)
		return;
	ElevationResizeDialog dialog(model->extent(), window);
	if (dialog.exec() != QDialog::Accepted)
		return;
	QString error;
	if (!cropElevation(dialog.box(), &error))
		QMessageBox::warning(window, tr("Crop elevation grid"), error);
	else
		window->showStatusBarMessage(tr("Elevation grid cropped"), 5000);
}

void MapEditorController::elevationSmoothClicked()
{
	if (!terrainModel(*map, window))
		return;
	bool ok = false;
	auto const radius = QInputDialog::getInt(window, tr("Smooth elevation grid"), tr("Radius (passes):"), 1, 1, 10, 1, &ok);
	if (!ok)
		return;
	QString error;
	if (!smoothElevation(radius, &error))
		QMessageBox::warning(window, tr("Smooth elevation grid"), error);
	else
		window->showStatusBarMessage(tr("Elevation grid smoothed"), 5000);
}

void MapEditorController::elevationCloseClicked()
{
	if (map->elevationSet().isEmpty())
		return;
	if (QMessageBox::question(window, tr("Close elevation grids"), tr("Remove every elevation grid from the map? The files stay.")) == QMessageBox::Yes)
		closeElevation();
}

// foom: the design notes, "Knolls and small depressions".
int MapEditorController::extractLandforms(const LandformParams& params)
{
	auto const* model = map->elevationSet().model(ElevationSet::Terrain, map->getGeoreferencing().getProjectedCRSSpec());
	if (!model)
		return 0;
	auto const landforms = findLandforms(*model, params);
	// the design notes, "Cliffs from the slope gradient".
	std::vector<CliffArea> cliffs;
	if (params.cliffs)
	{
		CliffParams cliff_params;
		cliff_params.threshold = params.cliff_threshold;
		cliff_params.min_area = params.cliff_min_area;
		cliff_params.min_length = params.cliff_min_length;
		cliffs = findCliffs(*model, cliff_params);
	}
	// the design notes, "Trees": from the surface less the terrain.
	std::vector<Tree> trees;
	if (params.trees)
	{
		auto const* surface = map->elevationSet().model(ElevationSet::Surface, map->getGeoreferencing().getProjectedCRSSpec());
		auto const heights = surface ? ElevationModel::difference(*surface, *model) : nullptr;
		if (heights)
		{
			TreeParams tree_params;
			tree_params.min_height = params.tree_min_height;
			trees = findTrees(*heights, tree_params);
		}
		else if (window)
		{
			window->showStatusBarMessage(tr("Trees need a surface model; import a point cloud with the surface model first."), 5000);
		}
	}
	if (landforms.empty() && cliffs.empty() && trees.empty())
		return 0;
	auto const knolls = int(std::count_if(landforms.begin(), landforms.end(), [](const Landform& l) { return l.kind == Landform::Knoll; }));
	std::vector<std::unique_ptr<Object>> objects;
	if (!landforms.empty())
		objects = makeLandformObjects(*map, landforms, landformSymbols(*map));
	auto const landform_count = int(objects.size());
	if (!cliffs.empty())
	{
		auto cliff_objects = makeCliffObjects(*map, cliffs, cliffSymbol(*map));
		std::move(cliff_objects.begin(), cliff_objects.end(), std::back_inserter(objects));
	}
	if (!trees.empty())
	{
		auto tree_objects = makeTreeObjects(*map, trees, treeSymbol(*map));
		std::move(tree_objects.begin(), tree_objects.end(), std::back_inserter(objects));
	}
	auto const count = int(objects.size());
	addContours(*map, std::move(objects));
	if (window)
	{
		if (params.cliffs || params.trees)
			window->showStatusBarMessage(tr("%1 knoll(s), %2 small depression(s), %3 cliff(s) and %4 tree(s) extracted").arg(knolls).arg(landform_count - knolls).arg(int(cliffs.size())).arg(int(trees.size())), 5000);
		else
			window->showStatusBarMessage(tr("%1 knoll(s) and %2 small depression(s) extracted").arg(knolls).arg(count - knolls), 5000);
	}
	return count;
}

// foom: the design notes, "Vegetation height from the map's grids".
QString MapEditorController::writeVegetationHeightMap(bool attach, QString* error)
{
	auto const crs = map->getGeoreferencing().getProjectedCRSSpec();
	auto& set = map->elevationSet();
	std::unique_ptr<ElevationModel> difference;
	auto const* heights = set.model(ElevationSet::Canopy, crs);
	if (!heights)
	{
		auto const* surface = set.model(ElevationSet::Surface, crs);
		auto const* terrain = set.model(ElevationSet::Terrain, crs);
		if (surface && terrain)
			difference = ElevationModel::difference(*surface, *terrain);
		heights = difference.get();
	}
	if (!heights)
	{
		if (error)
			*error = tr("The map needs a canopy grid, or a surface grid and a terrain grid.");
		return {};
	}
	auto base = set.href(ElevationSet::Terrain);
	if (base.isEmpty())
		base = set.href(ElevationSet::Canopy);
	auto const path = productPath(window ? window->currentPath() : QString{}, base, QStringLiteral("vegetation_height"));
	auto const product = vegetationHeightRaster(*heights, defaultHeightClasses());
	QString message = product.error;
	if (message.isEmpty() && !writeGeoTiff(product, path, &message))
		message = tr("Cannot write %1: %2").arg(path, message);
	if (message.isEmpty() && attach && !attachProductTemplate(*map, path, &message))
		message = tr("Cannot attach %1: %2").arg(path, message);
	if (!message.isEmpty())
	{
		if (error)
			*error = message;
		return {};
	}
	return path;
}

void MapEditorController::elevationVegetationHeightClicked()
{
	auto const answer = QMessageBox::question(window, tr("Vegetation height"), tr("Attach the vegetation height raster as a template?"),
	                                          QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel, QMessageBox::Yes);
	if (answer == QMessageBox::Cancel)
		return;
	auto const attach = answer == QMessageBox::Yes;
	QString error;
	auto const path = writeVegetationHeightMap(attach, &error);
	if (path.isEmpty())
	{
		QMessageBox::warning(window, tr("Vegetation height"), error);
		return;
	}
	if (attach)
	{
		hideAllTemplates(false);
		showTemplateWindow(true);
	}
	window->showStatusBarMessage(tr("Vegetation height written to %1").arg(path), 5000);
}

// foom: the design notes, "Merging contour lines".
int MapEditorController::mergeContourLines(double tolerance)
{
	std::vector<const Object*> in;
	if (symbol_widget)
	{
		auto const* part = map->getCurrentPart();
		for (int i = 0; i < part->getNumObjects(); ++i)
		{
			auto const* object = part->getObject(i);
			if (object->getSymbol() && symbol_widget->isSymbolSelected(object->getSymbol()))
				in.push_back(object);
		}
	}
	ops::JoinParams params;
	params.tolerance = tolerance;
	auto result = ops::mergeContourLines(*map, in, params);
	auto const joins = int(result.consumed.size()) - int(result.out.size());
	if (!result.error.isEmpty() || result.out.empty())
	{
		if (window)
			window->showStatusBarMessage(result.error.isEmpty() ? tr("No contour lines to merge.") : result.error, 5000);
		return 0;
	}
	applyOpResult(std::move(result));
	if (window)
		window->showStatusBarMessage(tr("%1 join(s) of contour lines").arg(joins), 5000);
	return joins;
}

void MapEditorController::elevationMergeContoursClicked()
{
	bool ok = false;
	auto const tolerance = QInputDialog::getDouble(window, tr("Merge contour lines"), tr("Join ends closer than:"), ops::JoinParams().tolerance, 0.0, 100.0, 2, &ok);
	if (ok)
		mergeContourLines(tolerance);
}

// foom: the design notes, "Profile".
Profile MapEditorController::elevationProfileOfSelection(QString* error)
{
	auto const* model = map->elevationSet().model(ElevationSet::Terrain, map->getGeoreferencing().getProjectedCRSSpec());
	if (!model)
	{
		if (error)
			*error = tr("The map has no terrain grid.");
		return {};
	}
	auto* object = map->getNumSelectedObjects() == 1 ? map->getFirstSelectedObject() : nullptr;
	if (!object || object->getType() != Object::Path)
	{
		if (error)
			*error = tr("Select one line or area.");
		return {};
	}
	auto* path = object->asPath();
	path->update();
	if (path->parts().empty())
	{
		if (error)
			*error = tr("Select one line or area.");
		return {};
	}
	auto const& georef = map->getGeoreferencing();
	std::vector<QPointF> line;
	for (auto const& coord : path->parts().front().path_coords)
		line.push_back(georef.toProjectedCoords(coord.pos));
	return elevationProfile(*model, line);
}

void MapEditorController::elevationProfileClicked()
{
	if (!terrainModel(*map, window))
		return;
	QString error;
	auto const profile = elevationProfileOfSelection(&error);
	if (profile.samples.empty())
	{
		QMessageBox::warning(window, tr("Profile"), error);
		return;
	}
	ProfileDialog dialog(profile, window);
	dialog.exec();
}

// foom: the design notes, "Hypsometric map".
QString MapEditorController::writeHypsometricMap(const HypsometricParams& params, bool attach, QString* error)
{
	auto const* model = map->elevationSet().model(ElevationSet::Terrain, map->getGeoreferencing().getProjectedCRSSpec());
	if (!model)
	{
		if (error)
			*error = tr("The map has no terrain grid.");
		return {};
	}
	auto const product = hypsometricRaster(*model, params);
	auto const path = productPath(window ? window->currentPath() : QString{}, map->elevationSet().href(ElevationSet::Terrain), QStringLiteral("hypsometric"));
	QString message = product.error;
	if (message.isEmpty() && !writeGeoTiff(product, path, &message))
		message = tr("Cannot write %1: %2").arg(path, message);
	if (message.isEmpty() && attach && !attachProductTemplate(*map, path, &message))
		message = tr("Cannot attach %1: %2").arg(path, message);
	if (!message.isEmpty())
	{
		if (error)
			*error = message;
		return {};
	}
	return path;
}

void MapEditorController::elevationHypsometricClicked()
{
	if (!terrainModel(*map, window))
		return;
	HypsometricDialog dialog(window);
	if (dialog.exec() != QDialog::Accepted)
		return;
	QString error;
	auto const path = writeHypsometricMap(dialog.params(), dialog.attach(), &error);
	if (path.isEmpty())
	{
		QMessageBox::warning(window, tr("Hypsometric map"), error);
		return;
	}
	if (dialog.attach())
	{
		hideAllTemplates(false);
		showTemplateWindow(true);
	}
	window->showStatusBarMessage(tr("Hypsometric map written to %1").arg(path), 5000);
}

void MapEditorController::elevationSlopeClicked()
{
	auto const* model = terrainModel(*map, window);
	if (!model)
		return;
	SlopeDialog dialog(model->cellSize(), window);
	if (dialog.exec() != QDialog::Accepted)
		return;
	auto const product = slopeRaster(*model, dialog.params());
	auto const path = productPath(window->currentPath(), map->elevationSet().href(ElevationSet::Terrain), QStringLiteral("slope"));
	QString error = product.error;
	if (error.isEmpty() && !writeGeoTiff(product, path, &error))
		error = tr("Cannot write %1: %2").arg(path, error);
	if (error.isEmpty() && dialog.attach() && !attachProductTemplate(*map, path, &error))
		error = tr("Cannot attach %1: %2").arg(path, error);
	if (!error.isEmpty())
	{
		QMessageBox::warning(window, tr("Slope gradient"), error);
		return;
	}
	if (dialog.attach())
	{
		hideAllTemplates(false);
		showTemplateWindow(true);
	}
	window->showStatusBarMessage(tr("Slope gradient written to %1").arg(path), 5000);
}

void MapEditorController::elevationExportClicked()
{
	auto const* model = terrainModel(*map, window);
	if (!model)
		return;
	QString const filter = tr("GeoTIFF") + QLatin1String(" (*.tif);;") + tr("ESRI ASCII grid") + QLatin1String(" (*.asc);;")
	                    + tr("XYZ grid") + QLatin1String(" (*.xyz);;") + tr("OCD grid") + QLatin1String(" (*.ocdDem)");
	auto const start = productPath(window->currentPath(), map->elevationSet().href(ElevationSet::Terrain), QStringLiteral("dtm"));
	auto const path = FileDialog::getSaveFileName(window, tr("Export grid"), start, filter);
	if (path.isEmpty())
		return;
	if (!model->save(path))
		QMessageBox::warning(window, tr("Export grid"), ElevationModel::lastError());
}


}  // namespace OpenOrienteering
