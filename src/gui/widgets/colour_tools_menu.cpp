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
#include <QInputDialog>
#include <QLabel>
#include <QLatin1Char>
#include <QLatin1String>
#include <QMenu>
#include <QMessageBox>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVBoxLayout>

#include "core/colour_load.h"
#include "core/colour_swatch.h"
#include "core/colour_usage.h"
#include "core/map.h"
#include "core/map_color.h"
#include "fileformats/file_format.h"
#include "fileformats/file_import_export.h"
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

// the design notes, "Load colours from file"
void loadColours(Map* map, QWidget* parent)
{
	auto const title = tr("Load colours from file");
	auto selected = MainWindow::getOpenFileName(parent, title, FileFormat::MapFile);
	if (!selected)
		return;
	Map source;
	auto importer = selected.fileFormat() ? selected.fileFormat()->makeImporter(selected.filePath(), &source, nullptr) : nullptr;
	if (!importer || !importer->doImport())
	{
		QMessageBox::warning(parent, title, tr("Cannot read %1.").arg(selected.filePath()));
		return;
	}
	auto const replace = tr("Replace: colours of the same name take the file's definition and order");
	auto const append = tr("Append: add only the colours which the map does not have");
	bool ok = false;
	auto const choice = QInputDialog::getItem(parent, title, tr("%1 colour(s) in the file.").arg(source.getNumColors()),
	                                          QStringList{ replace, append }, 0, false, &ok);
	if (!ok)
		return;
	auto const report = loadColoursFrom(*map, source, choice == append ? ColourLoadMode::Append : ColourLoadMode::Replace);
	showReport(parent, title,
	           tr("Changed: %1, added: %2").arg(report.changed.size()).arg(report.added.size()),
	           report.text());
}

// the design notes, "Colour usage report"
void showUsage(Map* map, QWidget* parent)
{
	auto const usage = colourUsage(*map);
	int unused = 0;
	for (auto const& use : usage)
		unused += use.used ? 0 : 1;
	showReport(parent, tr("Colour usage"),
	           tr("%1 colour(s), %2 of them not used.").arg(usage.size()).arg(unused),
	           colourUsageText(usage));
}

void deleteUnused(Map* map, QWidget* parent)
{
	auto const title = tr("Delete unused colours");
	QStringList names;
	for (auto const& use : colourUsage(*map))
	{
		if (!use.used)
			names.append(use.color->getName());
	}
	if (names.isEmpty())
	{
		QMessageBox::information(parent, title, tr("Every colour is used."));
		return;
	}
	if (QMessageBox::question(parent, title, tr("Delete %1 colour(s)?\n\n%2").arg(names.size()).arg(names.join(QLatin1Char('\n'))))
	    != QMessageBox::Yes)
		return;
	deleteUnusedColours(*map);
}

}  // namespace

QMenu* createColourToolsMenu(Map* map, MainWindow* window, QWidget* parent)
{
	Q_UNUSED(window)
	auto* menu = new QMenu(parent);
	QObject::connect(menu->addAction(tr("Import swatch...")), &QAction::triggered, parent, [map, parent]() {
		importSwatch(map, parent);
	});
	QObject::connect(menu->addAction(tr("Load colours from file...")), &QAction::triggered, parent, [map, parent]() {
		loadColours(map, parent);
	});
	menu->addSeparator();
	QObject::connect(menu->addAction(tr("Colour usage...")), &QAction::triggered, parent, [map, parent]() {
		showUsage(map, parent);
	});
	QObject::connect(menu->addAction(tr("Delete unused colours...")), &QAction::triggered, parent, [map, parent]() {
		deleteUnused(map, parent);
	});
	return menu;
}

}  // namespace OpenOrienteering
