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


#ifndef FOOM_FIX_GATE_H
#define FOOM_FIX_GATE_H

#include <QCoreApplication>
#include <QString>

#include "sensors/nmea_fix.h"

class QSettings;

namespace OpenOrienteering {


/**
 * The settings of the quality gate for position fixes.
 *
 * A limit of zero means that there is none.
 *
 * See the design notes, "GNSS sources: detail".
 */
struct FixGateSettings
{
	/** What happens to the marker of a fix which fails. */
	enum Policy
	{
		ShowGreyed,
		Hide,
	};
	
	FixInfo::Class min_class = FixInfo::Autonomous;
	int min_satellites = 4;
	double max_hdop = 0;
	/** The maximum horizontal error, in metres. */
	double max_error = 0;
	Policy policy = ShowGreyed;
	
	/** Reads the settings; keys which are absent or make no sense give the defaults. */
	static FixGateSettings load(const QSettings& settings);
	/** Reads the settings of the application. */
	static FixGateSettings load();
	
	void save(QSettings& settings) const;
	/** Writes the settings of the application. */
	void save() const;
};

bool operator==(const FixGateSettings& lhs, const FixGateSettings& rhs);
inline bool operator!=(const FixGateSettings& lhs, const FixGateSettings& rhs) { return !(lhs == rhs); }


/**
 * The quality gate: decides whether a fix is good enough to be used.
 *
 * A value which is unknown never fails a fix.
 */
class FixGate
{
	Q_DECLARE_TR_FUNCTIONS(OpenOrienteering::FixGate)
	
public:
	/** Accepted, or the first check which failed. */
	enum Verdict
	{
		Accepted,
		FixClass,
		Satellites,
		Hdop,
		Error,
	};
	
	static Verdict check(const FixInfo& fix, const FixGateSettings& settings);
	
	/** A sentence for the user which says why the fix is not used; empty for an accepted fix. */
	static QString verdictText(Verdict verdict, const FixInfo& fix, const FixGateSettings& settings);
	
	/** The name of a fix class for the user. */
	static QString className(FixInfo::Class fix_class);
	
	/** What is known about the fix, in one line. */
	static QString summary(const FixInfo& fix);
};


}  // namespace OpenOrienteering

#endif
