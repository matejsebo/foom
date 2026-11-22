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


#ifndef FOOM_COURSE_PUNCH_DIALOG_H
#define FOOM_COURSE_PUNCH_DIALOG_H

#include <QDialog>
#include <QObject>
#include <QString>
#include <QStringList>

class QComboBox;
class QListWidget;
class QListWidgetItem;
class QRadioButton;
class QTableWidget;
class QWidget;

namespace OpenOrienteering {

class Map;


/**
 * Gives the controls the ids of their punching units.
 *
 * See the design notes, "Punching unit ids".
 */
class CoursePunchDialog : public QDialog
{
Q_OBJECT
public:
	explicit CoursePunchDialog(Map* map, QWidget* parent = nullptr);
	~CoursePunchDialog() override;
	
	/**
	 * Puts the ids of the table into the course data, as one undo step
	 * when they differ from those of the map. Returns false, with the
	 * reason, when a cell holds what is no id.
	 */
	bool apply(QString* message = nullptr);
	
	/** What the apply() which was done last said. */
	const QString& report() const { return last_report; }
	
	/** Applies and closes; when a cell holds what is no id, says so and stays. */
	void accept() override;
	
	/** Adds the number of the item to the ids of the current row. */
	void addListedId(QListWidgetItem* item);
	
	/** Adds the default ids to the table as the radio buttons say. */
	void addDefaults();
	
private:
	/**
	 * The ids of a row: those of the map when its cell was not changed,
	 * else those which are read from the cell. False when they cannot be read.
	 */
	bool idsOfRow(int row, QStringList* ids) const;
	
	Map* map;
	QTableWidget* table;
	QListWidget* list;
	QComboBox* system_box;
	QRadioButton* only_radio;
	QRadioButton* replace_radio;
	QRadioButton* keep_radio;
	QString last_report;
};


}  // namespace OpenOrienteering

#endif
