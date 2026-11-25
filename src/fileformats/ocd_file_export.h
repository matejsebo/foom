/*
 *    Copyright 2016-2020 Kai Pastor
 *
 *    Some parts taken from file_format_oc*d8{.h,_p.h,cpp} which are
 *    Copyright 2012 Pete Curtis
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

#ifndef OPENORIENTEERING_OCD_FILE_EXPORT_H
#define OPENORIENTEERING_OCD_FILE_EXPORT_H

#include <cstddef>
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

#include <QtGlobal>
#include <QByteArray>
#include <QCoreApplication>
#include <QLocale>
#include <QString>
#include <QUuid>
#include <QSet>
#include <QHash>
#include <QTextCodec>

#include "core/map_coord.h"
#include "fileformats/file_import_export.h"
#include "fileformats/ocd_sketch.h"  // foom


template< class Format > class OcdFile;

namespace OpenOrienteering {

class AreaSymbol;
class CombinedSymbol;
class LineSymbol;
class Map;
class MapColor;
class MapView;
class Object;
class PathObject;
class PointObject;
class PointSymbol;
struct StoredRoute;  // foom
class Symbol;
class Template;
class TextObject;
class TextSymbol;


/**
 * An exporter for OCD files.
 * 
 * The functions in this class are designed around the following pattern:
 * 
 * From `template<class Format> void OcdFileExport::exportImplementation()` we
 * call into template functions so that we get a template function instantiation
 * for the actual types of the format. This is what we can do:
 *
 * - If a format is significantly different, so that it needs a different algorithm:
 *   We specialize the template function (as done in 
 *   `template<> void OcdFileExport::exportSetup(OcdFile<Ocd::FormatV8>& file)`).
 * - If the same algorithm can be used for multiple formats:
 *   - If the differences are completely represented by the exporter's state
 *     (which captures version and parameter string handler), then we may just
 *     call into a non-template function (as done in
 *     `template<class Format> void OcdFileExport::exportSetup(OcdFile<Format>&)`.
 *     The template function is likely to get inlined, thus reducing/avoiding
 *     template bloat.
 *   - Otherwise we still can use the same algorithm but need to specialize it
 *     for the actual types of the format. This specialization is solved by
 *     function template instantiation, i.e. the generic algorithm goes directly
 *     into the template function. This is the case for `exportSymbol()` where
 *     types and even the encoding of the symbol number vary over different
 *     versions of the format.
 *     This doesn't mean that there couldn't be room for reducing template bloat
 *     by factoring out universal parts. However, if the size gains are small,
 *     it might not be worth the increased complexity.
 */
class OcdFileExport : public Exporter
{
	Q_DECLARE_TR_FUNCTIONS(OpenOrienteering::OcdFileExport)
	
	/**
	 * A type for temporaries helping to convert strings to Ocd format.
	 */
	struct ExportableString
	{
		const QString& string;
		const QTextCodec* custom_8bit_encoding;
		
		operator QByteArray() const
		{
			return custom_8bit_encoding ? custom_8bit_encoding->fromUnicode(string) : string.toUtf8();
		}
		
		operator QString() const noexcept
		{
			return string;
		}
		
	};
	
	ExportableString toOcdString(const QString& string) const
	{
		return { string, custom_8bit_encoding };
	}
	
	
	using StringAppender = void (qint32, const QString&);
	
	struct TextFormatMapping;
	
public:
	/// \todo Add proper API
	static quint16 default_version;
	
	
	OcdFileExport(const QString& path, const Map* map, const MapView* view, quint16 version);
	
	~OcdFileExport() override;
	
protected:
	/**
	 * Exports an OCD file.
	 */
	bool exportImplementation() override;
	
protected:
	
	template< class Encoding >
	QTextCodec* determineEncoding();
	
	
	template< class Format >
	bool exportImplementation();
	
	
	/**
	 * Calculates an offset to be applied to the coordinates so that the
	 * map data is moved into the limited OCD drawing area.
	 */
	MapCoord calculateAreaOffset();
	
	
	template< class Format >
	void exportSetup(OcdFile<Format>& file);
	
	void exportSetup();
	
	void exportGeoreferencing();
	
	
	template< class Format >
	void exportSymbols(OcdFile<Format>& file);
	
	template< class OcdBaseSymbol >
	void setupBaseSymbol(const Symbol* symbol, quint32 symbol_number, OcdBaseSymbol& ocd_base_symbol);
	
	template< class OcdBaseSymbol >
	void setupSymbolColors(const Symbol* symbol, OcdBaseSymbol& ocd_base_symbol);
	
	template< typename Counter, typename Iterator >
	void setupSymbolColors(const Symbol* symbol, Counter& num_colors, Iterator first, Iterator last);
	
	template< class OcdBaseSymbol >
	void setupIcon(const Symbol* symbol, OcdBaseSymbol& ocd_base_symbol);
	
	template< class OcdPointSymbol >
	QByteArray exportPointSymbol(const PointSymbol* point_symbol);
	
	template< class Element >
	qint16 exportPattern(const PointSymbol* point, QByteArray& byte_array);		// returns the number of written coordinates, including the headers
	
	template< class Element >
	qint16 exportSubPattern(const MapCoordVector& coords, const Symbol* symbol, QByteArray& byte_array);
	
	template< class OcdAreaSymbol >
	QByteArray exportAreaSymbol(const AreaSymbol* area_symbol);
	
	template< class OcdAreaSymbol >
	QByteArray exportAreaSymbol(const AreaSymbol* area_symbol, quint32 symbol_number);
	
	template< class OcdAreaSymbolCommon >
	quint8 exportAreaSymbolCommon(const AreaSymbol* area_symbol, OcdAreaSymbolCommon& ocd_area_common, const PointSymbol*& pattern_symbol);
	
	template< class OcdAreaSymbol >
	void exportAreaSymbolSpecial(const AreaSymbol* area_symbol, OcdAreaSymbol& ocd_area_symbol);
	
	template< class OcdLineSymbol >
	QByteArray exportLineSymbol(const LineSymbol* line_symbol);
	
	template< class OcdLineSymbol >
	QByteArray exportLineSymbol(const LineSymbol* line_symbol, quint32 symbol_number);
	
	template< class OcdLineSymbolCommon >
	quint32 exportLineSymbolCommon(const LineSymbol* line_symbol, OcdLineSymbolCommon& ocd_line_common);
	
	template< class OcdLineSymbolCommon >
	void exportLineSymbolDoubleLine(const LineSymbol* line_symbol, quint32 fill_color, OcdLineSymbolCommon& ocd_line_common);
	
	template< class Format, class OcdTextSymbol >
	void exportTextSymbol(OcdFile<Format>& file, const TextSymbol* text_symbol);
	
	/** foom: Exports a text symbol whose objects follow their path as a line text symbol. */
	template< class Format, class OcdLineTextSymbol >
	void exportLineTextSymbol(OcdFile<Format>& file, const TextSymbol* text_symbol);
	
	template< class OcdTextSymbol >
	QByteArray exportTextSymbol(const TextSymbol* text_symbol, quint32 symbol_number, int alignment);
	
	std::vector<TextFormatMapping>::iterator prepareTextSymbolExport(const TextSymbol* text_symbol);
	
	/** foom: Returns the OCD alignment bits for a text object (vertical bits since version 10). */
	int ocdTextAlignment(const TextObject* text) const;
	
	template< class OcdTextSymbol >
	void setupTextSymbolExtra(const TextSymbol* text_symbol, OcdTextSymbol& ocd_text_symbol);
	
	template< class OcdTextSymbolBasic >
	void setupTextSymbolBasic(const TextSymbol* text_symbol, int alignment, OcdTextSymbolBasic& ocd_text_basic);
	
	template< class OcdTextSymbolSpecial >
	void setupTextSymbolSpecial(const TextSymbol* text_symbol, OcdTextSymbolSpecial& ocd_text_special);
	
	template< class OcdTextSymbolFraming >
	void setupTextSymbolFraming(const TextSymbol* text_symbol, OcdTextSymbolFraming& ocd_text_framing);
	
	int checkCombinedSymbol(const CombinedSymbol* combined_symbol) const;
	
	template< class Format >
	void exportCombinedSymbol(OcdFile<Format>& file, const CombinedSymbol* combined_symbol);
	
	template< class Format >
	void exportGenericCombinedSymbol(OcdFile<Format>& file, const CombinedSymbol* combined_symbol);
	
	template< class OcdAreaSymbol >
	QByteArray exportCombinedAreaSymbol(
	        quint32 symbol_number,
	        const CombinedSymbol* combined_symbol,
	        const AreaSymbol* area_symbol,
	        const Symbol* line_symbol );
	
	template< class OcdLineSymbol >
	QByteArray exportCombinedLineSymbol(
	        quint32 symbol_number,
	        const CombinedSymbol* combined_symbol,
	        const LineSymbol* main_line,
	        const LineSymbol* framing,
	        const LineSymbol* double_line );
	
	
	template< class Format >
	void exportObjects(OcdFile<Format>& file);
	
	template< class OcdObject >
	void handleObjectExtras(const Object* object, OcdObject& ocd_object, typename OcdObject::IndexEntryType& entry);
	
	template< class OcdObject >
	QByteArray exportPointObject(const PointObject* point, typename OcdObject::IndexEntryType& entry);
	
	template< class Format >
	void exportPathObject(OcdFile<Format>& file, const PathObject* path, bool lines_only = false);
	
	template< class OcdObject >
	QByteArray exportTextObject(const TextObject* text, typename OcdObject::IndexEntryType& entry);
	
	template< class OcdObject >
	QByteArray exportObjectCommon(const Object* object, OcdObject& ocd_object, typename OcdObject::IndexEntryType& entry);
	
	
	template< class Format >
	void exportTemplates(OcdFile<Format>& file);
	
	void exportTemplates();
	
	QString stringForTemplate(const Template& temp, const MapCoord& area_offset);
	
	QString pathForTemplate(const Template& temp) const;
	
	
	template< class Format >
	void exportExtras(OcdFile<Format>& file);
	
	/** foom: Returns whether the map's foreign data can be written to this file, warning once when not. */
	bool foreignDataApplies();
	
	/** foom: Writes the foreign parameter strings back. */
	template< class Format >
	void exportForeignStrings(OcdFile<Format>& file);
	
	/// foom: The embedded DEM for course setting, from the map's embedded grid.
	template< class Format >
	void exportEmbeddedDem(OcdFile<Format>& file);
	
	/** foom: Writes the foreign object records back. */
	template< class Format >
	void exportForeignObjects(OcdFile<Format>& file);
	
	void exportExtras();
	
	
	quint16 convertColor(const MapColor* color) const;
	
	quint16 getPointSymbolExtent(const PointSymbol* symbol) const;
	
	quint16 exportCoordinates(const MapCoordVector& coords, const Symbol* symbol, QByteArray& byte_array);
	
	quint16 exportCoordinates(const MapCoordVector& coords, const Symbol* symbol, QByteArray& byte_array, MapCoord& bottom_left, MapCoord& top_right);
	
	quint16 exportTextCoordinatesSingle(const TextObject* object, QByteArray& byte_array, MapCoord& bottom_left, MapCoord& top_right);
	
	quint16 exportTextCoordinatesBox(const TextObject* object, QByteArray& byte_array, MapCoord& bottom_left, MapCoord& top_right);
	
	QByteArray exportTextData(const TextObject* object, int chunk_size, int max_chunks);
	
	
	void addTextTruncationWarning(QString text, int pos);
	
	
	quint32 makeUniqueSymbolNumber(quint32 initial_number) const;
	
	
private:
	/// The locale is used for number formatting.
	QLocale locale;
	
	/// Character encoding to use for 1-byte (narrow) strings
	QTextCodec *custom_8bit_encoding = nullptr;
	
	MapCoord area_offset;
	bool foreign_data_checked = false;  // foom
	
	// foom: writing from the course data, the design notes, "OCD files from the course data"
	struct CourseRecord
	{
		int string_type;
		int view_type;
		QString string;
	};
	/** Collects the records of the objects which course objects and overrides name. */
	void prepareCourseRecords();
	/** The object string of an object and its type: from the course data, or else from its tags. */
	QString objectString(const Object* object, int* type) const;
	/** Writes the parameter strings of options, courses, classes and text blocks. */
	void exportCourseData();
	/** foom: Writes the symbol of the routes of the route analysis, when the map keeps routes and has no such symbol. */
	template< class Format >
	void exportRouteSymbol(OcdFile<Format>& file);
	/** foom: Writes the routes which the map keeps as OCD route objects. */
	template< class Format >
	void exportRouteObjects(OcdFile<Format>& file);
	/** foom: Writes the line and the caption of a kept route. */
	template< class Format >
	void exportRoute(OcdFile<Format>& file, const Symbol* line_symbol, const TextSymbol* caption_symbol, const StoredRoute& route, int number);
	const Symbol* route_symbol = nullptr;
	/** foom: The symbols of the routes over via points, 10603.010 and 10603.011, taken in turn. */
	const Symbol* via_route_symbols[2] = {};
	/** foom: The symbols of the captions of the fastest routes and of those over via points. */
	const TextSymbol* route_caption_symbols[2] = {};
	QHash<QUuid, CourseRecord> course_records;
	QSet<QUuid> hidden_course_objects;
	bool course_records_prepared = false;
	bool foreign_data_applies = false;  // foom
	
	std::function<StringAppender> addParameterString;
	
	std::unordered_map<const Symbol*, quint32> symbol_numbers;
	
	/**
	 * foom: The private symbols whose objects are written as OCD objects
	 * without a symbol, with the symbol number of these objects (-2 graphic,
	 * -4 layout). See the design notes, "Graphic and layout
	 * objects written to OCD files".
	 */
	QHash<const Symbol*, int> graphic_object_symbols;
	void prepareGraphicObjects();
	/** foom: In graphic_object_symbols, a symbol of strokes which go into the OCD sketch block. */
	static constexpr int ocd_sketch_stroke_kind = 1;
	/** foom: The strokes of the sketch part, which go into the OCD sketch block. */
	std::vector<OcdSketchStroke> sketch_strokes;
	void collectSketchStroke(const Object* object);
	
	struct TextFormatMapping
	{
		const Symbol* symbol;
		int           alignment;
		int           count;
		quint32       symbol_number;
	};
	std::vector<TextFormatMapping> text_format_mapping;
	
	struct BreakdownEntry
	{
		quint32 number;
		quint8 type;
	};
	std::vector<BreakdownEntry> breakdown_list;
	std::unordered_map<quint32, std::size_t> breakdown_index;
	
	std::vector<std::unique_ptr<const Symbol>> temporary_symbols;
	
	quint16 ocd_version;
	
	bool uses_registration_color = false;
};


}  // namespace OpenOrienteering

#endif // OPENORIENTEERING_OCD_FILE_EXPORT_H
