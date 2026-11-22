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


#include "field_copy.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <memory>
#include <vector>

#include <QtGlobal>
#include <QBuffer>
#include <QColor>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QImage>
#include <QPainter>
#include <QTransform>
#include <QLatin1Char>
#include <QLatin1String>
#include <QPointF>
#include <QUuid>

#include "core/georeferencing.h"
#include "core/map.h"
#include "core/map_coord.h"
#include "core/map_part.h"
#include "core/objects/object.h"
#include "core/symbols/symbol.h"
#include "sync/merge.h"
#include "util/transformation.h"
#include "fileformats/file_import_export.h"
#include "fileformats/xml_file_format.h"
#include "templates/template.h"
#include "templates/template_image.h"
#include "undo/undo_manager.h"


namespace OpenOrienteering {

namespace {

QString tr(const char* text)
{
	return QCoreApplication::translate("OpenOrienteering::Merge", text);
}

bool fail(QString* error, const QString& message)
{
	if (error)
		*error = message;
	return false;
}

bool copyFile(const QString& from, const QString& to, QString* error)
{
	if (QFile::exists(to) && !QFile::remove(to))
		return fail(error, tr("Cannot replace %1.").arg(to));
	if (!QFile::copy(from, to))
		return fail(error, tr("Cannot write %1.").arg(to));
	return true;
}

bool sameFrame(const Map& mine, const Map& theirs)
{
	auto const& a = mine.getGeoreferencing();
	auto const& b = theirs.getGeoreferencing();
	return mine.getScaleDenominator() == theirs.getScaleDenominator()
	       && a.getState() == b.getState()
	       && a.getProjectedCRSSpec() == b.getProjectedCRSSpec()
	       && a.getMapRefPoint() == b.getMapRefPoint()
	       && QPointF(a.getProjectedRefPoint() - b.getProjectedRefPoint()).manhattanLength() < 0.001
	       && qAbs(a.getGrivation() - b.getGrivation()) < 0.005
	       && qAbs(a.getDeclination() - b.getDeclination()) < 0.005;
}

bool geospatial(const Map& map)
{
	return map.getGeoreferencing().getState() == Georeferencing::Geospatial;
}

/** The transformation from the frame of other into that of target, as in TemplateMap::calculateTransformation(). */
bool frameTransform(const Map& target, const Map& other, QTransform& transform)
{
	auto const& georef = other.getGeoreferencing();
	auto const origin = MapCoordF { georef.getMapRefPoint() };
	bool ok0, ok1, ok2;
	PassPointList passpoints;
	passpoints.resize(3);
	passpoints[0].src_coords = origin;
	passpoints[0].dest_coords = target.getGeoreferencing().toMapCoordF(&georef, passpoints[0].src_coords, &ok0);
	passpoints[1].src_coords = origin + MapCoordF { 128.0, 0.0 };
	passpoints[1].dest_coords = target.getGeoreferencing().toMapCoordF(&georef, passpoints[1].src_coords, &ok1);
	passpoints[2].src_coords = origin + MapCoordF { 0.0, 128.0 };
	passpoints[2].dest_coords = target.getGeoreferencing().toMapCoordF(&georef, passpoints[2].src_coords, &ok2);
	return ok0 && ok1 && ok2 && passpoints.estimateNonIsometricSimilarityTransform(&transform);
}

/** Transforms every object, and turns the rotation of those which have one. */
void transformObjects(Map& map, const QTransform& transform)
{
	for (int p = 0; p < map.getNumParts(); ++p)
	{
		auto* part = map.getPart(std::size_t(p));
		for (int i = 0; i < part->getNumObjects(); ++i)
		{
			auto* object = part->getObject(i);
			auto const rotation = object->getRotation();
			object->transform(transform);
			auto const* symbol = object->getSymbol();
			if (symbol && (symbol->isRotatable() || symbol->hasRotatableFillPattern()))
			{
				// A rotation by r turns (1, 0) into (cos r, -sin r).
				auto const direction = transform.map(QPointF(std::cos(rotation), -std::sin(rotation))) - transform.map(QPointF(0.0, 0.0));
				object->setRotation(std::atan2(-direction.y(), direction.x()));
			}
		}
	}
}

/** Gives every object which is the same as in the reference within 0.01 mm the reference's state. */
void snapTo(Map& map, const Map& reference)
{
	for (int p = 0; p < map.getNumParts(); ++p)
	{
		auto* part = map.getPart(std::size_t(p));
		for (int i = 0; i < part->getNumObjects(); ++i)
		{
			auto* object = part->getObject(i);
			auto const* own = reference.findObject(object->uuid());
			if (!own || !sameContent(*object, *own, 0.01))
				continue;
			auto* symbol = object->getSymbol();
			auto const group = object->group();
			object->copyFrom(*own);
			object->setSymbol(symbol, true);
			object->setGroup(group);
		}
	}
}

/** Writes the map to the copy's file with the options applied. The map itself is left as it is. */
bool writeCopy(const Map& map, const QString& copy_path, const FieldCopyOptions& options, QString* error)
{
	// The copy is the map read back from its own file format.
	XMLFileFormat format;
	QBuffer buffer;
	buffer.open(QIODevice::ReadWrite);
	auto exporter = format.makeExporter({}, &map, nullptr);
	exporter->setDevice(&buffer);
	if (!exporter->doExport())
		return fail(error, tr("Cannot write %1.").arg(copy_path));
	buffer.seek(0);
	Map copy;
	auto importer = format.makeImporter({}, &copy, nullptr);
	importer->setDevice(&buffer);
	if (!importer->doImport())
		return fail(error, tr("Cannot write %1.").arg(copy_path));
	// Undoing in the copy must not bring back what is left out.
	copy.undoManager().clear();
	
	if (!options.extent.isNull())
	{
		for (int p = 0; p < copy.getNumParts(); ++p)
		{
			auto* part = copy.getPart(std::size_t(p));
			for (int i = part->getNumObjects() - 1; i >= 0; --i)
			{
				auto* object = part->getObject(i);
				object->update();
				if (!object->getExtent().intersects(options.extent))
					part->deleteObject(i);
			}
		}
	}
	
	if (options.template_dpi > 0)
	{
		auto const copy_info = QFileInfo(copy_path);
		auto folder = copy_info.absolutePath();
		folder += QLatin1Char('/');
		folder += copy_info.completeBaseName();
		folder += QLatin1String(".templates");
		auto const pixels_per_mm = options.template_dpi / 25.4;
		for (int i = 0; i < copy.getNumTemplates() && i < map.getNumTemplates(); ++i)
		{
			auto* original = const_cast<Template*>(map.getTemplate(i));  // loaded for drawing, and unloaded again
			auto const was_loaded = original->getTemplateState() == Template::Loaded;
			if (!was_loaded && !original->loadTemplateFile())
				continue;  // The copy refers to it as it is.
			auto region = original->calculateTemplateBoundingBox();
			if (!options.extent.isNull())
				region = region.intersected(options.extent);
			auto const width = int(std::ceil(region.width() * pixels_per_mm - 0.001));
			auto const height = int(std::ceil(region.height() * pixels_per_mm - 0.001));
			if (width <= 0 || height <= 0 || qint64(width) * height > 400000000)
			{
				if (!was_loaded)
					original->unloadTemplateFile();
				if (width > 0 && height > 0)
					return fail(error, tr("The template %1 is too large for an image at %2 dpi.").arg(original->getTemplateFilename()).arg(options.template_dpi));
				continue;
			}
			QImage image(width, height, QImage::Format_ARGB32_Premultiplied);
			image.fill(QColor(Qt::transparent));
			{
				QPainter painter(&image);
				painter.scale(width / region.width(), height / region.height());
				painter.translate(-region.topLeft());
				original->drawTemplate(&painter, region, pixels_per_mm, false, 1.0);
			}
			if (!was_loaded)
				original->unloadTemplateFile();
			
			auto path = folder;
			path += QLatin1Char('/');
			path += QString::number(i + 1);
			path += QLatin1String(".png");
			if (!QDir().mkpath(folder) || !image.save(path))
				return fail(error, tr("Cannot write %1.").arg(path));
			auto cached = std::make_unique<TemplateImage>(path, &copy);
			cached->setTemplatePosition(MapCoord(region.center()));
			cached->setTemplateScaleX(region.width() / width);
			cached->setTemplateScaleY(region.height() / height);
			cached->setTemplateShear(0);
			cached->setTemplateRotation(0);
			cached->setTemplateState(Template::Unloaded);
			copy.setTemplate(i, std::move(cached));
		}
	}
	
	if (options.review)
	{
		auto sketch = -1;
		for (int p = 0; p < copy.getNumParts() && sketch < 0; ++p)
		{
			if (copy.getPart(std::size_t(p))->role() == MapPart::Role::Sketch)
				sketch = p;
		}
		if (sketch < 0)
		{
			auto* part = new MapPart(tr("Sketch"), &copy);
			part->setRole(MapPart::Role::Sketch);
			sketch = copy.getNumParts();
			copy.addPart(part, std::size_t(sketch));
		}
		copy.setCurrentPartIndex(std::size_t(sketch));
	}
	
	auto writer = format.makeExporter(copy_path, &copy, nullptr);
	if (!writer || !writer->doExport())
		return fail(error, tr("Cannot write %1.").arg(copy_path));
	return true;
}

}  // namespace


QString baseFilePath(const QString& map_path, const QString& id)
{
	const QFileInfo info(map_path);
	QString path = info.path();
	path += QLatin1Char('/');
	path += info.completeBaseName();
	path += QLatin1String(".base-");
	path += id;
	path += QLatin1String(".xmap");
	return path;
}

QString createFieldCopy(Map& map, const QString& map_path, const QString& copy_path, QString* error)
{
	return createFieldCopy(map, map_path, copy_path, FieldCopyOptions{}, error);
}

QString createFieldCopy(Map& map, const QString& map_path, const QString& copy_path, const FieldCopyOptions& options, QString* error)
{
	if (map_path.isEmpty())
	{
		fail(error, tr("The map has no file yet. Save it first."));
		return {};
	}
	if (QFileInfo(copy_path) == QFileInfo(map_path))
	{
		fail(error, tr("The copy needs a file of its own."));
		return {};
	}
	
	SyncCopy record;
	record.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
	record.created = QDateTime::currentSecsSinceEpoch();
	auto const base_path = baseFilePath(map_path, record.id);
	record.base = QFileInfo(base_path).fileName();
	record.extent = options.extent;
	record.review = options.review;
	
	// The copy lists its own id only.
	auto const open_copies = map.syncCopies();
	auto own_record = record;
	own_record.base.clear();
	map.setSyncCopies({ own_record });
	auto written = false;
	QString problem = tr("Cannot write %1.").arg(copy_path);
	if (options.isDefault())
	{
		XMLFileFormat format;
		auto exporter = format.makeExporter(copy_path, &map, nullptr);
		written = exporter && exporter->doExport();
	}
	else
	{
		written = writeCopy(map, copy_path, options, &problem);
	}
	map.setSyncCopies(open_copies);
	if (!written)
	{
		fail(error, problem);
		return {};
	}
	if (!copyFile(copy_path, base_path, error))
		return {};
	
	auto copies = open_copies;
	copies.push_back(record);
	map.setSyncCopies(copies);
	map.setHasUnsavedChanges(true);
	return record.id;
}

bool closeFieldCopy(Map& map, const QString& map_path, const QString& id, QString* error)
{
	auto copies = map.syncCopies();
	if (copies.empty())
		return fail(error, tr("The map has no open field copy."));
	if (id.isEmpty() && copies.size() > 1)
		return fail(error, tr("The map has several open field copies. Name the one to close."));
	auto const found = id.isEmpty() ? copies.begin()
	                                : std::find_if(copies.begin(), copies.end(), [&id](const SyncCopy& copy) { return copy.id == id; });
	if (found == copies.end())
		return fail(error, tr("The map has no open field copy %1.").arg(id));
	
	if (!map_path.isEmpty())
		QFile::remove(baseFilePath(map_path, found->id));
	copies.erase(found);
	map.setSyncCopies(copies);
	map.setHasUnsavedChanges(true);
	return true;
}

bool loadForMerge(const Map& mine, const QString& map_path, const QString& copy_path, MergeInput& input, QString* error)
{
	return loadForMerge(mine, map_path, copy_path, input, error, false);
}

bool loadForMerge(const Map& mine, const QString& map_path, const QString& copy_path, MergeInput& input, QString* error, bool transform)
{
	input = {};
	auto theirs = std::make_unique<Map>();
	if (!theirs->loadFrom(copy_path))
		return fail(error, tr("Cannot read %1.").arg(copy_path));
	
	auto const& open_copies = mine.syncCopies();
	auto const found = std::find_if(open_copies.begin(), open_copies.end(), [&theirs](const SyncCopy& copy) {
		auto const& listed = theirs->syncCopies();
		return std::any_of(listed.begin(), listed.end(), [&copy](const SyncCopy& other) { return other.id == copy.id; });
	});
	if (found == open_copies.end())
		return fail(error, tr("%1 is not a field copy of this map: the sync ids do not match.").arg(copy_path));
	if (!sameFrame(mine, *theirs))
	{
		if (mine.getScaleDenominator() != theirs->getScaleDenominator() && !(geospatial(mine) && geospatial(*theirs)))
			return fail(error, tr("The copy has another scale than this map. Its objects can be placed in this map only when both maps are georeferenced."));
		if (!transform)
		{
			input.other_frame = true;
			return fail(error, tr("The copy has another georeferencing than this map. Its objects can be transformed into the frame of this map for the merge."));
		}
		QTransform to_mine;
		if (!frameTransform(mine, *theirs, to_mine))
			return fail(error, tr("The objects of the copy cannot be transformed into the frame of this map."));
		transformObjects(*theirs, to_mine);
		input.transformed = true;
	}
	
	input.copy = *found;
	input.theirs = std::move(theirs);
	auto const base_path = baseFilePath(map_path, found->id);
	if (!map_path.isEmpty() && QFile::exists(base_path))
	{
		auto base = std::make_unique<Map>();
		if (base->loadFrom(base_path))
		{
			QTransform to_mine;
			if (sameFrame(mine, *base))
			{
				input.base = std::move(base);
			}
			else if (frameTransform(mine, *base, to_mine))
			{
				transformObjects(*base, to_mine);
				snapTo(*base, mine);
				input.base = std::move(base);
			}
		}
	}
	if (input.transformed)
		snapTo(*input.theirs, input.base ? *input.base : mine);
	return true;
}

bool refreshBase(const QString& map_path, const QString& copy_path, const SyncCopy& copy, QString* error)
{
	if (map_path.isEmpty())
		return fail(error, tr("The map has no file yet. Save it first."));
	return copyFile(copy_path, baseFilePath(map_path, copy.id), error);
}


}  // namespace OpenOrienteering
