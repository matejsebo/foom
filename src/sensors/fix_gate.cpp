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


#include "fix_gate.h"

#include <cmath>

#include <QChar>
#include <QLatin1String>
#include <QLocale>
#include <QSettings>
#include <QStringList>
#include <QVariant>


namespace OpenOrienteering {

namespace {

const struct
{
	FixInfo::Class fix_class;
	const char* key;
} class_keys[] = {
    { FixInfo::Invalid, "invalid" },
    { FixInfo::Autonomous, "autonomous" },
    { FixInfo::Differential, "differential" },
    { FixInfo::RtkFloat, "rtk-float" },
    { FixInfo::RtkFixed, "rtk-fixed" },
};

QString minClassKey() { return QStringLiteral("Sensors/gate_min_class"); }
QString minSatellitesKey() { return QStringLiteral("Sensors/gate_min_satellites"); }
QString maxHdopKey() { return QStringLiteral("Sensors/gate_max_hdop"); }
QString maxErrorKey() { return QStringLiteral("Sensors/gate_max_error"); }
QString policyKey() { return QStringLiteral("Sensors/gate_policy"); }

QString number(double value, int decimals)
{
	return QLocale().toString(value, 'f', decimals);
}

}  // namespace



// static
FixGateSettings FixGateSettings::load(const QSettings& settings)
{
	FixGateSettings result;
	
	auto const class_key = settings.value(minClassKey()).toString();
	for (auto const& entry : class_keys)
	{
		if (class_key == QLatin1String(entry.key))
			result.min_class = entry.fix_class;
	}
	
	auto ok = false;
	auto const satellites = settings.value(minSatellitesKey()).toInt(&ok);
	if (settings.contains(minSatellitesKey()) && ok && satellites >= 0)
		result.min_satellites = satellites;
	
	auto const hdop = settings.value(maxHdopKey()).toDouble(&ok);
	if (settings.contains(maxHdopKey()) && ok && hdop >= 0)
		result.max_hdop = hdop;
	
	auto const error = settings.value(maxErrorKey()).toDouble(&ok);
	if (settings.contains(maxErrorKey()) && ok && error >= 0)
		result.max_error = error;
	
	auto const policy_key = settings.value(policyKey()).toString();
	if (policy_key == QLatin1String("hide"))
		result.policy = Hide;
	else if (policy_key == QLatin1String("grey"))
		result.policy = ShowGreyed;
	
	return result;
}

// static
FixGateSettings FixGateSettings::load()
{
	QSettings settings;
	return load(settings);
}

void FixGateSettings::save(QSettings& settings) const
{
	for (auto const& entry : class_keys)
	{
		if (entry.fix_class == min_class)
			settings.setValue(minClassKey(), QString::fromLatin1(entry.key));
	}
	settings.setValue(minSatellitesKey(), min_satellites);
	settings.setValue(maxHdopKey(), max_hdop);
	settings.setValue(maxErrorKey(), max_error);
	settings.setValue(policyKey(), policy == Hide ? QStringLiteral("hide") : QStringLiteral("grey"));
}

void FixGateSettings::save() const
{
	QSettings settings;
	save(settings);
}

bool operator==(const FixGateSettings& lhs, const FixGateSettings& rhs)
{
	return lhs.min_class == rhs.min_class
	       && lhs.min_satellites == rhs.min_satellites
	       && lhs.max_hdop == rhs.max_hdop
	       && lhs.max_error == rhs.max_error
	       && lhs.policy == rhs.policy;
}



// static
FixGate::Verdict FixGate::check(const FixInfo& fix, const FixGateSettings& settings)
{
	if (fix.fix_class != FixInfo::Unknown && fix.fix_class < settings.min_class)
		return FixClass;
	if (fix.satellites >= 0 && fix.satellites < settings.min_satellites)
		return Satellites;
	if (settings.max_hdop > 0 && !std::isnan(fix.hdop) && fix.hdop > settings.max_hdop)
		return Hdop;
	auto const error = fix.horizontalError();
	if (settings.max_error > 0 && !std::isnan(error) && error > settings.max_error)
		return Error;
	return Accepted;
}

// static
QString FixGate::verdictText(Verdict verdict, const FixInfo& fix, const FixGateSettings& settings)
{
	switch (verdict)
	{
	case Accepted:
		break;
	case FixClass:
		return tr("Position not used: the fix is %1, the minimum is %2.")
		       .arg(className(fix.fix_class), className(settings.min_class));
	case Satellites:
		return tr("Position not used: satellites in use %1, the minimum is %2.")
		       .arg(fix.satellites).arg(settings.min_satellites);
	case Hdop:
		return tr("Position not used: HDOP %1, the maximum is %2.")
		       .arg(number(fix.hdop, 1), number(settings.max_hdop, 1));
	case Error:
		return tr("Position not used: horizontal error %1 m, the maximum is %2 m.")
		       .arg(number(fix.horizontalError(), 2), number(settings.max_error, 2));
	}
	return {};
}

// static
QString FixGate::className(FixInfo::Class fix_class)
{
	switch (fix_class)
	{
	case FixInfo::Unknown:
		break;
	case FixInfo::Invalid:
		//: Class of a position fix
		return tr("No fix");
	case FixInfo::Autonomous:
		//: Class of a position fix
		return tr("Autonomous");
	case FixInfo::Differential:
		//: Class of a position fix
		return tr("Differential");
	case FixInfo::RtkFloat:
		//: Class of a position fix
		return tr("RTK float");
	case FixInfo::RtkFixed:
		//: Class of a position fix
		return tr("RTK fixed");
	}
	//: Class of a position fix
	return tr("Unknown");
}

// static
QString FixGate::summary(const FixInfo& fix)
{
	QStringList parts;
	parts << className(fix.fix_class);
	if (fix.satellites >= 0)
		parts << tr("satellites: %1").arg(fix.satellites);
	if (!std::isnan(fix.hdop))
		parts << tr("HDOP %1").arg(number(fix.hdop, 1));
	auto const error = fix.horizontalError();
	if (!std::isnan(error))
	{
		QString text = QString(QChar(0x00B1));
		text += tr("%1 m").arg(number(error, 2));
		parts << text;
	}
	return parts.join(QLatin1String(", "));
}


}  // namespace OpenOrienteering
