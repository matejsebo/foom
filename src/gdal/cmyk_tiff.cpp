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

#include <algorithm>
#include <cstddef>
#include <vector>

#include <QtGlobal>
#include <QByteArray>
#include <QCoreApplication>
#include <QImage>
#include <QPointF>
#include <QRgb>
#include <QTransform>

#include <cpl_conv.h>
#include <cpl_error.h>
#include <cpl_string.h>
#include <gdal.h>
#include <ogr_srs_api.h>

#include "core/georeferencing.h"
#include "core/map.h"


namespace OpenOrienteering {

namespace {

void setResolution(GDALDatasetH dataset, double dpi)
{
	auto const resolution = QByteArray::number(dpi, 'f', 2);
	GDALSetMetadataItem(dataset, "TIFFTAG_XRESOLUTION", resolution.constData(), nullptr);
	GDALSetMetadataItem(dataset, "TIFFTAG_YRESOLUTION", resolution.constData(), nullptr);
	GDALSetMetadataItem(dataset, "TIFFTAG_RESOLUTIONUNIT", "2", nullptr);  // pixels per inch
}

void setGeoreference(GDALDatasetH dataset, const PlateGeoreference* georeference)
{
	if (!georeference || !georeference->valid)
		return;
	double transform[6];
	std::copy(georeference->transform, georeference->transform + 6, transform);
	GDALSetGeoTransform(dataset, transform);
	if (!georeference->wkt.isEmpty())
		GDALSetProjection(dataset, georeference->wkt.constData());
}

}  // namespace


PlateGeoreference plateGeoreference(const Map& map, const QRectF& area, double dpi, double scale_adjustment)
{
	PlateGeoreference result;
	auto const& georef = map.getGeoreferencing();
	if (georef.getState() != Georeferencing::Geospatial || dpi <= 0 || scale_adjustment <= 0)
		return result;
	
	auto const& to_projected = georef.mapToProjected();
	auto const step = 25.4 / dpi / scale_adjustment;  // mm of the map per pixel
	auto const origin = to_projected.map(area.topLeft());
	auto const column = to_projected.map(area.topLeft() + QPointF(step, 0)) - origin;
	auto const row = to_projected.map(area.topLeft() + QPointF(0, step)) - origin;
	result.transform[0] = origin.x();
	result.transform[1] = column.x();
	result.transform[2] = row.x();
	result.transform[3] = origin.y();
	result.transform[4] = column.y();
	result.transform[5] = row.y();
	result.valid = true;
	
	auto const spec = georef.getProjectedCRSSpec().toUtf8();
	auto srs = OSRNewSpatialReference(nullptr);
	if (srs && !spec.isEmpty() && OSRSetFromUserInput(srs, spec.constData()) == OGRERR_NONE)
	{
		char* wkt = nullptr;
		if (OSRExportToWkt(srs, &wkt) == OGRERR_NONE && wkt)
			result.wkt = wkt;
		CPLFree(wkt);
	}
	if (srs)
		OSRDestroySpatialReference(srs);
	return result;
}


bool writeCmykTiff(const QString& path, const std::vector<RasterPlate>& plates, double dpi, QString* error,
                   const PlateGeoreference* georeference)
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
	
	setResolution(dataset, dpi);
	setGeoreference(dataset, georeference);
	
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


bool writePlateTiff(const QString& path, const QImage& source, double dpi, PlateCompression compression,
                    const PlateGeoreference* georeference, QString* error)
{
	auto const fail = [error](const QString& message) {
		if (error)
			*error = message;
		return false;
	};
	auto const tr = [](const char* text) {
		return QCoreApplication::translate("OpenOrienteering::PrintOutput", text);
	};
	
	auto const mono = source.depth() == 1;
	if (source.isNull() || (!mono && source.format() != QImage::Format_Grayscale8))
		return fail(tr("The area is empty or too large for an image."));
	if (compression == PlateCompression::Fax && !mono)
		return fail(tr("Fax compression needs a 1-bit image."));
	
	GDALAllRegister();
	auto const driver = GDALGetDriverByName("GTiff");
	if (!driver)
		return fail(tr("GDAL has no TIFF driver."));
	
	auto const width = source.width();
	auto const height = source.height();
	char** options = nullptr;
	if (mono)
	{
		options = CSLSetNameValue(options, "NBITS", "1");
		options = CSLSetNameValue(options, "PHOTOMETRIC", "MINISWHITE");
	}
	else
	{
		options = CSLSetNameValue(options, "PHOTOMETRIC", "MINISBLACK");
	}
	options = CSLSetNameValue(options, "COMPRESS", compression == PlateCompression::Fax ? "CCITTFAX4" : "LZW");
	CPLErrorReset();
	auto const dataset = GDALCreate(driver, path.toUtf8().constData(), width, height, 1, GDT_Byte, options);
	CSLDestroy(options);
	if (!dataset)
		return fail(tr("Cannot write %1: %2").arg(path, QString::fromUtf8(CPLGetLastErrorMsg())));
	setResolution(dataset, dpi);
	setGeoreference(dataset, georeference);
	
	// One row at a time; for 1 bit, 1 is ink, which MINISWHITE shows black.
	std::vector<unsigned char> row(std::size_t(width), 0);
	auto const band = GDALGetRasterBand(dataset, 1);
	auto ok = true;
	for (int y = 0; y < height && ok; ++y)
	{
		if (mono)
		{
			for (int x = 0; x < width; ++x)
				row[std::size_t(x)] = qGray(source.color(source.pixelIndex(x, y))) < 128 ? 1 : 0;
		}
		else
		{
			auto const* line = source.constScanLine(y);
			std::copy(line, line + width, row.begin());
		}
		ok = GDALRasterIO(band, GF_Write, 0, y, width, 1, row.data(), width, 1, GDT_Byte, 0, 0) == CE_None;
	}
	GDALClose(dataset);
	if (!ok)
		return fail(tr("Cannot write %1: %2").arg(path, QString::fromUtf8(CPLGetLastErrorMsg())));
	return true;
}


}  // namespace OpenOrienteering
