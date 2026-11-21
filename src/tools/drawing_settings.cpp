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


#include "drawing_settings.h"

#include <QtGlobal>
#include <QSettings>
#include <QString>
#include <QVariant>


namespace OpenOrienteering {

int freehandSmoothingLevel()
{
	return qBound(0, QSettings().value(QStringLiteral("Editor/freehand_smoothing"), 2).toInt(), 3);
}

void setFreehandSmoothingLevel(int level)
{
	QSettings().setValue(QStringLiteral("Editor/freehand_smoothing"), qBound(0, level, 3));
}

double freehandSmoothingTolerance(int level)
{
	switch (level)
	{
	case 1:
		return 0.05;
	case 2:
		return 0.1;
	case 3:
		return 0.2;
	default:
		return 0.0;
	}
}

bool autoJoinEnabled()
{
	return QSettings().value(QStringLiteral("Editor/auto_join"), false).toBool();
}

void setAutoJoinEnabled(bool enabled)
{
	QSettings().setValue(QStringLiteral("Editor/auto_join"), enabled);
}

double autoJoinToleranceMm()
{
	return QSettings().value(QStringLiteral("Editor/auto_join_tolerance_mm"), 0.25).toDouble();
}


}  // namespace OpenOrienteering
