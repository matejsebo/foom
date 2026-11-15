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


#include "map_service_dialog.h"

#include <algorithm>
#include <cmath>
#include <memory>

#include <QtGlobal>
#include <QAbstractItemView>
#include <QAuthenticator>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLatin1String>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProgressDialog>
#include <QPushButton>
#include <QRadioButton>
#include <QRectF>
#include <QRegExp>
#include <QSpinBox>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QUrl>
#include <QUrlQuery>
#include <QVBoxLayout>

#include "core/georeferencing.h"
#include "core/map.h"
#include "core/map_view.h"
#include "gdal/gdal_template.h"
#include "gdal/tile_service_template.h"
#include "gui/file_dialog.h"
#include "gui/main_window.h"
#include "gui/map/map_editor.h"
#include "gui/map/map_widget.h"
#include "templates/template.h"

namespace OpenOrienteering {

namespace {

QString scaleRange(const ServiceLayer& layer)
{
	if (layer.min_scale <= 0 && layer.max_scale <= 0)
		return {};
	auto const format = [](double value) {
		return value > 0 ? QStringLiteral("1:%1").arg(qRound64(value)) : QString{};
	};
	return format(layer.min_scale) + QStringLiteral(" – ") + format(layer.max_scale);
}

}  // namespace


// static
QString MapServiceDialog::mapSystem(const MapEditorController* controller)
{
	auto const& georef = controller->getMap()->getGeoreferencing();
	if (georef.getState() != Georeferencing::Geospatial)
		return {};
	auto const id = georef.getProjectedCRSId();
	auto const& parameters = georef.getProjectedCRSParameters();
	if (id == QLatin1String("EPSG") && parameters.size() == 1)
		return QStringLiteral("EPSG:") + parameters[0];
	if (id == QLatin1String("UTM") && parameters.size() == 1)
	{
		// "33 N", "33 S"
		auto const parts = parameters[0].simplified().split(QLatin1Char(' '));
		if (parts.size() == 2)
		{
			auto const zone = parts[0].toInt();
			auto const south = parts[1].startsWith(QLatin1Char('S'), Qt::CaseInsensitive);
			if (zone >= 1 && zone <= 60)
				return QStringLiteral("EPSG:%1").arg((south ? 32700 : 32600) + zone);
		}
	}
	return {};
}


MapServiceDialog::MapServiceDialog(MapEditorController* controller, QWidget* parent)
: QDialog(parent)
, controller(controller)
, services(MapService::fromSettings())
, network(new QNetworkAccessManager(this))
{
	setWindowTitle(tr("Add map service"));
	setWindowModality(Qt::WindowModal);

	connect(network, &QNetworkAccessManager::finished, this, &MapServiceDialog::replyFinished);
	connect(network, &QNetworkAccessManager::authenticationRequired, this, &MapServiceDialog::authenticationRequired);

	// Service list
	list = new QListWidget();
	for (auto const& service : services)
		list->addItem(service.name.isEmpty() ? tr("(unnamed)") : service.name);
	auto* new_button = new QPushButton(tr("New"));
	remove_button = new QPushButton(tr("Remove"));
	auto* list_buttons = new QHBoxLayout();
	list_buttons->addWidget(new_button);
	list_buttons->addWidget(remove_button);
	auto* list_layout = new QVBoxLayout();
	list_layout->addWidget(new QLabel(tr("Services:")));
	list_layout->addWidget(list, 1);
	list_layout->addLayout(list_buttons);

	// Service fields
	name_edit = new QLineEdit();
	kind_combo = new QComboBox();
	kind_combo->addItem(tr("WMS"), int(MapService::Wms));
	kind_combo->addItem(tr("WMTS"), int(MapService::Wmts));
	kind_combo->addItem(tr("Tiles (XYZ)"), int(MapService::Xyz));
	url_edit = new QLineEdit();
	url_edit->setMinimumWidth(360);
	layers_edit = new QLineEdit();
	format_combo = new QComboBox();
	format_combo->setEditable(true);
	crs_combo = new QComboBox();
	crs_combo->setEditable(true);
	matrix_combo = new QComboBox();
	matrix_combo->setEditable(true);
	min_zoom_edit = new QSpinBox();
	min_zoom_edit->setRange(0, 24);
	max_zoom_edit = new QSpinBox();
	max_zoom_edit->setRange(0, 24);
	auto* zoom_row = new QHBoxLayout();
	zoom_row->addWidget(min_zoom_edit);
	zoom_row->addWidget(new QLabel(tr("to")));
	zoom_row->addWidget(max_zoom_edit);
	zoom_row->addStretch();
	attribution_edit = new QLineEdit();
	bulk_check = new QCheckBox(tr("Allow Download extent"));
	user_edit = new QLineEdit();
	password_edit = new QLineEdit();
	password_edit->setEchoMode(QLineEdit::Password);
	connect_button = new QPushButton(tr("Connect"));
	status_label = new QLabel();
	status_label->setWordWrap(true);

	auto* form = new QFormLayout();
	form->addRow(tr("Name:"), name_edit);
	form->addRow(tr("Kind:"), kind_combo);
	form->addRow(tr("URL:"), url_edit);
	form->addRow(tr("Zoom levels:"), zoom_row);
	form->addRow(tr("Layers:"), layers_edit);
	form->addRow(tr("Image format:"), format_combo);
	form->addRow(tr("Coordinate system:"), crs_combo);
	form->addRow(tr("Tile matrix set:"), matrix_combo);
	form->addRow(tr("Attribution:"), attribution_edit);
	form->addRow(QString{}, bulk_check);
	form->addRow(tr("User name:"), user_edit);
	form->addRow(tr("Password:"), password_edit);
	auto* connect_row = new QHBoxLayout();
	connect_row->addWidget(connect_button);
	connect_row->addWidget(status_label, 1);

	// Layer table
	layer_table = new QTableWidget(0, 5);
	layer_table->setHorizontalHeaderLabels({ tr("Id"), tr("Title"), tr("Scale range"), tr("Formats or matrix sets"), tr("Coordinate systems") });
	layer_table->horizontalHeader()->setStretchLastSection(true);
	layer_table->verticalHeader()->setVisible(false);
	layer_table->setSelectionBehavior(QAbstractItemView::SelectRows);
	layer_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
	layer_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
	crs_warning = new QLabel();
	crs_warning->setWordWrap(true);

	auto* service_layout = new QVBoxLayout();
	service_layout->addLayout(form);
	service_layout->addLayout(connect_row);
	service_layout->addWidget(new QLabel(tr("Layers offered by the server:")));
	service_layout->addWidget(layer_table, 1);
	service_layout->addWidget(crs_warning);

	auto* buttons = new QDialogButtonBox();
	add_button = buttons->addButton(tr("Add online"), QDialogButtonBox::AcceptRole);
	download_button = buttons->addButton(tr("Download extent..."), QDialogButtonBox::ActionRole);
	buttons->addButton(QDialogButtonBox::Close);
	connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	connect(download_button, &QPushButton::clicked, this, &MapServiceDialog::downloadClicked);

	auto* columns = new QHBoxLayout();
	columns->addLayout(list_layout);
	columns->addLayout(service_layout, 1);
	auto* layout = new QVBoxLayout();
	layout->addLayout(columns, 1);
	layout->addWidget(buttons);
	setLayout(layout);

	connect(list, &QListWidget::currentRowChanged, this, &MapServiceDialog::currentServiceChanged);
	connect(new_button, &QPushButton::clicked, this, &MapServiceDialog::addService);
	connect(remove_button, &QPushButton::clicked, this, &MapServiceDialog::removeService);
	connect(connect_button, &QPushButton::clicked, this, &MapServiceDialog::connectClicked);
	connect(layer_table, &QTableWidget::itemSelectionChanged, this, &MapServiceDialog::layerSelectionChanged);
	for (auto* edit : { name_edit, url_edit, layers_edit, attribution_edit, user_edit, password_edit })
		connect(edit, &QLineEdit::textEdited, this, &MapServiceDialog::fieldEdited);
	for (auto* combo : { format_combo, crs_combo, matrix_combo })
		connect(combo, &QComboBox::editTextChanged, this, &MapServiceDialog::fieldEdited);
	connect(kind_combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MapServiceDialog::kindChanged);
	connect(min_zoom_edit, QOverload<int>::of(&QSpinBox::valueChanged), this, &MapServiceDialog::fieldEdited);
	connect(max_zoom_edit, QOverload<int>::of(&QSpinBox::valueChanged), this, &MapServiceDialog::fieldEdited);
	connect(bulk_check, &QCheckBox::toggled, this, &MapServiceDialog::fieldEdited);
	connect(this, &QDialog::finished, this, &MapServiceDialog::saveServices);

	if (list->count() > 0)
		list->setCurrentRow(0);
	else
		currentServiceChanged(-1);
}

MapServiceDialog::~MapServiceDialog() = default;


MapService MapServiceDialog::service() const
{
	return fieldsToService();
}


void MapServiceDialog::currentServiceChanged(int row)
{
	if (!updating && current >= 0 && current < int(services.size()))
		storeFields();
	current = row;
	capabilities = ServiceCapabilities{};
	layer_table->setRowCount(0);
	status_label->clear();
	crs_warning->clear();
	if (row >= 0 && row < int(services.size()))
		showService(services[std::size_t(row)]);
	else
		showService(MapService{});
	updateButtons();
}

void MapServiceDialog::addService()
{
	MapService service;
	service.name = tr("New service");
	services.push_back(service);
	list->addItem(service.name);
	list->setCurrentRow(list->count() - 1);
	name_edit->setFocus();
	name_edit->selectAll();
}

void MapServiceDialog::removeService()
{
	if (current < 0 || current >= int(services.size()))
		return;
	auto const row = current;
	current = -1;
	services.erase(services.begin() + row);
	delete list->takeItem(row);
	if (list->count() > 0)
		list->setCurrentRow(qMin(row, list->count() - 1));
	else
		currentServiceChanged(-1);
}

void MapServiceDialog::fieldEdited()
{
	if (updating)
		return;
	if (current >= 0 && current < int(services.size()))
	{
		storeFields();
		list->item(current)->setText(services[std::size_t(current)].name.isEmpty() ? tr("(unnamed)") : services[std::size_t(current)].name);
	}
	updateButtons();
}

void MapServiceDialog::kindChanged()
{
	auto const kind = MapService::Kind(kind_combo->currentData().toInt());
	auto const xyz = kind == MapService::Xyz;
	min_zoom_edit->setEnabled(xyz);
	max_zoom_edit->setEnabled(xyz);
	layers_edit->setEnabled(!xyz);
	format_combo->setEnabled(kind == MapService::Wms);
	crs_combo->setEnabled(kind == MapService::Wms);
	matrix_combo->setEnabled(kind == MapService::Wmts);
	connect_button->setEnabled(!xyz);
	layer_table->setEnabled(!xyz);
	fieldEdited();
}


void MapServiceDialog::showService(const MapService& service)
{
	updating = true;
	name_edit->setText(service.name);
	kind_combo->setCurrentIndex(kind_combo->findData(int(service.kind)));
	url_edit->setText(service.url);
	layers_edit->setText(service.layers);
	format_combo->clear();
	format_combo->addItem(service.format);
	format_combo->setCurrentText(service.format);
	crs_combo->clear();
	crs_combo->addItem(service.crs);
	crs_combo->setCurrentText(service.crs);
	matrix_combo->clear();
	if (!service.matrix_set.isEmpty())
		matrix_combo->addItem(service.matrix_set);
	matrix_combo->setCurrentText(service.matrix_set);
	min_zoom_edit->setValue(service.min_zoom);
	max_zoom_edit->setValue(service.max_zoom);
	attribution_edit->setText(service.attribution);
	bulk_check->setChecked(service.bulk_download);
	user_edit->setText(service.user);
	password_edit->setText(service.password);
	updating = false;
	kindChanged();
}

MapService MapServiceDialog::fieldsToService() const
{
	MapService service;
	if (current >= 0 && current < int(services.size()))
		service = services[std::size_t(current)];  // keeps bbox, resolution and version
	service.name = name_edit->text().trimmed();
	service.kind = MapService::Kind(kind_combo->currentData().toInt());
	service.url = url_edit->text().trimmed();
	service.layers = layers_edit->text().trimmed();
	service.format = format_combo->currentText().trimmed();
	service.crs = crs_combo->currentText().trimmed();
	service.matrix_set = matrix_combo->currentText().trimmed();
	service.min_zoom = min_zoom_edit->value();
	service.max_zoom = qMax(min_zoom_edit->value(), max_zoom_edit->value());
	service.attribution = attribution_edit->text().trimmed();
	service.bulk_download = bulk_check->isChecked();
	service.user = user_edit->text();
	service.password = password_edit->text();
	if (service.kind == MapService::Xyz)
		service.crs = QStringLiteral("EPSG:3857");
	// Take the bounding box and the finest resolution of the first chosen layer, if known.
	if (service.kind == MapService::Wms && !capabilities.layers.empty())
	{
		auto const first = service.layers.section(QLatin1Char(','), 0, 0).trimmed();
		if (auto const* layer = capabilities.layer(first))
		{
			service.bbox = layer->bboxes.value(service.crs);
			// 0.28 mm per pixel at the finest scale the server states
			service.resolution = layer->min_scale > 0 ? layer->min_scale * 0.00028 : 0.0;
		}
		service.version = capabilities.version.isEmpty() ? service.version : capabilities.version;
	}
	return service;
}

void MapServiceDialog::storeFields()
{
	if (current >= 0 && current < int(services.size()))
		services[std::size_t(current)] = fieldsToService();
}

void MapServiceDialog::saveServices()
{
	if (current >= 0 && current < int(services.size()))
		storeFields();
	MapService::toSettings(services);
}


void MapServiceDialog::connectClicked()
{
	auto const service = fieldsToService();
	if (service.url.isEmpty() || service.kind == MapService::Xyz)
		return;
	QUrl url(service.url);
	if (service.kind == MapService::Wms)
	{
		QUrlQuery query(url);
		query.removeAllQueryItems(QStringLiteral("SERVICE"));
		query.removeAllQueryItems(QStringLiteral("REQUEST"));
		query.addQueryItem(QStringLiteral("SERVICE"), QStringLiteral("WMS"));
		query.addQueryItem(QStringLiteral("REQUEST"), QStringLiteral("GetCapabilities"));
		url.setQuery(query);
	}
	if (!url.isValid())
	{
		status_label->setText(tr("Invalid URL."));
		return;
	}
	status_label->setText(tr("Connecting..."));
	connect_button->setEnabled(false);
	QNetworkRequest request(url);
	request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
	request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("FOOM"));
	network->get(request);
}

void MapServiceDialog::authenticationRequired(QNetworkReply* /*reply*/, QAuthenticator* authenticator)
{
	if (!user_edit->text().isEmpty() && authenticator->user() != user_edit->text())
	{
		authenticator->setUser(user_edit->text());
		authenticator->setPassword(password_edit->text());
		return;
	}
	bool ok = false;
	auto const user = QInputDialog::getText(this, tr("Credentials"), tr("The server asks for a user name:"), QLineEdit::Normal, user_edit->text(), &ok);
	if (!ok)
		return;
	auto const password = QInputDialog::getText(this, tr("Credentials"), tr("Password:"), QLineEdit::Password, QString{}, &ok);
	if (!ok)
		return;
	user_edit->setText(user);
	password_edit->setText(password);
	authenticator->setUser(user);
	authenticator->setPassword(password);
}

void MapServiceDialog::replyFinished(QNetworkReply* reply)
{
	reply->deleteLater();
	connect_button->setEnabled(true);
	if (reply->error() != QNetworkReply::NoError)
	{
		status_label->setText(tr("Connection failed: %1").arg(reply->errorString()));
		return;
	}
	capabilities = ServiceCapabilities::parse(reply->readAll());
	if (!capabilities.error.isEmpty())
	{
		status_label->setText(capabilities.error);
		return;
	}
	updating = true;
	if (capabilities.kind != MapService::Kind(kind_combo->currentData().toInt()))
		kind_combo->setCurrentIndex(kind_combo->findData(int(capabilities.kind)));
	updating = false;
	status_label->setText(tr("%1: %n layer(s).", nullptr, int(capabilities.layers.size())).arg(capabilities.title.isEmpty() ? tr("Connected") : capabilities.title));
	showCapabilities();
}

void MapServiceDialog::showCapabilities()
{
	updating = true;
	auto const previous_format = format_combo->currentText();
	auto const previous_crs = crs_combo->currentText();
	auto const previous_set = matrix_combo->currentText();
	auto const chosen = layers_edit->text().split(QLatin1Char(','), QString::SkipEmptyParts);

	layer_table->setRowCount(int(capabilities.layers.size()));
	int row = 0;
	for (auto const& layer : capabilities.layers)
	{
		auto const set = [this, row](int column, const QString& text) {
			auto* item = new QTableWidgetItem(text);
			item->setToolTip(text);
			layer_table->setItem(row, column, item);
		};
		set(0, layer.id);
		set(1, layer.title);
		set(2, scaleRange(layer));
		set(3, capabilities.kind == MapService::Wmts ? layer.matrix_sets.join(QLatin1String(", ")) : capabilities.formats.join(QLatin1String(", ")));
		set(4, layer.systems.join(QLatin1String(", ")));
		if (chosen.contains(layer.id))
			layer_table->selectRow(row);
		++row;
	}
	layer_table->resizeColumnsToContents();

	if (capabilities.kind == MapService::Wms)
	{
		format_combo->clear();
		format_combo->addItems(capabilities.formats);
		if (!previous_format.isEmpty() && capabilities.formats.contains(previous_format))
			format_combo->setCurrentText(previous_format);
		else if (capabilities.formats.contains(QLatin1String("image/png")))
			format_combo->setCurrentText(QStringLiteral("image/png"));
	}
	updating = false;
	layerSelectionChanged();
	if (capabilities.kind == MapService::Wms && !previous_crs.isEmpty() && crs_combo->findText(previous_crs) >= 0)
		crs_combo->setCurrentText(previous_crs);
	if (capabilities.kind == MapService::Wmts && !previous_set.isEmpty() && matrix_combo->findText(previous_set) >= 0)
		matrix_combo->setCurrentText(previous_set);
	fieldEdited();
}

void MapServiceDialog::layerSelectionChanged()
{
	if (updating || capabilities.layers.empty())
		return;
	QStringList ids;
	QStringList systems;
	QStringList sets;
	for (auto const* item : layer_table->selectedItems())
	{
		if (item->column() != 0)
			continue;
		auto const* layer = capabilities.layer(item->text());
		if (!layer)
			continue;
		ids.append(layer->id);
		if (systems.isEmpty())
			systems = layer->systems;
		else
		{
			// Only systems every chosen layer offers
			QStringList common;
			for (auto const& system : systems)
				if (layer->systems.contains(system))
					common.append(system);
			systems = common;
		}
		for (auto const& set : layer->matrix_sets)
			if (!sets.contains(set))
				sets.append(set);
	}
	updating = true;
	layers_edit->setText(ids.join(QLatin1Char(',')));
	auto const map_system = mapSystem(controller);
	if (capabilities.kind == MapService::Wms)
	{
		crs_combo->clear();
		crs_combo->addItems(systems);
		if (!map_system.isEmpty() && systems.contains(map_system))
			crs_combo->setCurrentText(map_system);
		else if (systems.contains(QLatin1String("EPSG:3857")))
			crs_combo->setCurrentText(QStringLiteral("EPSG:3857"));
	}
	else
	{
		matrix_combo->clear();
		matrix_combo->addItems(sets);
		for (auto const& set : sets)
		{
			if (!map_system.isEmpty() && capabilities.matrix_set_systems.value(set) == map_system)
			{
				matrix_combo->setCurrentText(set);
				break;
			}
		}
		crs_combo->clear();
		crs_combo->addItem(capabilities.matrix_set_systems.value(matrix_combo->currentText()));
	}
	updating = false;

	if (ids.isEmpty())
		crs_warning->clear();
	else if (map_system.isEmpty())
		crs_warning->setText(tr("The map's coordinate system has no EPSG code; the template will reproject the images."));
	else if (!systems.contains(map_system))
		crs_warning->setText(tr("The server does not offer the map's coordinate system %1; the template will reproject the images.").arg(map_system));
	else
		crs_warning->clear();
	fieldEdited();
}

void MapServiceDialog::updateButtons()
{
	auto const service = fieldsToService();
	auto const georeferenced = controller->getMap()->getGeoreferencing().getState() == Georeferencing::Geospatial;
	auto const complete = !service.url.isEmpty() && (service.kind == MapService::Xyz || !service.layers.isEmpty());
	add_button->setEnabled(georeferenced && complete);
	download_button->setEnabled(georeferenced && complete && service.bulk_download);
	download_button->setToolTip(service.bulk_download ? QString{} : tr("The service does not allow bulk download."));
	remove_button->setEnabled(current >= 0);
}


void MapServiceDialog::downloadClicked()
{
	auto const service = fieldsToService();
	auto* map = controller->getMap();
	auto const& georef = map->getGeoreferencing();
	auto* widget = controller->getMainWidget();
	auto const view_rect = widget->getMapView()->calculateViewedRect(widget->viewportToView(widget->rect()));
	auto const view_bounds = TileServiceTemplate::projectedBounds(georef, view_rect);

	// Ask for the window, the resolution and the file
	QDialog dialog(this);
	dialog.setWindowTitle(tr("Download extent"));
	auto* current_view = new QRadioButton(tr("Current view"));
	current_view->setChecked(true);
	auto* typed_box = new QRadioButton(tr("Box in projected coordinates:"));
	auto const spin = [](double value) {
		auto* edit = new QDoubleSpinBox();
		edit->setDecimals(1);
		edit->setRange(-1e8, 1e8);
		edit->setValue(value);
		return edit;
	};
	auto* min_x = spin(view_bounds.left());
	auto* min_y = spin(view_bounds.top());
	auto* max_x = spin(view_bounds.right());
	auto* max_y = spin(view_bounds.bottom());
	auto* resolution_edit = new QDoubleSpinBox();
	resolution_edit->setDecimals(2);
	resolution_edit->setRange(0.01, 1000.0);
	resolution_edit->setSuffix(tr(" m per pixel"));
	auto const view_px = widget->rect().width() > 0 ? view_bounds.width() / widget->rect().width() : 1.0;
	resolution_edit->setValue(std::max(0.01, std::round(view_px * 100.0) / 100.0));
	auto* path_edit = new QLineEdit();
	auto const map_path = controller->getWindow() ? controller->getWindow()->currentPath() : QString{};
	auto const base_name = (map_path.isEmpty() ? QDir::homePath() + QLatin1String("/map") : map_path.section(QLatin1Char('.'), 0, -2))
	                       + QLatin1Char('-') + QString(service.name).replace(QRegExp(QStringLiteral("[^A-Za-z0-9_-]+")), QStringLiteral("_")) + QLatin1String(".tif");
	path_edit->setText(QDir::toNativeSeparators(base_name));
	auto* browse = new QPushButton(tr("Browse..."));
	connect(browse, &QPushButton::clicked, &dialog, [&dialog, path_edit]() {
		auto const path = FileDialog::getSaveFileName(&dialog, tr("Download extent"), path_edit->text(), tr("GeoTIFF (*.tif)"));
		if (!path.isEmpty())
			path_edit->setText(QDir::toNativeSeparators(path));
	});
	auto* size_label = new QLabel();
	auto const update_size = [&]() {
		auto const window = current_view->isChecked() ? view_bounds
		                    : QRectF(QPointF(min_x->value(), min_y->value()), QPointF(max_x->value(), max_y->value()));
		auto const w = qint64(std::ceil(window.width() / resolution_edit->value()));
		auto const h = qint64(std::ceil(window.height() / resolution_edit->value()));
		size_label->setText(tr("%1 by %2 pixels (at most %3 on a side)").arg(w).arg(h).arg(TileServiceTemplate::max_download_pixels));
	};
	for (auto* edit : { min_x, min_y, max_x, max_y, resolution_edit })
		connect(edit, QOverload<double>::of(&QDoubleSpinBox::valueChanged), &dialog, update_size);
	connect(current_view, &QRadioButton::toggled, &dialog, update_size);
	update_size();

	auto* box_layout = new QFormLayout();
	box_layout->addRow(tr("Minimum easting:"), min_x);
	box_layout->addRow(tr("Minimum northing:"), min_y);
	box_layout->addRow(tr("Maximum easting:"), max_x);
	box_layout->addRow(tr("Maximum northing:"), max_y);
	auto* path_row = new QHBoxLayout();
	path_row->addWidget(path_edit, 1);
	path_row->addWidget(browse);
	auto* form = new QFormLayout();
	form->addRow(tr("Window:"), current_view);
	form->addRow(QString{}, typed_box);
	form->addRow(QString{}, box_layout);
	form->addRow(tr("Resolution:"), resolution_edit);
	form->addRow(tr("Size:"), size_label);
	form->addRow(tr("File:"), path_row);
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	auto* layout = new QVBoxLayout();
	layout->addLayout(form);
	layout->addWidget(buttons);
	dialog.setLayout(layout);
	if (dialog.exec() != QDialog::Accepted)
		return;

	auto const window = current_view->isChecked() ? view_bounds
	                    : QRectF(QPointF(min_x->value(), min_y->value()), QPointF(max_x->value(), max_y->value()));
	auto const path = QDir::fromNativeSeparators(path_edit->text());

	// Run the download on a temporary template
	TileServiceTemplate temp(service, map);
	if (!temp.setupAndLoad(this, nullptr))
	{
		QMessageBox::warning(this, tr("Download extent"), temp.errorString());
		return;
	}
	QString error;
	if (!temp.downloadExtent(window, resolution_edit->value(), path, &error))
	{
		QMessageBox::warning(this, tr("Download extent"), error);
		return;
	}
	QProgressDialog progress(tr("Downloading %1...").arg(QFileInfo(path).fileName()), tr("Cancel"), 0, 100, this);
	progress.setWindowModality(Qt::WindowModal);
	progress.setMinimumDuration(0);
	connect(&temp, &TileServiceTemplate::downloadProgress, &progress, &QProgressDialog::setValue);
	connect(&progress, &QProgressDialog::canceled, &temp, &TileServiceTemplate::cancelDownload);
	bool success = false;
	QString message;
	connect(&temp, &TileServiceTemplate::downloadFinished, &progress, [&](bool ok, const QString& text) {
		success = ok;
		message = text;
		progress.reset();
	});
	progress.exec();
	while (temp.isDownloading())
		QCoreApplication::processEvents(QEventLoop::WaitForMoreEvents, 100);
	if (!success)
	{
		if (message != QLatin1String("Cancelled"))
			QMessageBox::warning(this, tr("Download extent"), message);
		return;
	}

	// Add the file as an ordinary raster template
	auto raster = std::make_unique<GdalTemplate>(path, map);
	if (!raster->setupAndLoad(this, widget->getMapView()))
	{
		QMessageBox::warning(this, tr("Download extent"), tr("Cannot open the downloaded file:\n%1").arg(raster->errorString()));
		return;
	}
	map->addTemplate(-1, std::move(raster));
	reject();
}


}  // namespace OpenOrienteering
