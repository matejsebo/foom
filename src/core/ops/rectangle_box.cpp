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


#include "rectangle_box.h"

#include <cmath>
#include <memory>

#include <QtGlobal>
#include <QString>
#include <QStringLiteral>

#include "core/map_coord.h"
#include "core/objects/object.h"
#include "core/objects/text_object.h"
#include "core/path_coord.h"
#include "core/symbols/symbol.h"
#include "core/symbols/text_symbol.h"
#include "util/util.h"


namespace OpenOrienteering {

namespace ops {

OpResult rectangleBox(const Map& /*map*/, const RectangleBoxGeometry& geometry, const RectangleBoxParams& params)
{
	// the design notes, "Rectangle objects".
	OpResult result;
	if (!params.frame)
	{
		result.error = QStringLiteral("Select a line symbol for the frame.");
		return result;
	}
	
	auto right = geometry.top_right - geometry.top_left;
	auto down = geometry.bottom_left - geometry.top_left;
	auto const width = right.length();
	auto const height = down.length();
	if (!(width > 0.0) || !(height > 0.0))
	{
		result.error = QStringLiteral("The box has no size.");
		return result;
	}
	auto const angle = right.angle();
	right.normalize();
	down.normalize();
	auto const& top_left = geometry.top_left;
	auto const& top_right = geometry.top_right;
	auto const& bottom_left = geometry.bottom_left;
	auto const bottom_right = top_right + down * height;
	
	// The frame
	MapCoordVector coords;
	auto const radius = qBound(0.0, params.corner_radius, qMin(width, height) / 2);
	if (qIsNull(radius))
	{
		coords.emplace_back(top_left);
		coords.emplace_back(top_right);
		coords.emplace_back(bottom_right);
		coords.emplace_back(bottom_left);
	}
	else
	{
		auto const handle = (1 - BEZIER_KAPPA) * radius;
		coords.emplace_back(top_right - right * radius, MapCoord::CurveStart);
		coords.emplace_back(top_right - right * handle);
		coords.emplace_back(top_right + down * handle);
		coords.emplace_back(top_right + down * radius);
		coords.emplace_back(bottom_right - down * radius, MapCoord::CurveStart);
		coords.emplace_back(bottom_right - down * handle);
		coords.emplace_back(bottom_right - right * handle);
		coords.emplace_back(bottom_right - right * radius);
		coords.emplace_back(bottom_left + right * radius, MapCoord::CurveStart);
		coords.emplace_back(bottom_left + right * handle);
		coords.emplace_back(bottom_left - down * handle);
		coords.emplace_back(bottom_left - down * radius);
		coords.emplace_back(top_left + down * radius, MapCoord::CurveStart);
		coords.emplace_back(top_left + down * handle);
		coords.emplace_back(top_left + right * handle);
		coords.emplace_back(top_left + right * radius);
	}
	auto frame = std::make_unique<PathObject>(params.frame, coords);
	frame->parts().front().setClosed(true, false);
	frame->setGroup(params.group);
	result.out.push_back(std::move(frame));
	
	if (params.cell_width > 0.0 && params.cell_height > 0.0)
	{
		auto const* grid_symbol = params.grid ? params.grid : params.frame;
		auto const num_cells_x = qMax(1, qRound(width / params.cell_width));
		auto const num_cells_y = qMax(1, qRound(height / params.cell_height));
		auto const cell_width = width / num_cells_x;
		auto const cell_height = height / num_cells_y;
		
		// The grid lines
		coords.resize(2);
		for (int x = 1; x < num_cells_x; ++x)
		{
			coords[0] = MapCoord(top_left + right * (x * cell_width));
			coords[1] = MapCoord(bottom_left + right * (x * cell_width));
			auto line = std::make_unique<PathObject>(grid_symbol, coords);
			line->setGroup(params.group);
			result.out.push_back(std::move(line));
		}
		for (int y = 1; y < num_cells_y; ++y)
		{
			coords[0] = MapCoord(top_left + down * (y * cell_height));
			coords[1] = MapCoord(top_right + down * (y * cell_height));
			auto line = std::make_unique<PathObject>(grid_symbol, coords);
			line->setGroup(params.group);
			result.out.push_back(std::move(line));
		}
		
		// The number texts
		if (params.numbering && params.number_text && height >= params.cell_height / 2)
		{
			auto const* text_symbol = params.number_text;
			auto const ascent_offset = text_symbol->getFontMetrics().ascent() / text_symbol->calculateInternalScaling() - text_symbol->getFontSize();
			for (int y = 0; y < num_cells_y; ++y)
			{
				for (int x = 0; x < num_cells_x; ++x)
				{
					auto const cell_num = params.number_from_bottom
					                      ? (num_cells_y - 1 - y) * num_cells_x + x + 1
					                      : y * num_cells_x + x + 1;
					auto text = std::make_unique<TextObject>(text_symbol);
					text->setText(cell_num > num_cells_x * num_cells_y - params.unnumbered_cells ? params.unnumbered_text : QString::number(cell_num));
					text->setRotation(-angle);
					text->setHorizontalAlignment(TextObject::AlignLeft);
					text->setVerticalAlignment(TextObject::AlignTop);
					auto const position_x = (x + 0.07) * cell_width;
					auto const position_y = (y + 0.04) * cell_height + ascent_offset;
					text->setAnchorPosition(top_left + right * position_x + down * position_y);
					text->setGroup(params.group);
					result.out.push_back(std::move(text));
				}
			}
		}
	}
	return result;
}


}  // namespace ops

}  // namespace OpenOrienteering
