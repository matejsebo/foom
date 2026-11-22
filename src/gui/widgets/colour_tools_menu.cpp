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

#include "colour_tools_menu.h"

#include <QAction>
#include <QCheckBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QLabel>
#include <QLatin1String>
#include <QMenu>
#include <QMessageBox>
#include <QObject>
#include <QString>
#include <QVBoxLayout>

#include "core/colour_swatch.h"
#include "core/map.h"
#include "gui/main_window.h"


namespace OpenOrienteering {

namespace {

QString tr(const char* text)
{
	return QCoreApplication::translate("OpenOrienteering::ColourTools", text);
}

void showReport(QWidget* parent, const QString& title, const QString& summary, const QString& details)
{
	QMessageBox box(QMessageBox::Information, title, summary, QMessageBox::Ok, parent);
	if (!details.isEmpty())
		box.setDetailedText(details);
	box.exec();
}

// the design notes, "Swatch import"
void importSwatch(Map* map, QWidget* parent)
{
	auto const title = tr("Import swatch");
	auto const path = QFileDialog::getOpenFileName(parent, title, {},
	                                               tr("Colour swatches") + QLatin1String(" (*.txt *.csv);;")
	                                               + tr("All files") + QLatin1String(" (*.*)"));
	if (path.isEmpty())
		return;
	QString error;
	auto const entries = readColourSwatchFile(path, &error);
	if (entries.empty())
	{
		QMessageBox::warning(parent, title, error);
		return;
	}

	QDialog dialog(parent);
	dialog.setWindowTitle(title);
	auto* overprint = new QCheckBox(tr("Take the overprint setting of spot colours"));
	auto* opacity = new QCheckBox(tr("Take the opacity"));
	auto* add_missing = new QCheckBox(tr("Add the colours which the map does not have"));
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	auto* layout = new QVBoxLayout(&dialog);
	layout->addWidget(new QLabel(QCoreApplication::translate("OpenOrienteering::ColourTools", "%n colour(s) in the file. Colours of the same name take their CMYK values.", nullptr, int(entries.size()))));
	layout->addWidget(overprint);
	layout->addWidget(opacity);
	layout->addWidget(add_missing);
	layout->addWidget(buttons);
	if (dialog.exec() != QDialog::Accepted)
		return;

	SwatchOptions options;
	options.overprint = overprint->isChecked();
	options.opacity = opacity->isChecked();
	options.add_missing = add_missing->isChecked();
	auto const report = applyColourSwatch(*map, entries, options);
	showReport(parent, title,
	           tr("Updated: %1, unchanged: %2, added: %3, not in the map: %4")
	           .arg(report.updated.size()).arg(report.unchanged.size()).arg(report.added.size()).arg(report.not_found.size()),
	           report.text());
}

}  // namespace

QMenu* createColourToolsMenu(Map* map, MainWindow* window, QWidget* parent)
{
	Q_UNUSED(window)
	auto* menu = new QMenu(parent);
	QObject::connect(menu->addAction(tr("Import swatch...")), &QAction::triggered, parent, [map, parent]() {
		importSwatch(map, parent);
	});
	return menu;
}

}  // namespace OpenOrienteering
