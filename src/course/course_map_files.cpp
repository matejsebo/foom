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

#include "core/map.h"
#include "core/map_part.h"
#include "core/objects/object.h"
#include "core/symbols/symbol.h"
#include "course/course_data.h"
#include "course/course_map.h"
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


bool makeCourseMapFile(Map& map, const CourseMapSide& side)
{
	auto drawing = drawCourseMap(map, map.courseData(), side);
	if (!drawing.drawn)
		return false;
	
	// The objects which the side draws itself go, wherever they are.
	auto part_index = int(map.getCurrentPartIndex());
	auto first = true;
	for (int p = map.getNumParts() - 1; p >= 0; --p)
	{
		auto* part = map.getPart(std::size_t(p));
		for (int i = part->getNumObjects() - 1; i >= 0; --i)
		{
			auto const* symbol = part->getObject(i)->getSymbol();
			if (!symbol || !isDrawnByCourseMap(*symbol))
				continue;
			if (first)
				part_index = p;
			first = false;
			part->deleteObject(i);
		}
	}
	
	// What the side draws, with the symbols of the map; the scaled symbols of the description become symbols of the map.
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
	auto const insert = [&map, &symbolOf, part_index](std::vector<std::unique_ptr<Object>>& objects) {
		for (auto& object : objects)
		{
			auto const* symbol = symbolOf(object->getSymbol());
			auto const index = map.findSymbolIndex(symbol);
			if (index < 0)
				continue;
			// The map may have them hidden, for instance while the editor shows a preview.
			map.getSymbol(index)->setHidden(false);
			object->setSymbol(symbol, true);
			map.addObject(object.release(), part_index);
		}
		objects.clear();
	};
	// The description first: its ground lies below the course.
	insert(drawing.description.objects);
	insert(drawing.objects);
	
	// A course map is a plain map.
	map.setCourseData(CourseData{});
	return true;
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
	
	auto const& data = map.courseData();
	auto courses = options.courses;
	if (courses.isEmpty())
	{
		for (auto const& course : data.courses)
			courses.push_back(course.name);
	}
	for (auto const& course : qAsConst(courses))
	{
		auto const sheets = courseMapSheets(data, course, options.relay_maps);
		if (sheets.empty())
		{
			report.skipped.push_back(course);
			continue;
		}
		for (auto const& sheet : sheets)
		{
			for (auto const& side : sheet.sides)
			{
				Map copy;
				QBuffer buffer(&bytes);
				if (!buffer.open(QIODevice::ReadOnly) || !copy.importFromIODevice(buffer))
				{
					report.error = tr("Cannot copy the map.");
					return report;
				}
				auto const name = courseMapFileSide(sheet, side);
				if (!makeCourseMapFile(copy, side))
				{
					report.skipped.push_back(name);
					continue;
				}
				auto const path = folder.absoluteFilePath(courseMapFileName(options.base_name, name, format->primaryExtension()));
				auto exporter = format->makeExporter(path, &copy, nullptr);
				if (!exporter || !exporter->doExport())
				{
					QFile::remove(path);
					report.error = tr("Cannot write the file \"%1\".").arg(path);
					if (exporter && !exporter->warnings().empty())
						report.error += QLatin1Char('\n') + exporter->warnings().back();
					return report;
				}
				report.files.push_back(path);
			}
		}
	}
	report.ok = true;
	return report;
}


}  // namespace OpenOrienteering
