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


#include "image_object.h"

#include "core/print_output.h"

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
#include <QPainterPath>
#include <QPen>
#include <QPointF>
#include <QStringList>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

#include "core/map.h"
#include "core/map_color.h"
#include "core/map_part.h"
#include "core/objects/object.h"
#include "core/path_coord.h"
#include "core/renderables/renderable.h"
#include "core/symbols/image_symbol.h"
#include "undo/object_undo.h"
#include "undo/undo.h"
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

/** The smallest length of a side of an image, in mm. */
constexpr double min_image_side = 0.1;

/** The corners as factors of the top and left sides from the top left corner. */
constexpr int corner_u[4] = { 0, 1, 1, 0 };
constexpr int corner_v[4] = { 0, 0, 1, 1 };

QPainterPath clipPath(const std::vector<QPolygonF>& rings, qreal width, qreal height)
{
	QPainterPath path;
	path.setFillRule(Qt::OddEvenFill);
	for (auto const& ring : rings)
	{
		QPolygonF scaled;
		scaled.reserve(ring.size());
		for (auto const& point : ring)
			scaled.append(QPointF(point.x() * width, point.y() * height));
		path.addPolygon(scaled);
		path.closeSubpath();
	}
	return path;
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
, image_clip(proto.image_clip)
, spot_color_name(proto.spot_color_name)
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
	image_clip = other_image.image_clip;
	spot_color_name = other_image.spot_color_name;
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

void ImageObject::setCorners(const std::array<MapCoordF, 4>& corners)
{
	for (std::size_t i = 0; i < 4; ++i)
		coords[i] = MapCoord(corners[i]);
	setOutputDirty();
}

std::array<MapCoordF, 4> ImageObject::corners() const
{
	return { MapCoordF(coords[0]), MapCoordF(coords[1]), MapCoordF(coords[2]), MapCoordF(coords[3]) };
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
	if (!quad().containsPoint(point, Qt::OddEvenFill))
		return false;
	if (image_clip.empty())
		return true;
	return clipPath(image_clip, 1, 1).contains(imageFromMap(point));
}

MapCoordF ImageObject::mapFromImage(const QPointF& point) const
{
	return MapCoordF(pixelToMap(QSize(1, 1)).map(point));
}

QPointF ImageObject::imageFromMap(const MapCoordF& point) const
{
	bool invertible = false;
	auto const inverse = pixelToMap(QSize(1, 1)).inverted(&invertible);
	return invertible ? inverse.map(QPointF(point)) : QPointF(-1, -1);
}

void ImageObject::setClip(const std::vector<QPolygonF>& rings)
{
	image_clip = rings;
	setOutputDirty();
}

void ImageObject::setSpotColorName(const QString& name)
{
	spot_color_name = name;
	setOutputDirty();
}

const MapColor* ImageObject::spotColor() const
{
	auto const* owner = getMap();
	if (spot_color_name.isEmpty() || !owner)
		return nullptr;
	for (int i = 0; i < owner->getNumColors(); ++i)
	{
		auto const* color = owner->getColor(i);
		if (color->getSpotColorMethod() == MapColor::SpotColor && color->getSpotColorName() == spot_color_name)
			return color;
	}
	return nullptr;
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
	       && image_layer == other.image_layer
	       && image_clip == other.image_clip
	       && spot_color_name == other.spot_color_name;
}


void ImageObject::draw(QPainter* painter, const RenderConfig& config) const
{
	if (auto const* spot = spotColor())
	{
		// Printed on the spot colour's plate alone: on no process plate.
		if (config.process_channel < 0)
			drawPixels(painter, config, 2, static_cast<const QColor&>(*spot));
		return;
	}
	drawPixels(painter, config, config.process_channel >= 0 ? 1 : 0, {});
}

void ImageObject::drawSeparation(QPainter* painter, const RenderConfig& config, bool use_color) const
{
	auto const* spot = spotColor();
	if (!spot)
		return;
	drawPixels(painter, config, 2, use_color ? static_cast<const QColor&>(*spot) : QColor(Qt::black));
}

void ImageObject::drawPixels(QPainter* painter, const RenderConfig& config, int mode, const QColor& ink_color) const
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
		if (!image_clip.empty())
			painter->setClipPath(clipPath(image_clip, pixels.width(), pixels.height()), Qt::IntersectClip);
		switch (mode)
		{
		case 1:
			painter->drawImage(QPointF(0, 0), processPlateOfImage(pixels, config.process_channel));  // one process plate, in grey
			break;
		case 2:
			painter->drawImage(QPointF(0, 0), spotInkImage(pixels, ink_color));  // the ink of a spot colour
			break;
		default:
			painter->drawImage(QPointF(0, 0), pixels);
		}
	}
	painter->restore();
}


void ImageObject::saveImage(QXmlStreamWriter& xml) const
{
	XmlElementWriter element(xml, QLatin1String("image"));
	element.writeAttribute(QLatin1String("format"), format());
	element.writeAttribute(QLatin1String("layer"), image_layer == BelowMap ? "below" : "above");
	element.writeAttribute(QLatin1String("opacity"), image_opacity);
	if (!spot_color_name.isEmpty())
		element.writeAttribute(QLatin1String("spot_color"), spot_color_name);
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
	if (!image_clip.empty())
	{
		XmlElementWriter clip_element(xml, QLatin1String("clip"));
		for (auto const& ring : image_clip)
		{
			QStringList values;
			values.reserve(2 * ring.size());
			for (auto const& point : ring)
			{
				values.append(QString::number(point.x(), 'f', 6));
				values.append(QString::number(point.y(), 'f', 6));
			}
			xml.writeTextElement(QLatin1String("ring"), values.join(QLatin1Char(' ')));
		}
	}
}

void ImageObject::loadImage(QXmlStreamReader& xml)
{
	XmlElementReader element(xml);
	image_layer = element.attribute<QString>(QLatin1String("layer")) == QLatin1String("below") ? BelowMap : AboveMap;
	image_opacity = element.hasAttribute(QLatin1String("opacity")) ? qBound(0.0, element.attribute<double>(QLatin1String("opacity")), 1.0) : 1.0;
	image_href = element.attribute<QString>(QLatin1String("href"));
	spot_color_name = element.attribute<QString>(QLatin1String("spot_color"));
	image_data.clear();
	link_file.clear();
	image_clip.clear();
	while (xml.readNextStartElement())
	{
		if (xml.name() == QLatin1String("data"))
			image_data = QByteArray::fromBase64(xml.readElementText().toLatin1());
		else if (xml.name() == QLatin1String("clip"))
		{
			while (xml.readNextStartElement())
			{
				if (xml.name() != QLatin1String("ring"))
				{
					xml.skipCurrentElement();
					continue;
				}
				auto const values = xml.readElementText().split(QLatin1Char(' '), Qt::SkipEmptyParts);
				QPolygonF ring;
				for (int i = 0; i + 1 < values.size(); i += 2)
					ring.append(QPointF(values[i].toDouble(), values[i + 1].toDouble()));
				if (ring.size() >= 3)
					image_clip.push_back(ring);
			}
		}
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


std::array<MapCoordF, 4> imageCornersDraggedTo(const std::array<MapCoordF, 4>& corners, int corner, const MapCoordF& target, bool keep_aspect)
{
	auto const c = std::size_t(qBound(0, corner, 3));
	auto const o = (c + 2) % 4;
	auto const origin = corners[o];
	auto const e1 = corners[1] - corners[0];  // along the top side
	auto const e2 = corners[3] - corners[0];  // along the left side
	auto const length1 = e1.length();
	auto const length2 = e2.length();
	auto const det = e1.x() * e2.y() - e1.y() * e2.x();
	if (length1 <= 0 || length2 <= 0 || qAbs(det) < 1e-12)
		return corners;

	// The dragged corner relative to the fixed one, in sides: +1 or -1.
	auto const s1 = corner_u[c] - corner_u[o];
	auto const s2 = corner_v[c] - corner_v[o];

	auto const offset = target - origin;
	double f1, f2;
	if (keep_aspect)
	{
		auto const diagonal = corners[c] - origin;
		auto const f = (offset.x() * diagonal.x() + offset.y() * diagonal.y()) / (diagonal.x() * diagonal.x() + diagonal.y() * diagonal.y());
		f1 = f2 = qMax(f, qMax(min_image_side / length1, min_image_side / length2));
	}
	else
	{
		// Solve offset = a * e1 + b * e2.
		auto const a = (offset.x() * e2.y() - offset.y() * e2.x()) / det;
		auto const b = (e1.x() * offset.y() - e1.y() * offset.x()) / det;
		f1 = qMax(a / s1, min_image_side / length1);
		f2 = qMax(b / s2, min_image_side / length2);
	}

	std::array<MapCoordF, 4> result;
	for (std::size_t i = 0; i < 4; ++i)
	{
		auto const a = f1 * (corner_u[i] - corner_u[o]);
		auto const b = f2 * (corner_v[i] - corner_v[o]);
		result[i] = MapCoordF(origin.x() + a * e1.x() + b * e2.x(), origin.y() + a * e1.y() + b * e2.y());
	}
	return result;
}

QImage spotInkImage(const QImage& image, const QColor& color)
{
	auto result = image.convertToFormat(QImage::Format_ARGB32);
	auto const rgb = color.rgb() & 0xffffff;
	for (int y = 0; y < result.height(); ++y)
	{
		auto* line = reinterpret_cast<QRgb*>(result.scanLine(y));
		for (int x = 0; x < result.width(); ++x)
		{
			auto const pixel = line[x];
			auto const ink = (255 - qGray(pixel)) * qAlpha(pixel) / 255;
			line[x] = (QRgb(ink) << 24) | rgb;
		}
	}
	return result;
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

const ImageObject* firstSelectedImage(const Map& map)
{
	for (auto const* object : map.selectedObjects())
	{
		if (object->getType() == Object::Image)
			return static_cast<const ImageObject*>(object);
	}
	return nullptr;
}

namespace {

int setImageProperties(Map& map, qreal opacity, ImageObject::Layer layer, const QString* spot_color_name)
{
	opacity = qBound(0.0, opacity, 1.0);
	auto* part = map.getCurrentPart();
	auto* undo = new ReplaceObjectsUndoStep(&map);
	auto count = 0;
	for (auto* object : map.selectedObjects())
	{
		if (object->getType() != Object::Image)
			continue;
		auto* image = static_cast<ImageObject*>(object);
		if (qAbs(image->opacity() - opacity) < 0.0005 && image->layer() == layer
		    && (!spot_color_name || image->spotColorName() == *spot_color_name))
			continue;
		undo->addObject(part->findObjectIndex(object), image->duplicate());
		image->setOpacity(opacity);
		image->setLayer(layer);
		if (spot_color_name)
			image->setSpotColorName(*spot_color_name);
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

}  // namespace

int setSelectedImageProperties(Map& map, qreal opacity, ImageObject::Layer layer)
{
	return setImageProperties(map, opacity, layer, nullptr);
}

int setSelectedImageProperties(Map& map, qreal opacity, ImageObject::Layer layer, const QString& spot_color_name)
{
	return setImageProperties(map, opacity, layer, &spot_color_name);
}

namespace {

bool hasClosedPart(const Object* object)
{
	if (object->getType() != Object::Path)
		return false;
	for (auto const& part : object->asPath()->parts())
	{
		if (part.isClosed())
			return true;
	}
	return false;
}

}  // namespace

bool canClipSelectedImages(const Map& map)
{
	auto images = false;
	auto paths = false;
	for (auto const* object : map.selectedObjects())
	{
		images |= object->getType() == Object::Image;
		paths |= hasClosedPart(object);
	}
	return images && paths;
}

int clipSelectedImages(Map& map)
{
	if (!canClipSelectedImages(map))
		return 0;

	// The outline in map coordinates, curves flattened as drawn.
	std::vector<QPolygonF> outline;
	std::vector<Object*> paths;
	std::vector<ImageObject*> images;
	for (auto* object : map.selectedObjects())
	{
		if (object->getType() == Object::Image)
		{
			images.push_back(static_cast<ImageObject*>(object));
		}
		else if (hasClosedPart(object))
		{
			auto* path = object->asPath();
			path->update();
			for (auto const& part : path->parts())
			{
				if (!part.isClosed())
					continue;
				QPolygonF ring;
				for (auto const& path_coord : part.path_coords)
					ring.append(QPointF(path_coord.pos));
				if (ring.size() >= 3)
					outline.push_back(ring);
			}
			paths.push_back(object);
		}
	}
	if (outline.empty())
		return 0;

	auto* part = map.getCurrentPart();
	auto* replace = new ReplaceObjectsUndoStep(&map);
	for (auto* image : images)
	{
		replace->addObject(part->findObjectIndex(image), image->duplicate());
		std::vector<QPolygonF> rings;
		rings.reserve(outline.size());
		for (auto const& ring : outline)
		{
			QPolygonF image_ring;
			image_ring.reserve(ring.size());
			for (auto const& point : ring)
				image_ring.append(image->imageFromMap(MapCoordF(point)));
			rings.push_back(image_ring);
		}
		image->setClip(rings);
		image->update();
	}

	auto* add = new AddObjectsUndoStep(&map);
	for (auto* path : paths)
		add->addObject(part->findObjectIndex(path), path);
	for (auto* path : paths)
	{
		map.removeObjectFromSelection(path, false);
		map.releaseObject(path);
	}

	auto* undo = new CombinedUndoStep(&map);
	undo->push(replace);
	undo->push(add);
	map.push(undo);
	map.setObjectsDirty();
	map.emitSelectionChanged();
	map.emitSelectionEdited();
	return int(images.size());
}

bool isClippedImageSelected(const Map& map)
{
	for (auto const* object : map.selectedObjects())
	{
		if (object->getType() == Object::Image && !static_cast<const ImageObject*>(object)->clip().empty())
			return true;
	}
	return false;
}

int unclipSelectedImages(Map& map)
{
	auto* part = map.getCurrentPart();
	auto* undo = new ReplaceObjectsUndoStep(&map);
	auto count = 0;
	for (auto* object : map.selectedObjects())
	{
		if (object->getType() != Object::Image)
			continue;
		auto* image = static_cast<ImageObject*>(object);
		if (image->clip().empty())
			continue;
		undo->addObject(part->findObjectIndex(object), image->duplicate());
		image->setClip({});
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
