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

#include "map_transform_dialogs.h"

#include <QCheckBox>
#include <QDate>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLatin1String>
#include <QMessageBox>
#include <QPushButton>
#include <QString>
#include <QUrl>
#include <QUrlQuery>
#include <QVBoxLayout>
#include <QXmlStreamReader>
#if defined(QT_NETWORK_LIB)
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#endif

#include "core/crs_template.h"
#include "core/georeferencing.h"
#include "core/latlon.h"
#include "core/map.h"
#include "core/map_transform.h"
#include "gui/widgets/crs_selector.h"


namespace OpenOrienteering {

namespace {

QDoubleSpinBox* makeEdit(double value, double minimum, double maximum, const QString& suffix)
{
	auto* edit = new QDoubleSpinBox();
	edit->setDecimals(2);
	edit->setRange(minimum, maximum);
	edit->setSuffix(suffix);
	edit->setValue(value);
	return edit;
}

void finish(QDialog* dialog, QFormLayout* form)
{
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	QObject::connect(buttons, &QDialogButtonBox::accepted, dialog, &QDialog::accept);
	QObject::connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
	auto* layout = new QVBoxLayout(dialog);
	layout->addLayout(form);
	layout->addWidget(buttons);
}

}  // namespace



MapMoveDialog::MapMoveDialog(QWidget* parent)
: QDialog(parent)
{
	setWindowTitle(tr("Move map"));
	east_edit = makeEdit(0.0, -1000000.0, 1000000.0, QStringLiteral(" m"));
	north_edit = makeEdit(0.0, -1000000.0, 1000000.0, QStringLiteral(" m"));
	carry_check = new QCheckBox(tr("Also move templates, layout objects, extents and bookmarks"));
	carry_check->setChecked(true);
	auto* form = new QFormLayout();
	form->addRow(tr("Easting:"), east_edit);
	form->addRow(tr("Northing:"), north_edit);
	form->addRow(carry_check);
	finish(this, form);
}

MapMoveDialog::~MapMoveDialog() = default;

double MapMoveDialog::east() const { return east_edit->value(); }
double MapMoveDialog::north() const { return north_edit->value(); }
bool MapMoveDialog::carry() const { return carry_check->isChecked(); }



MapStretchDialog::MapStretchDialog(bool mirror, QWidget* parent)
: QDialog(parent)
{
	setWindowTitle(mirror ? tr("Mirror map") : tr("Stretch / shrink map"));
	x_edit = makeEdit(100.0, 1.0, 10000.0, QStringLiteral(" %"));
	y_edit = makeEdit(100.0, 1.0, 10000.0, QStringLiteral(" %"));
	horizontal_check = new QCheckBox(tr("Mirror horizontally"));
	horizontal_check->setChecked(mirror);
	vertical_check = new QCheckBox(tr("Mirror vertically"));
	vertical_check->setChecked(mirror);
	carry_check = new QCheckBox(tr("Also transform templates, layout objects, extents and bookmarks"));
	carry_check->setChecked(true);
	auto* form = new QFormLayout();
	form->addRow(tr("Horizontal stretch / shrink:"), x_edit);
	form->addRow(tr("Vertical stretch / shrink:"), y_edit);
	form->addRow(horizontal_check);
	form->addRow(vertical_check);
	form->addRow(carry_check);
	finish(this, form);
}

MapStretchDialog::~MapStretchDialog() = default;

double MapStretchDialog::factorX() const { return x_edit->value() / 100.0; }
double MapStretchDialog::factorY() const { return y_edit->value() / 100.0; }
bool MapStretchDialog::mirrorHorizontally() const { return horizontal_check->isChecked(); }
bool MapStretchDialog::mirrorVertically() const { return vertical_check->isChecked(); }
bool MapStretchDialog::carry() const { return carry_check->isChecked(); }


ReprojectDialog::ReprojectDialog(const Georeferencing& georef, QWidget* parent)
: QDialog(parent)
, georef(std::make_unique<Georeferencing>(georef))
{
	setWindowTitle(tr("Change coordinate system"));
	auto* form = new QFormLayout();
	form->addRow(tr("Change map from:"), new QLabel(georef.getProjectedCRSName() + QStringLiteral(" (") + georef.getProjectedCRSSpec() + QLatin1Char(')')));
	// The offsets: the projected coordinates of the map's origin.
	auto const origin = georef.toProjectedCoords(MapCoordF(0.0, 0.0));
	form->addRow(tr("Easting offset:"), new QLabel(QString::number(origin.x(), 'f', 2) + QStringLiteral(" m")));
	form->addRow(tr("Northing offset:"), new QLabel(QString::number(origin.y(), 'f', 2) + QStringLiteral(" m")));
	selector = new CRSSelector(georef, nullptr);
	form->addRow(tr("To:"), selector);
	selector->setDialogLayout(form);
	auto const& parameters = georef.getProjectedCRSParameters();
	auto const* current = CRSTemplateRegistry().find(georef.getProjectedCRSId());
	if (current && current->parameters().size() == parameters.size())
		selector->setCurrentCRS(current, parameters);
	else
		selector->setCurrentCRS(CRSTemplateRegistry().find(QStringLiteral("PROJ.4")), { georef.getProjectedCRSSpec() });
	// The target's offsets, proposed when a system is chosen.
	east_edit = makeEdit(origin.x(), -1e9, 1e9, QStringLiteral(" m"));
	east_edit->setObjectName(QStringLiteral("east_offset"));
	north_edit = makeEdit(origin.y(), -1e9, 1e9, QStringLiteral(" m"));
	north_edit->setObjectName(QStringLiteral("north_offset"));
	auto* target_form = new QFormLayout();
	target_form->addRow(tr("Easting offset:"), east_edit);
	target_form->addRow(tr("Northing offset:"), north_edit);
	connect(selector, &CRSSelector::crsChanged, this, &ReprojectDialog::proposeOffset);
	proposeOffset();
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	auto* layout = new QVBoxLayout(this);
	layout->addLayout(form);
	layout->addLayout(target_form);
	layout->addWidget(buttons);
}

ReprojectDialog::~ReprojectDialog() = default;

void ReprojectDialog::setTargetCRS(const QString& id, const std::vector<QString>& parameters)
{
	if (auto const* crs = CRSTemplateRegistry().find(id))
		selector->setCurrentCRS(crs, parameters);
	proposeOffset();
}

QPointF ReprojectDialog::targetOffset() const
{
	return { east_edit->value(), north_edit->value() };
}

void ReprojectDialog::proposeOffset()
{
	bool ok = false;
	auto const offset = proposedReprojectOffset(*georef, crsId(), crsSpec(), crsParameters(), &ok);
	if (!ok)
		return;
	east_edit->setValue(offset.x());
	north_edit->setValue(offset.y());
}

QString ReprojectDialog::crsId() const
{
	auto const* crs = selector->currentCRSTemplate();
	return crs ? crs->id() : QString{};
}

QString ReprojectDialog::crsSpec() const { return selector->currentCRSSpec(); }
std::vector<QString> ReprojectDialog::crsParameters() const { return selector->parameters(); }



MagneticNorthDialog::MagneticNorthDialog(const Georeferencing& georef, QWidget* parent)
: QDialog(parent)
, georef(std::make_unique<Georeferencing>(georef))
{
	setWindowTitle(tr("Rotate map to magnetic north"));
	auto* form = new QFormLayout();
	auto const geospatial = georef.getState() == Georeferencing::Geospatial;
	if (geospatial)
	{
		auto const lat_lon = georef.getGeographicRefPoint();
		form->addRow(tr("Reference point:"), new QLabel(tr("Latitude %1°, longitude %2°").arg(lat_lon.latitude(), 0, 'f', 5).arg(lat_lon.longitude(), 0, 'f', 5)));
	}
	declination_edit = makeEdit(georef.getDeclination(), -180.0, 180.0, QStringLiteral(" °"));
	lookup_button = new QPushButton(tr("Lookup..."));
	lookup_button->setEnabled(geospatial);
	auto* declination_row = new QHBoxLayout();
	declination_row->addWidget(declination_edit, 1);
	declination_row->addWidget(lookup_button, 0);
	form->addRow(tr("Magnetic declination:"), declination_row);
	lookup_label = new QLabel();
	form->addRow(lookup_label);
	form->addRow(tr("Grid convergence:"), new QLabel(QString::number(georef.getConvergence(), 'f', 2) + QStringLiteral(" °")));
	angle_label = new QLabel();
	form->addRow(tr("Grid magnetic angle:"), angle_label);
	form->addRow(tr("Current angle:"), new QLabel(QString::number(georef.getGrivation(), 'f', 2) + QStringLiteral(" °")));
	rotate_label = new QLabel();
	form->addRow(tr("Rotate by:"), rotate_label);
	symbols_check = new QCheckBox(tr("Do not rotate objects of the selected symbols"));
	layout_check = new QCheckBox(tr("Do not rotate layout objects"));
	layout_check->setChecked(true);
	form->addRow(symbols_check);
	form->addRow(layout_check);
	connect(declination_edit, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &MagneticNorthDialog::updateAngles);
	connect(lookup_button, &QPushButton::clicked, this, &MagneticNorthDialog::lookUpDeclination);
	updateAngles();
	finish(this, form);
}

MagneticNorthDialog::~MagneticNorthDialog() = default;

double MagneticNorthDialog::declination() const { return declination_edit->value(); }
bool MagneticNorthDialog::keepSelectedSymbols() const { return symbols_check->isChecked(); }
bool MagneticNorthDialog::keepLayout() const { return layout_check->isChecked(); }

QString MagneticNorthDialog::declinationText(const GeomagneticField& field)
{
	// The usual form, with the numbers of the lookup.
	QString text = tr("Magnetic declination: %1°").arg(field.declination, 0, 'f', 3);
	if (field.has_horizontal_intensity)
		text += QStringLiteral(" ±") + QString::number(declinationUncertainty(field.horizontal_intensity), 'f', 2) + QStringLiteral("°");
	if (field.has_yearly_change)
		text += QLatin1Char(' ') + tr("changing by %1° per year").arg(field.yearly_change, 0, 'f', 3);
	return text;
}

void MagneticNorthDialog::updateAngles()
{
	auto const rotation = magneticNorthRotation(*georef, declination());
	angle_label->setText(QString::number(georef->getGrivation() + rotation, 'f', 2) + QStringLiteral(" °"));
	rotate_label->setText(QString::number(rotation, 'f', 2) + QStringLiteral(" °"));
}

void MagneticNorthDialog::lookUpDeclination()
{
	// The online lookup of OOM's georeferencing dialog: the same service, request and question.
	auto const lat_lon = georef->getGeographicRefPoint();
	QString const service(QStringLiteral("https://geomag.bgs.ac.uk"));
	if (QMessageBox::question(this, tr("Online declination lookup"),
	                          tr("The magnetic declination for the reference point %1° %2° will now be retrieved from <a href=\"%3\">%3</a>. Do you want to continue?")
	                          .arg(lat_lon.latitude()).arg(lat_lon.longitude()).arg(service),
	                          QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes) != QMessageBox::Yes)
		return;
	QUrl url(service + QStringLiteral("/web_service/GMModels/wmm/2025"));
	QUrlQuery query;
	query.addQueryItem(QStringLiteral("latitude"), QString::number(lat_lon.latitude()));
	query.addQueryItem(QStringLiteral("longitude"), QString::number(lat_lon.longitude()));
	query.addQueryItem(QStringLiteral("altitude"), QStringLiteral("0"));
	query.addQueryItem(QStringLiteral("date"), QDate::currentDate().toString(Qt::ISODate));
#if defined(QT_NETWORK_LIB)
	query.addQueryItem(QStringLiteral("format"), QStringLiteral("xml"));
	url.setQuery(query);
	if (!network)
		network = new QNetworkAccessManager(this);
	lookup_button->setEnabled(false);
	lookup_label->setText(tr("Looking up the declination..."));
	auto* reply = network->get(QNetworkRequest(url));
	connect(reply, &QNetworkReply::finished, this, [this, reply]() {
		reply->deleteLater();
		lookup_button->setEnabled(true);
		GeomagneticField field;
		if (reply->error() == QNetworkReply::NoError)
			field = parseGeomagneticReply(reply->readAll());
		if (field.has_declination)
			declination_edit->setValue(Georeferencing::roundDeclination(field.declination));
		lookup_label->setText(field.has_declination ? declinationText(field) : tr("The online lookup failed."));
	});
#else
	query.addQueryItem(QStringLiteral("format"), QStringLiteral("html"));
	url.setQuery(query);
	QDesktopServices::openUrl(url);
#endif
}



CenterMapDialog::CenterMapDialog(const Map& map, QWidget* parent)
: QDialog(parent)
{
	setWindowTitle(tr("Center map in drawing area"));
	auto const offset = mapOffset(map);
	auto const proposed = proposedMapOffset(map);
	auto* form = new QFormLayout();
	form->addRow(new QLabel(tr("Existing map offset:")));
	form->addRow(tr("Easting:"), new QLabel(QString::number(offset.x(), 'f', 2) + QStringLiteral(" m")));
	form->addRow(tr("Northing:"), new QLabel(QString::number(offset.y(), 'f', 2) + QStringLiteral(" m")));
	form->addRow(new QLabel(tr("New map offset:")));
	east_edit = makeEdit(proposed.x(), -100000000.0, 100000000.0, QStringLiteral(" m"));
	north_edit = makeEdit(proposed.y(), -100000000.0, 100000000.0, QStringLiteral(" m"));
	form->addRow(tr("Easting:"), east_edit);
	form->addRow(tr("Northing:"), north_edit);
	finish(this, form);
	if (auto* buttons = findChild<QDialogButtonBox*>())
		buttons->button(QDialogButtonBox::Ok)->setText(tr("Center map"));
}

CenterMapDialog::~CenterMapDialog() = default;

QPointF CenterMapDialog::offset() const { return { east_edit->value(), north_edit->value() }; }


}  // namespace OpenOrienteering
