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

// the design notes, "PDF import"

#include "pdf_import_dialog.h"

#include <QAbstractButton>
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include "core/map.h"


namespace OpenOrienteering {

PdfImportDialog::PdfImportDialog(const QString& path, const Map& map, const QPointF& view_centre, QWidget* parent)
: QDialog(parent)
, map(map)
, view_centre(view_centre)
{
	setWindowTitle(tr("Import PDF"));
	sizes = pdfPageSizes(path, &read_error);

	auto* layout = new QVBoxLayout(this);
	auto* file_label = new QLabel(QFileInfo(path).fileName());
	layout->addWidget(file_label);

	auto* form = new QFormLayout();
	layout->addLayout(form);

	page_box = new QComboBox();
	page_box->setObjectName(QStringLiteral("page"));
	QLocale locale;
	for (std::size_t i = 0; i < sizes.size(); ++i)
	{
		page_box->addItem(tr("%1: %2 × %3 mm").arg(int(i) + 1)
		                  .arg(locale.toString(sizes[i].width(), 'f', 0), locale.toString(sizes[i].height(), 'f', 0)));
	}
	form->addRow(tr("Page:"), page_box);

	scale_box = new QSpinBox();
	scale_box->setObjectName(QStringLiteral("scale"));
	scale_box->setRange(1, 100000000);
	scale_box->setPrefix(QStringLiteral("1 : "));
	scale_box->setValue(int(map.getScaleDenominator()));
	form->addRow(tr("Scale of the PDF:"), scale_box);

	place_origin = new QRadioButton(tr("The map's origin"));
	place_origin->setObjectName(QStringLiteral("placeOrigin"));
	place_view = new QRadioButton(tr("The centre of the view"));
	place_view->setObjectName(QStringLiteral("placeView"));
	place_typed = new QRadioButton(tr("This point (mm):"));
	place_typed->setObjectName(QStringLiteral("placeTyped"));
	auto* group = new QButtonGroup(this);
	group->addButton(place_origin);
	group->addButton(place_view);
	group->addButton(place_typed);
	place_origin->setChecked(true);

	centre_x = new QDoubleSpinBox();
	centre_x->setObjectName(QStringLiteral("centreX"));
	centre_y = new QDoubleSpinBox();
	centre_y->setObjectName(QStringLiteral("centreY"));
	for (auto* box : { centre_x, centre_y })
	{
		box->setRange(-1000000, 1000000);
		box->setDecimals(2);
	}
	centre_x->setPrefix(QStringLiteral("x "));
	centre_y->setPrefix(QStringLiteral("y "));

	auto* place_layout = new QVBoxLayout();
	place_layout->addWidget(place_origin);
	place_layout->addWidget(place_view);
	auto* typed_layout = new QHBoxLayout();
	typed_layout->addWidget(place_typed);
	typed_layout->addWidget(centre_x);
	typed_layout->addWidget(centre_y);
	place_layout->addLayout(typed_layout);
	form->addRow(tr("Centre of the page at:"), place_layout);

	text_box = new QCheckBox(tr("Make text objects"));
	text_box->setObjectName(QStringLiteral("text"));
	text_box->setChecked(true);
	form->addRow(QString(), text_box);

	extent_label = new QLabel();
	extent_label->setWordWrap(true);
	layout->addWidget(extent_label);

	auto* note = new QLabel(tr("Embedded images, shadings and patterns are not imported; the import reports them."));
	note->setWordWrap(true);
	layout->addWidget(note);

	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	layout->addWidget(buttons);
	connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	buttons->button(QDialogButtonBox::Ok)->setEnabled(!sizes.empty());

	connect(page_box, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &PdfImportDialog::updateWidgets);
	connect(scale_box, QOverload<int>::of(&QSpinBox::valueChanged), this, &PdfImportDialog::updateWidgets);
	connect(group, QOverload<QAbstractButton*>::of(&QButtonGroup::buttonClicked), this, &PdfImportDialog::updateWidgets);
	connect(place_typed, &QRadioButton::toggled, this, &PdfImportDialog::updateWidgets);
	updateWidgets();
}

PdfImportDialog::~PdfImportDialog() = default;


PdfImportOptions PdfImportDialog::options() const
{
	PdfImportOptions options;
	options.page = page_box->currentIndex() + 1;
	options.pdf_scale = scale_box->value();
	if (place_view->isChecked())
		options.centre = view_centre;
	else if (place_typed->isChecked())
		options.centre = { centre_x->value(), centre_y->value() };
	options.text = text_box->isChecked();
	return options;
}


void PdfImportDialog::updateWidgets()
{
	centre_x->setEnabled(place_typed->isChecked());
	centre_y->setEnabled(place_typed->isChecked());

	auto const index = page_box->currentIndex();
	if (index < 0 || std::size_t(index) >= sizes.size())
	{
		extent_label->setText(read_error);
		return;
	}
	auto const page = sizes[std::size_t(index)];
	auto const metres = scale_box->value() / 1000.0;
	auto const on_map = double(scale_box->value()) / map.getScaleDenominator();
	QLocale locale;
	extent_label->setText(tr("The page covers %1 × %2 m on the ground and %3 × %4 mm on the map.")
	                      .arg(locale.toString(page.width() * metres, 'f', 0), locale.toString(page.height() * metres, 'f', 0),
	                           locale.toString(page.width() * on_map, 'f', 1), locale.toString(page.height() * on_map, 'f', 1)));
}


}  // namespace OpenOrienteering
