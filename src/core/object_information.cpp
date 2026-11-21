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

#include "object_information.h"

#include <cmath>
#include <cstddef>

#include <QtMath>
#include <QDateTime>
#include <QLatin1String>
#include <QUuid>

#include "core/map.h"
#include "core/map_color.h"
#include "core/map_coord.h"
#include "core/map_part.h"
#include "core/objects/object.h"
#include "core/objects/text_object.h"
#include "core/symbols/symbol.h"


namespace OpenOrienteering {

namespace {

QString timeText(qint64 seconds)
{
	if (seconds == 0)
		return QStringLiteral("unknown");
	return QDateTime::fromSecsSinceEpoch(seconds, Qt::UTC).toString(Qt::ISODate);
}

QString flagsText(const MapCoord& coord)
{
	QStringList flags;
	if (coord.isCurveStart())
		flags << QStringLiteral("curve start");
	if (coord.isDashPoint())
		flags << QStringLiteral("dash");
	if (coord.isGapPoint())
		flags << QStringLiteral("gap");
	if (coord.isHolePoint())
		flags << QStringLiteral("end of part");  // OOM's hole flag ends every part, the outer one too
	if (coord.isClosePoint())
		flags << QStringLiteral("close");
	return flags.join(QStringLiteral(", "));
}

QString number(double value, int decimals)
{
	return std::isnan(value) ? QStringLiteral("-") : QString::number(value, 'f', decimals);
}

}  // namespace



ObjectInformation objectInformation(const Map& map, const Object& object)
{
	// the design notes, "Object information".
	ObjectInformation info;
	info.uuid = object.uuid().toString(QUuid::WithoutBraces);
	auto const* symbol = object.getSymbol();
	auto const has_area = symbol && (symbol->getContainedTypes() & Symbol::Area);
	switch (object.getType())
	{
	case Object::Point:
		info.kind = QStringLiteral("Point");
		break;
	case Object::Path:
		info.kind = has_area ? QStringLiteral("Area") : QStringLiteral("Line");
		break;
	case Object::Text:
		info.kind = QStringLiteral("Text");
		break;
	}
	if (symbol)
		info.symbol = symbol->getNumberAsString() + QLatin1Char(' ') + symbol->getPlainTextName();
	for (int p = 0; p < map.getNumParts(); ++p)
	{
		auto const* part = map.getPart(p);
		auto const index = part->findObjectIndex(&object);
		if (index >= 0)
		{
			info.part = part->getName();
			info.index = index + 1;
			break;
		}
	}
	if (object.group() != 0)
		info.group = map.objectGroupName(object.group());
	info.created = timeText(object.createdTime());
	info.modified = timeText(object.modifiedTime());
	if (object.getType() == Object::Point || object.getType() == Object::Text)
		info.rotation_deg = qRadiansToDegrees(object.getRotation());
	if (object.getType() == Object::Text)
		info.text = static_cast<const TextObject&>(object).getText();
	for (auto const& coord : object.getRawCoordinateVector())
		info.vertices.push_back({ coord.x(), coord.y(), flagsText(coord) });
	
	auto const metres_per_mm = map.getScaleDenominator() / 1000.0;
	if (object.getType() == Object::Path)
	{
		object.update();
		auto const* path = object.asPath();
		info.length_mm = path->getPaperLength();
		info.length_m = info.length_mm * metres_per_mm;
		if (has_area)
		{
			auto const& parts = path->parts();
			double area = 0.0;
			for (std::size_t k = 0; k < parts.size(); ++k)
				area += (k == 0 ? 1.0 : -1.0) * std::abs(parts[k].calculateArea());
			info.area_mm2 = area;
			info.area_m2 = area * metres_per_mm * metres_per_mm;
		}
	}
	if (symbol)
	{
		for (int i = 0; i < map.getNumColors(); ++i)
		{
			auto const* color = map.getColor(i);
			if (symbol->containsColor(color))
				info.colours.push_back(color->getName());
		}
	}
	for (auto const& tag : object.tags())
		info.tags.push_back(tag.key + QLatin1Char('=') + tag.value);
	return info;
}


QString objectInformationText(const ObjectInformation& info)
{
	QString text;
	auto const line = [&text](const char* label, const QString& value) {
		if (!value.isEmpty())
			text += QLatin1String(label) + QStringLiteral(": ") + value + QLatin1Char('\n');
	};
	line("Kind", info.kind);
	line("Symbol", info.symbol);
	line("Part", info.part.isEmpty() ? QString{} : info.part + QStringLiteral(", object ") + QString::number(info.index));
	line("Group", info.group);
	line("UUID", info.uuid);
	line("Created", info.created);
	line("Modified", info.modified);
	if (!std::isnan(info.rotation_deg))
		line("Rotation", number(info.rotation_deg, 1) + QStringLiteral(" deg"));
	line("Text", info.text);
	if (!std::isnan(info.length_mm))
		line("Length", number(info.length_mm, 2) + QStringLiteral(" mm, ") + number(info.length_m, 1) + QStringLiteral(" m"));
	if (!std::isnan(info.area_mm2))
		line("Area", number(info.area_mm2, 2) + QStringLiteral(" mm2, ") + number(info.area_m2, 0) + QStringLiteral(" m2"));
	line("Colours", info.colours.join(QStringLiteral(", ")));
	line("Tags", info.tags.join(QStringLiteral(", ")));
	text += QStringLiteral("Coordinates (mm):");
	for (auto const& vertex : info.vertices)
	{
		text += QStringLiteral("\n  ") + number(vertex.x, 3) + QStringLiteral("; ") + number(vertex.y, 3);
		if (!vertex.flags.isEmpty())
			text += QStringLiteral(" (") + vertex.flags + QLatin1Char(')');
	}
	return text;
}


}  // namespace OpenOrienteering
