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


#ifndef FOOM_PRINT_OUTPUT_H
#define FOOM_PRINT_OUTPUT_H

#include <vector>

#include <QImage>
#include <QRectF>
#include <QString>
#include <QStringList>

class QPainter;

namespace OpenOrienteering {

class Map;
class MapColor;
class MapView;


/**
 * What print and export leave out, and the map as plates of ink.
 *
 * See the design notes, "Print and raster output".
 */

/** The hidden symbols and parts which have objects. */
struct HiddenContent
{
	struct Entry
	{
		QString name;
		int objects = 0;
	};
	
	std::vector<Entry> symbols;  ///< Hidden symbols with objects in visible parts; no helper symbols.
	std::vector<Entry> parts;    ///< Hidden parts with objects.
	
	bool isEmpty() const { return symbols.empty() && parts.empty(); }
	
	/** One line per entry, for a message or a log. */
	QStringList lines() const;
};

/** Lists what an output of the map will not show because it is hidden. */
HiddenContent hiddenContent(const Map& map);


/** One plate of the print area: 8 bit per pixel, 0 for no ink, 255 for full ink. */
struct RasterPlate
{
	QString name;
	QImage ink;                    ///< QImage::Format_Grayscale8
	double screen_frequency = -1;  ///< Lines per inch, or not positive when not set.
	double screen_angle = 0;       ///< Degrees.
};

/**
 * Draws one plate per spot colour, in the order of the vector separations.
 *
 * The area is in map coordinates; the scale adjustment is the map scale
 * divided by the print scale.
 */
std::vector<RasterPlate> renderSpotPlates(Map& map, const QRectF& area, double dpi, double scale_adjustment = 1,
                                         bool with_templates = false, const MapView* view = nullptr);

/**
 * Draws the loaded templates which are assigned to the spot colour and
 * visible in the view (all, without a view), for that colour's plate.
 *
 * See the design notes, "Templates on a spot colour plate".
 */
void drawPlateTemplates(QPainter* painter, const Map& map, const MapColor* color, const QRectF& area, const MapView* view);

/** Draws the plates cyan, magenta, yellow and black from the colours' CMYK values. */
std::vector<RasterPlate> renderProcessPlates(Map& map, const QRectF& area, double dpi, double scale_adjustment = 1);

/** An image for one process plate, in grey with the image's alpha: 0 cyan to 3 black. */
QImage processPlateOfImage(const QImage& image, int channel);

/** The plate as a grey image, ink dark. */
QImage plateAsGrey(const QImage& ink);

/** The plate as a 1-bit image: black where the ink is 50 % or more. */
QImage plateAsLineArt(const QImage& ink);

/**
 * The plate as a 1-bit image with a round-dot screen.
 *
 * A frequency which is not positive stands for the smaller of 150 lines per
 * inch and an eighth of the resolution, at 45 degrees. Returns a null image
 * and a message when a cell of the screen would be less than 8 pixels wide.
 */
QImage plateAsHalftone(const QImage& ink, double dpi, double frequency, double angle, QString* error);

/** The file of a plate: the path with the plate's name before the extension. */
QString plateFileName(const QString& path, const QString& plate_name);

enum class PlateForm { Grey, LineArt, Halftone };

/**
 * Writes one image per spot colour beside the path.
 * Returns false with a message when the map has no spot colour or a file cannot be written,
 * and, before any file is written, when a colour's screen is too fine for the resolution.
 */
bool saveSpotPlates(Map& map, const QRectF& area, double dpi, double scale_adjustment, const QString& path, PlateForm form, QStringList* files, QString* error,
                    bool with_templates = false, const MapView* view = nullptr);


}  // namespace OpenOrienteering

#endif
