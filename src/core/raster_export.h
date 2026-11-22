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

#ifndef FOOM_CORE_RASTER_EXPORT_H
#define FOOM_CORE_RASTER_EXPORT_H

#include <functional>
#include <vector>

#include <QImage>
#include <QRect>
#include <QRectF>
#include <QRgb>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QTransform>
#include <QVector>

namespace OpenOrienteering {

class Map;


/**
 * The colour depth of a raster export.
 *
 * See the design notes, "Print and raster output", fourth change.
 */
enum class RasterColours
{
	Rgb,      ///< As rendered.
	Palette,  ///< 8 bit, the map's colours (mapPalette()).
	Grey,     ///< 8 bit grey.
};

/** White, then each RGB of the map's colours in the order of the colour table, once, at most 256. */
QVector<QRgb> mapPalette(const Map& map);

/** Converts an RGB rendering to the colour depth; Palette takes the nearest entry, without dithering. */
QImage convertRasterColours(const QImage& rgb, RasterColours colours, const Map& map);

/** Tile size in pixels; 0 means no tiles. */
struct RasterTiles
{
	int width = 0;
	int height = 0;

	bool isOn() const { return width > 0 && height > 0; }
};

struct RasterTile
{
	QRect rect;    ///< In pixels of the whole image.
	QString path;  ///< <base>_<row>_<column>.<ext>, counted from 1.
};

/** The tiles of an image, rows from the top, columns from the left. */
std::vector<RasterTile> rasterTiles(const QSize& image, const RasterTiles& tiles, const QString& path);

/** Pixel to projected coordinates of the print area, as PrintWidget::exportWorldFile() computes it. */
QTransform rasterWorldTransform(const Map& map, const QRectF& area, double dpi, double scale_adjustment = 1);

/** Writes one image to a file; returns false with a message. For formats which Qt cannot write. */
using RasterWriter = std::function<bool (const QString& file, const QImage& image, QString* error)>;

struct RasterSaveOptions
{
	RasterColours colours = RasterColours::Rgb;
	RasterTiles tiles;
	bool world_file = false;
	QTransform world;  ///< Pixel to projected, for the world files.
	RasterWriter writer;  ///< Used for every file when given (GIF).
};

/**
 * Converts the rendering, cuts it into tiles and writes the files with
 * Qt's writers or the given writer, with world files when asked.
 * Returns false with a message; lists the files written.
 */
bool saveRasterImage(const QImage& rgb, const Map& map, const QString& path, const RasterSaveOptions& options,
                     QStringList* files, QString* error);


}  // namespace OpenOrienteering

#endif
