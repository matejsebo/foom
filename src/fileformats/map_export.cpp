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


#include "map_export.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include <QtGlobal>
#include <QCoreApplication>
#include <QHash>
#include <QChar>
#include <QPainter>
#include <QSize>
#include <QSvgGenerator>

#include "core/map.h"
#include "core/objects/object.h"
#include "core/ops/crop.h"
#include "core/renderables/renderable.h"
#include "core/symbols/symbol.h"
#include "fileformats/file_format_registry.h"
#include "fileformats/file_import_export.h"


namespace OpenOrienteering {

namespace {

/// SVG user units per mm on paper.
constexpr double svg_units_per_mm = 100.0;

}  // namespace


bool exportSvg(Map& map, const QString& path, const QRectF& area, QString* error)
{
	// the design notes, "SVG export".
	if (!area.isValid() || area.width() <= 0 || area.height() <= 0)
	{
		if (error)
			*error = QCoreApplication::translate("OpenOrienteering::MapExport", "The area to export is empty.");
		return false;
	}
	auto const width = int(std::ceil(area.width() * svg_units_per_mm));
	auto const height = int(std::ceil(area.height() * svg_units_per_mm));

	QSvgGenerator generator;
	generator.setFileName(path);
	generator.setResolution(qRound(svg_units_per_mm * 25.4));
	generator.setSize(QSize(width, height));
	generator.setViewBox(QRectF(0, 0, width, height));

	QPainter painter;
	if (!painter.begin(&generator))
	{
		if (error)
			*error = QCoreApplication::translate("OpenOrienteering::MapExport", "Cannot write the file %1.").arg(path);
		return false;
	}
	painter.setRenderHint(QPainter::Antialiasing);
	painter.scale(svg_units_per_mm, svg_units_per_mm);
	painter.translate(-area.left(), -area.top());
	painter.setClipRect(area);
	RenderConfig config = { map, area, svg_units_per_mm, RenderConfig::NoOptions, 1.0 };
	map.draw(&painter, config);
	if (!painter.end())
	{
		if (error)
			*error = QCoreApplication::translate("OpenOrienteering::MapExport", "Cannot write the file %1.").arg(path);
		return false;
	}
	return true;
}


}  // namespace OpenOrienteering
