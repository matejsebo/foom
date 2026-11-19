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


#ifndef FOOM_IMAGE_OBJECT_H
#define FOOM_IMAGE_OBJECT_H

#include <array>

#include <QtGlobal>
#include <QByteArray>
#include <QImage>
#include <QPolygonF>
#include <QRectF>
#include <QSize>
#include <QSizeF>
#include <QString>
#include <QTransform>

#include "core/map_coord.h"
#include "core/objects/object.h"

class QPainter;
class QXmlStreamReader;
class QXmlStreamWriter;

namespace OpenOrienteering {

class Map;
class RenderConfig;
class Symbol;


/**
 * A raster image in the map: PNG or JPEG bytes kept as they were read
 * (embedded), or the path of an image file (linked).
 *
 * The coordinates are the four corners of the image: top left, top right,
 * bottom right, bottom left. Centre, size and rotation follow from them, so
 * every transformation of coordinates applies to images.
 *
 * See the design notes, "Image object".
 */
class ImageObject : public Object
{
public:
	/** Whether the image is drawn below or above the map content. */
	enum Layer { BelowMap, AboveMap };
	
	/** Constructs an empty image at the origin. The symbol is Map::getImageSymbol() if not given. */
	explicit ImageObject(const Symbol* symbol = nullptr);
	
protected:
	explicit ImageObject(const ImageObject& proto);
	
public:
	ImageObject* duplicate() const override;
	void copyFrom(const Object& other) override;
	void transform(const QTransform& t) override;
	bool intersectsBox(const QRectF& box) const override;
	
	
	/** The bytes of an embedded image; empty for a linked image. */
	const QByteArray& data() const { return image_data; }
	
	/** The file name an embedded image came from, or empty. */
	const QString& href() const { return image_href; }
	
	/** The absolute path of a linked image's file, or empty. */
	const QString& linkedFile() const { return link_file; }
	
	/** Returns true if the image is read from a file, not from data(). */
	bool isLinked() const { return image_data.isEmpty() && !link_file.isEmpty(); }
	
	/** Makes this an embedded image of the given PNG or JPEG bytes. */
	void setData(const QByteArray& bytes, const QString& file_name = {});
	
	/** Makes this a linked image of the file with the given absolute path. */
	void setLink(const QString& absolute_path);
	
	/** Turns a linked image into an embedded one. Returns false if the file cannot be read. */
	bool embed();
	
	/** "png" or "jpeg", or empty if the image cannot be read. */
	QString format() const;
	
	/** The decoded image; a null image if the bytes or the file cannot be read. */
	const QImage& image() const;
	
	
	/** Sets the corners from the centre, the size in mm and the rotation (radians, counter-clockwise). */
	void setCorners(const MapCoordF& centre, const QSizeF& size_mm, qreal rotation = 0);
	
	MapCoordF anchor() const;
	QSizeF size() const;
	qreal imageRotation() const;
	QPolygonF quad() const;
	
	/** Returns true if the point is inside the image. */
	bool containsPoint(const MapCoordF& point) const;
	
	
	qreal opacity() const { return image_opacity; }
	void setOpacity(qreal opacity);
	
	Layer layer() const { return image_layer; }
	void setLayer(Layer layer);
	
	
	/** Compares everything but the coordinates and the general object properties. */
	bool imageEquals(const ImageObject& other) const;
	
	/** Draws the image. Called by Map::draw(). */
	void draw(QPainter* painter, const RenderConfig& config) const;
	
	/** Writes the image element of the object element. */
	void saveImage(QXmlStreamWriter& xml) const;
	
	/** Reads the image element of the object element. */
	void loadImage(QXmlStreamReader& xml);
	
	
	/**
	 * Sets the directory of the map file which is being read or written.
	 *
	 * Links are saved relative to it, and relative links are resolved in it.
	 * An empty directory means absolute links.
	 */
	static void setFileDirectory(const QString& directory);
	
private:
	/** The transformation from pixels of an image of the given size to map coordinates. */
	QTransform pixelToMap(const QSize& pixels) const;
	
	/** The image, halved the given number of times (0 to 3). */
	const QImage& level(int n) const;
	
	QByteArray image_data;
	QString image_href;
	QString link_file;
	qreal image_opacity = 1;
	Layer image_layer = AboveMap;
	
	mutable std::array<QImage, 4> levels;
	mutable bool decoded = false;
	
	static QString file_directory;
};


/**
 * Adds the PNG or JPEG file as an image object to the current part, centred at
 * the given point, as one undo step, and selects it (image-add).
 *
 * The size follows from the pixel size and the resolution Qt reports for the
 * file; if max_size_mm is valid and the image is larger, it is reduced to fit.
 *
 * Returns nullptr, with a message in error, if the file is no such image.
 */
ImageObject* addImage(Map& map, const QString& path, bool embed, const MapCoordF& centre, const QSizeF& max_size_mm, QString* error);

/**
 * Turns the selected linked images into embedded ones, as one undo step
 * (image-embed). Returns the number of images embedded.
 */
int embedSelectedImages(Map& map);

/** Returns true if a linked image is selected. */
bool isLinkedImageSelected(const Map& map);

/** Returns the first selected image object, or nullptr. */
const ImageObject* firstSelectedImage(const Map& map);

/**
 * Gives the selected images the opacity and the layer, as one undo step
 * (image-properties). Returns the number of images changed.
 */
int setSelectedImageProperties(Map& map, qreal opacity, ImageObject::Layer layer);


}  // namespace OpenOrienteering

#endif
