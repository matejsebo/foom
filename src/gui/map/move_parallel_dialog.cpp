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


#include "move_parallel_dialog.h"

#include <Qt>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QRadioButton>
#include <QVBoxLayout>

#include "core/map.h"
#include "gui/map/map_editor.h"


namespace OpenOrienteering {

MoveParallelDialog::MoveParallelDialog(MapEditorController* editor, QWidget* parent)
: QDialog(parent)
, editor(editor)
{
	setWindowTitle(tr("Move parallel"));
	setModal(false);
	setAttribute(Qt::WA_DeleteOnClose);
	
	distance_edit = new QDoubleSpinBox();
	distance_edit->setDecimals(3);
	distance_edit->setRange(0.001, 100000.0);
	distance_edit->setValue(1.0);
	unit_combo = new QComboBox();
	unit_combo->addItem(tr("mm on the map"));
	unit_combo->addItem(tr("m on the ground"));
	auto* distance_row = new QHBoxLayout();
	distance_row->addWidget(distance_edit);
	distance_row->addWidget(unit_combo);
	
	left_button = new QRadioButton(tr("Left of the line direction"));
	left_button->setChecked(true);
	auto* right_button = new QRadioButton(tr("Right of the line direction"));
	
	auto* buttons = new QDialogButtonBox();
	auto* move_button = buttons->addButton(tr("Move"), QDialogButtonBox::ActionRole);
	auto* duplicate_button = buttons->addButton(tr("Duplicate"), QDialogButtonBox::ActionRole);
	buttons->addButton(QDialogButtonBox::Close);
	move_button->setDefault(true);
	connect(move_button, &QPushButton::clicked, this, &MoveParallelDialog::move);
	connect(duplicate_button, &QPushButton::clicked, this, &MoveParallelDialog::duplicate);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	
	auto* form = new QFormLayout();
	form->addRow(tr("Distance:"), distance_row);
	form->addRow(tr("Side:"), left_button);
	form->addRow(QString{}, right_button);
	auto* layout = new QVBoxLayout(this);
	layout->addLayout(form);
	layout->addWidget(buttons);
}

MoveParallelDialog::~MoveParallelDialog() = default;

double MoveParallelDialog::signedDistance() const
{
	auto distance = distance_edit->value();
	if (unit_combo->currentIndex() == 1)
		distance = distance * 1000.0 / editor->getMap()->getScaleDenominator();
	return left_button->isChecked() ? distance : -distance;
}

void MoveParallelDialog::move()
{
	editor->moveParallelBy(signedDistance(), false);
}

void MoveParallelDialog::duplicate()
{
	editor->moveParallelBy(signedDistance(), true);
}


}  // namespace OpenOrienteering
