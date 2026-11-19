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


#include "image_properties_dialog.h"

#include <Qt>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QImage>
#include <QLabel>
#include <QLocale>
#include <QSize>
#include <QSizeF>
#include <QSpinBox>
#include <QString>
#include <QVBoxLayout>


namespace OpenOrienteering {

ImagePropertiesDialog::ImagePropertiesDialog(const ImageObject& image, QWidget* parent)
: QDialog(parent)
{
	setWindowTitle(tr("Image properties"));
	setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
	
	auto* form = new QFormLayout();
	
	auto const name = image.isLinked() ? QFileInfo(image.linkedFile()).fileName() : image.href();
	form->addRow(tr("File:"), new QLabel(name.isEmpty() ? tr("(no name)") : name));
	form->addRow(tr("Stored:"), new QLabel(image.isLinked() ? tr("Linked: %1").arg(image.linkedFile()) : tr("Embedded in the map file")));
	
	auto const pixels = image.image().size();
	form->addRow(tr("Pixels:"), new QLabel(pixels.isEmpty() ? tr("The image cannot be read.") : tr("%1 x %2").arg(pixels.width()).arg(pixels.height())));
	
	auto const size = image.size();
	QLocale locale;
	form->addRow(tr("Size:"), new QLabel(tr("%1 x %2 mm").arg(locale.toString(size.width(), 'f', 2), locale.toString(size.height(), 'f', 2))));
	
	opacity_edit = new QSpinBox();
	opacity_edit->setRange(0, 100);
	opacity_edit->setSuffix(tr(" %"));
	opacity_edit->setValue(qRound(image.opacity() * 100));
	form->addRow(tr("Opacity:"), opacity_edit);
	
	layer_combo = new QComboBox();
	layer_combo->addItem(tr("Above the map"), int(ImageObject::AboveMap));
	layer_combo->addItem(tr("Below the map"), int(ImageObject::BelowMap));
	layer_combo->setCurrentIndex(image.layer() == ImageObject::BelowMap ? 1 : 0);
	form->addRow(tr("Position:"), layer_combo);
	
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, Qt::Horizontal);
	connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	
	auto* layout = new QVBoxLayout();
	layout->addLayout(form);
	layout->addWidget(buttons);
	setLayout(layout);
}

ImagePropertiesDialog::~ImagePropertiesDialog() = default;

qreal ImagePropertiesDialog::opacity() const
{
	return opacity_edit->value() / 100.0;
}

ImageObject::Layer ImagePropertiesDialog::layer() const
{
	return ImageObject::Layer(layer_combo->currentData().toInt());
}


}  // namespace OpenOrienteering
