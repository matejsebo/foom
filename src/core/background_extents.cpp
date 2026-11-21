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


#include "background_extents.h"

#include <algorithm>

#include <QCoreApplication>
#include <QPointF>
#include <QRectF>
#include <QVariant>
#include <QtGlobal>

#include "core/georeferencing.h"
#include "core/map.h"
#include "core/map_coord.h"
#include "templates/template.h"
#include "templates/template_map.h"


namespace OpenOrienteering {

std::vector<BackgroundExtent> backgroundMapExtents(const Map& map)
{
	std::vector<BackgroundExtent> result;
	for (int i = 0; i < map.getNumTemplates(); ++i)
	{
		auto const* temp = qobject_cast<const TemplateMap*>(map.getTemplate(i));
		// Not the vector data (OgrTemplate), which is a TemplateMap too.
		if (!temp || qstrcmp(temp->getTemplateType(), "TemplateMap") != 0)
			continue;
		// Read afresh: a georeferenced map template has its objects moved at load, but not its extents.
		Map source;
		if (!source.loadFrom(temp->getTemplatePath()) || source.extents().empty())
			continue;
		// An OCD background which is not loaded yet has no transform: it gets the offset
		// of its origin at load (TemplateMap::transformForOcd()), which is taken here.
		auto const ocd_pending = temp->property(TemplateMap::ocdTransformProperty()).toBool();
		auto const ocd_offset = ocd_pending
		        ? QPointF(map.getGeoreferencing().toMapCoordF(source.getGeoreferencing().toProjectedCoords(MapCoordF{})))
		        : QPointF();
		auto const place = [&map, &source, temp, ocd_pending, ocd_offset](const QPointF& corner) -> QPointF {
			if (ocd_pending)
				return corner + ocd_offset;
			if (temp->isTemplateGeoreferenced())
			{
				auto ok = false;
				auto const placed = map.getGeoreferencing().toMapCoordF(&source.getGeoreferencing(), MapCoordF(corner), &ok);
				if (ok)
					return placed;
			}
			return temp->templateToMap(corner);
		};
		for (auto const& extent : source.extents())
		{
			auto const r = extent.rect_mm;
			auto const first = place(r.topLeft());
			auto left = first.x(), right = first.x(), top = first.y(), bottom = first.y();
			for (auto const& corner : { r.topRight(), r.bottomRight(), r.bottomLeft() })
			{
				auto const p = place(corner);
				left = std::min(left, p.x());
				right = std::max(right, p.x());
				top = std::min(top, p.y());
				bottom = std::max(bottom, p.y());
			}
			auto const placed = QRectF(QPointF(left, top), QPointF(right, bottom));
			BackgroundExtent entry;
			entry.background = temp->getTemplateFilename();
			entry.extent = extent;
			entry.extent.rect_mm = placed;
			result.push_back(entry);
		}
	}
	return result;
}

QString backgroundExtentLabel(const BackgroundExtent& entry)
{
	return QCoreApplication::translate("OpenOrienteering::BackgroundExtents", "%1 (Background map: %2)")
	        .arg(entry.extent.name, entry.background);
}

bool loadExtentFromBackgroundMap(Map& map, const BackgroundExtent& entry)
{
	return importExtents(map, { entry.extent }) == 1;
}


}  // namespace OpenOrienteering
