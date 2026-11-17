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


#include "print_job.h"

#ifdef QT_PRINTSUPPORT_LIB

#include <QtGlobal>
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QLatin1String>
#include <QPageSize>
#include <QRectF>

#include "core/map_printer.h"


namespace OpenOrienteering {

namespace {

double rounded(double value)
{
	return qRound64(value * 1000) / 1000.0;
}

}  // namespace


QJsonObject printJob(const MapPrinter& printer, const QString& map_path, const QString& name)
{
	auto const to_image = printer.getTarget() == MapPrinter::imageTarget();
	auto const& options = printer.getOptions();
	auto const& area = printer.getPrintArea();
	
	QJsonObject open;
	open.insert(QLatin1String("step"), QLatin1String("open"));
	open.insert(QLatin1String("path"), map_path);
	
	QJsonObject output;
	output.insert(QLatin1String("step"), QLatin1String("export"));
	QString path = name;
	path += to_image ? QLatin1String(".png") : QLatin1String(".pdf");
	output.insert(QLatin1String("path"), path);
	output.insert(QLatin1String("extent"), QJsonArray{ rounded(area.left()), rounded(area.top()), rounded(area.width()), rounded(area.height()) });
	output.insert(QLatin1String("scale"), int(options.scale));
	output.insert(QLatin1String("dpi"), options.resolution);
	output.insert(QLatin1String("templates"), options.show_templates);
	output.insert(QLatin1String("grid"), options.show_grid);
	output.insert(QLatin1String("overprinting"), options.simulate_overprinting);
	if (!to_image)
	{
		switch (options.mode)
		{
		case MapPrinterOptions::Vector:
			output.insert(QLatin1String("mode"), QLatin1String("vector"));
			break;
		case MapPrinterOptions::Raster:
			output.insert(QLatin1String("mode"), QLatin1String("raster"));
			break;
		case MapPrinterOptions::Separations:
			output.insert(QLatin1String("mode"), QLatin1String("separations"));
			break;
		}
		auto const& format = printer.getPageFormat();
		if (format.page_size == QPageSize::Custom)
		{
			output.insert(QLatin1String("paper"), QJsonArray{ rounded(format.paper_dimensions.width()), rounded(format.paper_dimensions.height()) });
		}
		else
		{
			output.insert(QLatin1String("paper"), QPageSize::key(format.page_size));
			output.insert(QLatin1String("orientation"), format.orientation == MapPrinterPageFormat::Landscape ? QLatin1String("landscape") : QLatin1String("portrait"));
		}
		output.insert(QLatin1String("overlap"), QJsonArray{ rounded(format.h_overlap), rounded(format.v_overlap) });
	}
	
	QJsonObject job;
	job.insert(QLatin1String("steps"), QJsonArray{ open, output });
	return job;
}

bool savePrintJob(const MapPrinter& printer, const QString& map_path, const QString& job_path, QString* error)
{
	auto const job = printJob(printer, map_path, QFileInfo(job_path).completeBaseName());
	QFile file(job_path);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) || file.write(QJsonDocument(job).toJson()) < 0)
	{
		if (error)
			*error = QCoreApplication::translate("OpenOrienteering::PrintOutput", "Cannot write %1: %2").arg(job_path, file.errorString());
		return false;
	}
	return true;
}


}  // namespace OpenOrienteering

#endif  // QT_PRINTSUPPORT_LIB
