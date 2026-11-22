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

#ifndef FOOM_POINT_PAIRS_WINDOW_H
#define FOOM_POINT_PAIRS_WINDOW_H

#include <vector>

#include <QDialog>
#include <QObject>
#include <QString>
#include <QTableWidget>

#include "core/map_transform.h"

class QGroupBox;
class QPushButton;
class QVBoxLayout;
class QWidget;

namespace OpenOrienteering {

class Map;


/**
 * The table of point pairs: Used, No., x-Old, y-Old, x-New, y-New in projected metres,
 * Distance [m], and optionally Residual [m].
 *
 * See the design notes, "Pair tables, perimeters and the dialogs beside the map".
 */
class PointPairTable : public QTableWidget
{
Q_OBJECT
public:
	PointPairTable(const Map& map, bool with_residuals, QWidget* parent = nullptr);
	~PointPairTable() override;
	
	/// Shows the pairs; residuals in map mm, one per pair, or none.
	void setPairs(const std::vector<PointPair>& pairs, const std::vector<double>& residuals);
	
	/// The selected pair, or -1.
	int selectedPair() const;
	
signals:
	void usedChanged(int index, bool used);
	
private:
	void cellChanged(QTableWidgetItem* item);
	
	const Map& map;
	bool with_residuals;
	bool updating = false;
};


/**
 * The window of a pair tool beside the map: the table with Remove, Remove all,
 * Load..., Save... and Transform, below sections which a tool may add.
 */
class PointPairsWindow : public QDialog
{
Q_OBJECT
public:
	PointPairsWindow(const Map& map, const QString& title, const QString& points_title, bool with_residuals, QWidget* parent);
	~PointPairsWindow() override;
	
	PointPairTable* table() const { return pair_table; }
	
	/// Adds a section above the table.
	void addSection(QWidget* section);
	
	/// The file filter of the text files of points.
	static QString fileFilter();
	
signals:
	void removeRequested(int index);
	void removeAllRequested();
	void loadRequested();
	void saveRequested();
	void transformRequested();
	void closed();
	
protected:
	void reject() override;
	
private:
	PointPairTable* pair_table;
	QVBoxLayout* sections;
	QPushButton* remove_button;
	QPushButton* remove_all_button;
	QPushButton* load_button;
	QPushButton* save_button;
	QPushButton* transform_button;
};


}  // namespace OpenOrienteering

#endif
