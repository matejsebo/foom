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


#include "image_object.h"

#include <cmath>
#include <cstddef>
#include <stdexcept>

#include <Qt>
#include <QBuffer>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QIODevice>
#include <QLatin1String>
#include <QPaintEngine>
#include <QPainter>
#include <QPen>
#include <QPointF>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

#include "core/map.h"
#include "core/map_part.h"
#include "core/renderables/renderable.h"
#include "core/symbols/image_symbol.h"
#include "undo/object_undo.h"
#include "util/xml_stream_util.h"


namespace OpenOrienteering {

namespace {

QString tr(const char* text)
{
	return QCoreApplication::translate("OpenOrienteering::ImageObject", text);
}

QString formatOf(const QByteArray& bytes)
{
	QBuffer buffer;
	buffer.setData(bytes);
	buffer.open(QIODevice::ReadOnly);
	auto const format = QImageReader::imageFormat(&buffer);
	if (format == "png")
		return QStringLiteral("png");
	if (format == "jpeg" || format == "jpg")
		return QStringLiteral("jpeg");
	return {};
}

QByteArray readFile(const QString& path)
{
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly))
		return {};
	return file.readAll();
}

}  // namespace


QString ImageObject::file_directory;

// static
void ImageObject::setFileDirectory(const QString& directory)
{
	file_directory = directory;
}


ImageObject::ImageObject(const Symbol* symbol)
: Object(Object::Image, symbol ? symbol : Map::getImageSymbol())
{
	coords.resize(4, MapCoord(0, 0));
}

ImageObject::ImageObject(const ImageObject& proto)
: Object(proto)
, image_data(proto.image_data)
, image_href(proto.image_href)
, link_file(proto.link_file)
, image_opacity(proto.image_opacity)
, image_layer(proto.image_layer)
, levels(proto.levels)
, decoded(proto.decoded)
{
	// nothing else
}

ImageObject* ImageObject::duplicate() const
{
	return new ImageObject(*this);
}

void ImageObject::copyFrom(const Object& other)
{
	if (&other == this)
		return;
	
	Object::copyFrom(other);
	auto const& other_image = static_cast<const ImageObject&>(other);
	image_data = other_image.image_data;
	image_href = other_image.image_href;
	link_file = other_image.link_file;
	image_opacity = other_image.image_opacity;
	image_layer = other_image.image_layer;
	levels = other_image.levels;
	decoded = other_image.decoded;
}

void ImageObject::transform(const QTransform& t)
{
	if (t.isIdentity())
		return;
	for (auto& coord : coords)
		coord = MapCoord(t.map(MapCoordF(coord)));
	setOutputDirty();
}

bool ImageObject::intersectsBox(const QRectF& box) const
{
	return !quad().intersected(QPolygonF(box)).isEmpty();
}


void ImageObject::setData(const QByteArray& bytes, const QString& file_name)
{
	image_data = bytes;
	image_href = file_name;
	link_file.clear();
	levels = {};
	decoded = false;
	setOutputDirty();
}

void ImageObject::setLink(const QString& absolute_path)
{
	image_data.clear();
	image_href.clear();
	link_file = absolute_path;
	levels = {};
	decoded = false;
	setOutputDirty();
}

bool ImageObject::embed()
{
	if (!isLinked())
		return false;
	auto const bytes = readFile(link_file);
	if (bytes.isEmpty() || formatOf(bytes).isEmpty())
		return false;
	setData(bytes, QFileInfo(link_file).fileName());
	return true;
}

QString ImageObject::format() const
{
	return formatOf(isLinked() ? readFile(link_file) : image_data);
}

const QImage& ImageObject::image() const
{
	if (!decoded)
	{
		levels[0] = QImage::fromData(isLinked() ? readFile(link_file) : image_data);
		decoded = true;
	}
	return levels[0];
}

const QImage& ImageObject::level(int n) const
{
	auto const& full = image();
	if (n <= 0 || full.isNull())
		return full;
	auto const index = std::size_t(n);
	if (levels[index].isNull())
	{
		auto const& larger = level(n - 1);
		levels[index] = larger.scaled(qMax(1, larger.width() / 2), qMax(1, larger.height() / 2), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
	}
	return levels[index];
}


void ImageObject::setCorners(const MapCoordF& centre, const QSizeF& size_mm, qreal rotation)
{
	// As Object::rotate(): counter-clockwise on a map whose y axis points down.
	auto const sin_angle = std::sin(rotation);
	auto const cos_angle = std::cos(rotation);
	auto const w = size_mm.width() / 2;
	auto const h = size_mm.height() / 2;
	const QPointF corners[4] = { { -w, -h }, { w, -h }, { w, h }, { -w, h } };
	for (std::size_t i = 0; i < 4; ++i)
	{
		coords[i] = MapCoord(centre.x() + cos_angle * corners[i].x() + sin_angle * corners[i].y(),
		                     centre.y() - sin_angle * corners[i].x() + cos_angle * corners[i].y());
	}
	setOutputDirty();
}

MapCoordF ImageObject::anchor() const
{
	return MapCoordF((QPointF(MapCoordF(coords[0])) + QPointF(MapCoordF(coords[2]))) / 2);
}

QSizeF ImageObject::size() const
{
	return { MapCoordF(coords[0]).distanceTo(MapCoordF(coords[1])),
	         MapCoordF(coords[0]).distanceTo(MapCoordF(coords[3])) };
}

qreal ImageObject::imageRotation() const
{
	auto const top = MapCoordF(coords[1]) - MapCoordF(coords[0]);
	return std::atan2(-top.y(), top.x());
}

QPolygonF ImageObject::quad() const
{
	QPolygonF polygon;
	polygon.reserve(4);
	for (auto const& coord : coords)
		polygon.append(MapCoordF(coord));
	return polygon;
}

bool ImageObject::containsPoint(const MapCoordF& point) const
{
	return quad().containsPoint(point, Qt::OddEvenFill);
}

QTransform ImageObject::pixelToMap(const QSize& pixels) const
{
	auto const origin = MapCoordF(coords[0]);
	auto const top = MapCoordF(coords[1]) - origin;
	auto const left = MapCoordF(coords[3]) - origin;
	auto const w = qMax(1, pixels.width());
	auto const h = qMax(1, pixels.height());
	return { top.x() / w, top.y() / w, left.x() / h, left.y() / h, origin.x(), origin.y() };
}


void ImageObject::setOpacity(qreal opacity)
{
	image_opacity = qBound(0.0, opacity, 1.0);
	setOutputDirty();
}

void ImageObject::setLayer(Layer layer)
{
	image_layer = layer;
	setOutputDirty();
}

bool ImageObject::imageEquals(const ImageObject& other) const
{
	return image_data == other.image_data
	       && image_href == other.image_href
	       && link_file == other.link_file
	       && qAbs(image_opacity - other.image_opacity) < 0.000001
	       && image_layer == other.image_layer;
}


void ImageObject::draw(QPainter* painter, const RenderConfig& config) const
{
	auto const outline = quad();
	if (!outline.boundingRect().intersects(config.bounding_box))
		return;
	
	painter->save();
	painter->setOpacity(painter->opacity() * config.opacity * image_opacity);
	auto const& full = image();
	if (full.isNull())
	{
		// A missing or unreadable image: a frame with a diagonal.
		QPen pen(Qt::gray);
		pen.setCosmetic(true);
		painter->setPen(pen);
		painter->setBrush(Qt::NoBrush);
		painter->drawPolygon(outline);
		painter->drawLine(outline[0], outline[2]);
	}
	else
	{
		// Raster output of a much reduced image reads a halved copy.
		// PDF output keeps the original pixels.
		auto n = 0;
		auto const* engine = painter->paintEngine();
		if (!engine || engine->type() != QPaintEngine::Pdf)
		{
			auto shown = config.scaling * size().width() / full.width();  // device pixels per image pixel
			while (n < 3 && shown < 0.5)
			{
				++n;
				shown *= 2;
			}
		}
		auto const& pixels = level(n);
		painter->setRenderHint(QPainter::SmoothPixmapTransform, !config.testFlag(RenderConfig::DisableAntialiasing));
		painter->setTransform(pixelToMap(pixels.size()), true);
		painter->drawImage(QPointF(0, 0), pixels);
	}
	painter->restore();
}


void ImageObject::saveImage(QXmlStreamWriter& xml) const
{
	XmlElementWriter element(xml, QLatin1String("image"));
	element.writeAttribute(QLatin1String("format"), format());
	element.writeAttribute(QLatin1String("layer"), image_layer == BelowMap ? "below" : "above");
	element.writeAttribute(QLatin1String("opacity"), image_opacity);
	if (isLinked())
	{
		auto const link = file_directory.isEmpty() ? link_file : QDir(file_directory).relativeFilePath(link_file);
		element.writeAttribute(QLatin1String("href"), link);
	}
	else
	{
		if (!image_href.isEmpty())
			element.writeAttribute(QLatin1String("href"), image_href);
		xml.writeTextElement(QLatin1String("data"), QString::fromLatin1(image_data.toBase64()));
	}
}

void ImageObject::loadImage(QXmlStreamReader& xml)
{
	XmlElementReader element(xml);
	image_layer = element.attribute<QString>(QLatin1String("layer")) == QLatin1String("below") ? BelowMap : AboveMap;
	image_opacity = element.hasAttribute(QLatin1String("opacity")) ? qBound(0.0, element.attribute<double>(QLatin1String("opacity")), 1.0) : 1.0;
	image_href = element.attribute<QString>(QLatin1String("href"));
	image_data.clear();
	link_file.clear();
	while (xml.readNextStartElement())
	{
		if (xml.name() == QLatin1String("data"))
			image_data = QByteArray::fromBase64(xml.readElementText().toLatin1());
		else
			xml.skipCurrentElement();  // unknown
	}
	if (image_data.isEmpty() && !image_href.isEmpty())
	{
		link_file = QDir::cleanPath(file_directory.isEmpty() ? image_href : QDir(file_directory).absoluteFilePath(image_href));
		image_href.clear();
	}
	levels = {};
	decoded = false;
}


ImageObject* addImage(Map& map, const QString& path, bool embed, const MapCoordF& centre, const QSizeF& max_size_mm, QString* error)
{
	auto const bytes = readFile(path);
	auto const image = QImage::fromData(bytes);
	if (formatOf(bytes).isEmpty() || image.isNull())
	{
		if (error)
			*error = tr("Cannot read a PNG or JPEG image from %1.").arg(path);
		return nullptr;
	}
	
	auto const dots_per_mm_x = image.dotsPerMeterX() > 0 ? image.dotsPerMeterX() / 1000.0 : 96 / 25.4;
	auto const dots_per_mm_y = image.dotsPerMeterY() > 0 ? image.dotsPerMeterY() / 1000.0 : 96 / 25.4;
	QSizeF size(image.width() / dots_per_mm_x, image.height() / dots_per_mm_y);
	if (max_size_mm.isValid() && (size.width() > max_size_mm.width() || size.height() > max_size_mm.height()))
		size.scale(max_size_mm, Qt::KeepAspectRatio);
	
	auto* object = new ImageObject();
	if (embed)
		object->setData(bytes, QFileInfo(path).fileName());
	else
		object->setLink(QFileInfo(path).absoluteFilePath());
	object->setCorners(centre, size);
	
	auto const index = map.addObject(object);
	auto* undo = new DeleteObjectsUndoStep(&map);
	undo->addObject(index);
	map.push(undo);
	map.setObjectsDirty();
	map.clearObjectSelection(false);
	map.addObjectToSelection(object, true);
	return object;
}

int embedSelectedImages(Map& map)
{
	auto* part = map.getCurrentPart();
	auto* undo = new ReplaceObjectsUndoStep(&map);
	auto count = 0;
	for (auto* object : map.selectedObjects())
	{
		if (object->getType() != Object::Image)
			continue;
		auto* image = static_cast<ImageObject*>(object);
		if (!image->isLinked())
			continue;
		auto* before = image->duplicate();
		if (!image->embed())
		{
			delete before;
			continue;
		}
		undo->addObject(part->findObjectIndex(object), before);
		image->update();
		++count;
	}
	if (count == 0)
	{
		delete undo;
		return 0;
	}
	map.push(undo);
	map.setObjectsDirty();
	map.emitSelectionEdited();
	return count;
}

bool isLinkedImageSelected(const Map& map)
{
	for (auto const* object : map.selectedObjects())
	{
		if (object->getType() == Object::Image && static_cast<const ImageObject*>(object)->isLinked())
			return true;
	}
	return false;
}


}  // namespace OpenOrienteering
