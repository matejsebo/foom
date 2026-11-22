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



#include <algorithm>
#include <cstddef>
#include <iterator>
#include <memory>
#include <utility>
#include <vector>

#include <QBuffer>
#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QIODevice>
#include <QLatin1Char>
#include <QRectF>

#include "core/map.h"
#include "core/map_coord.h"
#include "core/map_part.h"
#include "core/objects/object.h"
#include "core/symbols/symbol.h"
#include "course/course_data.h"
#include "course/course_map.h"
#include "course/course_storage.h"
#include "fileformats/file_format.h"
#include "fileformats/file_format_registry.h"
#include "fileformats/file_import_export.h"

// the design notes, "Course maps as map files"

namespace OpenOrienteering {

namespace {

QString tr(const char* text)
{
	return QCoreApplication::translate("OpenOrienteering::CourseMap", text);
}

}  // namespace



QString courseMapFileSide(const CourseMapSheet& sheet, const CourseMapSide& side)
{
	if (side.variant.leg > 0)
		return sheet.name;
	if (side.part >= 0)
		return side.course + QLatin1Char('.') + QString::number(side.part + 1);
	return side.course;
}


namespace {

/**
 * Removes the objects which a course map draws itself, wherever they are, and
 * the objects of the overrides of all courses, which the drawing has used;
 * returns the part where the first drawn object was.
 */
int removeDrawnObjects(Map& map)
{
	auto part_index = int(map.getCurrentPartIndex());
	auto first = true;
	auto const overrides = findCourseOverridesPart(map);
	for (int p = map.getNumParts() - 1; p >= 0; --p)
	{
		auto* part = map.getPart(std::size_t(p));
		for (int i = part->getNumObjects() - 1; i >= 0; --i)
		{
			if (p == overrides)  // foom: seen on 2026-11-22, every course's numbers and lines in each file
			{
				part->deleteObject(i);
				continue;
			}
			auto const* symbol = part->getObject(i)->getSymbol();
			if (!symbol || !isDrawnByCourseMap(*symbol))
				continue;
			if (first)
				part_index = p;
			first = false;
			part->deleteObject(i);
		}
	}
	return part_index;
}

/**
 * Puts what a side draws into the map, moved by the offset, with the symbols
 * of the map; the scaled symbols of the description become symbols of the map.
 */
void insertDrawing(Map& map, CourseMapDrawing& drawing, int part_index, const MapCoord& offset)
{
	auto const symbolOf = [&map, &drawing](const Symbol* symbol) -> const Symbol* {
		for (std::size_t i = 0; i < drawing.symbols.size(); ++i)
		{
			if (drawing.symbols[i].get() == symbol)
				return drawing.symbol_sources[i];
		}
		for (auto& scaled : drawing.description.symbols)
		{
			if (scaled.get() == symbol)
			{
				scaled->setHidden(false);
				map.addSymbol(scaled.release(), map.getNumSymbols());
				return symbol;
			}
		}
		return symbol;
	};
	auto const insert = [&map, &symbolOf, part_index, &offset](std::vector<std::unique_ptr<Object>>& objects) {
		for (auto& object : objects)
		{
			auto const* symbol = symbolOf(object->getSymbol());
			auto const index = map.findSymbolIndex(symbol);
			if (index < 0)
				continue;
			// The map may have them hidden, for instance while the editor shows a preview.
			map.getSymbol(index)->setHidden(false);
			object->setSymbol(symbol, true);
			object->move(offset);
			map.addObject(object.release(), part_index);
		}
		objects.clear();
	};
	// The description first: its ground lies below the course.
	insert(drawing.description.objects);
	insert(drawing.objects);
}

/** The box of what a drawing has, in mm; invalid for nothing. */
QRectF extentOf(const CourseMapDrawing& drawing)
{
	QRectF box;
	for (auto const* objects : { &drawing.description.objects, &drawing.objects })
	{
		for (auto const& object : *objects)
		{
			object->update();
			if (object->getExtent().isValid())
				box = box.isValid() ? box.united(object->getExtent()) : object->getExtent();
		}
	}
	return box;
}

}  // namespace


bool makeCourseMapFile(Map& map, const CourseMapSide& side)
{
	auto drawing = drawCourseMap(map, map.courseData(), side);
	if (!drawing.drawn)
		return false;
	auto const part_index = removeDrawnObjects(map);
	insertDrawing(map, drawing, part_index, MapCoord{});
	// A course map is a plain map.
	map.setCourseData(CourseData{});
	return true;
}


// the design notes, "Several courses in one file"
std::vector<bool> makeCourseMapsFile(Map& map, const std::vector<CourseMapSide>& sides, CourseMapPlacing placing)
{
	std::vector<bool> drawn;
	std::vector<CourseMapDrawing> drawings;
	auto box = map.calculateExtent();
	for (auto const& side : sides)
	{
		auto drawing = drawCourseMap(map, map.courseData(), side);
		drawn.push_back(drawing.drawn);
		if (!drawing.drawn)
			continue;
		auto const extent = extentOf(drawing);
		if (extent.isValid())
			box = box.isValid() ? box.united(extent) : extent;
		drawings.push_back(std::move(drawing));
	}

	auto const part_index = removeDrawnObjects(map);
	// The copy k of the map lies k steps to the right or downwards.
	auto const gap = 10.0;
	auto const step = placing == CourseMapPlacing::Beside ? MapCoordF(box.width() + gap, 0.0)
	                : placing == CourseMapPlacing::Below ? MapCoordF(0.0, box.height() + gap)
	                : MapCoordF(0.0, 0.0);
	auto const repeated = placing == CourseMapPlacing::Beside || placing == CourseMapPlacing::Below;
	if (repeated && drawings.size() > 1)
	{
		for (int p = 0; p < map.getNumParts(); ++p)
		{
			auto* part = map.getPart(std::size_t(p));
			auto const count = part->getNumObjects();
			for (std::size_t k = 1; k < drawings.size(); ++k)
			{
				auto const offset = MapCoord(step * double(k));
				for (int i = 0; i < count; ++i)
				{
					auto* copy = part->getObject(i)->duplicate();
					copy->move(offset);
					map.addObject(copy, p);
				}
			}
		}
	}
	for (std::size_t k = 0; k < drawings.size(); ++k)
		insertDrawing(map, drawings[k], part_index, repeated ? MapCoord(step * double(k)) : MapCoord{});
	map.setCourseData(CourseData{});
	return drawn;
}


CourseMapExportReport exportCourseMapFiles(Map& map, const CourseMapExportOptions& options)
{
	CourseMapExportReport report;
	auto const* format = FileFormats.findFormat(options.map_format.toLatin1().constData());
	if (!format || !format->supportsFileSaveAs())
	{
		report.error = tr("Map files of the format \"%1\" cannot be written.").arg(options.map_format);
		return report;
	}
	auto const options_error = courseMapOptionsError(options);
	if (!options_error.isEmpty())
	{
		report.error = options_error;
		return report;
	}
	if (options.placing == CourseMapPlacing::Pages)
	{
		report.error = tr("A page for every map is for PDF files; map files place the maps beside or below each other, or at their place.");
		return report;
	}
	QDir folder(options.folder);
	if (options.folder.isEmpty() || !folder.mkpath(QStringLiteral(".")))
	{
		report.error = tr("Cannot write into the folder \"%1\".").arg(options.folder);
		return report;
	}
	
	// Every file begins as a copy of the map.
	QByteArray bytes;
	{
		QBuffer buffer(&bytes);
		if (!buffer.open(QIODevice::WriteOnly) || !map.exportToIODevice(buffer))
		{
			report.error = tr("Cannot copy the map.");
			return report;
		}
	}
	
	auto const write = [&](const QString& name, Map& copy) {
		auto const path = folder.absoluteFilePath(courseMapFileName(options.base_name, name, format->primaryExtension()));
		auto exporter = format->makeExporter(path, &copy, nullptr);
		if (!exporter || !exporter->doExport())
		{
			QFile::remove(path);
			report.error = tr("Cannot write the file \"%1\".").arg(path);
			if (exporter && !exporter->warnings().empty())
				report.error += QLatin1Char('\n') + exporter->warnings().back();
			return false;
		}
		report.files.push_back(path);
		return true;
	};
	auto const copyOfMap = [&](Map& copy) {
		QBuffer buffer(&bytes);
		if (!buffer.open(QIODevice::ReadOnly) || !copy.importFromIODevice(buffer))
		{
			report.error = tr("Cannot copy the map.");
			return false;
		}
		return true;
	};

	auto const& data = map.courseData();
	auto courses = options.courses;
	if (courses.isEmpty())
	{
		for (auto const& course : data.courses)
			courses.push_back(course.name);
	}
	// Every side with the name of its own file.
	std::vector<std::pair<QString, CourseMapSide>> sides;
	for (auto const& course : qAsConst(courses))
	{
		auto const sheets = courseMapSheets(data, course, options.relay_maps);
		if (sheets.empty())
			report.skipped.push_back(course);
		for (auto const& sheet : sheets)
		{
			for (auto const& side : sheet.sides)
				sides.emplace_back(courseMapFileSide(sheet, side), side);
		}
	}

	if (options.placing != CourseMapPlacing::Separate)
	{
		// the design notes, "Several courses in one file"
		if (sides.empty())
		{
			report.ok = true;
			return report;
		}
		std::vector<CourseMapSide> one;
		for (auto const& side : sides)
			one.push_back(side.second);
		Map copy;
		if (!copyOfMap(copy))
			return report;
		auto const drawn = makeCourseMapsFile(copy, one, options.placing);
		for (std::size_t i = 0; i < drawn.size(); ++i)
		{
			if (!drawn[i])
				report.skipped.push_back(sides[i].first);
		}
		if (std::find(drawn.begin(), drawn.end(), true) != drawn.end() && !write(courseMapsOneFileName(), copy))
			return report;
		report.ok = true;
		return report;
	}

	for (auto const& side : sides)
	{
		Map copy;
		if (!copyOfMap(copy))
			return report;
		if (!makeCourseMapFile(copy, side.second))
		{
			report.skipped.push_back(side.first);
			continue;
		}
		if (!write(side.first, copy))
			return report;
	}
	report.ok = true;
	return report;
}


}  // namespace OpenOrienteering
