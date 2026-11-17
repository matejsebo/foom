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


#include "image_symbol.h"

#include <QtGlobal>
#include <QPointF>
#include <QRectF>
#include <QXmlStreamReader>

#include "core/map_coord.h"
#include "core/renderables/renderable.h"
#include "core/virtual_coord_vector.h"
#include "util/util.h"


namespace OpenOrienteering {

namespace {

/** Carries the extent of an image object. Never drawn: its colour priority is MapColor::Reserved. */
class ImageRenderable : public Renderable
{
public:
	explicit ImageRenderable(const QRectF& box)
	: Renderable(nullptr)
	{
		extent = box;
	}
	
	PainterConfig getPainterConfig(const QPainterPath* clip_path = nullptr) const override
	{
		return { color_priority, PainterConfig::BrushOnly, 0, clip_path };
	}
	
	void render(QPainter& /*painter*/, const RenderConfig& /*config*/) const override
	{
		// nothing, see Map::draw()
	}
};

}  // namespace


ImageSymbol::ImageSymbol() noexcept
: Symbol { Symbol::Image }
{
	// nothing else
}

ImageSymbol::ImageSymbol(const ImageSymbol& proto)
: Symbol { proto }
{
	// nothing else
}

ImageSymbol::~ImageSymbol() = default;

ImageSymbol* ImageSymbol::duplicate() const
{
	return new ImageSymbol(*this);
}

void ImageSymbol::createRenderables(
        const Object* /*object*/,
        const VirtualCoordVector& coords,
        ObjectRenderables& output,
        Symbol::RenderableOptions /*options*/) const
{
	if (coords.size() == 0)
		return;
	QRectF box(coords[0], coords[0]);
	for (VirtualCoordVector::size_type i = 1; i < coords.size(); ++i)
		rectIncludeSafe(box, QPointF(coords[i]));
	output.insertRenderable(new ImageRenderable(box));
}

void ImageSymbol::colorDeletedEvent(const MapColor* /*color*/)
{
	// nothing
}

bool ImageSymbol::containsColor(const MapColor* /*color*/) const
{
	return false;
}

const MapColor* ImageSymbol::guessDominantColor() const
{
	return nullptr;
}

void ImageSymbol::replaceColors(const MapColorMap& /*color_map*/)
{
	// nothing
}

void ImageSymbol::scale(double /*factor*/)
{
	// nothing
}

SymbolPropertiesWidget* ImageSymbol::createPropertiesWidget(SymbolSettingDialog* /*dialog*/)
{
	return nullptr;
}

void ImageSymbol::saveImpl(QXmlStreamWriter& /*xml*/, const Map& /*map*/) const
{
	// The image symbol is not saved.
}

bool ImageSymbol::loadImpl(QXmlStreamReader& /*xml*/, const Map& /*map*/, SymbolDictionary& /*symbol_dict*/, int /*version*/)
{
	return false;
}

bool ImageSymbol::equalsImpl(const Symbol* /*other*/, Qt::CaseSensitivity /*case_sensitivity*/) const
{
	return true;
}


}  // namespace OpenOrienteering
