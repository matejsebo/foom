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

#ifndef OPENORIENTEERING_GRID_TILES_H
#define OPENORIENTEERING_GRID_TILES_H

#include <functional>
#include <list>
#include <memory>
#include <unordered_map>
#include <vector>

#include <QtGlobal>
#include <QCoreApplication>
#include <QImage>
#include <QPointF>
#include <QRectF>
#include <QRgb>
#include <QString>
#include <QVector>

namespace OpenOrienteering {

class ElevationModel;
struct VegetationHeightOptions;


/**
 * Reports the progress of a run made row of tiles by row of tiles: called with
 * 0 of total before the first row and with done of total after each.
 * Returning false cancels the run.
 *
 * See the design notes, "Grids on disk, a tile cache, and a progress bar".
 */
using TileProgress = std::function<bool (const QString& step, int done, int total)>;


/**
 * Writes a float grid into a GeoTIFF row by row, as ElevationModel::save()
 * writes a whole grid: DEFLATE, predictor 3, blocks of 256 by 256 cells,
 * no-data -9999, BigTIFF when needed.
 */
class GridFileWriter
{
	Q_DECLARE_TR_FUNCTIONS(OpenOrienteering::GridFileWriter)

public:
	GridFileWriter();
	GridFileWriter(const GridFileWriter&) = delete;
	GridFileWriter& operator=(const GridFileWriter&) = delete;
	~GridFileWriter();

	bool open(const QString& path, int width, int height, const QPointF& top_left, double cell, const QString& crs_spec, QString* error);

	/// Writes rows of width values each, the northern first; NaN for a void cell.
	bool writeRows(int first_row, int rows, const float* values, QString* error);

	/// Sets the system, which may be known only after the rows are written.
	bool setCrs(const QString& crs_spec, QString* error);

	bool close(QString* error);

	/// Closes the file, if open, and removes it.
	void discard();

private:
	void* dataset = nullptr;
	QString file;
	int grid_width = 0;
};


/**
 * Writes a raster product (Grayscale8, Indexed8 with its palette, or RGB32 as
 * three bands) into a GeoTIFF in pieces of rows, as writeGeoTiff() writes a whole one.
 */
class RasterFileWriter
{
	Q_DECLARE_TR_FUNCTIONS(OpenOrienteering::RasterFileWriter)

public:
	RasterFileWriter();
	RasterFileWriter(const RasterFileWriter&) = delete;
	RasterFileWriter& operator=(const RasterFileWriter&) = delete;
	~RasterFileWriter();

	/// extent: left, bottom, width, height.
	bool open(const QString& path, int width, int height, const QRectF& extent, const QString& crs_spec,
	          QImage::Format format, const QVector<QRgb>& palette, QString* error);

	/// Writes the rows of the image, which has the raster's width and format, at the row.
	bool writeRows(int first_row, const QImage& rows, QString* error);

	bool close(QString* error);
	void discard();

private:
	void* dataset = nullptr;
	QString file;
	int raster_width = 0;
	bool single = true;
};


/**
 * Reads a Byte GeoTIFF: one band with a palette as Indexed8, one without as
 * Grayscale8, three as RGB32. extent receives left, bottom, width, height.
 */
QImage readRasterFile(const QString& path, QRectF* extent = nullptr, QString* error = nullptr);


/**
 * A grid file read in blocks of 256 by 256 cells, the most recently used
 * kept under a ceiling of bytes, at least one.
 *
 * Its answers are those of an ElevationModel of the whole file.
 * See the design notes, "Grids on disk, a tile cache, and a progress bar".
 */
class GridTileCache
{
	Q_DECLARE_TR_FUNCTIONS(OpenOrienteering::GridTileCache)

public:
	static constexpr int block_size = 256;

	/// The ceiling of a cache made with a memory limit of 0: 256 MiB.
	static qint64 defaultMemoryLimit();

	/// Opens any single-band raster GDAL reads; nullptr on failure.
	static std::unique_ptr<GridTileCache> open(const QString& path, qint64 memory_limit, QString* error);

	GridTileCache(const GridTileCache&) = delete;
	GridTileCache& operator=(const GridTileCache&) = delete;
	~GridTileCache();

	int width() const { return grid_width; }
	int height() const { return grid_height; }
	double cellSize() const { return cell; }
	QPointF topLeft() const { return top_left; }
	/// The outer edge of the cells; QRectF::top() is the southern edge.
	QRectF extent() const;
	const QString& crsSpec() const { return crs; }
	const QString& path() const { return file; }

	/// The value of a cell; NaN for a void cell or a failed read.
	float value(int col, int row) const;

	/// As ElevationModel::cellAt().
	bool cellAt(const QPointF& projected, int& col, int& row) const;

	/// As ElevationModel::at(): bilinear between cell centres.
	double at(const QPointF& projected) const;

	/// The cells from (col, row), cols by rows, as a grid; void outside the file's grid.
	std::unique_ptr<ElevationModel> window(int col, int row, int cols, int rows) const;

	qint64 memoryLimit() const { return limit; }
	qint64 blocksRead() const { return blocks_read; }
	qint64 bytesHeld() const { return bytes_held; }
	qint64 peakBytes() const { return peak_bytes; }

	/// The last read error, empty when none.
	const QString& error() const { return read_error; }

private:
	GridTileCache() = default;

	struct Block
	{
		std::vector<float> values;
		int cols = 0;
		std::list<qint64>::iterator use;
	};

	const Block& block(int block_col, int block_row) const;

	void* dataset = nullptr;
	QString file;
	int grid_width = 0;
	int grid_height = 0;
	QPointF top_left;
	double cell = 1.0;
	QString crs;
	double nodata = 0.0;
	bool has_nodata = false;
	qint64 limit = 0;
	int blocks_per_row = 0;

	mutable std::unordered_map<qint64, Block> blocks;
	mutable std::list<qint64> uses;   ///< Most recently used first.
	mutable qint64 blocks_read = 0;
	mutable qint64 bytes_held = 0;
	mutable qint64 peak_bytes = 0;
	mutable QString read_error;
	mutable Block failed;
};


/**
 * The vegetation height raster of the surface model less the terrain model,
 * both on disk and on one lattice, made in rows of tile_size metres and
 * written as it is made; the same pixels as vegetationHeightProduct() of the
 * whole difference.
 */
bool vegetationHeightToFile(const GridTileCache& dsm, const GridTileCache& dtm, const VegetationHeightOptions& options,
                            const QString& path, double tile_size, const TileProgress& progress, QString* error);


}  // namespace OpenOrienteering

#endif
