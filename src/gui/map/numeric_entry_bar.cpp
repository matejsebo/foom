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


#include "numeric_entry_bar.h"

#include <cmath>

#include <QtMath>
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QPointF>
#include <QPushButton>

#include "core/georeferencing.h"
#include "core/map.h"
#include "core/map_coord.h"
#include "gui/main_window.h"
#include "gui/map/map_editor.h"
#include "tools/tool.h"


namespace OpenOrienteering {

NumericEntryBar::NumericEntryBar(MapEditorController* editor, QWidget* parent)
: QToolBar(tr("Numeric entry"), parent)
, editor(editor)
{
	setObjectName(QString::fromLatin1("Numeric entry toolbar"));
	mode_combo = new QComboBox();
	mode_combo->addItem(tr("Position"));
	mode_combo->addItem(tr("Length and angle"));
	first_label = new QLabel();
	first_edit = new QLineEdit();
	first_edit->setMaximumWidth(120);
	second_label = new QLabel();
	second_edit = new QLineEdit();
	second_edit->setMaximumWidth(120);
	grid_check = new QCheckBox(tr("Add grid angle"));
	add_button = new QPushButton(tr("Add"));
	addWidget(mode_combo);
	addWidget(first_label);
	addWidget(first_edit);
	addWidget(second_label);
	addWidget(second_edit);
	addWidget(grid_check);
	addWidget(add_button);
	
	connect(mode_combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &NumericEntryBar::updateForMode);
	connect(first_edit, &QLineEdit::returnPressed, this, &NumericEntryBar::accept);
	connect(second_edit, &QLineEdit::returnPressed, this, &NumericEntryBar::accept);
	connect(add_button, &QPushButton::clicked, this, &NumericEntryBar::accept);
	connect(editor, &MapEditorController::toolChanged, this, &NumericEntryBar::toolChanged);
	updateForMode();
	toolChanged(editor->getTool());
}

NumericEntryBar::~NumericEntryBar() = default;

NumericEntryBar::Mode NumericEntryBar::mode() const
{
	return Mode(mode_combo->currentIndex());
}

void NumericEntryBar::setMode(Mode mode)
{
	mode_combo->setCurrentIndex(int(mode));
}

void NumericEntryBar::setValues(const QString& first, const QString& second)
{
	first_edit->setText(first);
	second_edit->setText(second);
}

bool NumericEntryBar::addGridAngle() const
{
	return grid_check->isChecked();
}

void NumericEntryBar::setAddGridAngle(bool add)
{
	grid_check->setChecked(add);
}

bool NumericEntryBar::computeVertex(const Map& map, Mode mode, double first, double second, bool add_grid_angle, const MapCoordF* reference, MapCoordF& out, QString& error)
{
	// the design notes, "Numeric entry bar".
	auto const& georef = map.getGeoreferencing();
	auto const local = georef.getState() == Georeferencing::Local;
	if (mode == Position)
	{
		out = local ? MapCoordF(first, -second) : georef.toMapCoordF(QPointF(first, second));
		return true;
	}
	if (!reference)
	{
		error = tr("Enter the first vertex as a position.");
		return false;
	}
	auto const length_mm = local ? first : first * 1000.0 / georef.getScaleDenominator();
	auto angle = second;
	if (add_grid_angle)
		angle += gridNorthAngle(georef);
	auto const radians = qDegreesToRadians(angle);
	out = MapCoordF(reference->x() + length_mm * std::sin(radians), reference->y() - length_mm * std::cos(radians));
	return true;
}

double NumericEntryBar::gridNorthAngle(const Georeferencing& georef)
{
	auto const origin = georef.toMapCoordF(QPointF(0.0, 0.0));
	auto const north = georef.toMapCoordF(QPointF(0.0, 1.0));
	return qRadiansToDegrees(std::atan2(north.x() - origin.x(), origin.y() - north.y()));
}

bool NumericEntryBar::accept()
{
	auto* tool = editor->getTool();
	if (!tool || !tool->acceptsNumericVertices())
	{
		showMessage(tr("The active tool does not take numeric vertices."));
		return false;
	}
	auto const locale = QLocale();
	bool first_ok = false, second_ok = false;
	auto first = locale.toDouble(first_edit->text().trimmed(), &first_ok);
	if (!first_ok)
		first = first_edit->text().trimmed().toDouble(&first_ok);
	auto second = locale.toDouble(second_edit->text().trimmed(), &second_ok);
	if (!second_ok)
		second = second_edit->text().trimmed().toDouble(&second_ok);
	if (!first_ok || !second_ok)
	{
		showMessage(tr("Enter two numbers."));
		return false;
	}
	MapCoordF reference;
	auto const has_reference = tool->numericReferenceVertex(reference);
	MapCoordF vertex;
	QString error;
	if (!computeVertex(*editor->getMap(), mode(), first, second, addGridAngle(), has_reference ? &reference : nullptr, vertex, error))
	{
		showMessage(error);
		return false;
	}
	tool->acceptNumericVertex(vertex);
	return true;
}

void NumericEntryBar::updateForMode()
{
	auto const local = editor->getMap()->getGeoreferencing().getState() == Georeferencing::Local;
	if (mode() == Position)
	{
		first_label->setText(local ? tr("X (mm):") : tr("Easting:"));
		second_label->setText(local ? tr("Y (mm):") : tr("Northing:"));
	}
	else
	{
		first_label->setText(local ? tr("Length (mm):") : tr("Length (m):"));
		second_label->setText(tr("Angle (\u00b0):"));
	}
	grid_check->setEnabled(mode() == LengthAndAngle);
}

void NumericEntryBar::toolChanged(MapEditorTool* tool)
{
	auto const accepts = tool && tool->acceptsNumericVertices();
	for (auto* widget : { static_cast<QWidget*>(first_edit), static_cast<QWidget*>(second_edit), static_cast<QWidget*>(add_button) })
		widget->setEnabled(accepts);
}

void NumericEntryBar::showMessage(const QString& text)
{
	if (auto* main_window = qobject_cast<MainWindow*>(window()))
		main_window->showStatusBarMessage(text, 3000);
}


}  // namespace OpenOrienteering
