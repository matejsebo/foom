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


#include "extent_dialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPageSize>
#include <QPushButton>
#include <QRadioButton>
#include <QRectF>
#include <QSpinBox>
#include <QVBoxLayout>

#include "core/georeferencing.h"
#include "core/map.h"
#include "core/map_printer.h"
#include "core/map_view.h"
#include "gui/map/map_editor.h"
#include "gui/map/map_widget.h"


namespace OpenOrienteering {

ExtentDialog::ExtentDialog(MapEditorController* editor, const Extent& extent, QWidget* parent)
: QDialog(parent)
, editor(editor)
{
	setWindowTitle(tr("Define extent"));
	name_edit = new QLineEdit();
	auto const spin = []() {
		auto* edit = new QDoubleSpinBox();
		edit->setDecimals(2);
		edit->setRange(-999999.0, 999999.0);
		edit->setSuffix(QStringLiteral(" mm"));
		return edit;
	};
	left_edit = spin();
	top_edit = spin();
	width_edit = spin();
	width_edit->setMinimum(0.01);
	height_edit = spin();
	height_edit->setMinimum(0.01);
	scale_edit = new QSpinBox();
	scale_edit->setRange(0, 10000000);
	scale_edit->setPrefix(QStringLiteral("1 : "));
	scale_edit->setSpecialValueText(tr("Map scale"));
	paper_combo = new QComboBox();
	paper_combo->addItem(tr("None"), QString{});
	for (auto id : { QPageSize::A4, QPageSize::A3, QPageSize::A2, QPageSize::A1, QPageSize::A0, QPageSize::A5, QPageSize::B4, QPageSize::B5, QPageSize::Letter, QPageSize::Legal, QPageSize::Tabloid })
		paper_combo->addItem(QPageSize::name(id), QPageSize::key(id));
	portrait_button = new QRadioButton(tr("Portrait"));
	portrait_button->setChecked(true);
	auto* landscape_button = new QRadioButton(tr("Landscape"));
	auto* orientation_row = new QHBoxLayout();
	orientation_row->addWidget(portrait_button);
	orientation_row->addWidget(landscape_button);
	margin_edit = spin();
	margin_edit->setMinimum(0.0);
	back_of_combo = new QComboBox();
	back_of_combo->addItem(tr("None"), QString{});
	for (auto const& other : editor->getMap()->extents())
	{
		if (other.name != extent.name)
			back_of_combo->addItem(other.name, other.name);
	}
	
	auto* from_paper = new QPushButton(tr("From paper format"));
	auto* from_view = new QPushButton(tr("From current view"));
	auto* from_print = new QPushButton(tr("From print area"));
	auto* source_row = new QHBoxLayout();
	source_row->addWidget(from_paper);
	source_row->addWidget(from_view);
	source_row->addWidget(from_print);
	connect(from_paper, &QPushButton::clicked, this, &ExtentDialog::fromPaperFormat);
	connect(from_view, &QPushButton::clicked, this, &ExtentDialog::fromCurrentView);
	connect(from_print, &QPushButton::clicked, this, &ExtentDialog::fromPrintArea);
	
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	
	auto* form = new QFormLayout();
	form->addRow(tr("Name:"), name_edit);
	form->addRow(tr("Left:"), left_edit);
	form->addRow(tr("Top:"), top_edit);
	form->addRow(tr("Width:"), width_edit);
	form->addRow(tr("Height:"), height_edit);
	form->addRow(tr("Print scale:"), scale_edit);
	form->addRow(tr("Paper:"), paper_combo);
	form->addRow(tr("Orientation:"), orientation_row);
	form->addRow(tr("Margin:"), margin_edit);
	form->addRow(tr("Back of:"), back_of_combo);
	auto* layout = new QVBoxLayout(this);
	layout->addLayout(form);
	layout->addLayout(source_row);
	layout->addWidget(buttons);
	setExtent(extent);
}

ExtentDialog::~ExtentDialog() = default;

void ExtentDialog::setRect(const QRectF& rect)
{
	left_edit->setValue(rect.left());
	top_edit->setValue(-rect.top());  // y up, as the print widget shows it
	width_edit->setValue(rect.width());
	height_edit->setValue(rect.height());
}

void ExtentDialog::setExtent(const Extent& extent)
{
	name_edit->setText(extent.name);
	setRect(extent.rect_mm);
	scale_edit->setValue(int(extent.scale));
	auto const paper_index = paper_combo->findData(extent.paper);
	paper_combo->setCurrentIndex(paper_index < 0 ? 0 : paper_index);
	portrait_button->setChecked(extent.rect_mm.width() <= extent.rect_mm.height());
	margin_edit->setValue(extent.margin_mm);
	auto const back_index = back_of_combo->findData(extent.back_of);
	back_of_combo->setCurrentIndex(back_index < 0 ? 0 : back_index);
}

Extent ExtentDialog::extent() const
{
	Extent extent;
	extent.name = name_edit->text().trimmed();
	extent.rect_mm = QRectF(left_edit->value(), -top_edit->value(), width_edit->value(), height_edit->value());
	extent.scale = unsigned(scale_edit->value());
	extent.paper = paper_combo->currentData().toString();
	extent.margin_mm = margin_edit->value();
	extent.back_of = back_of_combo->currentData().toString();
	return extent;
}

void ExtentDialog::fromPaperFormat()
{
	auto const key = paper_combo->currentData().toString();
	if (key.isEmpty())
		return;
	auto const map_scale = editor->getMap()->getScaleDenominator();
	auto const print_scale = scale_edit->value() > 0 ? unsigned(scale_edit->value()) : map_scale;
	auto const size = extentSizeFromPaper(pageSizeFromKey(key), !portrait_button->isChecked(), print_scale, map_scale);
	width_edit->setValue(size.width());
	height_edit->setValue(size.height());
}

void ExtentDialog::fromCurrentView()
{
	auto const* widget = editor->getMainWidget();
	setRect(widget->getMapView()->calculateViewedRect(widget->rect()));
}

void ExtentDialog::fromPrintArea()
{
	setRect(editor->getMap()->printerConfig().print_area);
}


}  // namespace OpenOrienteering
