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


#ifndef FOOM_COURSE_WIDGET_H
#define FOOM_COURSE_WIDGET_H

#include <Qt>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QWidget>

#include "course/course_data.h"

class QAction;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QScrollArea;
class QSpinBox;
class QTableWidget;
class QTreeWidget;
class QTreeWidgetItem;

namespace OpenOrienteering {

class CourseDiagramWidget;
class Map;


/**
 * The course dock: the course objects, the courses with their items, and
 * the classes of a course; and for the course object which is selected,
 * the cells, the size and the text of its description.
 *
 * Every change goes into the map as one undo step. The functions which
 * take a name or a code ask nothing; the buttons ask and then call them.
 *
 * See the design notes, "Course dock" and "The editor of
 * a description".
 */
class CourseWidget : public QWidget
{
Q_OBJECT
public:
	/** The data of a row of the list of course objects. */
	enum RowRole
	{
		CodeRole = Qt::UserRole,
		UnusedRole,  ///< true for an object which could be in a course and is in none
		UuidRole     ///< the uuid of the map object
	};
	
	/** The data of an entry of a list of description symbols. */
	enum CellRole
	{
		CellNumberRole = Qt::UserRole,  ///< the number of the symbol as the cells have it; 0 for the empty entry
		CellMissingRole                 ///< true for the entry of a symbol which the map has not
	};
	
	explicit CourseWidget(Map* map, QWidget* parent = nullptr);
	~CourseWidget() override;
	
	/** The name of the course which is chosen; empty for all course objects. */
	QString currentCourse() const;
	void setCurrentCourse(const QString& name);
	
	/** Gives a course object another code. */
	bool changeCode(const QString& old_code, const QString& new_code);
	/** Deletes a course object: its map object, or the text block. */
	bool deleteObject(const QString& code);
	/** Deletes course objects as one undo step. */
	bool deleteObjects(const QStringList& codes);
	
	/** Puts the selected course objects into the course, behind the current item. */
	void insertSelectedObjects();
	void removeCurrentItem();
	/** Moves the current item up (-1) or down (1). */
	void moveCurrentItem(int direction);
	
	bool addCourse(const QString& name);
	bool duplicateCurrentCourse(const QString& name);
	bool renameCurrentCourse(const QString& name);
	void removeCurrentCourse();
	
	/** Adds a class to the course which is chosen. */
	bool addClass(const QString& name);
	void removeCurrentClass();
	
	/**
	 * Makes the course which is chosen a normal course, a relay or a one-man
	 * relay of so many legs, as one undo step. What is refused changes
	 * nothing; lastRefusal() says why.
	 */
	bool setCourseType(Course::Type type, int legs);
	/** Puts a team variation of so many branches behind the current item; its first branch marker is current then. */
	bool addTeamVariation(int branches);
	/** Puts a leg variation behind the current item; a text for every branch names its legs, as "1-2". */
	bool addLegVariation(const QStringList& branch_legs);
	/** Removes the variation of the marker which is the current item, with all that is in it. */
	bool removeCurrentVariation();
	/**
	 * Puts an item which has no course object behind the current one: a
	 * mandatory crossing point, a mandatory passage, a map exchange or a map flip.
	 */
	bool insertMarker(CourseItem::Type type);
	/** The part of the course which is drawn, counted from 0; -1 for the whole course. */
	int previewPart() const;
	/** The code of the variant of a relay which is drawn, as the list shows it; empty for a course which is no relay. */
	QString previewVariant() const;
	/** Why the last change of the course was refused; empty when it was made. */
	QString lastRefusal() const { return last_refusal; }
	
	/** Sets the climb of the course which is chosen, as one undo step. */
	void setClimb(int metres);
	/** Sets the extra length of the course which is chosen, as one undo step. */
	void setExtraLength(double km);
	
	/**
	 * The course data as they are measured and drawn: those of the map, and
	 * for a relay with the items of the variant which is chosen in its place.
	 */
	CourseData previewData() const;
	
	/** The code of the course object whose description is shown: the one which is selected alone. */
	QString descriptionObject() const;
	/** Puts a description symbol into a cell of that object; 0 empties the cell. */
	bool setCell(CourseObject::Column column, int number);
	bool setSize(const QString& size);
	bool setDescriptionText(const QString& text);
	/** Says whether the marked route which is selected is the funnel only. */
	bool setFunnel(bool funnel);
	/** Makes a text block, selects it, and returns its code. */
	QString addTextBlock(const QString& text);
	
	static QString kindName(CourseObject::Kind kind);
	/** The text of an item in the list of a course. */
	static QString itemText(const CourseItem& item);
	
signals:
	/** Another course is chosen, or the chosen one may be drawn differently. */
	void currentCourseChanged();
	
private:
	void refresh();
	void refreshCourseValues();
	void updateLength();
	void refreshItems(int current, int top = -1);
	/** The branch diagram, which stands in the place of the list for a course with variations. */
	void refreshDiagram();
	void refreshClasses();
	void updateButtons();
	void fillCellLists();
	void refreshDescription();
	bool selectRowsOfMapSelection(bool scroll);
	void insertCodes(const QStringList& codes);
	QStringList selectedCodes() const;
	/** The index of a course in the selector; 0 for an empty name, -1 for a name which is not there. */
	int courseIndex(const QString& name) const;
	
	void objectRowsSelected();
	void mapSelectionChanged();
	void objectDoubleClicked(QTreeWidgetItem* item, int column);
	void courseChosen(int index);
	void refreshVariants();
	void refreshParts();
	void variantChosen(int index);
	void classCellChanged(int row, int column);
	
	void changeCodeClicked();
	void deleteObjectClicked();
	void newCourseClicked();
	void duplicateCourseClicked();
	void renameCourseClicked();
	void deleteCourseClicked();
	void addClassClicked();
	void addTextBlockClicked();
	void typeChosen(int index);
	void legsEntered(int legs);
	void teamVariationClicked();
	void legVariationClicked();
	void removeVariationClicked();
	/** Says why the last change was refused, when it was. */
	void showRefusal();
	/** The index which an item gets that is put behind the current one. */
	int insertIndex(const Course& course) const;
	bool addVariation(bool of_team, int branches, const QStringList& branch_legs);
	void cellChosen(int column, int index);
	void sizeEntered();
	void textEntered();
	
	Map* map;
	QTreeWidget* object_list;
	QComboBox* cell_list[CourseObject::ColumnCount];
	QLineEdit* size_edit;
	QCheckBox* funnel_check;
	QLineEdit* text_edit;
	QComboBox* selector;
	QComboBox* variant_box;
	QString variant_course;
	QComboBox* part_box;
	QString part_course;
	QComboBox* type_box;
	QSpinBox* legs_edit;
	QLabel* length_label;
	QSpinBox* climb_edit;
	QDoubleSpinBox* extra_edit;
	QTreeWidget* item_list;
	QScrollArea* diagram_area;
	CourseDiagramWidget* diagram_view;
	QCheckBox* diagram_check;
	QTableWidget* class_table;
	QPushButton* code_button;
	QPushButton* delete_object_button;
	QPushButton* text_block_button;
	QPushButton* new_course_button;
	QPushButton* duplicate_course_button;
	QPushButton* rename_course_button;
	QPushButton* delete_course_button;
	QPushButton* insert_button;
	QPushButton* remove_button;
	QPushButton* up_button;
	QPushButton* down_button;
	QPushButton* other_button;
	QAction* team_variation_act;
	QAction* leg_variation_act;
	QPushButton* remove_variation_button;
	QPushButton* add_class_button;
	QPushButton* delete_class_button;
	
	bool refreshing = false;
	bool syncing_selection = false;
	bool editing_class = false;
	/** The lists of description symbols are filled anew at the next refresh. */
	bool cell_lists_dirty = true;
	/** The rows which were selected when the course data changed, and whether that was just now. */
	QStringList codes_at_refresh;
	bool just_refreshed = false;
	/** The course to choose and the item to make current at the next refresh. */
	bool has_wanted_course = false;
	QString wanted_course;
	int wanted_item = -2;
	QString last_refusal;
};


}  // namespace OpenOrienteering

#endif
