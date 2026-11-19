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


#ifndef FOOM_IMAGE_SYMBOL_H
#define FOOM_IMAGE_SYMBOL_H

#include <Qt>

#include "core/symbols/symbol.h"

class QXmlStreamReader;
class QXmlStreamWriter;

namespace OpenOrienteering {

class Map;
class MapColor;
class MapColorMap;
class Object;
class ObjectRenderables;
class SymbolPropertiesWidget;
class SymbolSettingDialog;
class VirtualCoordVector;


/**
 * The symbol of image objects.
 *
 * It has no properties. There is one instance, Map::getImageSymbol(), which
 * is in no map's symbol list, like the symbols for undefined objects.
 * Its renderable gives the object an extent and draws nothing: Map::draw()
 * draws images in passes of their own.
 *
 * See the design notes, "Image object".
 */
class ImageSymbol : public Symbol
{
public:
	ImageSymbol() noexcept;
	~ImageSymbol() override;
	
protected:
	explicit ImageSymbol(const ImageSymbol& proto);
	ImageSymbol* duplicate() const override;
	
public:
	void createRenderables(
	        const Object *object,
	        const VirtualCoordVector &coords,
	        ObjectRenderables &output,
	        Symbol::RenderableOptions options) const override;
	
	void colorDeletedEvent(const MapColor* color) override;
	bool containsColor(const MapColor* color) const override;
	const MapColor* guessDominantColor() const override;
	void replaceColors(const MapColorMap& color_map) override;
	void scale(double factor) override;
	
	SymbolPropertiesWidget* createPropertiesWidget(SymbolSettingDialog* dialog) override;
	
protected:
	void saveImpl(QXmlStreamWriter& xml, const Map& map) const override;
	bool loadImpl(QXmlStreamReader& xml, const Map& map, SymbolDictionary& symbol_dict, int version) override;
	bool equalsImpl(const Symbol* other, Qt::CaseSensitivity case_sensitivity) const override;
};


}  // namespace OpenOrienteering

#endif
