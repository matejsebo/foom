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


#include "fix_gate_rows.h"

#include <QChar>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QSpinBox>
#include <QVariant>
#include <QWidget>

#include "gui/util_gui.h"


namespace OpenOrienteering {

FixGateRows::FixGateRows(QFormLayout* layout, QWidget* parent)
: QObject(parent)
{
	layout->addRow(Util::Headline::create(tr("Position quality:")));
	
	class_box = new QComboBox();
	class_box->setObjectName(QStringLiteral("gate-min-class"));
	class_box->addItem(tr("No minimum"), int(FixInfo::Invalid));
	for (auto fix_class : { FixInfo::Autonomous, FixInfo::Differential, FixInfo::RtkFloat, FixInfo::RtkFixed })
		class_box->addItem(FixGate::className(fix_class), int(fix_class));
	layout->addRow(tr("Minimum fix:"), class_box);
	
	satellites_box = new QSpinBox();
	satellites_box->setObjectName(QStringLiteral("gate-min-satellites"));
	satellites_box->setRange(0, 50);
	satellites_box->setSpecialValueText(tr("No minimum"));
	layout->addRow(tr("Minimum satellites:"), satellites_box);
	
	hdop_box = new QDoubleSpinBox();
	hdop_box->setObjectName(QStringLiteral("gate-max-hdop"));
	hdop_box->setDecimals(1);
	hdop_box->setRange(0, 50);
	hdop_box->setSingleStep(0.1);
	hdop_box->setSpecialValueText(tr("No limit"));
	layout->addRow(tr("Maximum HDOP:"), hdop_box);
	
	error_box = new QDoubleSpinBox();
	error_box->setObjectName(QStringLiteral("gate-max-error"));
	error_box->setDecimals(2);
	error_box->setRange(0, 1000);
	error_box->setSingleStep(0.05);
	QString suffix = QString(QChar::Space);
	suffix += tr("m", "meters");
	error_box->setSuffix(suffix);
	error_box->setSpecialValueText(tr("No limit"));
	layout->addRow(tr("Maximum horizontal error:"), error_box);
	
	policy_box = new QComboBox();
	policy_box->setObjectName(QStringLiteral("gate-policy"));
	policy_box->addItem(tr("Show in grey, do not use"), int(FixGateSettings::ShowGreyed));
	policy_box->addItem(tr("Hide, do not use"), int(FixGateSettings::Hide));
	layout->addRow(tr("Positions which fail:"), policy_box);
	
	setSettings({});
}

FixGateRows::~FixGateRows() = default;


FixGateSettings FixGateRows::settings() const
{
	FixGateSettings result;
	result.min_class = FixInfo::Class(class_box->currentData().toInt());
	result.min_satellites = satellites_box->value();
	result.max_hdop = hdop_box->value();
	result.max_error = error_box->value();
	result.policy = FixGateSettings::Policy(policy_box->currentData().toInt());
	return result;
}

void FixGateRows::setSettings(const FixGateSettings& settings)
{
	class_box->setCurrentIndex(qMax(0, class_box->findData(int(settings.min_class))));
	satellites_box->setValue(settings.min_satellites);
	hdop_box->setValue(settings.max_hdop);
	error_box->setValue(settings.max_error);
	policy_box->setCurrentIndex(qMax(0, policy_box->findData(int(settings.policy))));
}


}  // namespace OpenOrienteering
