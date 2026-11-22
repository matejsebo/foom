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

#include "gif_writer.h"

#include <QtGlobal>
#include <QCoreApplication>
#include <QFile>
#include <QImage>
#include <QRgb>
#include <QString>
#include <QVector>

#include <cpl_error.h>
#include <gdal.h>


namespace OpenOrienteering {

namespace {

QString tr(const char* text)
{
	return QCoreApplication::translate("OpenOrienteering::RasterExport", text);
}

}  // namespace


bool writeGif(const QString& path, const QImage& source, QString* error)
{
	auto fail = [error, &path](const QString& reason) {
		if (error)
			*error = tr("Cannot write the GIF \"%1\": %2").arg(path, reason);
		return false;
	};

	QImage image = source;
	if (image.format() == QImage::Format_Grayscale8)
	{
		QVector<QRgb> greys;
		for (int i = 0; i < 256; ++i)
			greys.append(qRgb(i, i, i));
		image = image.convertToFormat(QImage::Format_Indexed8, greys, Qt::ThresholdDither);
	}
	else if (image.format() != QImage::Format_Indexed8)
	{
		image = image.convertToFormat(QImage::Format_Indexed8, Qt::ThresholdDither);
	}
	if (image.isNull())
		return fail(tr("Not enough memory for the image."));

	GDALAllRegister();
	auto const mem_driver = GDALGetDriverByName("MEM");
	auto const gif_driver = GDALGetDriverByName("GIF");
	if (!mem_driver || !gif_driver)
		return fail(tr("GDAL has no GIF driver."));

	auto const width = image.width();
	auto const height = image.height();
	auto const memory = GDALCreate(mem_driver, "", width, height, 1, GDT_Byte, nullptr);
	if (!memory)
		return fail(QString::fromUtf8(CPLGetLastErrorMsg()));
	auto const band = GDALGetRasterBand(memory, 1);
	auto ok = true;
	for (int y = 0; y < height && ok; ++y)
	{
		auto* line = const_cast<uchar*>(image.constScanLine(y));
		ok = GDALRasterIO(band, GF_Write, 0, y, width, 1, line, width, 1, GDT_Byte, 0, 0) == CE_None;
	}
	auto const table = GDALCreateColorTable(GPI_RGB);
	auto const colours = image.colorTable();
	for (int i = 0; i < colours.size(); ++i)
	{
		GDALColorEntry entry { short(qRed(colours[i])), short(qGreen(colours[i])), short(qBlue(colours[i])), 255 };
		GDALSetColorEntry(table, i, &entry);
	}
	GDALSetRasterColorTable(band, table);
	GDALDestroyColorTable(table);

	GDALDatasetH gif = nullptr;
	if (ok)
		gif = GDALCreateCopy(gif_driver, path.toUtf8().constData(), memory, FALSE, nullptr, nullptr, nullptr);
	GDALClose(memory);
	if (!gif)
	{
		auto const reason = QString::fromUtf8(CPLGetLastErrorMsg());
		QFile::remove(path);
		return fail(reason);
	}
	GDALClose(gif);
	// The GIF driver writes an .aux.xml beside the file for nothing which GIF cannot hold here.
	QFile::remove(path + QLatin1String(".aux.xml"));
	return true;
}


}  // namespace OpenOrienteering
