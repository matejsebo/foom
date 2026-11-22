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

#include "elevation_progress_dialog.h"

#include <algorithm>

#include <QCoreApplication>
#include <QPointer>

namespace OpenOrienteering {

ElevationProgressDialog::ElevationProgressDialog(const QString& title, QWidget* parent)
: QProgressDialog(parent)
{
	setWindowTitle(title);
	setWindowModality(Qt::WindowModal);
	setMinimumDuration(500);
	setAutoClose(false);
	setAutoReset(false);
	setCancelButtonText(tr("Cancel"));
	setRange(0, 1);
	setValue(0);
}

ElevationProgressDialog::~ElevationProgressDialog() = default;

TileProgress ElevationProgressDialog::callback()
{
	QPointer<ElevationProgressDialog> dialog(this);
	return [dialog](const QString& step, int done, int total) {
		if (!dialog)
			return true;
		if (dialog->wasCanceled())
			return false;
		dialog->setLabelText(tr("%1: row of tiles %2 of %3").arg(step).arg(done).arg(total));
		dialog->setMaximum(std::max(1, total));
		dialog->setValue(done);
		QCoreApplication::processEvents();
		return dialog && !dialog->wasCanceled();
	};
}

}  // namespace OpenOrienteering
