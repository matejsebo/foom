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


#include "new_map_wizard.h"

#include <algorithm>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

#include <Qt>
#include <QtMath>
#include <QApplication>
#include <QByteArray>
#include <QCheckBox>
#include <QComboBox>
#include <QCursor>
#include <QDate>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLatin1String>
#include <QLineEdit>
#include <QMessageBox>
#include <QMouseEvent>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPainter>
#include <QPen>
#include <QPointF>
#include <QPolygonF>
#include <QPushButton>
#include <QRadioButton>
#include <QRectF>
#include <QSettings>
#include <QStandardPaths>
#include <QUrl>
#include <QUrlQuery>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QWizardPage>
#include <QXmlStreamReader>

#include "core/georeferencing.h"
#include "core/latlon.h"
#include "core/map.h"
#include "core/map_coord.h"
#include "gdal/map_service.h"
#include "gdal/tile_service_template.h"
#include "gui/util_gui.h"
#include "templates/template.h"


namespace OpenOrienteering {

namespace {

QString trWizard(const char* text)
{
	return QCoreApplication::translate("OpenOrienteering::NewMapWizard", text);
}

QDoubleSpinBox* degreeSpin(double min, double max)
{
	return Util::SpinBox::create(6, min, max, QString::fromUtf8("°"));
}


/**
 * A map view backed by a tile service, for choosing the box.
 *
 * Left drag draws the box, right or middle drag pans, the wheel zooms.
 */
class AreaPicker : public QWidget
{
public:
	explicit AreaPicker(QWidget* parent = nullptr)
	: QWidget(parent)
	{
		setMinimumSize(420, 300);
		setMouseTracking(false);
		setCursor(Qt::CrossCursor);
	}
	
	std::function<void()> box_changed;
	
	void setService(const MapService* service)
	{
		service_ = service ? std::make_unique<MapService>(*service) : nullptr;
		rebuild(centre_latlon);
	}
	
	void setBox(const GeoBox& box, bool recentre)
	{
		box_ = box;
		if (recentre && box.isValid())
		{
			rebuild(box.centre());
			fitBox();
		}
		update();
	}
	
	GeoBox box() const { return box_; }
	
	void start(const LatLon& centre)
	{
		rebuild(centre);
		fitBox();
	}
	
protected:
	void paintEvent(QPaintEvent* /*event*/) override
	{
		QPainter painter(this);
		painter.fillRect(rect(), QColor(240, 240, 235));
		painter.setRenderHint(QPainter::Antialiasing);
		painter.setRenderHint(QPainter::SmoothPixmapTransform);
		if (map && tile && tile->getTemplateState() == Template::Loaded)
		{
			painter.save();
			painter.setTransform(mapToWidget());
			tile->drawTemplate(&painter, visibleMapRect(), px_per_mm, true, 1.0);
			painter.restore();
		}
		if (map && box_.isValid())
		{
			auto const polygon = mapToWidget().map(newMapBoxPolygon(*map, box_));
			painter.setPen(QPen(QColor(200, 0, 0), 2));
			painter.setBrush(QColor(200, 0, 0, 40));
			painter.drawPolygon(polygon);
		}
		if (service_ && !service_->attribution.isEmpty())
		{
			painter.setPen(Qt::black);
			auto const text_rect = rect().adjusted(4, 4, -4, -4);
			painter.drawText(text_rect, int(Qt::AlignRight | Qt::AlignBottom), service_->attribution);
		}
	}
	
	void mousePressEvent(QMouseEvent* event) override
	{
		press_pos = event->pos();
		press_centre = centre_mm;
		if (event->button() == Qt::LeftButton)
			drawing = true;
		else
			panning = true;
	}
	
	void mouseMoveEvent(QMouseEvent* event) override
	{
		if (panning)
		{
			auto const delta = QPointF(event->pos() - press_pos) / px_per_mm;
			centre_mm = press_centre - delta;
			update();
		}
		else if (drawing && map)
		{
			box_ = boxFromCorners(widgetToMap(press_pos), widgetToMap(event->pos()));
			update();
		}
	}
	
	void mouseReleaseEvent(QMouseEvent* event) override
	{
		if (drawing && map && (event->pos() - press_pos).manhattanLength() > 3)
		{
			box_ = boxFromCorners(widgetToMap(press_pos), widgetToMap(event->pos()));
			if (box_changed)
				box_changed();
		}
		drawing = panning = false;
		update();
	}
	
	void wheelEvent(QWheelEvent* event) override
	{
		auto const factor = event->angleDelta().y() > 0 ? 1.25 : 0.8;
		auto const before = widgetToMap(event->position());
		px_per_mm = qBound(0.002, px_per_mm * factor, 50.0);
		auto const after = widgetToMap(event->position());
		centre_mm += before - after;
		update();
	}
	
private:
	QTransform mapToWidget() const
	{
		QTransform transform;
		transform.translate(width() / 2.0, height() / 2.0);
		transform.scale(px_per_mm, px_per_mm);
		transform.translate(-centre_mm.x(), -centre_mm.y());
		return transform;
	}
	
	QPointF widgetToMap(const QPointF& pos) const
	{
		return mapToWidget().inverted().map(pos);
	}
	
	QRectF visibleMapRect() const
	{
		return mapToWidget().inverted().mapRect(QRectF(rect()));
	}
	
	GeoBox boxFromCorners(const QPointF& a, const QPointF& b) const
	{
		auto const& georef = map->getGeoreferencing();
		auto const p = georef.toGeographicCoords(MapCoordF(a));
		auto const q = georef.toGeographicCoords(MapCoordF(b));
		return { std::min(p.latitude(), q.latitude()), std::min(p.longitude(), q.longitude()),
		         std::max(p.latitude(), q.latitude()), std::max(p.longitude(), q.longitude()) };
	}
	
	void fitBox()
	{
		if (!map || !box_.isValid())
			return;
		auto const bounds = newMapBoxPolygon(*map, box_).boundingRect();
		centre_mm = bounds.center();
		if (bounds.width() > 0 && bounds.height() > 0)
			px_per_mm = qBound(0.002, 0.6 * std::min(width() / bounds.width(), height() / bounds.height()), 50.0);
		update();
	}
	
	/// A scratch map in the UTM zone of the centre, with the tile service as its template.
	void rebuild(const LatLon& centre)
	{
		centre_latlon = centre;
		tile = nullptr;
		map = std::make_unique<Map>();
		map->setScaleDenominator(10000);
		Georeferencing georef;
		georef.setScaleDenominator(10000);
		auto const code = newMapUtmEpsg(centre);
		georef.setProjectedCRS(QStringLiteral("EPSG"), QStringLiteral("+init=epsg:%1").arg(code), { QString::number(code) });
		georef.setMapRefPoint(MapCoord(0, 0));
		georef.setGeographicRefPoint(centre);
		map->setGeoreferencing(georef);
		centre_mm = {};
		if (service_ && TileServiceTemplate::driversAvailable())
		{
			auto temp = std::make_unique<TileServiceTemplate>(*service_, map.get());
			tile = temp.get();
			map->addTemplate(-1, std::move(temp));
			if (tile->setupAndLoad(this, nullptr))
				connect(tile, &TileServiceTemplate::tilesChanged, this, [this]() { update(); });
		}
		update();
	}
	
	std::unique_ptr<MapService> service_;
	std::unique_ptr<Map> map;
	TileServiceTemplate* tile = nullptr;
	LatLon centre_latlon { 47.0, 8.0 };
	QPointF centre_mm;
	double px_per_mm = 0.5;
	GeoBox box_;
	QPoint press_pos;
	QPointF press_centre;
	bool drawing = false;
	bool panning = false;
};

}  // namespace



// ### AreaPage ###

class AreaPage : public QWizardPage
{
public:
	AreaPage()
	{
		setTitle(trWizard("Area"));
		setSubTitle(trWizard("Drag a box with the left mouse button, or type its bounds. Drag with the right button to pan, use the wheel to zoom."));
		auto* layout = new QVBoxLayout(this);
		
		service_combo = new QComboBox();
		service_combo->addItem(trWizard("No background map"));
		services = MapService::fromSettings();
		for (auto const& service : services)
		{
			if (service.kind == MapService::Xyz || service.kind == MapService::Wmts || service.kind == MapService::Wms)
				service_combo->addItem(service.name);
		}
		auto* service_layout = new QFormLayout();
		service_layout->addRow(trWizard("Background map:"), service_combo);
		layout->addLayout(service_layout);
		
		picker = new AreaPicker();
		layout->addWidget(picker, 1);
		
		auto* bounds_layout = new QHBoxLayout();
		south = degreeSpin(-90, 90);
		west = degreeSpin(-180, 180);
		north = degreeSpin(-90, 90);
		east = degreeSpin(-180, 180);
		bounds_layout->addWidget(new QLabel(trWizard("S:")));
		bounds_layout->addWidget(south);
		bounds_layout->addWidget(new QLabel(trWizard("W:")));
		bounds_layout->addWidget(west);
		bounds_layout->addWidget(new QLabel(trWizard("N:")));
		bounds_layout->addWidget(north);
		bounds_layout->addWidget(new QLabel(trWizard("E:")));
		bounds_layout->addWidget(east);
		layout->addLayout(bounds_layout);
		area_label = new QLabel();
		area_label->setWordWrap(true);
		layout->addWidget(area_label);
		
		QSettings settings;
		auto const last = settings.value(QStringLiteral("NewMap/box")).toList();
		GeoBox box { 47.0, 8.0, 47.01, 8.015 };
		if (last.size() == 4)
			box = { last[0].toDouble(), last[1].toDouble(), last[2].toDouble(), last[3].toDouble() };
		setSpins(box);
		auto const last_service = settings.value(QStringLiteral("NewMap/service"), services.empty() ? QString{} : services.front().name).toString();
		service_combo->setCurrentIndex(std::max(0, service_combo->findText(last_service)));
		
		picker->box_changed = [this]() {
			setSpins(picker->box());
			updateArea();
		};
		for (auto* spin : { south, west, north, east })
		{
			connect(spin, &QDoubleSpinBox::editingFinished, this, [this]() {
				picker->setBox(spinBox(), true);
				updateArea();
			});
		}
		connect(service_combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() {
			picker->setService(service());
			picker->setBox(spinBox(), true);
		});
	}
	
	void initializePage() override
	{
		picker->setService(service());
		picker->setBox(spinBox(), false);
		picker->start(spinBox().centre());
		picker->setBox(spinBox(), true);
		updateArea();
	}
	
	bool isComplete() const override
	{
		return spinBox().isValid();
	}
	
	bool validatePage() override
	{
		QSettings settings;
		auto const box = spinBox();
		settings.setValue(QStringLiteral("NewMap/box"), QVariantList{ box.south, box.west, box.north, box.east });
		settings.setValue(QStringLiteral("NewMap/service"), service_combo->currentText());
		return true;
	}
	
	GeoBox spinBox() const
	{
		return { south->value(), west->value(), north->value(), east->value() };
	}
	
	const MapService* service() const
	{
		auto const index = service_combo->currentIndex();
		if (index <= 0)
			return nullptr;
		for (auto const& s : services)
		{
			if (s.name == service_combo->currentText())
				return &s;
		}
		return nullptr;
	}
	
private:
	void setSpins(const GeoBox& box)
	{
		south->setValue(box.south);
		west->setValue(box.west);
		north->setValue(box.north);
		east->setValue(box.east);
	}
	
	void updateArea()
	{
		auto const box = spinBox();
		if (!box.isValid())
		{
			area_label->setText(trWizard("The box is empty."));
		}
		else
		{
			auto const area = newMapAreaKm2(box);
			auto text = trWizard("Area: %1 km².").arg(QString::number(area, 'f', 2));
			if (area > new_map_max_download_km2)
				text += QLatin1Char(' ') + trWizard("This is larger than %1 km², so OpenStreetMap data must come from a file.").arg(QString::number(new_map_max_download_km2));
			area_label->setText(text);
		}
		emit completeChanged();
	}
	
	std::vector<MapService> services;
	QComboBox* service_combo;
	AreaPicker* picker;
	QDoubleSpinBox* south;
	QDoubleSpinBox* west;
	QDoubleSpinBox* north;
	QDoubleSpinBox* east;
	QLabel* area_label;
};


// ### GeoreferencingPage ###

class GeoreferencingPage : public QWizardPage
{
public:
	explicit GeoreferencingPage(AreaPage* area_page)
	: area_page(area_page)
	{
		setTitle(trWizard("Georeferencing"));
		setSubTitle(trWizard("The map's reference point is the centre of the box."));
		auto* layout = new QFormLayout(this);
		utm_radio = new QRadioButton();
		utm_radio->setChecked(true);
		layout->addRow(utm_radio);
		other_radio = new QRadioButton(trWizard("Other coordinate reference system:"));
		crs_edit = new QLineEdit();
		crs_edit->setPlaceholderText(trWizard("EPSG code or PROJ specification"));
		layout->addRow(other_radio, crs_edit);
		
		declination_spin = Util::SpinBox::create(2, -180.0, 180.0, trUtf8("°"));
		lookup_button = new QPushButton(trWizard("Look up online"));
		auto* declination_layout = new QHBoxLayout();
		declination_layout->addWidget(declination_spin);
		declination_layout->addWidget(lookup_button);
		layout->addRow(trWizard("Declination:"), declination_layout);
		lookup_label = new QLabel();
		lookup_label->setWordWrap(true);
		layout->addRow(lookup_label);
		rotate_check = new QCheckBox(trWizard("Rotate the map to magnetic north"));
		rotate_check->setChecked(true);
		layout->addRow(rotate_check);
		
		connect(lookup_button, &QPushButton::clicked, this, [this]() { lookUpDeclination(); });
		connect(other_radio, &QRadioButton::toggled, crs_edit, &QLineEdit::setEnabled);
		crs_edit->setEnabled(false);
	}
	
	void initializePage() override
	{
		auto const code = newMapUtmEpsg(area_page->spinBox().centre());
		auto const zone = code % 100;
		auto const hemisphere = code / 100 == 326 ? trWizard("north") : trWizard("south");
		utm_radio->setText(trWizard("UTM zone %1 %2 (EPSG:%3), from the centre of the box").arg(zone).arg(hemisphere).arg(code));
	}
	
	bool isComplete() const override
	{
		return !other_radio->isChecked() || !crs_edit->text().trimmed().isEmpty();
	}
	
	QString crsSpec() const
	{
		return other_radio->isChecked() ? crs_edit->text().trimmed() : QString{};
	}
	
	double declination() const { return declination_spin->value(); }
	bool rotate() const { return rotate_check->isChecked(); }
	
private:
	/// The online lookup of OOM's georeferencing dialog, at the box centre.
	void lookUpDeclination()
	{
		auto const centre = area_page->spinBox().centre();
		QUrl url(QStringLiteral("https://geomag.bgs.ac.uk/web_service/GMModels/wmm/2025"));
		QUrlQuery query;
		query.addQueryItem(QStringLiteral("latitude"), QString::number(centre.latitude()));
		query.addQueryItem(QStringLiteral("longitude"), QString::number(centre.longitude()));
		query.addQueryItem(QStringLiteral("altitude"), QStringLiteral("0"));
		query.addQueryItem(QStringLiteral("date"), QDate::currentDate().toString(Qt::ISODate));
		query.addQueryItem(QStringLiteral("format"), QStringLiteral("xml"));
		url.setQuery(query);
		if (!network)
			network = new QNetworkAccessManager(this);
		lookup_button->setEnabled(false);
		lookup_label->setText(trWizard("Looking up the declination..."));
		auto* reply = network->get(QNetworkRequest(url));
		connect(reply, &QNetworkReply::finished, this, [this, reply]() {
			reply->deleteLater();
			lookup_button->setEnabled(true);
			auto found = false;
			if (reply->error() == QNetworkReply::NoError)
			{
				QXmlStreamReader xml(reply);
				while (!xml.atEnd() && !found)
				{
					if (xml.readNext() == QXmlStreamReader::StartElement && xml.name() == QLatin1String("declination"))
					{
						bool ok = false;
						auto const value = xml.readElementText(QXmlStreamReader::IncludeChildElements).toDouble(&ok);
						if (ok)
						{
							declination_spin->setValue(Georeferencing::roundDeclination(value));
							found = true;
						}
					}
				}
			}
			lookup_label->setText(found ? QString{} : trWizard("The online lookup failed; the declination stays as typed (0 means none)."));
		});
	}
	
	AreaPage* area_page;
	QRadioButton* utm_radio;
	QRadioButton* other_radio;
	QLineEdit* crs_edit;
	QDoubleSpinBox* declination_spin;
	QPushButton* lookup_button;
	QLabel* lookup_label;
	QCheckBox* rotate_check;
	QNetworkAccessManager* network = nullptr;
};


// ### DataPage ###

class DataPage : public QWizardPage
{
public:
	explicit DataPage(AreaPage* area_page)
	: area_page(area_page)
	{
		setTitle(trWizard("Data"));
		setSubTitle(trWizard("OpenStreetMap data for the box, clipped to it and given the symbols of the symbol set's rule file."));
		auto* layout = new QFormLayout(this);
		download_radio = new QRadioButton();
		layout->addRow(download_radio);
		file_radio = new QRadioButton(trWizard("Open a downloaded file:"));
		file_edit = new QLineEdit();
		auto* browse = new QPushButton(trWizard("Browse..."));
		auto* file_layout = new QHBoxLayout();
		file_layout->addWidget(file_edit, 1);
		file_layout->addWidget(browse);
		layout->addRow(file_radio, file_layout);
		limit_label = new QLabel();
		limit_label->setWordWrap(true);
		layout->addRow(limit_label);
		template_check = new QCheckBox();
		layout->addRow(template_check);
		
		// foom: "Elevation on the data page"
		elevation_edit = new QLineEdit();
		elevation_edit->setPlaceholderText(trWizard("None"));
		auto* elevation_browse = new QPushButton(trWizard("Browse..."));
		auto* elevation_layout = new QHBoxLayout();
		elevation_layout->addWidget(elevation_edit, 1);
		elevation_layout->addWidget(elevation_browse);
		layout->addRow(trWizard("Elevation (optional):"), elevation_layout);
		cell_spin = new QDoubleSpinBox();
		cell_spin->setRange(0.25, 100.0);
		cell_spin->setDecimals(2);
		cell_spin->setSingleStep(0.5);
		cell_spin->setValue(1.0);
		cell_spin->setSuffix(QLatin1String(" m"));
		layout->addRow(trWizard("Cell size for a point cloud:"), cell_spin);
		connect(elevation_browse, &QPushButton::clicked, this, [this]() {
			QString const filter = trWizard("Elevation grids and point clouds") + QLatin1String(" (*.tif *.tiff *.asc *.xyz *.hgt *.ocdDem *.img *.vrt *.dem *.las *.laz);;")
			                       + trWizard("All files") + QLatin1String(" (*.*)");
			auto const path = QFileDialog::getOpenFileName(this, trWizard("Open elevation data"), elevation_edit->text(), filter);
			if (!path.isEmpty())
				elevation_edit->setText(QDir::toNativeSeparators(path));
		});
		
		connect(browse, &QPushButton::clicked, this, [this]() {
			auto const path = QFileDialog::getOpenFileName(this, trWizard("Open OpenStreetMap file"), file_edit->text(), trWizard("OpenStreetMap") + QLatin1String(" (*.osm *.pbf);;") + trWizard("All files") + QLatin1String(" (*.*)"));
			if (!path.isEmpty())
			{
				file_edit->setText(QDir::toNativeSeparators(path));
				file_radio->setChecked(true);
			}
		});
		connect(file_edit, &QLineEdit::textChanged, this, &QWizardPage::completeChanged);
		connect(file_radio, &QRadioButton::toggled, this, &QWizardPage::completeChanged);
	}
	
	void initializePage() override
	{
		auto const area = newMapAreaKm2(area_page->spinBox());
		auto const can_download = area <= new_map_max_download_km2;
		download_radio->setText(trWizard("Download from %1").arg(newMapDefaultEndpoint()));
		download_radio->setEnabled(can_download);
		limit_label->setText(can_download ? QString{} : trWizard("The box is larger than %1 km², the limit for downloading. Download the data elsewhere and open the file here.").arg(QString::number(new_map_max_download_km2)));
		if (can_download && !file_radio->isChecked())
			download_radio->setChecked(true);
		else if (!can_download)
			file_radio->setChecked(true);
		auto const* service = area_page->service();
		template_check->setEnabled(service);
		template_check->setChecked(service);
		template_check->setText(service ? trWizard("Add %1 as a template").arg(service->name) : trWizard("Add the background map as a template"));
	}
	
	bool isComplete() const override
	{
		return download_radio->isChecked() || !file_edit->text().trimmed().isEmpty();
	}
	
	QString osmPath() const
	{
		return file_radio->isChecked() ? QDir::fromNativeSeparators(file_edit->text().trimmed()) : QString{};
	}
	
	QString elevationPath() const
	{
		return QDir::fromNativeSeparators(elevation_edit->text().trimmed());
	}
	
	double elevationCell() const
	{
		return cell_spin->value();
	}
	
	QString tileService() const
	{
		auto const* service = area_page->service();
		return service && template_check->isChecked() ? service->name : QString{};
	}
	
private:
	AreaPage* area_page;
	QRadioButton* download_radio;
	QRadioButton* file_radio;
	QLineEdit* file_edit;
	QLabel* limit_label;
	QCheckBox* template_check;
	QLineEdit* elevation_edit;
	QDoubleSpinBox* cell_spin;
};


// ### RunPage ###

class RunPage : public QWizardPage
{
public:
	explicit RunPage(NewMapWizard* wizard)
	: wizard(wizard)
	{
		setTitle(trWizard("Run"));
		setSubTitle(trWizard("Finish downloads the data if chosen, imports it, adds the template, and saves the map."));
		auto* layout = new QFormLayout(this);
		path_edit = new QLineEdit();
		auto* browse = new QPushButton(trWizard("Browse..."));
		auto* path_layout = new QHBoxLayout();
		path_layout->addWidget(path_edit, 1);
		path_layout->addWidget(browse);
		layout->addRow(trWizard("Save the map as:"), path_layout);
		status_label = new QLabel();
		status_label->setWordWrap(true);
		layout->addRow(status_label);
		
		QSettings settings;
		auto const dir = settings.value(QStringLiteral("openFileDirectory"), QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)).toString();
		path_edit->setText(QDir::toNativeSeparators(QDir(dir).absoluteFilePath(trWizard("New map") + QLatin1String(".foom"))));
		
		connect(browse, &QPushButton::clicked, this, [this]() {
			auto const path = QFileDialog::getSaveFileName(this, trWizard("Save the map as"), path_edit->text(), trWizard("FOOM maps") + QLatin1String(" (*.foom)"));
			if (!path.isEmpty())
				path_edit->setText(QDir::toNativeSeparators(path));
		});
		connect(path_edit, &QLineEdit::textChanged, this, &QWizardPage::completeChanged);
	}
	
	bool isComplete() const override
	{
		return !path_edit->text().trimmed().isEmpty();
	}
	
	bool validatePage() override
	{
		auto path = mapPath();
		if (QFileInfo(path).suffix().isEmpty())
			path_edit->setText(QDir::toNativeSeparators(path + QLatin1String(".foom")));
		if (QFileInfo::exists(mapPath())
		    && QMessageBox::question(this, trWizard("New map"), trWizard("%1 exists. Replace it?").arg(QFileInfo(mapPath()).fileName())) != QMessageBox::Yes)
			return false;
		status_label->setText(trWizard("Working..."));
		QApplication::setOverrideCursor(Qt::WaitCursor);
		auto const ok = wizard->run();
		QApplication::restoreOverrideCursor();
		status_label->clear();
		return ok;
	}
	
	QString mapPath() const
	{
		return QDir::fromNativeSeparators(path_edit->text().trimmed());
	}
	
private:
	NewMapWizard* wizard;
	QLineEdit* path_edit;
	QLabel* status_label;
};



// ### NewMapWizard ###

NewMapWizard::NewMapWizard(const QString& symbol_set_path, unsigned int scale, QWidget* parent)
: QWizard(parent)
, symbol_set_path(symbol_set_path)
, scale(scale)
{
	setWindowTitle(tr("New map wizard"));
	setOption(QWizard::NoBackButtonOnStartPage);
	area_page = new AreaPage();
	georef_page = new GeoreferencingPage(area_page);
	data_page = new DataPage(area_page);
	run_page = new RunPage(this);
	addPage(area_page);
	addPage(georef_page);
	addPage(data_page);
	addPage(run_page);
	resize(760, 640);
}


NewMapWizard::~NewMapWizard() = default;


NewMapParams NewMapWizard::params() const
{
	NewMapParams params;
	params.box = area_page->spinBox();
	params.symbol_set_path = symbol_set_path;
	params.scale = scale;
	params.crs_spec = georef_page->crsSpec();
	params.declination = georef_page->declination();
	params.rotate_to_magnetic = georef_page->rotate();
	params.osm_path = data_page->osmPath();
	params.tile_service = data_page->tileService();
	params.elevation_path = data_page->elevationPath();
	params.elevation_cell = data_page->elevationCell();
	params.map_path = run_page->mapPath();
	return params;
}


bool NewMapWizard::run()
{
	auto const result = createNewMap(params());
	if (!result.ok)
	{
		QMessageBox::warning(this, tr("New map wizard"), result.error);
		return false;
	}
	if (!result.warnings.isEmpty())
		QMessageBox::information(this, tr("New map wizard"), result.warnings.join(QLatin1Char('\n')));
	map_path = run_page->mapPath();
	return true;
}


}  // namespace OpenOrienteering
