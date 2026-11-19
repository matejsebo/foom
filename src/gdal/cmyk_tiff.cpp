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


#include "cmyk_tiff.h"

#include <cstddef>

#include <QtGlobal>
#include <QByteArray>
#include <QCoreApplication>
#include <QImage>

#include <cpl_conv.h>
#include <cpl_error.h>
#include <cpl_string.h>
#include <gdal.h>


namespace OpenOrienteering {

bool writeCmykTiff(const QString& path, const std::vector<RasterPlate>& plates, double dpi, QString* error)
{
	auto const fail = [error](const QString& message) {
		if (error)
			*error = message;
		return false;
	};
	auto const tr = [](const char* text) {
		return QCoreApplication::translate("OpenOrienteering::PrintOutput", text);
	};
	
	if (plates.size() != 4 || plates.front().ink.isNull())
		return fail(tr("The area is empty or too large for an image."));
	auto const width = plates.front().ink.width();
	auto const height = plates.front().ink.height();
	for (auto const& plate : plates)
	{
		if (plate.ink.format() != QImage::Format_Grayscale8 || plate.ink.width() != width || plate.ink.height() != height)
			return fail(tr("The plates do not fit together."));
	}
	
	GDALAllRegister();
	auto const driver = GDALGetDriverByName("GTiff");
	if (!driver)
		return fail(tr("GDAL has no TIFF driver."));
	
	char** options = nullptr;
	options = CSLSetNameValue(options, "PHOTOMETRIC", "CMYK");
	options = CSLSetNameValue(options, "COMPRESS", "LZW");
	options = CSLSetNameValue(options, "INTERLEAVE", "PIXEL");
	CPLErrorReset();
	auto const dataset = GDALCreate(driver, path.toUtf8().constData(), width, height, 4, GDT_Byte, options);
	CSLDestroy(options);
	if (!dataset)
		return fail(tr("Cannot write %1: %2").arg(path, QString::fromUtf8(CPLGetLastErrorMsg())));
	
	auto const resolution = QByteArray::number(dpi, 'f', 2);
	GDALSetMetadataItem(dataset, "TIFFTAG_XRESOLUTION", resolution.constData(), nullptr);
	GDALSetMetadataItem(dataset, "TIFFTAG_YRESOLUTION", resolution.constData(), nullptr);
	GDALSetMetadataItem(dataset, "TIFFTAG_RESOLUTIONUNIT", "2", nullptr);  // pixels per inch
	
	auto ok = true;
	for (int band = 0; band < 4 && ok; ++band)
	{
		// The pixels are read only; GDAL's interface is not const.
		auto const& ink = plates[std::size_t(band)].ink;
		auto* pixels = const_cast<uchar*>(ink.constBits());
		ok = GDALRasterIO(GDALGetRasterBand(dataset, band + 1), GF_Write, 0, 0, width, height,
		                  pixels, width, height, GDT_Byte, 1, ink.bytesPerLine()) == CE_None;
	}
	GDALClose(dataset);
	if (!ok)
		return fail(tr("Cannot write %1: %2").arg(path, QString::fromUtf8(CPLGetLastErrorMsg())));
	return true;
}


}  // namespace OpenOrienteering
