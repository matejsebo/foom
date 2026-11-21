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

#ifndef FOOM_GUI_SYMBOL_TOOLS_DIALOGS_H
#define FOOM_GUI_SYMBOL_TOOLS_DIALOGS_H

#include <vector>

#include <QDialog>
#include <QObject>
#include <QString>

class QCheckBox;
class QComboBox;
class QFontComboBox;
class QLineEdit;
class QSpinBox;
class QStackedWidget;
class QWidget;

namespace OpenOrienteering {

class Map;
class MapColor;
class Symbol;


/**
 * Select symbols by colour, font, type, status or number pattern.
 *
 * See the design notes, "Selecting symbols, replacing fonts and colours, renumbering".
 */
class SelectSymbolsDialog : public QDialog
{
Q_OBJECT
public:
	SelectSymbolsDialog(const Map& map, QWidget* parent);
	~SelectSymbolsDialog() override;
	
	/// The symbols found with the dialog's choice.
	std::vector<const Symbol*> symbols() const;
	
private:
	const Map& map;
	QComboBox* criterion_combo;
	QStackedWidget* value_stack;
	QComboBox* color_combo;
	QComboBox* font_combo;
	QComboBox* type_combo;
	QComboBox* status_combo;
	QLineEdit* pattern_edit;
};


/**
 * Replace font, replace colour or renumber, each with "Only the selected symbols".
 */
class SymbolReplaceDialog : public QDialog
{
Q_OBJECT
public:
	enum Mode { Font, Color, Renumber };
	
	SymbolReplaceDialog(Mode mode, const Map& map, QWidget* parent);
	~SymbolReplaceDialog() override;
	
	QString fromFont() const;
	QString toFont() const;
	const MapColor* fromColor() const;
	const MapColor* toColor() const;
	int start() const;
	int step() const;
	bool onlySelected() const;
	
private:
	const Map& map;
	QComboBox* from_combo = nullptr;
	QFontComboBox* to_font_combo = nullptr;
	QComboBox* to_color_combo = nullptr;
	QSpinBox* start_edit = nullptr;
	QSpinBox* step_edit = nullptr;
	QCheckBox* selected_check = nullptr;
};


}  // namespace OpenOrienteering

#endif
