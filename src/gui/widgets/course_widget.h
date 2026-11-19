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

class QComboBox;
class QPushButton;
class QTableWidget;
class QTreeWidget;
class QTreeWidgetItem;

namespace OpenOrienteering {

class Map;


/**
 * The course dock: the course objects, the courses with their items, and
 * the classes of a course.
 *
 * Every change goes into the map as one undo step. The functions which
 * take a name or a code ask nothing; the buttons ask and then call them.
 *
 * See the design notes, "Course dock".
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
	
	static QString kindName(CourseObject::Kind kind);
	/** The text of an item in the list of a course. */
	static QString itemText(const CourseItem& item);
	
private:
	void refresh();
	void refreshItems(int current, int top = -1);
	void refreshClasses();
	void updateButtons();
	bool selectRowsOfMapSelection(bool scroll);
	void insertCodes(const QStringList& codes);
	QStringList selectedCodes() const;
	/** The index of a course in the selector; 0 for an empty name, -1 for a name which is not there. */
	int courseIndex(const QString& name) const;
	
	void objectRowsSelected();
	void mapSelectionChanged();
	void objectDoubleClicked(QTreeWidgetItem* item, int column);
	void courseChosen(int index);
	void classCellChanged(int row, int column);
	
	void changeCodeClicked();
	void deleteObjectClicked();
	void newCourseClicked();
	void duplicateCourseClicked();
	void renameCourseClicked();
	void deleteCourseClicked();
	void addClassClicked();
	
	Map* map;
	QTreeWidget* object_list;
	QComboBox* selector;
	QTreeWidget* item_list;
	QTableWidget* class_table;
	QPushButton* code_button;
	QPushButton* delete_object_button;
	QPushButton* new_course_button;
	QPushButton* duplicate_course_button;
	QPushButton* rename_course_button;
	QPushButton* delete_course_button;
	QPushButton* insert_button;
	QPushButton* remove_button;
	QPushButton* up_button;
	QPushButton* down_button;
	QPushButton* add_class_button;
	QPushButton* delete_class_button;
	
	bool refreshing = false;
	bool syncing_selection = false;
	bool editing_class = false;
	/** The course to choose and the item to make current at the next refresh. */
	bool has_wanted_course = false;
	QString wanted_course;
	int wanted_item = -2;
};


}  // namespace OpenOrienteering

#endif
