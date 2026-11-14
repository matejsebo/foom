/*
 *    Copyright 2012, 2013 Thomas Schöps
 *    Copyright 2012-2019 Kai Pastor
 *
 *    This file is part of OpenOrienteering.
 *
 *    OpenOrienteering is free software: you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation, either version 3 of the License, or
 *    (at your option) any later version.
 *
 *    OpenOrienteering is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with OpenOrienteering.  If not, see <http://www.gnu.org/licenses/>.
 */


#ifndef OPENORIENTEERING_TEXT_SYMBOL_H
#define OPENORIENTEERING_TEXT_SYMBOL_H

#include "symbol.h"

#include <vector>

#include <Qt>
#include <QFont>
#include <QFontMetricsF>
#include <QString>

class QXmlStreamReader;
class QXmlStreamWriter;
// IWYU pragma: no_forward_declare QFontMetricsF

namespace OpenOrienteering {

class Map;
class MapColor;
class MapColorMap;
class Object;
class ObjectRenderables;
class PointSymbol;
class SymbolPropertiesWidget;
class SymbolSettingDialog;
class TextObject;
struct PathTextLayout;  // foom
class VirtualCoordVector;



/** Symbol for text, can be applied to TextObjects.
 * 
 * NOTE: Uses a hack to disable hinting for fonts:
 * Uses a big internal font size, internal_point_size, where hinting is
 * neglegible and scales it down for output.
 * TODO: A maybe better solution would be to use FreeType directly.
 */
class TextSymbol : public Symbol
{
friend class TextSymbolSettings;
friend class PointSymbolEditorWidget;
public:
	/** Modes for text framing */
	enum FramingMode
	{
		/** Only the text itself is displayed. */
		NoFraming = 0,
		
		/**
		 * The text path is drawn with a QPainter with active QPen in addition
		 * to the normal fill, resulting in a line around the letters.
		 */
		LineFraming = 1,
		
		/**
		 * The text is drawn a second time with a slight offset to the main
		 * text, creating a shadow effect.
		 */
		ShadowFraming = 2,
		
		/** foom: A filled rectangle around the text, offset by the box margins. */
		BoxFraming = 3
	};
	
	/** Creates an empty text symbol. */
	TextSymbol();
	~TextSymbol() override;
	
protected:
	explicit TextSymbol(const TextSymbol& proto);
	TextSymbol* duplicate() const override;
	
public:
	void createRenderables(
	        const Object *object,
	        const VirtualCoordVector &coords,
	        ObjectRenderables &output,
	        Symbol::RenderableOptions options) const override;
	
	using Symbol::createBaselineRenderables;
	void createBaselineRenderables(
	        const TextObject* text_object,
	        const VirtualCoordVector &coords,
	        ObjectRenderables &output) const;
	
	void createLineBelowRenderables(const Object* object, ObjectRenderables& output) const;
	void colorDeletedEvent(const MapColor* color) override;
	bool containsColor(const MapColor* color) const override;
	const MapColor* guessDominantColor() const override;
	void replaceColors(const MapColorMap& color_map) override;
	void scale(double factor) override;
	
	/** Updates the internal QFont from the font settings. */
	void updateQFont();
	
	/** Calculates the factor to convert from the real font size to the internal font size */
	inline double calculateInternalScaling() const {return internal_point_size / (0.001 * font_size);}
	
	// Getters
	inline const MapColor* getColor() const {return color;}
	inline void setColor(const MapColor* color) {this->color = color;}
	inline const QString& getFontFamily() const {return font_family;}
	inline double getFontSize() const {return 0.001 * font_size;}
	inline bool isBold() const {return bold;}
	inline bool isItalic() const {return italic;}
	inline bool isUnderlined() const {return underline;}
	inline float getLineSpacing() const {return line_spacing;}
	inline double getParagraphSpacing() const {return 0.001 * paragraph_spacing;}
	inline double getCharacterSpacing() const {return 0.001 * character_spacing;}
	inline bool usesKerning() const {return kerning;}
	QString getIconText() const; // returns a default text if no custom text is set.
	inline bool usesFraming() const {return framing;}
	inline const MapColor* getFramingColor() const {return framing_color;}
	inline int getFramingMode() const {return framing_mode;}
	inline int getFramingLineHalfWidth() const {return framing_line_half_width;}
	inline int getFramingShadowXOffset() const {return framing_shadow_x_offset;}
	inline int getFramingShadowYOffset() const {return framing_shadow_y_offset;}
	inline bool hasLineBelow() const {return line_below;}
	inline const MapColor* getLineBelowColor() const {return line_below_color;}
	inline double getLineBelowWidth() const {return 0.001 * line_below_width;}
	inline double getLineBelowDistance() const {return 0.001 * line_below_distance;}
	inline int getNumCustomTabs() const {return (int)custom_tabs.size();}
	inline int getCustomTab(int index) const {return custom_tabs[index];}
	inline const QFont& getQFont() const {return qfont;}
	inline const QFontMetricsF& getFontMetrics() const { return metrics; }
	
	double getNextTab(double pos) const;
	
	/** foom: Returns whether objects of this symbol follow their anchor path. */
	bool followsPath() const { return follows_path; }
	/** foom: Sets whether objects follow their anchor path. */
	void setFollowsPath(bool value) { follows_path = value; }
	/** foom: Returns the alignment of text on a path, in the bit layout of OCD files (see the design notes). */
	int getPathAlignment() const { return path_alignment; }
	/** foom: Sets the alignment of text on a path. */
	void setPathAlignment(int value) { path_alignment = value; }
	/** foom: Lays the text object out along its anchor path. */
	PathTextLayout layoutPathText(const TextObject* text_object) const;
	/** foom: Sets the font family and updates the font. */
	void setFontFamily(const QString& family) { font_family = family; updateQFont(); }
	/** foom: Sets the font size in 1/1000 mm and updates the font. */
	void setFontSize(int size) { font_size = size; updateQFont(); }
	
	// foom: text symbol properties, see the design notes, "Text symbol properties".
	/** Returns the word spacing as a factor of the space advance (1 = normal). */
	double getWordSpacing() const { return double(word_spacing); }
	/** Sets the word spacing factor and updates the font. */
	void setWordSpacing(double factor) { word_spacing = float(factor); updateQFont(); }
	/** Returns the font weight, 100 to 900; 400 or 700 from the bold flag when no weight is set. */
	int getFontWeight() const { return font_weight > 0 ? font_weight : (bold ? 700 : 400); }
	/** Sets the font weight (100 to 900); 0 follows the bold flag. */
	void setFontWeight(int weight) { font_weight = weight; updateQFont(); }
	/** Sets the bold flag and updates the font. */
	void setBold(bool value) { bold = value; updateQFont(); }
	/** Returns the indent of the first line of a paragraph in 1/1000 mm. */
	int getIndentFirst() const { return indent_first; }
	/** Returns the indent of the other lines of a paragraph in 1/1000 mm. */
	int getIndentOther() const { return indent_other; }
	/** Sets the indents in 1/1000 mm. */
	void setIndents(int first, int other) { indent_first = first; indent_other = other; }
	void setFraming(bool value) { framing = value; }
	void setFramingColor(const MapColor* value) { framing_color = value; }
	void setFramingMode(int mode) { framing_mode = mode; }
	/** Returns the left margin of the box framing in 1/1000 mm. */
	int getFramingBoxLeft() const { return framing_box_left; }
	int getFramingBoxRight() const { return framing_box_right; }
	int getFramingBoxTop() const { return framing_box_top; }
	int getFramingBoxBottom() const { return framing_box_bottom; }
	/** Sets the margins of the box framing in 1/1000 mm. */
	void setFramingBox(int left, int right, int top, int bottom) { framing_box_left = left; framing_box_right = right; framing_box_top = top; framing_box_bottom = bottom; }
	/** Returns the point symbol drawn at the centre of the text, or nullptr. */
	const PointSymbol* getFrameSymbol() const { return frame_symbol; }
	/** Sets the point symbol drawn at the centre of the text. */
	void setFrameSymbol(const PointSymbol* symbol) { frame_symbol = symbol; }
	/** Converts an OpenType weight (100 to 900) to QFont's weight scale. */
	static int qfontWeight(int weight);
	
	bool symbolChangedEvent(const Symbol* old_symbol, const Symbol* new_symbol) override;
	bool containsSymbol(const Symbol* symbol) const override;
	bool loadingFinishedEvent(Map* map) override;
	
	constexpr static qreal internal_point_size = 256;
	
	SymbolPropertiesWidget* createPropertiesWidget(SymbolSettingDialog* dialog) override;
	
protected:
	void saveImpl(QXmlStreamWriter& xml, const Map& map) const override;
	bool loadImpl(QXmlStreamReader& xml, const Map& map, SymbolDictionary& symbol_dict, int version) override;
	
	/** foom: Creates the renderables of a text object which follows its anchor path. */
	void createPathTextRenderables(const TextObject* text_object, ObjectRenderables& output) const;
	/** foom: Creates the filled rectangle of the box framing. */
	void createBoxFramingRenderables(const TextObject* text_object, ObjectRenderables& output) const;
	/** foom: Creates the renderables of the attached point symbol. */
	void createFrameSymbolRenderables(const TextObject* text_object, ObjectRenderables& output) const;
	bool equalsImpl(const Symbol* other, Qt::CaseSensitivity case_sensitivity) const override;
	
	
	// Members ordered for minimizing padding
	
	QFont qfont;
	QFontMetricsF metrics;
	QString font_family;
	QString icon_text;			// text to be drawn in the symbol's icon
	
	const MapColor* color;
	const MapColor* framing_color;
	const MapColor* line_below_color;
	
	std::vector<int> custom_tabs;
	
	double tab_interval;		/// default tab interval length in text coordinates
	float line_spacing;			// as factor of original line spacing
	float character_spacing;	// as a factor of the space character width
	int font_size;				// this defines the font size in 1000 mm. How big the letters really are depends on the design of the font though
	int paragraph_spacing;		// in mm
	int framing_mode;
	int framing_line_half_width;
	int framing_shadow_x_offset;
	int framing_shadow_y_offset;
	int line_below_width;
	int line_below_distance;
	bool bold;
	bool italic;
	bool underline;
	bool kerning;
	bool framing;
	bool line_below;
	bool follows_path;     // foom: text on a path, see the design notes
	int path_alignment;    // foom: the alignment bits for text on a path, as in OCD files
	float word_spacing;    // foom: as a factor of the space advance
	int font_weight;       // foom: 100 to 900, or 0 for the bold flag
	int indent_first;      // foom: in 1/1000 mm
	int indent_other;      // foom: in 1/1000 mm
	int framing_box_left;  // foom: box framing margins in 1/1000 mm
	int framing_box_right;
	int framing_box_top;
	int framing_box_bottom;
	const PointSymbol* frame_symbol;  // foom: drawn at the centre of the text
	int temp_frame_symbol_index;      // foom: used just for loading
	
};


}  // namespace OpenOrienteering

#endif
