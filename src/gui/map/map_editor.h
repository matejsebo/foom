/*
 *    Copyright 2012, 2013, 2014 Thomas Schöps
 *    Copyright 2013-2025 Kai Pastor
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


#ifndef OPENORIENTEERING_MAP_EDITOR_H
#define OPENORIENTEERING_MAP_EDITOR_H

#include <memory>
#include <vector>

#include <QClipboard>
#include <QHash>
#include <functional>  // foom

#include <QKeySequence>  // foom
#include <QObject>
#include <QPointer>
#include <QVector>  // foom
#include <QUuid>  // foom
#include <QScopedPointer>
#include <QString>
#include <QTimer>

#include "core/map.h"
#include "check/issue.h"  // foom
#include "core/bookmarks.h"  // foom
#include "core/extents.h"  // foom
#include "core/map_view.h"  // foom
#include "core/ops/align.h"  // foom
#include "core/ops/replace_text.h"  // foom
#include "gui/map/view_history.h"  // foom
#include "core/ops/buildings.h"  // foom
#include "core/ops/rectangle_box.h"  // foom
#include "core/ops/smooth.h"  // foom
#include "core/ops/topology.h"  // foom
#include "core/ops/transform.h"  // foom
#include "gui/main_window_controller.h"

class QAction;
class QPainter;  // foom
class QByteArray;
class QComboBox;
class QDockWidget;
class QFrame;
class QKeyEvent;
class QLabel;
class QMenu;
class QPointF;  // foom
class QStringList;  // foom
class QRectF;  // foom
class QSignalMapper;
// IWYU pragma: no_forward_declare QString
class QToolBar;
class QToolButton;
class QWidget;

namespace OpenOrienteering {

class ActionGridBar;
class CompassDisplay;
class EditorDockWidget;
class FileFormat;
class GPSDisplay;
class GpsPathRecorder;  // foom
class GpsPositionAverage;  // foom
class GPSTemporaryMarkers;
class GPSTrackRecorder;
class GeoreferencingDialog;
class MainWindow;
class MapCoordF;
class MapEditorActivity;
class ElevationModel;  // foom
class ExtentDialog;  // foom
class IssueModel;  // foom
struct LegibilityParams;  // foom
struct LandformParams;  // foom
struct HypsometricParams;  // foom
struct GriddingParams;  // foom
struct PointCloudProducts;  // foom
struct PointPair;  // foom
struct RubbersheetParams;  // foom
struct LocalWarpParams;  // foom
class FindReplaceTextDialog;  // foom
class MoveParallelDialog;  // foom
class NumericEntryBar;  // foom
class MapEditorTool;
class MapFindFeature;
class MapView;
class MapWidget;
class PaintOnTemplateFeature;
class CourseWidget;  // foom
class RangefinderWidget;  // foom
class PrintWidget;
class ReopenTemplateDialog;
class Symbol;
class SymbolReportFeature;
class SymbolWidget;
class Template;
class TemplateListWidget;


/**
 * MainWindowController for editing a map.
 * 
 * Creates menus and toolbars, manages editing tools,
 * dock widgets, and much more.
 */
class MapEditorController : public MainWindowController
{
friend class Map;
Q_OBJECT
public:
	/** See MapEditorController constructor. */
	enum OperatingMode
	{
		MapEditor = 0,
		SymbolEditor = 1
	};
	
	/** Locations where to place popup windows. */
	enum PopupLocation
	{
		PopupLocationTop = 0x04,
		PopupLocationBottom = 0x08,
	};
	
	/**
	 * Constructs a new MapEditorController for a map.
	 * 
	 * @param mode Normally, MapEditor should be used. However, as a HACK the
	 *     MapEditorController is also used in the symbol editor for the preview.
	 *     In this case, SymbolEditor is passed to disable showing the menus,
	 *     toolbars, etc.
	 * @param map       A Map which is to be edited by the controller.
	 * @param map_view  A MapView for the given map.
	 * 
	 * \todo Review/remove mode hack. 
	 * \todo Document and fix ownership of map and map_view. Double deletes waiting...
	 */
	MapEditorController(OperatingMode mode, Map* map = nullptr, MapView* map_view = nullptr);
	
	/** Destroys the MapEditorController. */
	~MapEditorController() override;
	
	/**
	 * In SymbolEditor mode, this controller doesn't want a menu bar.
	 */
	bool menuBarVisible() override;
	
	/** Returns if the editor is in mobile mode. */
	bool isInMobileMode() const;
	
	/**
	 * Changes to new_tool as the new active tool.
	 * If there is a current tool before, calls deleteLater() on it.
	 * new_tool may be nullptr, but it is unusual to have no active tool, so
	 * consider setEditTool() instead.
	 */
	void setTool(MapEditorTool* new_tool);
	
	/** foom: Applies the result of a geometry operation, or shows its error. */
	void applyOpResult(ops::OpResult&& result);
	
	/**
	 * Shortcut to change to the point edit tool as new active tool.
	 * See setTool().
	 */
	void setEditTool();
	
	/**
	 * Sets new_override_tool as the new active override tool.
	 * This takes precedence over all tools set via setTool().
	 * new_override_tool may be nullptr, which disables using an override tool
	 * and re-enables the normal tool set via setTool().
	 */
	void setOverrideTool(MapEditorTool* new_override_tool);
	
	/** Returns the current tool. */
	inline MapEditorTool* getTool() const {return current_tool;}
	
	/** Returns the default drawing tool for a given symbol. */
	MapEditorTool* getDefaultDrawToolForSymbol(const Symbol* symbol);
	
	
	/**
	 * @brief Returns the active symbol, or nullptr.
	 * 
	 * The active symbol is the single symbol which is to be used by drawing
	 * tools and actions.
	 * 
	 * It there is no active symbol, this function returns nullptr.
	 */
	Symbol* activeSymbol() const;
	
	
	/**
	 * If this is set to true (usually by the current tool),
	 * undo/redo and saving the map is deactivated.
	 * 
	 * This is important if the map is in an "unstable" state temporarily.
	 */
	void setEditingInProgress(bool value);
	
	/**
	 * Returns true when editing is in progress.
	 * @see setEditingInProgress
	 */
	bool isEditingInProgress() const override;
	
	/**
	 * Adds a floating dock widget to the main window.
	 * Adjusts some geometric properties.
	 */
	void addFloatingDockWidget(QDockWidget* dock_widget);
	
	/**
	 * Sets the current editor activity.
	 * 
	 * Before setting the new activity, this function deletes the previous
	 * activity. Parameter new_activity may be nullptr to just terminate the
	 * current editor activity.
	 */
	void setEditorActivity(MapEditorActivity* new_activity);
	
	/** Returns the current editor activity. */
	inline MapEditorActivity* getEditorActivity() const {return editor_activity;}
	
	/** Returns the map on which this controller operates. */
	inline Map* getMap() const {return map;}
	/** Returns the main map widget (which is currently the only map widget). */
	inline MapWidget* getMainWidget() const {return map_widget;}
	/** foom: Returns the GPS display of this editor; there is none in the symbol editor. */
	const GPSDisplay* gpsDisplay() const { return gps_display; }
	/** Returns this controller's symbol widget, where the symbol selection happens. */
	inline SymbolWidget* getSymbolWidget() const {return symbol_widget;}
	
	/** foom: Returns the actions by id, for the shortcut editor. */
	const QHash<QByteArray, QAction*>& actions() const { return actionsById; }
	
	/** foom: Returns the shortcut an action had before the stored shortcuts were applied. */
	QKeySequence defaultShortcut(const QByteArray& id) const { return default_shortcuts.value(id); }
	
	/** foom: Draws the active extent as a sheet outline; the map widget's overlay. */
	void drawActiveExtent(QPainter* painter, MapWidget* widget) const;
	void drawTemplateAttributions(QPainter* painter, MapWidget* widget) const;  // foom
	/** foom: Draws the reference of the rangefinder and the target of its fields. */
	void drawRangefinder(QPainter* painter, MapWidget* widget) const;
	
	/** foom: Draws the lines and numbers of the course which is chosen in the course dock. */
	void drawCoursePreview(QPainter* painter, MapWidget* widget) const;
	
	/** foom: Returns the issue model of the Issues window. */
	IssueModel* issueModel() const { return issue_model; }
	
	/** foom: Returns the numeric entry bar. */
	inline NumericEntryBar* numericEntryBar() const {return numeric_entry_bar;}
	
	
	/**
	 * Shows the given widget in a popup window with specified title.
	 * 
	 * In the desktop version, the widget is shown inside a dock widget.
	 * In the mobile version, the widget is shown as a popup over the map,
	 * ignoring the title.
	 * 
	 * Make sure that the child widget has a reasonable size hint.
	 */
	void showPopupWidget(QWidget* child_widget, const QString& title, PopupLocation location = PopupLocationBottom);
	
	/**
	 * Deletes the given popup widget, which was previously shown with
	 * showPopupWidget().
	 */
	void deletePopupWidget(QWidget* child_widget);
	
	
	/**
	 * Returns the action identified by id if it exists, or nullptr.
	 * This allows the reuse of the controller's actions in dock widgets etc.
	 */
	QAction* getAction(const char* id);
	
	/** Override from MainWindowController */
	bool saveTo(const QString& path, const FileFormat& format) override;
	/** Override from MainWindowController */
	bool exportTo(const QString& path, const FileFormat& format) override;
	/** Override from MainWindowController */
	bool loadFrom(const QString& path, const FileFormat& format, QWidget* dialog_parent = nullptr) override;
	
	/** Override from MainWindowController */
	void attach(MainWindow* window) override;
	/** Override from MainWindowController */
	void detach() override;
	
	/**
	 * @copybrief MainWindowController::keyPressEventFilter
	 * This implementation passes the event to MapWidget::keyPressEventFilter.
	 */
	bool keyPressEventFilter(QKeyEvent* event) override;
	
	/** 
	 * @copybrief MainWindowController::keyReleaseEventFilter
	 * This implementation passes the event to MapWidget::keyReleaseEventFilter.
	 */
	bool keyReleaseEventFilter(QKeyEvent* event) override;
	
	
	/**
	 * Lets the user export the map as vector data file of one of the given types.
	 */
	void exportVectorData(int file_types, const QString& format_settings_key);
	
	
public slots:
	/**
	 * foom: Takes a GPS position for the path which is recorded.
	 * 
	 * Does nothing unless gps-record is on and the current tool takes
	 * vertices by their coordinates. See "Recording into a symbol" in
	 * the design notes.
	 */
	void gpsRecordPosition(const OpenOrienteering::MapCoordF& position, float accuracy);
	
	/** foom: Adds a vertex at the position to the path which is drawn, whatever its distance. */
	void gpsAddVertexAt(const OpenOrienteering::MapCoordF& position);
	
	/**
	 * foom: Sets the offset of the GPS position so that the latest position
	 * falls on the given place, and says in the status bar what happened.
	 * 
	 * Returns false when there is no position from which to measure. See
	 * "Position offset and sentence log" in the design notes.
	 */
	bool gpsSetOffsetAt(const OpenOrienteering::MapCoordF& true_position);
	
	/**
	 * foom: Sets the reference point of the rangefinder. Does nothing before its dock was shown.
	 * See "Rangefinder dock: detail" in the design notes.
	 */
	void rangefinderSetReference(const OpenOrienteering::MapCoordF& position);
	
	/**
	 * foom: Draws the target of a rangefinder reading: the next vertex of
	 * the path which is drawn, or a point object of the active symbol.
	 * 
	 * Returns false, with a message in the status bar, when the active
	 * symbol cannot be drawn in this way.
	 */
	bool rangefinderAdd(const OpenOrienteering::MapCoordF& target);
	
	/**
	 * Lets the user export a PathObject as a simple course.
	 */
	void exportSimpleCourse();
	
	/**
	 * Lets the user export the map as geospatial vector data.
	 */
	void exportVector();
	
	/**
	 * Makes the print/export dock widget visible, and configures it for 
	 * the given task (which is of type PrintWidget::TaskFlags).
	 */
	void printClicked(int task);
	
	/** Undoes the last object edit step. */
	void undo();
	/** Redoes the last object edit step */
	void redo();
	/** Cuts the selected object(s). */
	void cut();
	/** Copies the selected object(s). */
	void copy();
	/** Pastes the object(s) from the clipboard. */
	void paste(int paste_at_center);
	/** Empties the undo / redo history to save space. */
	void clearUndoRedoHistory();
	
	/** Toggles visivbility of the map grid. */
	void showGrid();
	/** Shows the map grid configuration dialog. */
	void configureGrid();
	
	/** Activates the pan tool. */
	void pan();
	/** Moves view to GPS position. */
	void moveToGpsPos();
	/** Activates or stops follow-position mode. */
	void followPositionClicked(bool enable);
	/** Follow-position mode update handler. */
	void followPositionUpdate(OpenOrienteering::MapCoordF position);
	/** Zooms in the current map widget. */
	void zoomIn();
	/** Zooms out in the current map widget. */
	void zoomOut();
	/** Activates the box zoom tool. */
	void boxZoom(bool checked);
	/** Shows the dialog to set a custom zoom factor in the current map widget. */
	void setCustomZoomFactorClicked();
	
	/** Sets the hatch areas view option. */
	void hatchAreas(bool checked);
	/** Sets the baseline view option. */
	void baselineView(bool checked);
	/** Sets the "hide all templates" view option. */
	void hideAllTemplates(bool checked);
	/** Sets the overprinting simulation view option. */
	void overprintingSimulation(bool checked);
	
	/** Adjusts the coordinates display of the map widget to the selected option. */
	void coordsDisplayChanged();
	/** Copies the displayed coordinates to the clipboard. */
	void copyDisplayedCoords();
	
	/** Shows or hides the symbol pane. */
	void showSymbolWindow(bool show);
	/** Shows or hides the color dock widget. */
	void showColorWindow(bool show);
	/** Shows a dialog for changing the symbol set ID. */
	void symbolSetIdClicked();
	/** Shows the "load symbols from" dialog. */
	void loadSymbolsFromClicked();
	/** Loads a CRT file and shows the symbol replacement dialog. */
	void loadCrtClicked();
	/** TODO: not implemented yet. */
	void loadColorsFromClicked();
	/** Shows the "scale all symbols" dialog. */
	void scaleAllSymbolsClicked();
	
	/** Shows the ScaleMapDialog. */
	void scaleMapClicked();
	/** Shows the RotateMapDialog. */
	void rotateMapClicked();
	/** Shows the dialog to enter map notes. */
	void mapNotesClicked();
	/** Shows the map information. */
	void mapInfoClicked();
	
	/** Shows or hides the template setup dock widget. */
	void showTemplateWindow(bool show);
	/** Shows a file selector to open a template. */
	void openTemplateClicked();
	void addMapServiceClicked();  // foom
	/** foom: Asks for geodata files, then imports them with the import options (import-geodata). */
	void importGeodataClicked();
	/** foom: Asks for a delimited text file, then imports its rows as points (import-points). */
	void importPointsClicked();
	/** foom: Adds the layout part (layout-create). */
	void createLayoutClicked();
	/** foom: Hides or shows the layout part (layout-hide). */
	void hideLayoutClicked(bool hide);
	/** foom: Moves the selected objects to the layout part (to-layout). */
	void moveToLayoutClicked();
	/** foom: Moves the selected objects of the layout part to the map (to-map). */
	void moveToMapClicked();
	/** foom: Writes a field copy and its base snapshot (field-copy-create). */
	void fieldCopyCreateClicked();
	/** foom: Merges the changes of a returned field copy (merge-from-file). */
	void mergeFromFileClicked();
	/** foom: Closes a field copy (field-copy-close). */
	void fieldCopyCloseClicked();
	/** foom: Asks for a file, then writes the layout part to it (layout-save). */
	void saveLayoutClicked();
	/** foom: Asks for a file, then merges its layout part (layout-import). */
	void importLayoutClicked();
	/** foom: Adjusts the availability of the layout commands. */
	void updateLayoutActions();
	/** foom: Gives the selected objects private copies of their symbols (to-graphic). */
	void convertToGraphicClicked();
	/** foom: Edits the private symbol of the selected objects (object-style). */
	void objectStyleClicked();
	/** foom: Asks for an image file and adds it as an image object (image-add). */
	void addImageClicked();
	/** foom: Embeds the selected linked images (image-embed). */
	void embedImageClicked();
	/** foom: Asks for the opacity and layer of the selected images (image-properties). */
	void imagePropertiesClicked();
	/** foom: The generators (gen-scale-bar, gen-north-lines, gen-legend). */
	void generateScaleBarClicked();
	void generateNorthLinesClicked();
	void generateLegendClicked();
	/** foom: Runs the generator whose id the sending action holds. */
	void generateClicked();
	/** foom: Asks for the parameters of the generator, runs it at the centre of the view and selects its group. */
	void runGeneratorDialog(const QString& id);
	/** foom: Rebuilds the generated groups of the selection (regenerate). */
	void regenerateClicked();
	void updateMapServiceAction();  // foom
	/** Shows the ReopenTemplateDialog. */
	void reopenTemplateClicked();
	/** Adjusts action availability based on the presence of templates */
	void templateAvailabilityChanged();
	/** Adjusts action availability based on the presence of closed templates */
	void closedTemplateAvailabilityChanged();
	
	/** Shows or hides the tags editor dock widget. */
	void showTagsWindow(bool show);
	
	/** Shows the GeoreferencingDialog. */
	void editGeoreferencing();
	
	/**
	 * Makes the editor aware of a change of the selected symbols.
	 */
	void selectedSymbolsChanged();
	
	/**
	 * Makes the editor aware of a change of the selected object.
	 */
	void objectSelectionChanged();
	
	/** Adjusts the enabled state of the undo / redo actions. */
	void undoStepAvailabilityChanged();
	/** Adjusts the enabled state of the paste action (specific signature required). */
	void clipboardChanged(QClipboard::Mode mode);
	/** Adjusts the enabled state of the paste action. */
	void updatePasteAvailability();
	
	/**
	 * Checks the presence of spot colors,
	 * and to disables overprinting simulation if there are no spot colors.
	 */
	void spotColorPresenceChanged(bool has_spot_colors);
	
	/** Adjusts the view in the current map widget to show the whole map. */
	void showWholeMap();
	
	/** Activates the point edit tool. */
	void editToolClicked();
	/** Activates the line edit tool. */
	void editLineToolClicked();
	/** Activates the draw point tool. */
	void drawPointClicked();
	/** Activates the draw path tool. */
	void drawPathClicked();
	/** Activates the draw circle tool. */
	void drawCircleClicked();
	/** Activates the draw rectangle tool. */
	void drawRectangleClicked();
	/** foom: Asks for the parameters of a rectangle box, then starts the box tool. */
	void drawRectangleBoxClicked();
	/** foom: Starts the rectangle box tool with the given parameters. */
	void drawRectangleBox(const ops::RectangleBoxParams& params);
	/** Activates the draw freehand tool. */
	void drawFreehandClicked();
	/** Activates the draw fill tool. */
	void drawFillClicked();
	/** Activates the draw text tool. */
	void drawTextClicked();
	
	/** Deletes the selected object(s) */
	void deleteClicked();
	/** Duplicates the selected object(s) */
	void duplicateClicked();
	/** Switches the symbol of the selected object(s) to the selected symbol. */
	void switchSymbolClicked();
	/** Creates duplicates of the selected object(s) and assigns them the selected symbol. */
	void fillBorderClicked();
	/** Selects all objects with the selected symbol(s) */
	void selectObjectsClicked(bool select_exclusively);
	/** Deselects all objects with the selected symbol(s) */
	void deselectObjectsClicked();
	
	/** Selects all objects in the current map part. */
	void selectAll();
	/** Clears the object selection. */
	void selectNothing();
	/** Inverts in the object selection in the current map part. */
	void invertSelection();
	/** Selects all objects having the current selected symbols. */
	void selectByCurrentSymbols();
	
	/**
	 * Reverses the selected object(s) direcction(s),
	 * thus switching dash directions for lines.
	 */
	void switchDashesClicked();
	/** Connects close endpoints of selected lines */
	void connectPathsClicked();
	/** Activates the cut tool */
	void cutClicked();
	/** Activates the cut hole tool */
	void cutHoleClicked();
	/** Activates the cut circular hole tool */
	void cutHoleCircleClicked();
	/** Activates the cut rectangular hole tool */
	void cutHoleRectangleClicked();
	/** Activates the rotate tool */
	void rotateClicked();
	/** Activates the rotate pattern tool */
	void rotatePatternClicked();
	/** Activates the scale tool */
	void scaleClicked();
	/** Shows or hides the MeasureWidget */
	void measureClicked(bool checked);
	/** foom: Shows or hides the course dock. */
	void courseDockClicked(bool checked);
	/** foom: Adds the symbols of course setting to the map. */
	void addCourseSymbolsClicked();
	/** foom: Activates the tool which composes the course of the course dock by clicks. */
	void composeCourseClicked();
	/** foom: Puts a course object into many courses. */
	void insertIntoCoursesClicked();
	/** foom: Takes a course object out of many courses. */
	void deleteFromCoursesClicked();
	/** foom: Gives the controls new codes. */
	void renumberControlsClicked();
	/** foom: Shows the statistics of the courses and of the event. */
	void courseStatisticsClicked();
	/** foom: Runs the consistency check of the courses and shows the issues; returns their number. */
	int runCourseCheck();
	/** foom: Write the courses as IOF XML 3.0, GPX or KML, after asking for the file. */
	void exportCoursesIofClicked();
	void exportCoursesGpxClicked();
	void exportCoursesKmlClicked();
	/** foom: Takes course objects, courses and classes from an IOF XML 3.0 file. */
	void importIofCoursesClicked();
	/** foom: Writes which team runs which variant of the relays, as a text file. */
	void exportRelayVariationsClicked();
	void exportCourseMapsClicked();
	void courseCanvasesClicked();
	void analyseCourseRoutesClicked();
	void analyseAllCourseRoutesClicked();
	void showCourseRoutesClicked();
	void hideCourseRoutesClicked();
	void deleteCourseRoutesClicked();
	void exportCourseRoutesClicked();
	void routeViaPointsClicked();
	void routeFreeLegClicked();
	void routeSettingsClicked();
	/** foom: Shows or hides the rangefinder. */
	void rangefinderClicked(bool checked);
	/** foom: Switches to the tool which sets the reference of the rangefinder. */
	void rangefinderReferenceClicked();
	/** foom: Ends the path which is drawn at its last vertex, for the rangefinder. */
	void rangefinderFinish();
	/** foom: Takes the GPS position as the reference of the rangefinder. */
	void rangefinderReferenceFromGps();
	/** Calculates the union of selected same-symbol area objects */
	void booleanUnionClicked();
	/** Calculates the intersection of selected same-symbol area objects */
	void booleanIntersectionClicked();
	/** Calculates the difference of selected area objects from the first selected area object */
	void booleanDifferenceClicked();
	/** Calculates the boolean XOr of selected same-symbol area objects */
	void booleanXOrClicked();
	/** Merges holes of the (single) selected area object */
	void booleanMergeHolesClicked();
	/** Converts selected polygonal paths to curves */
	void convertToCurvesClicked();
	/** Tries to remove points of selected paths while retaining their shape */
	void simplifyPathClicked();
	/** foom: Asks for the tolerance, then replaces the curves of the selected paths by straight segments. */
	void toPolylineClicked();
	/** foom: Replaces the curves of the selected paths by straight segments ("to polyline"). */
	void toPolyline(double tolerance);
	/** foom: Asks for the axis, then mirrors the selected objects. */
	void mirrorClicked();
	/** foom: Mirrors the selected objects about an axis at the given angle (degrees) through their centre. */
	void mirror(double axis_angle, bool duplicate);
	/** foom: Asks for the offset, then moves the selected objects. */
	void moveByClicked();
	/** foom: Moves the selected objects by dx, dy (mm of map coordinates). */
	void moveBy(double dx, double dy, bool duplicate);
	/** foom: Asks for the angle and the pivot, then rotates the selected objects. */
	void rotateByClicked();
	/** foom: Rotates the selected objects by the angle (degrees, counter-clockwise) about the common centre or each object's own. */
	void rotateBy(double angle, bool each_object, bool duplicate);
	/** foom: Asks for the factors, then stretches the selected objects. */
	void stretchClicked();
	/** foom: Stretches the selected objects by the factors about their common centre. */
	void stretch(double factor_x, double factor_y, bool duplicate);
	/** foom: Aligns the selected objects; the sending action's data holds the edge. */
	void alignClicked();
	/** foom: Moves every selected object but the first-selected one so that the named edge or centre of its extent matches the first's. */
	void align(ops::AlignParams::Edge edge);
	/** foom: Distributes the selected objects; the sending action's data says whether horizontally. */
	void distributeClicked();
	/** foom: Spaces the centres of the selected objects equally between the two outermost. */
	void distribute(bool horizontal);
	/** foom: Activates the reshape tool. */
	void reshapeClicked();
	/** foom: Activates the lasso selection tool. */
	void lassoClicked();
	/** foom: The sketch layer: pen, eraser, note, list and flattening. */
	void sketchPenClicked();
	void sketchEraserClicked();
	void sketchNoteClicked();
	void sketchManageClicked();
	void sketchFlattenClicked();
	/** foom: Activates the move-parallel tool. */
	void moveParallelClicked();
	/** foom: Shows the move-parallel-by dialog, which stays open. */
	void moveParallelByClicked();
	/** foom: Offsets the selected path objects by the distance (mm, positive to the left of travel). */
	void moveParallelBy(double distance, bool duplicate);
	/** foom: Sets or clears the dash flag of the selected paths' vertices; the sending action's data says which. */
	void setVertexTypeClicked();
	/** foom: Sets (dash) or clears the dash flag of every vertex of the selected paths. */
	void setVertexType(bool dash, bool keep_ends);
	/** foom: Asks for the tolerance, then joins the selected lines. */
	void joinLinesClicked();
	/** foom: Joins the selected open lines of one symbol whose ends meet within the tolerance (mm). */
	void joinLines(double tolerance);
	/** foom: Asks for the scope, then closes open area outlines. */
	void closeAreasClicked();
	/** foom: Closes the open area outlines in the scope (a TopologyScope value). */
	void closeAreas(int scope);
	/** foom: Asks for the scope, then normalises the winding of areas. */
	void areasAnticlockwiseClicked();
	/** foom: Makes outer rings anticlockwise and holes clockwise in the scope (a TopologyScope value). */
	void areasAnticlockwise(int scope);
	/** foom: Asks for the parameters, then fixes overshoots and undershoots among the selected paths. */
	void fixOvershootsClicked();
	/** foom: Trims and extends the selected lines' ends against the other selected paths. */
	void fixOvershoots(const ops::OvershootParams& params);
	/** foom: Asks what to do at the intersections of the selected paths, then does it. */
	void intersectionsClicked();
	/** foom: Inserts vertices, cuts or adds points at the intersections of the selected paths. */
	void intersections(const ops::IntersectionParams& params);
	/** foom: Drops repeated vertices from the selected paths. */
	void removeDuplicateVertices();
	/** foom: Asks for the parameters, then smooths the selected paths. */
	void smoothClicked();
	/** foom: Simplifies and curve-fits the selected paths. */
	void smoothPaths(const ops::SmoothParams& params);
	/** foom: Asks for the parameters, then squares the selected closed paths. */
	void makeRectangularClicked();
	/** foom: Squares the selected closed paths; the reference in the parameters is left alone. */
	void makeRectangular(const ops::RectangularParams& params);
	/** foom: Asks for the mode and parameters, then generalizes the selected buildings. */
	void generalizeBuildingsClicked();
	/** foom: Simplifies the outlines of the selected closed paths. */
	void simplifyBuildings(const ops::SimplifyBuildingParams& params);
	/** foom: Replaces the selected closed paths by rectangles. */
	void buildingRectangles(const ops::RectangleParams& params);
	/** foom: Asks for a grid file and options, then sets it as the map's elevation grid (dem-open). */
	void elevationOpenClicked();
	/** foom: Shows extent, cell size, minimum, maximum and void count of the terrain grid (dem-info). */
	void elevationInfoClicked();
	/** foom: Asks for the contour sets, then generates contour objects (dem-contours). */
	void elevationContoursClicked();
	/** foom: Asks for the parameters, then writes and attaches a hill shading (dem-hillshade). */
	void elevationHillShadeClicked();
	/** foom: Asks for the parameters, then writes and attaches a slope gradient raster (dem-slope). */
	void elevationSlopeClicked();
	/** foom: Asks for the ramp, then writes and attaches a hypsometric map (dem-hypsometric). */
	void elevationHypsometricClicked();
	/** foom: Writes the hypsometric map of the terrain grid beside the map; returns its path, empty on failure. */
	QString writeHypsometricMap(const HypsometricParams& params, bool attach, QString* error);
	/** foom: Asks for the minimum height and depth, then extracts knolls and small depressions (dem-features). */
	void elevationFeaturesClicked();
	/** foom: Extracts knolls and small depressions of the terrain model as one undo step; returns their number. */
	int extractLandforms(const LandformParams& params);
	/** foom: Asks for point clouds and grids them (dem-point-cloud). */
	void elevationPointCloudClicked();
	/** foom: Asks for metres east and north and moves the map (map-move). */
	void mapMoveClicked();
	/** foom: Asks for stretch factors and mirrors (map-stretch). */
	void mapStretchClicked();
	/** foom: As map-stretch, with both mirrors ticked (map-mirror). */
	void mapMirrorClicked();
	/** foom: Starts the affine tool (map-affine). */
	void mapAffineClicked();
	/** foom: Starts the rubbersheet tool (map-rubbersheet). */
	void mapRubbersheetClicked();
	/** foom: Asks for the radius, then starts the local transformation tool (map-local-warp). */
	void mapLocalWarpClicked();
	/** foom: Asks for the target system and changes the map to it (map-reproject). */
	void mapReprojectClicked();
	/** foom: Changes the map's coordinate reference system; see the design notes. */
	bool reprojectMap(const QString& id, const QString& spec, const std::vector<QString>& params);
	/** foom: Asks for the declination and turns the map to magnetic north (map-rotate-magnetic). */
	void mapRotateMagneticClicked();
	/** foom: Turns the map to magnetic north; see the design notes. */
	bool rotateMapToMagneticNorth(double declination, bool keep_selected_symbols, bool keep_layout);
	/** foom: Asks for the new offset and centres the map (map-center). */
	void mapCenterClicked();
	/** foom: Moves the map so that the projected offset is its origin; see the design notes. */
	bool centerMap(const QPointF& offset);
	/** foom: Starts the local transformation tool with the settings, without asking. */
	void startLocalWarp(const LocalWarpParams& settings);
	/** foom: One drag of the local transformation; see the design notes. */
	bool localWarpMapWith(const LocalWarpParams& params);
	/** foom: Rubbersheets the map; see the design notes. */
	bool rubbersheetMapWith(const RubbersheetParams& params);
	/** foom: Transforms the map by the fit to the point pairs, turning orientations; see the design notes. */
	bool affineTransformMap(const std::vector<PointPair>& pairs);
	/** foom: Moves the map in the world; see the design notes. */
	bool moveMap(double east_m, double north_m, bool carry);
	/** foom: Stretches and mirrors the map about its origin. */
	bool stretchMap(double factor_x, double factor_y, bool mirror_horizontally, bool mirror_vertically, bool carry);
	/** foom: Grids the point clouds into the map's terrain and surface models, saved as GeoTIFF, and makes the products. */
	bool importPointClouds(const QStringList& paths, const GriddingParams& params, const PointCloudProducts& products, QString* error);
	/** foom: Saves the terrain grid in a chosen format (dem-export). */
	void elevationExportClicked();
	/** foom: Asks for grids and merges them after the terrain grid (dem-merge). */
	void elevationMergeClicked();
	/** foom: Makes the canopy grid of the surface less the terrain (dem-difference). */
	void elevationDifferenceClicked();
	/** foom: Asks for a box and crops the terrain grid (dem-resize). */
	void elevationResizeClicked();
	/** foom: Asks for a radius and smooths the terrain grid (dem-smooth). */
	void elevationSmoothClicked();
	/** foom: Asks, then removes every elevation grid from the map (dem-close). */
	void elevationCloseClicked();
	/** foom: The grid commands without asking; see the design notes, "Grid commands". */
	bool mergeElevationFiles(const QStringList& paths, QString* error);
	bool elevationDifference(QString* error);
	bool cropElevation(const QRectF& box, QString* error);
	bool smoothElevation(int radius, QString* error);
	void closeElevation();
	/** foom: Runs the map check and shows the issues; returns their number. */
	int runMapCheck();
	/** foom: Asks for the legibility parameters, then runs the scan. */
	void checkLegibilityClicked();
	/** foom: Runs the legibility scan and shows the issues; returns their number. */
	int runLegibilityCheck(const LegibilityParams& params);
	/** foom: Repeats whichever check ran last (the map check when none did). */
	void checkAgain();
	/** foom: Shows or hides the Issues window. */
	void showIssuesWindow(bool show);
	/** foom: Applies the fix of the issue shown in the row and checks again; returns false when nothing changed. */
	bool fixIssueRow(int row);
	/** foom: Applies the fixes of every shown issue and checks again; returns how many changed something. */
	int fixShownIssues();
	/** foom: Selects the objects of an activated issue and shows them. */
	void issueActivated(const QVector<QUuid>& objects, const OpenOrienteering::MapCoordF& where);
	/** foom: Opens the extent dialog for a new extent. */
	void extentDefineClicked();
	/** foom: The extent which the dialog proposes: the next free name, the print area or the default one. The map gets no print area. */
	Extent newExtent() const;
	/** foom: Adds the extent, or replaces the one of its name, as one undo step. */
	void defineExtent(const Extent& extent);
	/** foom: Asks which extent to delete. */
	void extentDeleteClicked();
	/** foom: Asks for a map file and takes its extents (extent-import). */
	void extentImportClicked();
	/** foom: Deletes the extent of the name as one undo step; returns false when there is none. */
	bool deleteExtent(const QString& name);
	/** foom: Makes the extent of the name the active one, drawn in the view; an empty name clears it. */
	void setActiveExtent(const QString& name);
	/** foom: Returns the name of the active extent, or an empty string. */
	const QString& activeExtent() const { return active_extent_name; }
	/** foom: Makes the extent the sending action names active. */
	void activeExtentClicked();
	/** foom: Rebuilds the active extent submenu. */
	void updateExtentMenus();
	/** foom: Opens the find-and-replace-text dialog, which stays open. */
	void findReplaceTextClicked();
	/** foom: Selects the next matching text object after the first selected one, wrapping round; returns false when nothing matches. */
	bool findNextText(const ops::ReplaceTextParams& params, bool selected_symbols_only);
	/** foom: Replaces the matches in the scope, or in the selected objects of the scope; returns the number of changed objects. */
	int replaceText(const ops::ReplaceTextParams& params, bool selected_symbols_only, bool selected_objects_only);
	/** foom: Deletes the matching text objects of the scope, or the selected ones; returns their number. */
	int deleteMatchingText(const ops::ReplaceTextParams& params, bool selected_symbols_only, bool selected_objects_only);
	/** foom: Asks for a name, then stores the current view as a bookmark. */
	void bookmarkAddClicked();
	/** foom: Stores the current view centre and zoom as a bookmark with the name. */
	void addBookmark(const QString& name);
	/** foom: Opens the bookmark dialog. */
	void bookmarkManageClicked();
	/** foom: Moves the view to the bookmark with the index. */
	void jumpToBookmark(int index);
	/** foom: Moves the view to the bookmark the sending action names. */
	void bookmarkJumpClicked();
	/** foom: Rebuilds the bookmarks submenu. */
	void updateBookmarksMenu();
	/** foom: Records the current view in the view history. */
	void recordView();
	/** foom: Starts the delay after which a settled view is recorded. */
	void viewChangedForHistory(OpenOrienteering::MapView::ChangeFlags change);
	/** foom: Returns to the previous view of the history. */
	void viewBack();
	/** foom: Goes to the next view of the history. */
	void viewForward();
	/** foom: Asks for a name, then groups the selected objects. */
	void groupClicked();
	/** foom: Gives the selected objects the next free group number, and the group the name. */
	void groupSelection(const QString& name);
	/** foom: Clears the group of the selected objects. */
	void ungroupSelection();
	/** foom: Adds the groups of the selected objects to the selection. */
	void selectGroup();
	/** Activates the physical cutout tool */
	void clipAreaClicked();
	/** Activates the physical cutout tool (inversed) */
	void eraseAreaClicked();
	/** Executes the "distribute points along path" action.
	 *  The prerequisites for using the tool must be given. */
	void distributePointsClicked();
	
	/** Enables or disables GPS display. */
	void enableGPSDisplay(bool enable);
	/** Enables or disables showing distance rings when GPS display is active. */
	void enableGPSDistanceRings(bool enable);
	/** Updates availability of the GPS point drawing tool. */
	void updateDrawPointGPSAvailability();
	/** foom: Switches the recording of GPS positions into the path on or off. */
	void gpsRecordClicked(bool checked);
	/** foom: Pauses the recording or goes on with it. */
	void gpsRecordPauseClicked(bool checked);
	/** foom: Adds a vertex at the current GPS position. */
	void gpsAddVertexClicked();
	/** foom: Starts averaging GPS positions for a vertex, or ends it and adds the vertex. */
	void gpsAverageVertexClicked(bool checked);
	/** foom: Switches to the tool which sets the offset of the GPS position. */
	void gpsSetOffsetClicked();
	/** foom: Removes the offset of the GPS position. */
	void gpsClearOffsetClicked();
	/** foom: Shows the size of the offset of the GPS position in the status bar. */
	void showGpsOffsetMessage();
	/** foom: Updates the availability of the recording commands, and ends a recording which cannot go on. */
	void updateGpsRecordActions();
	/** Switches to the GPS point drawing tool. */
	void drawPointGPSClicked();
	/** Sets a temporary marker at the GPS position. */
	void gpsTemporaryPointClicked();
	/** Draws a temporary path at the GPS position. */
	void gpsTemporaryPathClicked(bool enable);
	/** Clears temporary GPS markers. */
	void gpsTemporaryClearClicked();
	
	/** Enables or disables digital compass display. */
	void enableCompassDisplay(bool enable);
	/** Enables or disables map auto-rotation according to compass. */
	void alignMapWithNorth(bool enable);
	/** Called regularly after enabled with alignMapWithNorth() to update the map rotation. */
	void alignMapWithNorthUpdate();
	
	/** For mobile UI: hides the top action bar. */
	void hideTopActionBar();
	/** For mobile UI: shows the top action bar again after hiding it. */
	void showTopActionBar();
	/** For mobile UI: shows the symbol selection screen. */
	void mobileSymbolSelectorClicked();
	/** Counterpart to mobileSymbolSelectorClicked(). */
	void mobileSymbolSelectorFinished();

	/** Creates and adds a new map part */
	void addMapPart();
	/** Removes the current map part */
	void removeMapPart();
	/** Renames the current map part */
	void renameMapPart();
	/** Moves all selected objects to a different map part */
	void reassignObjectsToMapPart(int target);
	/** Merges the current map part with another one */
	void mergeCurrentMapPartTo(int target);
	/** Merges all map parts into the current one. */
	void mergeAllMapParts();
	
	/** Updates action enabled states after a template has been added */
	void templateAdded(int pos, const OpenOrienteering::Template* temp);
	/** Updates action enabled states after a template has been deleted */
	void templateDeleted(int pos, const OpenOrienteering::Template* temp);
	
	/** Shows the import file selector and imports the selected file, if any. */
	void importClicked();
	
public:
	/**
	 * Imports another map into this map, after successful symbol replacement.
	 * 
	 * The filepath given in crt_file_hint is used in addition (and preferred)
	 * to the symbol set IDs to locate a CRT file for initializing the
	 * replacement table.
	 * 
	 * Returns true on success.
	 */
	bool importMapWithReplacement(
	        Map& imported_map,
	        Map::ImportMode mode,
	        const QString& crt_file_hint
	);
	
	/**
	 * Imports another map into this map.
	 * 
	 * This method changes the given 'other' map if the	maps' scales differ.
	 * This is an optimization for the use cases where temporary maps are
	 * created just for this kind of import.
	 * 
	 * \see Map::importMap
	 */
	QHash<const Symbol*, Symbol*> importMap(
	        Map& other,
	        Map::ImportMode mode,
	        QWidget* dialog_parent,
	        std::vector<bool>* filter = nullptr,
	        int symbol_insert_pos = -1,
	        bool merge_duplicate_symbols = true
	);
	
	/** Imports a track file (GPX) into the map */
	bool importGpxFile(const QString& filename);
	/** Imports a map file into the loaded map */
	bool importMapFile(const QString& filename, bool show_errors);
	/** Imports a data file supported by OGR into the map */
	bool importOgrFile(const QString& filename);
	/** foom: Shows the import options for the files, then imports them. */
	bool importGeodataFiles(const QStringList& filenames);
	
public slots:
	/** Sets the enabled state of actions which change how the map is rendered,
	 *  such as with grid, with templates, with overprinting simulation. */
	void setViewOptionsEnabled(bool enabled = true);
	
	/**
	 * Indicates a change of the current toolbar and dock widget visibilities
	 * and locations, and schedules saving.
	 */
	void setWindowStateChanged();
	
private:
	/**
	 * Saves the window state in the permanent settings.
	 * 
	 * The window state consists of current toolbar and dock widget visibility
	 * and locations.
	 * 
	 * This function does nothing in mobile mode or symbol editor mode.
	 */
	void saveWindowState();
	
	/**
	 * Restores previously saved toolbar and dock widget positions.
	 * 
	 * This function does nothing in mobile mode or symbol editor mode.
	 */
	void restoreWindowState();
	
signals:
	/**
	 * @brief Indicates a change of the active symbol.
	 * @param symbol The new active symbol, or nullptr.
	 */
	void activeSymbolChanged(const OpenOrienteering::Symbol* symbol);
	
	void templatePositionDockWidgetClosed(OpenOrienteering::Template* temp);
	
	/** foom: Indicates a change of the active tool, which may be nullptr. */
	void toolChanged(OpenOrienteering::MapEditorTool* tool);

protected:
	/**
	 * Adjusts the enabled state of various actions
	 * after the selected symbol(s) have changed.
	 * 
	 * In addition, it disables actions as long as some editing is in progress.
	 * 
	 * The caller shall also call updateSymbolAndObjectDependentActions().
	 */
	void updateSymbolDependentActions();
	
	/**
	 * Adjusts the enabled state of various actions
	 * after the selected object(s) have changed.
	 * 
	 * In addition, it disables actions as long as some editing is in progress.
	 * 
	 * The caller shall also call updateSymbolAndObjectDependentActions().
	 */
	void updateObjectDependentActions();
	
	/**
	 * Adjusts the enabled state of various actions
	 * after the selected symbol(s) or object(s) have changed.
	 * 
	 * In addition, it disables actions as long as some editing is in progress.
	 */
	void updateSymbolAndObjectDependentActions();
	
protected slots:
	void projectionChanged();
	void georeferencingDialogFinished();
	
	/**
	 * Sets the map's current part.
	 */
	void changeMapPart(int index);
	
	/**
	 * Updates all UI components related to map parts.
	 */
	void updateMapPartsUI();
	
private:
	void setMapAndView(Map* map, MapView* map_view);
	
	/// Updates enabled state of all widgets
	void updateWidgets();
	
	void createSymbolWidget(QWidget* parent = nullptr);
	
	void createColorWindow();
	
	void createTemplateWindow();
	
	void createTagEditor();
	
	QAction* newAction(const char* id, const QString& tr_text, QObject* receiver, const char* slot, const char* icon = nullptr, const QString& tr_tip = QString{}, const char* whats_this_link = nullptr);
	QAction* newCheckAction(const char* id, const QString& tr_text, QObject* receiver, const char* slot, const char* icon = nullptr, const QString& tr_tip = QString{}, const char* whats_this_link = nullptr);
	QAction* newToolAction(const char* id, const QString& tr_text, QObject* receiver, const char* slot, const char* icon = nullptr, const QString& tr_tip = QString{}, const char* whats_this_link = nullptr);
	QAction* findAction(const char* id);
	void assignKeyboardShortcuts();
	void createActions();
	void createMenuAndToolbars();
	void createMobileGUI();
	
	void doUndo(bool redo);
	
	Map* map;
	MapView* main_view;
	MapWidget* map_widget;
	
	OperatingMode mode;
	bool mobile_mode;
	bool window_state_changed = false;
	
	MapEditorTool* current_tool;
	MapEditorTool* override_tool;
	MapEditorActivity* editor_activity;
	
	Symbol* active_symbol;
	
	bool editing_in_progress;
	
	// Action handling
	QHash<QByteArray, QAction*> actionsById;
	QHash<QByteArray, QKeySequence> default_shortcuts;  // foom
	
	EditorDockWidget* print_dock_widget;
	PrintWidget* print_widget;
	
	QAction* print_act = {};
	QAction* export_image_act = {};
	QAction* export_kmz_act = {};
	QAction* export_simple_course_act = {};
	QAction* export_pdf_act = {};
	QAction* export_vector_act = {};
	
	QAction* undo_act = {};
	QAction* redo_act = {};
	QAction* cut_act = {};
	QAction* copy_act = {};
	QAction* paste_act = {};
	QAction* paste_original_act = {};
	QAction* delete_act = {};
	QAction* select_all_act = {};
	QAction* select_nothing_act = {};
	QAction* invert_selection_act = {};
	QAction* select_by_current_symbol_act = {};
	std::unique_ptr<MapFindFeature> find_feature;
	QAction* clear_undo_redo_history_act = {};
	
	QAction* pan_act = {};
	QAction* move_to_gps_pos_act = {};
	QAction* follow_position_act = {};
	QAction* zoom_in_act = {};
	QAction* zoom_out_act = {};
	QAction* box_zoom_act = {};
	QAction* show_all_act = {};
	QAction* fullscreen_act = {};
	QAction* custom_zoom_act = {};
	QAction* show_grid_act = {};
	QAction* configure_grid_act = {};
	QAction* hatch_areas_view_act = {};
	QAction* baseline_view_act = {};
	QAction* hide_all_templates_act = {};
	QAction* overprinting_simulation_act = {};
	
	QAction* map_coordinates_act = {};
	QAction* projected_coordinates_act = {};
	QAction* geographic_coordinates_act = {};
	QAction* geographic_coordinates_dms_act = {};
	
	QMenu* toolbars_menu = {};
	
	QAction* scale_all_symbols_act = {};
	QAction* georeferencing_act = {};
	QAction* scale_map_act = {};
	QAction* rotate_map_act = {};
	QAction* map_notes_act = {};
	QAction* map_info_act = {};
	QAction* symbol_set_id_act = {};
	std::unique_ptr<SymbolReportFeature> symbol_report_feature;
	
	
	QAction* color_window_act = {};
	QPointer<EditorDockWidget> color_dock_widget;
	QAction* load_symbols_from_act = {};
	QAction* load_crt_act = {};
	
	QAction* symbol_window_act = {};
	EditorDockWidget* symbol_dock_widget;
	SymbolWidget* symbol_widget;
	
	QAction* template_window_act = {};
	QPointer<QWidget> template_dock_widget;
	TemplateListWidget* template_list_widget;
	QAction* open_template_act = {};
	QAction* reopen_template_act = {};
	QAction* add_map_service_act = {};  // foom
	QAction* import_geodata_act = {};  // foom
	QAction* import_points_act = {};  // foom
	QAction* layout_create_act = {};  // foom
	QAction* layout_hide_act = {};  // foom
	QAction* to_layout_act = {};  // foom
	QAction* to_map_act = {};  // foom
	QAction* layout_save_act = {};  // foom
	QAction* field_copy_create_act = {};  // foom
	QAction* merge_from_file_act = {};  // foom
	QAction* field_copy_close_act = {};  // foom
	QAction* layout_import_act = {};  // foom
	QAction* to_graphic_act = {};  // foom
	QAction* object_style_act = {};  // foom
	QAction* image_add_act = {};  // foom
	QAction* image_embed_act = {};  // foom
	QAction* image_properties_act = {};  // foom
	QAction* gen_scale_bar_act = {};  // foom
	QAction* gen_north_lines_act = {};  // foom
	QAction* gen_legend_act = {};  // foom
	std::vector<QAction*> more_generator_acts;  // foom
	QAction* regenerate_act = {};  // foom
	
	QAction* tags_window_act = {};
	QPointer<EditorDockWidget> tags_dock_widget;
	
	QAction* edit_tool_act = {};
	QAction* edit_line_tool_act = {};
	QAction* draw_point_act = {};
	QAction* draw_path_act = {};
	QAction* draw_circle_act = {};
	QAction* draw_rectangle_act = {};
	QAction* draw_rectangle_box_act = {};  // foom
	QAction* draw_freehand_act = {};
	QAction* draw_fill_act = {};
	QAction* draw_text_act = {};
	
	QAction* duplicate_act = {};
	QAction* switch_symbol_act = {};
	QAction* fill_border_act = {};
	QAction* switch_dashes_act = {};
	QAction* connect_paths_act = {};
	QAction* cut_tool_act = {};
	QMenu* cut_hole_menu = {};
	QAction* cut_hole_act = {};
	QAction* cut_hole_circle_act = {};
	QAction* cut_hole_rectangle_act = {};
	QAction* rotate_act = {};
	QAction* rotate_pattern_act = {};
	QAction* scale_act = {};
	QAction* measure_act = {};
	EditorDockWidget* measure_dock_widget;
	QAction* course_dock_act = {};  // foom
	QAction* course_symbols_act = {};  // foom
	QAction* compose_course_act = {};  // foom
	QAction* course_insert_act = {};  // foom
	QAction* course_delete_act = {};  // foom
	QAction* course_renumber_act = {};  // foom
	QAction* course_statistics_act = {};  // foom
	QAction* course_check_act = {};  // foom
	QAction* course_export_iof_act = {};  // foom
	QAction* course_export_gpx_act = {};  // foom
	QAction* course_export_kml_act = {};  // foom
	QAction* course_import_iof_act = {};  // foom
	QAction* course_relay_variations_act = {};  // foom
	QAction* course_export_maps_act = {};  // foom
	QAction* course_canvases_act = {};  // foom
	QAction* course_routes_analyse_act = {};  // foom
	QAction* course_routes_analyse_all_act = {};  // foom
	QAction* course_routes_show_act = {};  // foom
	QAction* course_routes_hide_act = {};  // foom
	QAction* course_routes_delete_act = {};  // foom
	QAction* course_routes_export_act = {};  // foom
	QAction* course_routes_via_act = {};  // foom
	QAction* course_routes_leg_act = {};  // foom
	QAction* course_routes_settings_act = {};  // foom
	/** foom: The routes which the map keeps are drawn with the course; the design notes, "Route analysis: all courses, and the routes in the file". */
	bool show_course_routes = false;
	QPointer<QWidget> course_dock_widget;  // foom
	QPointer<CourseWidget> course_widget;  // foom
	QAction* rangefinder_act = {};  // foom
	QAction* rangefinder_reference_act = {};  // foom
	QPointer<RangefinderWidget> rangefinder_widget;  // foom
	QPointer<QWidget> rangefinder_container;  // foom: the dock, or in the mobile interface the widget
	QAction* boolean_union_act = {};
	QAction* boolean_intersection_act = {};
	QAction* boolean_difference_act = {};
	QAction* boolean_xor_act = {};
	QAction* boolean_merge_holes_act = {};
	QAction* convert_to_curves_act = {};
	QAction* simplify_path_act = {};
	QAction* to_polyline_act = {};  // foom
	double to_polyline_tolerance = 0.05;  // foom: mm on paper, kept for the session
	QAction* mirror_act = {};  // foom
	double mirror_axis_angle = 90.0;  // foom: degrees, kept for the session
	bool mirror_duplicate = false;  // foom: kept for the session
	QAction* move_by_act = {};  // foom
	QAction* rotate_by_act = {};  // foom
	QAction* stretch_act = {};  // foom
	ops::MoveParams move_by_params;  // foom: kept for the session
	ops::RotateParams rotate_by_params;  // foom: kept for the session
	ops::StretchParams stretch_params;  // foom: kept for the session
	QAction* align_acts[6] = {};  // foom: indexed by ops::AlignParams::Edge
	QAction* distribute_h_act = {};  // foom
	QAction* distribute_v_act = {};  // foom
	QAction* reshape_act = {};  // foom
	QAction* lasso_act = {};  // foom
	QAction* sketch_pen_act = {};  // foom
	QAction* sketch_eraser_act = {};  // foom
	QAction* sketch_note_act = {};  // foom
	QAction* sketch_manage_act = {};  // foom
	QAction* sketch_flatten_act = {};  // foom
	QAction* move_parallel_act = {};  // foom
	QAction* move_parallel_by_act = {};  // foom
	QPointer<MoveParallelDialog> move_parallel_dialog;  // foom
	QAction* find_replace_text_act = {};  // foom
	QAction* extent_define_act = {};  // foom
	QAction* check_map_act = {};  // foom
	QAction* topo_join_act = {};  // foom
	QAction* close_areas_act = {};  // foom
	QAction* areas_ccw_act = {};  // foom
	QAction* fix_overshoots_act = {};  // foom
	QAction* intersections_act = {};  // foom
	QAction* remove_duplicate_vertices_act = {};  // foom
	QAction* topo_smooth_act = {};  // foom
	QAction* make_rectangular_act = {};  // foom
	QAction* generalize_buildings_act = {};  // foom
	QAction* dem_open_act = {};  // foom
	QAction* map_move_act = {};  // foom
	QAction* map_stretch_act = {};  // foom
	QAction* map_mirror_act = {};  // foom
	QAction* map_affine_act = {};  // foom
	QAction* map_rubbersheet_act = {};  // foom
	QAction* map_local_warp_act = {};  // foom
	QAction* map_reproject_act = {};  // foom
	QAction* map_rotate_magnetic_act = {};  // foom
	QAction* map_center_act = {};  // foom
	QAction* dem_point_cloud_act = {};  // foom
	QAction* dem_info_act = {};  // foom
	QAction* dem_contours_act = {};  // foom
	QAction* dem_hillshade_act = {};  // foom
	QAction* dem_slope_act = {};  // foom
	QAction* dem_hypsometric_act = {};  // foom
	QAction* dem_features_act = {};  // foom
	QAction* dem_export_act = {};  // foom
	/// foom: Saves the grid beside the map and makes it the kind's grid (an ElevationSet::Kind).
	bool storeElevationGrid(int kind, std::unique_ptr<ElevationModel> grid, QString* error);
	QAction* dem_merge_act = {};  // foom
	QAction* dem_difference_act = {};  // foom
	QAction* dem_resize_act = {};  // foom
	QAction* dem_smooth_act = {};  // foom
	QAction* dem_close_act = {};  // foom
	QAction* check_legibility_act = {};  // foom
	std::function<std::vector<Issue>()> last_check;  // foom: what Check again repeats
	QAction* issues_window_act = {};  // foom
	IssueModel* issue_model = nullptr;  // foom
	QPointer<EditorDockWidget> issues_dock_widget;  // foom
	QAction* extent_delete_act = {};  // foom
	QAction* extent_import_act = {};  // foom
	QMenu* active_extent_menu = nullptr;  // foom
	QString active_extent_name;  // foom: view state
	QPointer<FindReplaceTextDialog> find_replace_text_dialog;  // foom
	NumericEntryBar* numeric_entry_bar = nullptr;  // foom
	QAction* view_back_act = {};  // foom
	QAction* view_forward_act = {};  // foom
	QAction* bookmark_add_act = {};  // foom
	QAction* bookmark_manage_act = {};  // foom
	QMenu* bookmarks_menu = nullptr;  // foom
	ViewHistory view_history;  // foom
	QTimer view_history_timer;  // foom
	bool applying_view_history = false;  // foom
	QAction* vertex_type_normal_act = {};  // foom
	QAction* vertex_type_dash_act = {};  // foom
	QAction* vertex_type_keep_ends_act = {};  // foom
	QAction* group_act = {};  // foom
	QAction* ungroup_act = {};  // foom
	QAction* select_group_act = {};  // foom
	QAction* clip_area_act = {};
	QAction* erase_area_act = {};
	QAction* distribute_points_act = {};
	
	std::unique_ptr<PaintOnTemplateFeature> paint_feature;
	
	QAction* touch_cursor_action = {};
	QAction* gps_display_action = {};
	QAction* gps_distance_rings_action = {};
	QAction* draw_point_gps_act = {};
	QAction* gps_temporary_point_act = {};
	QAction* gps_temporary_path_act = {};
	QAction* gps_temporary_clear_act = {};
	QAction* gps_record_act = {};  // foom
	QAction* gps_record_pause_act = {};  // foom
	QAction* gps_add_vertex_act = {};  // foom
	QAction* gps_average_vertex_act = {};  // foom
	QAction* gps_set_offset_act = {};  // foom
	QAction* gps_clear_offset_act = {};  // foom
	std::unique_ptr<GpsPositionAverage> gps_position_average;  // foom
	std::unique_ptr<GpsPathRecorder> gps_path_recorder;  // foom
	GPSTemporaryMarkers* gps_marker_display;
	GPSDisplay* gps_display;
	GPSTrackRecorder* gps_track_recorder;
	QAction* compass_action = {};
	CompassDisplay* compass_display;
	QAction* align_map_with_north_act = {};
	QTimer align_map_with_north_timer;
	
	QAction* mappart_add_act = {};
	QAction* mappart_rename_act = {};
	QAction* mappart_remove_act = {};
	QAction* mappart_merge_act = {};
	QMenu* mappart_merge_menu;
	QMenu* mappart_move_menu;
	
	QAction* import_act = {};
	
	QFrame* statusbar_zoom_frame;
	QLabel* statusbar_cursorpos_label;
	QAction* copy_coords_act = {};
	
	QToolBar* toolbar_view;
	QToolBar* toolbar_drawing;
	QToolBar* toolbar_editing;
	QToolBar* toolbar_advanced_editing;
	QToolBar* toolbar_mapparts = nullptr;
	
	// For mobile UI
	ActionGridBar* bottom_action_bar = nullptr;
	ActionGridBar* top_action_bar = nullptr;
	QToolButton* show_top_bar_button;
	QAction* mobile_symbol_selector_action;
	QMenu* mobile_symbol_button_menu;
	
	QPointer<QComboBox> mappart_selector_box;
	
	QScopedPointer<GeoreferencingDialog> georeferencing_dialog;
	QScopedPointer<ReopenTemplateDialog> reopen_template_dialog;
	
	QSignalMapper* mappart_merge_mapper;
	QSignalMapper* mappart_move_mapper;
};



//### MapEditorController inline code ###

inline
Symbol* MapEditorController::activeSymbol() const
{
	return active_symbol;
}


}  // namespace OpenOrienteering

#endif
