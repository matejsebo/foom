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


#include "legibility_feedback.h"

#include <QtGlobal>
#include <QColor>
#include <QCoreApplication>
#include <QLatin1String>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPointF>
#include <QString>

#include "settings.h"
#include "check/legibility.h"
#include "check/standard_minimums.h"
#include "core/map.h"
#include "core/map_coord.h"
#include "core/map_view.h"
#include "core/objects/object.h"
#include "core/ops/flatten.h"
#include "core/symbols/symbol.h"
#include "core/virtual_path.h"
#include "gui/map/map_widget.h"


namespace OpenOrienteering {

namespace {

QString tr(const char* text)
{
	return QCoreApplication::translate("OpenOrienteering::LegibilityFeedback", text);
}

QString readout(const QString& label, double value, double minimum, const QString& unit)
{
	QString const numbers = QString::number(value, 'f', 2) + QLatin1String(" / ") + QString::number(minimum, 'f', 2) + QLatin1Char(' ') + unit;
	QString text;
	if (value < minimum)
	{
		auto const shortfall = qRound(100.0 * (1.0 - value / minimum));
		text = QLatin1String("<span style=\"color: red;\"><b>") + label + QLatin1String("</b> ") + numbers
		       + QLatin1Char(' ') + tr("(%1 % short)").arg(shortfall) + QLatin1String("</span>");
	}
	else
	{
		text = QLatin1String("<span style=\"color: green;\"><b>") + label + QLatin1String("</b> ") + numbers + QLatin1String("</span>");
	}
	return text;
}

}  // namespace


QString legibilityReadout(const Map& map, const Symbol* symbol, const PathObject& preview)
{
	if (!symbol || !Settings::getInstance().getSettingCached(Settings::MapEditor_LegibilityMinimumCheck).toBool())
		return {};
	if (preview.getCoordinateCount() < 2 || preview.parts().empty())
		return {};
	for (auto const& part : preview.parts())
	{
		if (part.path_coords.empty())
			return {};  // not updated yet
	}
	
	// The limits of the scan: the standard's where the symbol has none of its own.
	auto const standard = mapStandard(map);
	auto const enlargement = standardEnlargement(map);
	auto const types = symbol->getContainedTypes();
	if ((types & Symbol::Line) && !(types & Symbol::Area))
	{
		auto const minimum = minimumLineLength(symbol, enlargement, standard);
		if (minimum <= 0.0)
			return {};
		auto length = 0.0;
		for (auto const& part : preview.parts())
			length += part.length();
		return readout(tr("Length:"), length, minimum, tr("mm"));
	}
	if (types & Symbol::Area)
	{
		auto const minimum = minimumAreaSize(symbol, enlargement, standard);
		if (minimum <= 0.0)
			return {};
		auto const area = preview.parts().front().calculateArea();
		return readout(tr("Area:"), area, minimum, tr("mm\u00b2"));
	}
	return {};
}

bool legibilityHaloEnabled()
{
	return Settings::getInstance().getSettingCached(Settings::MapEditor_LegibilityHalo).toBool();
}

double legibilityHaloDistance(const Map& map)
{
	auto const setting = Settings::getInstance().getSettingCached(Settings::MapEditor_LegibilityHaloMM).toDouble();
	return setting > 0.0 ? setting : defaultGap(map);
}

double legibilityHaloMargin(const Map& map, const Symbol* symbol)
{
	return strokeHalfWidth(symbol) + legibilityHaloDistance(map);
}

void drawLegibilityHalo(QPainter* painter, const MapWidget* widget, const PathObject& preview, const Symbol* symbol, double distance_mm)
{
	if (preview.getCoordinateCount() < 2)
		return;
	auto const coords = ops::flattenCoords(preview.getRawCoordinateVector(), 0.05);
	QPainterPath path;
	auto start_part = true;
	for (auto const& coord : coords)
	{
		auto const point = widget->mapToViewport(MapCoordF(coord));
		if (start_part)
			path.moveTo(point);
		else
			path.lineTo(point);
		start_part = coord.isHolePoint();
	}
	
	auto const width_mm = 2.0 * strokeHalfWidth(symbol) + 2.0 * distance_mm;
	auto const width_px = widget->getMapView()->lengthToPixel(1000.0 * width_mm);
	QPen pen(QColor(0, 102, 204, 56));
	pen.setWidthF(width_px);
	pen.setCapStyle(Qt::RoundCap);
	pen.setJoinStyle(Qt::RoundJoin);
	painter->save();
	painter->setRenderHint(QPainter::Antialiasing);
	painter->setPen(pen);
	painter->setBrush(Qt::NoBrush);
	painter->drawPath(path);
	painter->restore();
}


}  // namespace OpenOrienteering
