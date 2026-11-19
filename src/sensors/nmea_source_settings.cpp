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


#include "nmea_source_settings.h"

#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QSettings>
#include <QToolButton>
#include <QVariant>
#include <QWidget>


namespace OpenOrienteering {

namespace {

QString tcpKey() { return QStringLiteral("Sensors/nmea_tcp"); }
QString baudKey() { return QStringLiteral("Sensors/nmea_baud"); }
QString logFolderKey() { return QStringLiteral("Sensors/nmea_log_folder"); }

}  // namespace



// static
NmeaSourceSettings NmeaSourceSettings::load(const QSettings& settings)
{
	NmeaSourceSettings result;
	result.tcp = settings.value(tcpKey()).toString().trimmed();
	auto ok = false;
	auto const baud = settings.value(baudKey()).toInt(&ok);
	if (settings.contains(baudKey()) && ok && baud > 0)
		result.baud = baud;
	result.log_folder = settings.value(logFolderKey()).toString().trimmed();
	return result;
}

// static
NmeaSourceSettings NmeaSourceSettings::load()
{
	QSettings settings;
	return load(settings);
}

void NmeaSourceSettings::save(QSettings& settings) const
{
	settings.setValue(tcpKey(), tcp);
	settings.setValue(baudKey(), baud);
	settings.setValue(logFolderKey(), log_folder);
}

void NmeaSourceSettings::save() const
{
	QSettings settings;
	save(settings);
}

QVariantMap NmeaSourceSettings::parameters() const
{
	QVariantMap result;
	if (!tcp.isEmpty())
		result.insert(QStringLiteral("nmea.tcp"), tcp);
	result.insert(QStringLiteral("nmea.baud"), baud);
	return result;
}

bool operator==(const NmeaSourceSettings& lhs, const NmeaSourceSettings& rhs)
{
	return lhs.tcp == rhs.tcp && lhs.baud == rhs.baud && lhs.log_folder == rhs.log_folder;
}



NmeaSourceRows::NmeaSourceRows(QFormLayout* layout, QWidget* parent)
: QObject(parent)
{
	tcp_edit = new QLineEdit();
	tcp_edit->setObjectName(QStringLiteral("nmea-tcp"));
	//: Example of the address of a network receiver. Keep the form host:port.
	tcp_edit->setPlaceholderText(tr("host:port, empty for the serial port"));
	layout->addRow(tr("Network receiver (NMEA):"), tcp_edit);
	
	baud_box = new QComboBox();
	baud_box->setObjectName(QStringLiteral("nmea-baud"));
	for (auto baud : { 4800, 9600, 19200, 38400, 57600, 115200 })
		baud_box->addItem(QString::number(baud), baud);
	layout->addRow(tr("Baud rate:"), baud_box);
	
	log_folder_edit = new QLineEdit();
	log_folder_edit->setObjectName(QStringLiteral("nmea-log-folder"));
	log_folder_edit->setPlaceholderText(tr("Folder, empty for no log"));
	log_folder_edit->setToolTip(tr("Every sentence which a receiver sends is added to a file in this folder, one file for every day. The position sources of the system deliver no sentences."));
	auto* choose_button = new QToolButton();
	choose_button->setText(tr("..."));
	choose_button->setToolTip(tr("Choose the folder"));
	auto* log_folder_layout = new QHBoxLayout();
	log_folder_layout->addWidget(log_folder_edit, 1);
	log_folder_layout->addWidget(choose_button);
	layout->addRow(tr("Log of received sentences:"), log_folder_layout);
	connect(choose_button, &QToolButton::clicked, this, [this, parent]() {
		auto const folder = QFileDialog::getExistingDirectory(parent, tr("Folder for the log of received sentences"), log_folder_edit->text().trimmed());
		if (!folder.isEmpty())
			log_folder_edit->setText(QDir::toNativeSeparators(folder));
	});
	
	setSettings({});
}

NmeaSourceRows::~NmeaSourceRows() = default;


NmeaSourceSettings NmeaSourceRows::settings() const
{
	NmeaSourceSettings result;
	result.tcp = tcp_edit->text().trimmed();
	result.baud = baud_box->currentData().toInt();
	result.log_folder = QDir::fromNativeSeparators(log_folder_edit->text().trimmed());
	return result;
}

void NmeaSourceRows::setSettings(const NmeaSourceSettings& settings)
{
	tcp_edit->setText(settings.tcp);
	auto index = baud_box->findData(settings.baud);
	if (index < 0)
	{
		// A rate which was set by hand is kept.
		baud_box->addItem(QString::number(settings.baud), settings.baud);
		index = baud_box->count() - 1;
	}
	baud_box->setCurrentIndex(index);
	log_folder_edit->setText(QDir::toNativeSeparators(settings.log_folder));
}


}  // namespace OpenOrienteering
