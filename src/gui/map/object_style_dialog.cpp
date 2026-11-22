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


#include "object_style_dialog.h"

#include <Qt>
#include <QAbstractButton>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFont>
#include <QFontComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QString>
#include <QVBoxLayout>

#include "gui/widgets/color_dropdown.h"


namespace OpenOrienteering {

namespace {

QDoubleSpinBox* millimetreBox(double minimum, double maximum, double value)
{
	auto* box = new QDoubleSpinBox();
	box->setDecimals(3);
	box->setRange(minimum, maximum);
	box->setSingleStep(0.05);
	box->setSuffix(QString::fromLatin1(" mm"));
	box->setValue(value);
	return box;
}

}  // namespace


ObjectStyleDialog::ObjectStyleDialog(const Map& map, const ObjectStyle& style, QWidget* parent)
: QDialog(parent)
, initial(style)
{
	setWindowTitle(tr("Object style"));
	setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

	auto* layout = new QVBoxLayout();

	if (style.has_line)
	{
		auto* group = new QGroupBox(tr("Line"));
		auto* form = new QFormLayout(group);
		line_color = new ColorDropDown(&map, style.line_color);
		form->addRow(tr("Colour:"), line_color);
		line_width = millimetreBox(0.01, 100, style.line_width);
		form->addRow(tr("Width:"), line_width);
		layout->addWidget(group);
	}
	if (style.has_fill)
	{
		auto* group = new QGroupBox(tr("Fill"));
		auto* form = new QFormLayout(group);
		fill_color = new ColorDropDown(&map, style.fill_color);
		form->addRow(tr("Colour:"), fill_color);
		layout->addWidget(group);
	}
	if (style.has_text)
	{
		auto* group = new QGroupBox(tr("Text"));
		auto* form = new QFormLayout(group);
		text_color = new ColorDropDown(&map, style.text_color);
		form->addRow(tr("Colour:"), text_color);
		font_family = new QFontComboBox();
		font_family->setCurrentFont(QFont(style.font_family));
		form->addRow(tr("Font:"), font_family);
		font_size = millimetreBox(0.1, 1000, style.font_size);
		form->addRow(tr("Size:"), font_size);
		layout->addWidget(group);
	}
	if (!style.has_line && !style.has_fill && !style.has_text)
	{
		auto* label = new QLabel(tr("This style has no colour, width or font to set here. Use the symbol editor."));
		label->setWordWrap(true);
		layout->addWidget(label);
	}

	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, Qt::Horizontal);
	auto* editor_button = buttons->addButton(tr("Symbol editor..."), QDialogButtonBox::ActionRole);
	connect(editor_button, &QAbstractButton::clicked, this, [this]() {
		editor_requested = true;
		reject();
	});
	connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	layout->addWidget(buttons);
	setLayout(layout);
}

ObjectStyleDialog::~ObjectStyleDialog() = default;

ObjectStyle ObjectStyleDialog::style() const
{
	auto result = initial;
	if (line_color)
	{
		result.line_color = line_color->color();
		result.line_width = line_width->value();
	}
	if (fill_color)
		result.fill_color = fill_color->color();
	if (text_color)
	{
		result.text_color = text_color->color();
		// Keep the family as stored when the user did not pick another one.
		if (font_family->currentFont().family() != QFont(initial.font_family).family())
			result.font_family = font_family->currentFont().family();
		result.font_size = font_size->value();
	}
	return result;
}


}  // namespace OpenOrienteering
