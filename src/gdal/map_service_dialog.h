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


#ifndef OPENORIENTEERING_MAP_SERVICE_DIALOG_H
#define OPENORIENTEERING_MAP_SERVICE_DIALOG_H

#include <vector>

#include <QDialog>
#include <QObject>
#include <QString>

#include "gdal/map_service.h"
#include "gdal/map_service_capabilities.h"

class QAuthenticator;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QNetworkAccessManager;
class QNetworkReply;
class QPushButton;
class QSpinBox;
class QTableWidget;
class QWidget;

namespace OpenOrienteering {

class MapEditorController;


/**
 * The dialog of the template-add-service command (Templates > Add map service...).
 *
 * See the design notes, "Tile and map service templates".
 */
class MapServiceDialog : public QDialog
{
Q_OBJECT
public:
	MapServiceDialog(MapEditorController* controller, QWidget* parent);
	~MapServiceDialog() override;

	/** The service as configured, after the dialog was accepted. */
	MapService service() const;

	/** The map's coordinate system as "EPSG:nnnn", or an empty string when it has no code. */
	static QString mapSystem(const MapEditorController* controller);

private slots:
	void currentServiceChanged(int row);
	void addService();
	void removeService();
	void openDescriptor();  // foom
	void saveDescriptor();  // foom
	void fieldEdited();
	void kindChanged();
	void connectClicked();
	void replyFinished(QNetworkReply* reply);
	void authenticationRequired(QNetworkReply* reply, QAuthenticator* authenticator);
	void layerSelectionChanged();
	void downloadClicked();
	void updateButtons();
	void saveServices();

private:
	void showService(const MapService& service);
	void showCapabilities();
	void storeFields();
	MapService fieldsToService() const;

	MapEditorController* controller;
	std::vector<MapService> services;
	int current = -1;
	bool updating = false;
	ServiceCapabilities capabilities;
	QNetworkAccessManager* network;

	QListWidget* list;
	QPushButton* remove_button;
	QPushButton* save_button = nullptr;  // foom
	QLineEdit* name_edit;
	QComboBox* kind_combo;
	QLineEdit* url_edit;
	QLineEdit* layers_edit;
	QComboBox* format_combo;
	QComboBox* crs_combo;
	QComboBox* matrix_combo;
	QSpinBox* min_zoom_edit;
	QSpinBox* max_zoom_edit;
	QLineEdit* attribution_edit;
	QCheckBox* bulk_check;
	QLineEdit* user_edit;
	QLineEdit* password_edit;
	QPushButton* connect_button;
	QLabel* status_label;
	QTableWidget* layer_table;
	QLabel* crs_warning;
	QPushButton* add_button;
	QPushButton* download_button;
};


}  // namespace OpenOrienteering

#endif
