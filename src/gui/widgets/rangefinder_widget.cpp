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


#include "rangefinder_widget.h"

#include <cmath>
#include <utility>

#include <Qt>
#include <QtGlobal>
#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLatin1Char>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QToolButton>
#include <QVariant>

#include "core/georeferencing.h"
#include "core/map.h"
#include "gui/util_gui.h"
#include "sensors/rangefinder_source.h"


namespace OpenOrienteering {

namespace {

QString metres(double value)
{
	return QString::number(qAbs(value), 'f', 2);
}

}  // namespace



RangefinderWidget::RangefinderWidget(Map* map, bool mobile, QWidget* parent)
: QWidget(parent)
, map(map)
{
	auto const settings = RangefinderSettings::load();
	current_survey.sight = settings.sight;
	current_survey.traverse = settings.traverse;
	current_survey.azimuth_is_magnetic = settings.azimuth_is_magnetic;
	
	reference_label = new QLabel();
	reference_label->setObjectName(QStringLiteral("rangefinder-reference"));
	
	reference_click_button = new QToolButton();
	reference_click_button->setObjectName(QStringLiteral("rangefinder-reference-click"));
	reference_click_button->setToolButtonStyle(Qt::ToolButtonTextOnly);
	reference_click_button->setCheckable(true);
	reference_click_button->setText(tr("Click on map"));
	reference_click_button->setToolTip(tr("Click the reference point on the map."));
	
	reference_gps_button = new QPushButton(tr("GPS position"));
	reference_gps_button->setObjectName(QStringLiteral("rangefinder-reference-gps"));
	reference_gps_button->setToolTip(tr("Take the GPS position as the reference point."));
	
	reference_previous_button = new QPushButton(tr("Previous target"));
	reference_previous_button->setObjectName(QStringLiteral("rangefinder-reference-previous"));
	reference_previous_button->setToolTip(tr("Take the target of the reading before as the reference point."));
	
	distance_box = Util::SpinBox::create(2, 0.0, 9999.99, tr("m"), 1.0);
	distance_box->setObjectName(QStringLiteral("rangefinder-distance"));
	distance_kind_box = new QComboBox();
	distance_kind_box->setObjectName(QStringLiteral("rangefinder-distance-kind"));
	distance_kind_box->addItem(tr("horizontal"), false);
	distance_kind_box->addItem(tr("slope"), true);
	distance_kind_box->setCurrentIndex(distance_kind_box->findData(settings.slope_distance));
	
	auto const degrees = Util::InputProperties<Util::RotationalDegrees>::unit();
	inclination_box = Util::SpinBox::create(1, -89.9, 89.9, degrees, 1.0);
	inclination_box->setObjectName(QStringLiteral("rangefinder-inclination"));
	
	azimuth_box = Util::SpinBox::create(1, 0.0, 360.0, degrees, 1.0);
	azimuth_box->setObjectName(QStringLiteral("rangefinder-azimuth"));
	azimuth_box->setWrapping(true);
	magnetic_check = new QCheckBox(tr("Azimuth is magnetic"));
	magnetic_check->setObjectName(QStringLiteral("rangefinder-magnetic"));
	magnetic_check->setToolTip(tr("On: the azimuth is taken from the map's north. Off: it is from true north, and the declination of the map is taken off."));
	magnetic_check->setChecked(settings.azimuth_is_magnetic);
	
	sight_box = new QComboBox();
	sight_box->setObjectName(QStringLiteral("rangefinder-sight"));
	sight_box->addItem(tr("Forward (from the reference to the target)"), int(RangefinderSurvey::ForwardSight));
	sight_box->addItem(tr("Rear (from the target back to the reference)"), int(RangefinderSurvey::RearSight));
	sight_box->setCurrentIndex(sight_box->findData(int(settings.sight)));
	
	traverse_box = new QComboBox();
	traverse_box->setObjectName(QStringLiteral("rangefinder-traverse"));
	traverse_box->addItem(tr("Linear (the target is the next reference)"), int(RangefinderSurvey::LinearTraverse));
	traverse_box->addItem(tr("Radial (the reference stays)"), int(RangefinderSurvey::RadialTraverse));
	traverse_box->setCurrentIndex(traverse_box->findData(int(settings.traverse)));
	
	height_label = new QLabel();
	height_label->setObjectName(QStringLiteral("rangefinder-height"));
	target_label = new QLabel();
	target_label->setObjectName(QStringLiteral("rangefinder-target"));
	
	add_button = new QPushButton(tr("Add"));
	add_button->setObjectName(QStringLiteral("rangefinder-add"));
	add_button->setToolTip(tr("Draw the target: a point object, or the next vertex of the path."));
	
	finish_button = new QPushButton(tr("Finish path"));
	finish_button->setObjectName(QStringLiteral("rangefinder-finish"));
	finish_button->setToolTip(tr("End the path which is drawn at its last vertex."));
	
	// The device.
	device_source = new RangefinderSource(this);
	port_box = new QComboBox();
	port_box->setObjectName(QStringLiteral("rangefinder-port"));
	port_box->setEditable(true);
	// Wide enough for a host and its port.
	port_box->setMinimumContentsLength(18);
	port_box->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
	port_box->addItems(RangefinderSource::serialPorts());
	port_box->setCurrentIndex(-1);
	port_box->setEditText(settings.port);
	port_box->lineEdit()->setPlaceholderText(tr("Serial port, or host:port"));
	port_box->setToolTip(tr("The serial port of the rangefinder. A text of the form host:port connects to a network port."));
	baud_box = new QComboBox();
	baud_box->setObjectName(QStringLiteral("rangefinder-baud"));
	for (auto baud : { 4800, 9600, 19200, 38400, 57600, 115200 })
		baud_box->addItem(QString::number(baud), baud);
	if (baud_box->findData(settings.baud) < 0)
		baud_box->addItem(QString::number(settings.baud), settings.baud);  // a speed which was set by hand is kept
	baud_box->setCurrentIndex(baud_box->findData(settings.baud));
	baud_box->setToolTip(tr("The speed of the serial port."));
	connect_button = new QPushButton(tr("Connect"));
	connect_button->setObjectName(QStringLiteral("rangefinder-connect"));
	connect_button->setCheckable(true);
	state_label = new QLabel();
	state_label->setObjectName(QStringLiteral("rangefinder-state"));
	auto_check = new QCheckBox(tr("Draw automatically"));
	auto_check->setObjectName(QStringLiteral("rangefinder-auto"));
	auto_check->setToolTip(tr("On: a reading from the device is drawn at once. Off: it waits in the fields for Add."));
	auto_check->setChecked(settings.draw_automatically);
	counts_label = new QLabel();
	counts_label->setObjectName(QStringLiteral("rangefinder-counts"));
	counts_label->setToolTip(tr("Lines of the device which gave a reading, and lines which did not. Many other lines and no readings: the device sends another kind of sentence."));
	
	auto* device_layout = new QHBoxLayout();
	device_layout->addWidget(port_box, 1);
	device_layout->addWidget(baud_box);
	device_layout->addWidget(connect_button);
	
	auto* reference_buttons = new QHBoxLayout();
	reference_buttons->addWidget(reference_click_button);
	reference_buttons->addWidget(reference_gps_button);
	reference_buttons->addWidget(reference_previous_button);
	reference_buttons->addStretch(1);
	
	auto* distance_layout = new QHBoxLayout();
	distance_layout->addWidget(distance_box, 1);
	distance_layout->addWidget(distance_kind_box);
	
	auto* bottom_buttons = new QHBoxLayout();
	bottom_buttons->addWidget(add_button);
	bottom_buttons->addWidget(finish_button);
	bottom_buttons->addStretch(1);
	if (mobile)
	{
		auto* close_button = new QPushButton(tr("Close"));
		close_button->setObjectName(QStringLiteral("rangefinder-close"));
		bottom_buttons->addWidget(close_button);
		connect(close_button, &QPushButton::clicked, this, &RangefinderWidget::closeClicked);
	}
	
	// Two pairs of label and field in a row, so that the mobile form stays low.
	auto* layout = new QGridLayout(this);
	auto row = 0;
	layout->addWidget(new QLabel(tr("Reference:")), row, 0);
	layout->addWidget(reference_label, row, 1);
	layout->addLayout(reference_buttons, row, 2, 1, 2);
	++row;
	layout->addWidget(new QLabel(tr("Distance:")), row, 0);
	layout->addLayout(distance_layout, row, 1);
	layout->addWidget(new QLabel(tr("Inclination:")), row, 2);
	layout->addWidget(inclination_box, row, 3);
	++row;
	layout->addWidget(new QLabel(tr("Azimuth:")), row, 0);
	layout->addWidget(azimuth_box, row, 1);
	layout->addWidget(magnetic_check, row, 2, 1, 2);
	++row;
	layout->addWidget(new QLabel(tr("Sight:")), row, 0);
	layout->addWidget(sight_box, row, 1);
	layout->addWidget(new QLabel(tr("Traverse:")), row, 2);
	layout->addWidget(traverse_box, row, 3);
	++row;
	layout->addWidget(new QLabel(tr("Target:")), row, 0);
	layout->addWidget(target_label, row, 1);
	layout->addWidget(new QLabel(tr("Height difference:")), row, 2);
	layout->addWidget(height_label, row, 3);
	++row;
	layout->addWidget(new QLabel(tr("Device:")), row, 0);
	layout->addLayout(device_layout, row, 1);
	layout->addWidget(state_label, row, 2, 1, 2);
	++row;
	layout->addWidget(auto_check, row, 0, 1, 2);
	layout->addWidget(counts_label, row, 2, 1, 2);
	++row;
	layout->addLayout(bottom_buttons, row, 0, 1, 4);
	++row;
	layout->setRowStretch(row, 1);
	layout->setColumnStretch(1, 1);
	layout->setColumnStretch(3, 1);
	
	auto const value_changed = QOverload<double>::of(&QDoubleSpinBox::valueChanged);
	auto const index_changed = QOverload<int>::of(&QComboBox::currentIndexChanged);
	connect(distance_box, value_changed, this, &RangefinderWidget::updateDisplay);
	connect(inclination_box, value_changed, this, &RangefinderWidget::updateDisplay);
	connect(azimuth_box, value_changed, this, &RangefinderWidget::updateDisplay);
	connect(distance_kind_box, index_changed, this, &RangefinderWidget::storeChoices);
	connect(sight_box, index_changed, this, &RangefinderWidget::storeChoices);
	connect(traverse_box, index_changed, this, &RangefinderWidget::storeChoices);
	connect(magnetic_check, &QCheckBox::toggled, this, &RangefinderWidget::storeChoices);
	connect(reference_gps_button, &QPushButton::clicked, this, &RangefinderWidget::referenceFromGpsRequested);
	connect(reference_previous_button, &QPushButton::clicked, this, [this]() {
		if (current_survey.usePreviousTargetAsReference())
			updateDisplay();
	});
	connect(add_button, &QPushButton::clicked, this, &RangefinderWidget::addClicked);
	connect(finish_button, &QPushButton::clicked, this, &RangefinderWidget::finishRequested);
	connect(connect_button, &QPushButton::clicked, this, &RangefinderWidget::connectClicked);
	connect(auto_check, &QCheckBox::toggled, this, &RangefinderWidget::storeChoices);
	connect(baud_box, index_changed, this, &RangefinderWidget::storeChoices);
	connect(device_source, &RangefinderSource::stateChanged, this, &RangefinderWidget::updateDeviceState);
	connect(device_source, &RangefinderSource::countsChanged, this, &RangefinderWidget::updateCounts);
	connect(device_source, &RangefinderSource::readingReceived, this, &RangefinderWidget::deviceReading);
	// Return in a field is Add. The spin box has taken the text as its value by then.
	for (auto* box : { distance_box, inclination_box, azimuth_box })
	{
		if (auto* line_edit = box->findChild<QLineEdit*>())
			connect(line_edit, &QLineEdit::returnPressed, this, &RangefinderWidget::addClicked);
	}
	
	updateDisplay();
	updateDeviceState();
	updateCounts();
}

RangefinderWidget::~RangefinderWidget() = default;


RangefinderReading RangefinderWidget::reading() const
{
	RangefinderReading result;
	auto const distance = distance_box->value();
	if (distance > 0)
	{
		if (distance_kind_box->currentData().toBool())
			result.slope_distance = distance;
		else
			result.horizontal_distance = distance;
	}
	result.inclination = inclination_box->value();
	result.azimuth = azimuth_box->value();
	return result;
}

bool RangefinderWidget::previewTarget(MapCoordF* target) const
{
	return current_survey.target(reading(), map->getGeoreferencing(), target);
}

void RangefinderWidget::setReference(const MapCoordF& position)
{
	current_survey.setReference(position);
	updateDisplay();
}

void RangefinderWidget::setAddFunction(std::function<bool (const MapCoordF&)> function)
{
	add_function = std::move(function);
}

void RangefinderWidget::setReferenceClickAction(QAction* action)
{
	// Not as the button's default action: the button keeps its short text,
	// and the action has the long one for the list of shortcuts.
	reference_click_button->setChecked(action->isChecked());
	connect(action, &QAction::toggled, reference_click_button, &QToolButton::setChecked);
	connect(reference_click_button, &QToolButton::clicked, action, [this, action]() {
		action->trigger();
		// A tool's action stays ticked when it is triggered again.
		reference_click_button->setChecked(action->isChecked());
	});
}


void RangefinderWidget::updateDisplay()
{
	if (current_survey.hasReference())
	{
		// As the map widget shows the position of the cursor: y to the top.
		auto const& reference = current_survey.reference();
		reference_label->setText(QStringLiteral("%1 %2 (%3)").arg(QString::number(reference.x(), 'f', 2), QString::number(-reference.y(), 'f', 2), tr("mm")));
	}
	else
	{
		reference_label->setText(tr("Not set"));
	}
	reference_previous_button->setEnabled(current_survey.hasPreviousTarget()
	                                      && !(current_survey.hasReference() && current_survey.reference() == current_survey.previousTarget()));
	
	auto const current_reading = reading();
	auto const height = current_survey.heightDifference(current_reading);
	if (qIsNaN(height))
		height_label->setText(QString{});
	else if (qAbs(height) < 0.005)
		height_label->setText(tr("%1 m").arg(metres(0)));
	else
		height_label->setText(QLatin1Char(height > 0 ? '+' : '-') + tr("%1 m").arg(metres(height)));
	
	MapCoordF target;
	auto const has_target = previewTarget(&target);
	if (has_target)
	{
		auto const to_metres = double(map->getScaleDenominator()) / 1000.0;
		auto east = (target.x() - current_survey.reference().x()) * to_metres;
		auto north = -(target.y() - current_survey.reference().y()) * to_metres;
		// No "0.00 m west".
		if (qAbs(east) < 0.005)
			east = 0;
		if (qAbs(north) < 0.005)
			north = 0;
		target_label->setText((east >= 0 ? tr("%1 m east") : tr("%1 m west")).arg(metres(east))
		                      + QStringLiteral(", ")
		                      + (north >= 0 ? tr("%1 m north") : tr("%1 m south")).arg(metres(north)));
	}
	else
	{
		target_label->setText(QString{});
	}
	add_button->setEnabled(has_target);
	
	emit changed();
}

void RangefinderWidget::storeChoices()
{
	current_survey.sight = RangefinderSurvey::Sight(sight_box->currentData().toInt());
	current_survey.traverse = RangefinderSurvey::Traverse(traverse_box->currentData().toInt());
	current_survey.azimuth_is_magnetic = magnetic_check->isChecked();
	
	RangefinderSettings settings;
	settings.sight = current_survey.sight;
	settings.traverse = current_survey.traverse;
	settings.azimuth_is_magnetic = current_survey.azimuth_is_magnetic;
	settings.slope_distance = distance_kind_box->currentData().toBool();
	settings.port = port_box->currentText().trimmed();
	settings.baud = baud_box->currentData().toInt();
	settings.draw_automatically = auto_check->isChecked();
	settings.save();
	
	updateDisplay();
}

void RangefinderWidget::addClicked()
{
	MapCoordF target;
	if (!previewTarget(&target))
		return;
	if (!add_function || !add_function(target))
		return;
	current_survey.take(reading(), map->getGeoreferencing());
	updateDisplay();
	// Ready for the next distance.
	distance_box->selectAll();
}

void RangefinderWidget::deviceReading(const RangefinderReading& reading)
{
	auto const has_horizontal = !qIsNaN(reading.horizontal_distance);
	auto const has_slope = !qIsNaN(reading.slope_distance);
	if (!has_horizontal && !has_slope)
		return;
	
	{
		// One update for all fields. A field for which the reading has nothing keeps what was typed.
		const QSignalBlocker block_kind(distance_kind_box);
		const QSignalBlocker block_distance(distance_box);
		const QSignalBlocker block_inclination(inclination_box);
		const QSignalBlocker block_azimuth(azimuth_box);
		distance_kind_box->setCurrentIndex(distance_kind_box->findData(!has_horizontal));
		distance_box->setValue(has_horizontal ? reading.horizontal_distance : reading.slope_distance);
		if (!qIsNaN(reading.inclination))
			inclination_box->setValue(reading.inclination);
		if (!qIsNaN(reading.azimuth))
		{
			auto azimuth = std::fmod(reading.azimuth, 360.0);
			if (azimuth < 0)
				azimuth += 360.0;
			azimuth_box->setValue(azimuth);
		}
	}
	storeChoices();
	if (auto_check->isChecked())
		addClicked();
}

void RangefinderWidget::connectClicked(bool checked)
{
	storeChoices();
	if (checked)
		device_source->open(port_box->currentText(), baud_box->currentData().toInt());
	else
		device_source->close();
	updateDeviceState();
}

void RangefinderWidget::updateDeviceState()
{
	auto const busy = device_source->isOpen() || device_source->isConnecting();
	connect_button->setChecked(busy);
	connect_button->setText(busy ? tr("Disconnect") : tr("Connect"));
	port_box->setEnabled(!busy);
	baud_box->setEnabled(!busy);
	if (device_source->isOpen())
		state_label->setText(tr("Connected"));
	else if (device_source->isConnecting())
		state_label->setText(tr("Connecting..."));
	else if (device_source->errorString().isEmpty())
		state_label->setText(tr("Not connected"));
	else
		state_label->setText(device_source->errorString());
}

void RangefinderWidget::updateCounts()
{
	counts_label->setText(tr("Readings: %1, other lines: %2").arg(device_source->readingCount()).arg(device_source->otherCount()));
}


}  // namespace OpenOrienteering
