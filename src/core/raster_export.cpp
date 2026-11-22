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

#include "raster_export.h"

#include <QtGlobal>
#include <QColor>
#include <QCoreApplication>
#include <QLatin1Char>
#include <QLatin1String>
#include <QPoint>
#include <QPointF>

#include "core/georeferencing.h"
#include "core/map.h"
#include "core/map_color.h"
#include "core/map_coord.h"
#include "templates/world_file.h"


namespace OpenOrienteering {

namespace {

QString tr(const char* text)
{
	return QCoreApplication::translate("OpenOrienteering::RasterExport", text);
}

QVector<QRgb> greyPalette()
{
	QVector<QRgb> palette;
	palette.reserve(256);
	for (int i = 0; i < 256; ++i)
		palette.append(qRgb(i, i, i));
	return palette;
}

bool isSuffix(const QString& path, const char* suffix)
{
	auto const dot = path.lastIndexOf(QLatin1Char('.'));
	return dot >= 0 && path.midRef(dot + 1).compare(QLatin1String(suffix), Qt::CaseInsensitive) == 0;
}

}  // namespace


QVector<QRgb> mapPalette(const Map& map)
{
	QVector<QRgb> palette { qRgb(255, 255, 255) };
	for (int i = 0; i < map.getNumColors() && palette.size() < 256; ++i)
	{
		auto const rgb = static_cast<const QColor&>(*map.getColor(i)).rgb() | 0xff000000u;
		if (!palette.contains(rgb))
			palette.append(rgb);
	}
	return palette;
}

QImage convertRasterColours(const QImage& rgb, RasterColours colours, const Map& map)
{
	QImage result;
	switch (colours)
	{
	case RasterColours::Rgb:
		return rgb;
	case RasterColours::Palette:
		// Qt takes the nearest colour of the table, without dithering.
		result = rgb.convertToFormat(QImage::Format_RGB32).convertToFormat(QImage::Format_Indexed8, mapPalette(map), Qt::ThresholdDither);
		break;
	case RasterColours::Grey:
		result = rgb.convertToFormat(QImage::Format_RGB32).convertToFormat(QImage::Format_Grayscale8);
		break;
	}
	result.setDotsPerMeterX(rgb.dotsPerMeterX());
	result.setDotsPerMeterY(rgb.dotsPerMeterY());
	return result;
}

std::vector<RasterTile> rasterTiles(const QSize& image, const RasterTiles& tiles, const QString& path)
{
	std::vector<RasterTile> result;
	if (!tiles.isOn() || image.isEmpty())
		return result;

	auto const slash = path.lastIndexOf(QLatin1Char('/'));
	auto dot = path.lastIndexOf(QLatin1Char('.'));
	if (dot <= slash)
		dot = path.length();
	auto const base = path.left(dot);
	auto const suffix = path.mid(dot);

	auto const rows = (image.height() + tiles.height - 1) / tiles.height;
	auto const columns = (image.width() + tiles.width - 1) / tiles.width;
	result.reserve(std::size_t(rows) * std::size_t(columns));
	for (int row = 0; row < rows; ++row)
	{
		for (int column = 0; column < columns; ++column)
		{
			QRect rect(column * tiles.width, row * tiles.height, tiles.width, tiles.height);
			rect = rect.intersected(QRect(QPoint(0, 0), image));
			result.push_back({ rect, QStringLiteral("%1_%2_%3%4").arg(base).arg(row + 1).arg(column + 1).arg(suffix) });
		}
	}
	return result;
}

QTransform rasterWorldTransform(const Map& map, const QRectF& area, double dpi, double scale_adjustment)
{
	// As PrintWidget::exportWorldFile()
	auto const& georef = map.getGeoreferencing();
	auto const& mm_to_world = georef.mapToProjected();
	auto const pixel_per_mm = (dpi / 25.4) * scale_adjustment;
	auto const xscale = mm_to_world.m11() / pixel_per_mm;
	auto const yscale = mm_to_world.m22() / pixel_per_mm;
	auto const xskew  = mm_to_world.m12() / pixel_per_mm;
	auto const yskew  = mm_to_world.m21() / pixel_per_mm;
	auto const top_left = georef.toProjectedCoords(MapCoord{area.topLeft()});
	return QTransform(xscale, yskew, 0, xskew, yscale, 0, top_left.x(), top_left.y());
}

bool saveRasterImage(const QImage& rgb, const Map& map, const QString& path, const RasterSaveOptions& options,
                     QStringList* files, QString* error)
{
	auto image = convertRasterColours(rgb, options.colours, map);
	if (image.isNull())
	{
		if (error)
			*error = tr("Not enough memory for the image.");
		return false;
	}
	// BMP has no grey format of its own: 8 bit with a grey palette.
	if (!options.writer && image.format() == QImage::Format_Grayscale8 && isSuffix(path, "bmp"))
	{
		auto const dpm_x = image.dotsPerMeterX();
		auto const dpm_y = image.dotsPerMeterY();
		image = image.convertToFormat(QImage::Format_Indexed8, greyPalette(), Qt::ThresholdDither);
		image.setDotsPerMeterX(dpm_x);
		image.setDotsPerMeterY(dpm_y);
	}

	auto tiles = rasterTiles(image.size(), options.tiles, path);
	if (tiles.empty())
		tiles.push_back({ QRect(QPoint(0, 0), image.size()), path });

	for (auto const& tile : tiles)
	{
		auto const piece = tile.rect == QRect(QPoint(0, 0), image.size()) ? image : image.copy(tile.rect);
		if (options.writer)
		{
			if (!options.writer(tile.path, piece, error))
				return false;
		}
		else if (!piece.save(tile.path))
		{
			if (error)
				*error = tr("Cannot write the image \"%1\".").arg(tile.path);
			return false;
		}
		if (files)
			files->append(tile.path);

		if (options.world_file)
		{
			auto const origin = options.world.map(QPointF(tile.rect.topLeft()));
			auto const& w = options.world;
			const WorldFile world_file(QTransform(w.m11(), w.m12(), 0, w.m21(), w.m22(), 0, origin.x(), origin.y()));
			if (!world_file.save(WorldFile::pathForImage(tile.path)))
			{
				if (error)
					*error = tr("Cannot write the world file of \"%1\".").arg(tile.path);
				return false;
			}
		}
	}
	return true;
}


}  // namespace OpenOrienteering
