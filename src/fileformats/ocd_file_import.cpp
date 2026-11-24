/*
 *    Copyright 2013-2022, 2024, 2025 Kai Pastor
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

#include "ocd_file_import.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <vector>

#include <Qt>
#include <QtGlobal>
#include <QtMath>
#include <QtNumeric>
#include <QChar>
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFlags>
#include <QFontMetricsF>
#include <QIODevice>
#include <QImage>
#include <QLatin1Char>
#include <QLatin1String>
#include <QPointF>
#include <QStringRef>
#include <QTextCodec>
#include <QTextDecoder>
#include <QVariant>

#include "settings.h"
#include "core/bookmarks.h"
#include "core/georeferencing.h"
#include "core/map.h"
#include "core/map_color.h"
#include "core/map_grid.h"
#include "core/map_part.h"
#include "course/course_data.h"  // foom
#include "course/course_storage.h"  // foom
#include "course/ocd_course_records.h"  // foom
#include "core/map_view.h"
#include "core/object_groups.h"
#include "core/objects/image_object.h"  // foom
#include "core/objects/text_object.h"
#include "core/ops/op.h"
#include "core/ops/rectangle_box.h"
#include "core/symbols/area_symbol.h"
#include "core/symbols/combined_symbol.h"
#include "core/symbols/line_symbol.h"
#include "core/symbols/point_symbol.h"
#include "core/symbols/symbol.h"
#include "fileformats/ocd_bookmark.h"
#include "fileformats/ocd_guide.h"  // foom
#include "core/symbols/text_symbol.h"
#include "elevation/elevation_model.h"
#include "elevation/elevation_set.h"
#include "elevation/ocd_course_dem.h"
#include "fileformats/file_format.h"
#include "fileformats/ocd_datetime.h"
#include "fileformats/ocd_file_format.h"
#include "fileformats/ocd_layout_image.h"  // foom
#include "fileformats/ocd_georef_fields.h"
#include "fileformats/ocd_graphic_object.h"
#include "fileformats/ocd_icon.h"
#include "fileformats/ocd_parameter_stream_reader.h"
#include "fileformats/ocd_types_v8.h"
#include "fileformats/ocd_types_v9.h"
#include "fileformats/ocd_types_v10.h"
#include "fileformats/ocd_types_v11.h"  // IWYU pragma: keep
#include "fileformats/ocd_types_v12.h"  // IWYU pragma: keep
#include "fileformats/ocd_types_v2018.h"
#include "templates/template.h"
#include "templates/template_map.h"
#include "templates/template_placeholder.h"
#include "util/encoding.h"
#include "util/util.h"


namespace OpenOrienteering {

namespace {

static QTextCodec* codecFromSettings()
{
	const auto& settings = Settings::getInstance();
	const auto name = settings.getSetting(Settings::General_Local8BitEncoding).toByteArray();
	return Util::codecForName(name);
}	


}  // namespace




OcdFileImport::OcdImportedPathObject::~OcdImportedPathObject() = default;



OcdFileImport::OcdFileImport(const QString& path, Map* map, MapView* view)
 : Importer { path, map, view }
 , custom_8bit_encoding { codecFromSettings() }
{
	if (!custom_8bit_encoding)
	{
		addWarning(tr("Encoding '%1' is not available. Check the settings."));
		custom_8bit_encoding = QTextCodec::codecForLocale();
	}
}

OcdFileImport::~OcdFileImport() = default;


void OcdFileImport::setCustom8BitEncoding(QTextCodec* encoding)
{
	custom_8bit_encoding = encoding;
}


template< unsigned char N >
QString OcdFileImport::convertOcdString(const Ocd::PascalString<N>& src) const
{
	return custom_8bit_encoding->toUnicode(src.data, src.length);
}

template< unsigned char N >
QString OcdFileImport::convertOcdString(const Ocd::Utf8PascalString<N>& src) const
{
	return QString::fromUtf8(src.data, src.length);
}

template< std::size_t N >
QString OcdFileImport::convertOcdString(const Ocd::Utf16PascalString<N>& src) const
{
	Q_STATIC_ASSERT(N <= std::numeric_limits<unsigned int>::max() / 2);
	return convertOcdString(src.data, N);
}

template< >
QString OcdFileImport::convertOcdString< Ocd::Custom8BitEncoding >(const char* src, uint len) const
{
	len = qMin(uint(std::numeric_limits<int>::max()), qstrnlen(src, len));
	return custom_8bit_encoding->toUnicode(src, int(len));
}

template< >
QString OcdFileImport::convertOcdString< Ocd::Utf8Encoding >(const char* src, uint len) const
{
	len = qMin(uint(std::numeric_limits<int>::max()), qstrnlen(src, len));
	return QString::fromUtf8(src, int(len));
}

template< class E >
QString OcdFileImport::convertOcdString(const QByteArray& data) const
{
	return OcdFileImport::convertOcdString< E >(data.constData(), uint(data.length()));
}

QString OcdFileImport::convertOcdString(const QChar* src, uint maxlen) const
{
	auto last = src;
	if (src)
	{
		while (maxlen && *last != 0)
		{
			last ++;
			--maxlen;
		}
	}
	/// \todo Create and use static decoder
	QTextCodec* utf16 = QTextCodec::codecForName("UTF-16LE");
	FILEFORMAT_ASSERT(utf16);
	auto decoder = std::unique_ptr<QTextDecoder>(utf16->makeDecoder(QTextCodec::ConvertInvalidToNull));
	return decoder->toUnicode(reinterpret_cast<const char*>(src), 2*int(last - src));
}


MapCoord OcdFileImport::convertOcdPoint(const Ocd::OcdPoint32& ocd_point) const
{
	qint32 ocad_x = ocd_point.x >> 8;
	qint32 ocad_y = ocd_point.y >> 8;
	// Recover from broken coordinate export from Mapper 0.6.2 ... 0.6.4 (#749)
	// Cf. broken::convertPointMember in file_format_ocad8.cpp:
	// The values -4 ... -1 (-0.004 mm ... -0.001 mm) were converted to 0x80000000u instead of 0.
	// This is the maximum value. Thus it is okay to assume it won't occur in regular data,
	// and we can safely replace it with 0 here.
	// But the input parameter were already subject to right shift ...
	constexpr auto invalid_value = qint32(0x80000000u) >> 8; // ... so we use this value here.
	if (ocad_x == invalid_value)
		ocad_x = 0;
	if (ocad_y == invalid_value)
		ocad_y = 0;
	return MapCoord::fromNative(ocad_x * 10, ocad_y * -10);
}


qreal OcdFileImport::convertAngle(int ocd_angle) const
{
	// OC*D uses tenths of a degree, counterclockwise
	// BUG: if sin(rotation) is < 0 for a hatched area pattern, the pattern's createRenderables() will go into an infinite loop.
	// So until that's fixed, we keep a between 0 and PI
	return qDegreesToRadians(0.1 * ((ocd_angle + 3600) % 3600));
}


int OcdFileImport::convertLength(qint16 ocd_length) const
{
	return convertLength<qint16, int>(ocd_length);
}

int OcdFileImport::convertLength(quint16 ocd_length) const
{
	return convertLength<quint16, int>(ocd_length);
}

template< class T, class R >
R OcdFileImport::convertLength(T ocd_length) const
{
	// OC*D uses hundredths of a millimeter.
	// oo-mapper uses 1/1000 mm
	return static_cast<R>(ocd_length) * 10;
}


const MapColor* OcdFileImport::convertColor(int ocd_color)
{
	if (!color_index.contains(ocd_color))
	{
		addWarning(tr("Color id not found: %1, ignoring this color").arg(ocd_color));
		return nullptr;
	}
	
	return color_index[ocd_color];
}


void OcdFileImport::addSymbolWarning(const AreaSymbol* symbol, const QString& warning)
{
	addWarning( tr("In area symbol %1 '%2': %3").
	            arg(symbol->getNumberAsString(), symbol->getName(), warning) );
}

void OcdFileImport::addSymbolWarning(const LineSymbol* symbol, const QString& warning)
{
	addWarning( tr("In line symbol %1 '%2': %3").
	            arg(symbol->getNumberAsString(), symbol->getName(), warning) );
}

void OcdFileImport::addSymbolWarning(const TextSymbol* symbol, const QString& warning)
{
	addWarning( tr("In text symbol %1 '%2': %3").
	            arg(symbol->getNumberAsString(), symbol->getName(), warning) );
}


#ifndef NDEBUG

// Heuristic detection of implementation errors
template< >
qint64 OcdFileImport::convertLength< quint8 >(quint8 ocd_length) const
{
	// OC*D uses hundredths of a millimeter.
	// oo-mapper uses 1/1000 mm
	if (ocd_length > 200)
		qDebug("quint8 has value %d, might be qint8 %d", ocd_length, qint8(ocd_length));
	return qint64(ocd_length) * 10;
}

template< >
qint64 OcdFileImport::convertLength< quint16 >(quint16 ocd_length) const
{
	// OC*D uses hundredths of a millimeter.
	// oo-mapper uses 1/1000 mm
	if (ocd_length > 50000)
		qDebug("quint16 has value %d, might be qint16 %d", ocd_length, qint16(ocd_length));
	return qint64(ocd_length) * 10;
}

template< >
qint64 OcdFileImport::convertLength< quint32 >(quint32 ocd_length) const
{
	// OC*D uses hundredths of a millimeter.
	// oo-mapper uses 1/1000 mm
	if (ocd_length > 3000000)
		qDebug("quint32 has value %d, might be qint32 %d", ocd_length, qint32(ocd_length));
	return qint64(ocd_length) * 10;
}

#endif // !NDEBUG


template< class F >
void OcdFileImport::importImplementation()
{
	const OcdFile<F> file(buffer);
	if (!file.header())
	{
		qWarning("*** OcdFileImport: Incomplete or missing header");
		return;
	}

#ifdef MAPPER_DEVELOPMENT_BUILD
	if (!qApp->applicationName().endsWith(QLatin1String("Test")))
	{
		qDebug("*** OcdFileImport: Importing a version %d.%d file", file.header()->version, file.header()->subversion);
		for (const auto ocd_string : file.strings())
		{
			qDebug(" %d \t%s", ocd_string.entry->type, qPrintable(convertOcdString< typename F::Encoding >(ocd_string)));
		}
	}
#endif
	
	importGeoreferencing(file);
	importColors(file);
	importSymbols(file);
	if (!loadSymbolsOnly())
	{
		importExtras(file);
		importObjects(file);
		importTemplates(file);
		if (view)
		{
			importView(file);
		}
		importBookmarks(file);  // foom
		importGuides(file);  // foom
		importLayoutImages(file);  // foom
		importForeignData(file);  // foom
		importCourseData(file.header()->file_type == Ocd::TypeCourseSetting);  // foom: reads what importForeignData() kept
	}
	
	// No deep copy during import
	FILEFORMAT_ASSERT(file.byteArray().constData() == buffer.constData());
}


void OcdFileImport::importGeoreferencing(const OcdFile<Ocd::FormatV8>& file)
{
	const Ocd::FileHeaderV8* header = file.header();
	const Ocd::SetupV8* setup = Ocd::getBlockChecked<Ocd::SetupV8>(file.byteArray(), header->setup_pos);
	if (Q_UNLIKELY(!setup))
		return;
	
	Georeferencing georef;
	georef.setScaleDenominator(qRound(setup->map_scale));
	georef.setProjectedRefPoint(QPointF(setup->real_offset_x, setup->real_offset_y));
	georef.setGrivation(qIsFinite(setup->real_angle) ? setup->real_angle : 0);
	map->setGeoreferencing(georef);
}

template< class F >
void OcdFileImport::importGeoreferencing(const OcdFile< F >& file)
{
	handleStrings(file, { { 1039, &OcdFileImport::importGeoreferencing } });
}

namespace {

void tryParamConvert(int& out, const QStringRef& param_value)
{
	bool ok;
	auto value = qRound(param_value.toFloat(&ok));
	if (ok)
		out = value;
}

}  // anonymous namespace

void OcdFileImport::importGeoreferencing(const QString& param_string)
{
	OcdParameterStreamReader parameters(param_string);
	
	// si_ScalePar (type 1039) contains both georeferencing and map grid
	// display parameters. Georeferencing data is extracted into a structure
	// and passed on to a dedicated class. Screen grid parameters are
	// processed below.
	auto add_warning = [this](const QString& w){ addWarning(w); };
	Georeferencing georef;
	OcdGeorefFields fields;

	while (parameters.readNext())
	{
		bool ok;
		QStringRef param_value = parameters.value();
		switch (parameters.key())
		{
		case 'm':
			tryParamConvert(fields.m, param_value);
			break;
		case 'x':
			tryParamConvert(fields.x, param_value);
			break;
		case 'y':
			tryParamConvert(fields.y, param_value);
			break;
		case 'i':
			tryParamConvert(fields.i, param_value);
			break;
		case 'r':
			tryParamConvert(fields.r, param_value);
			break;
		case 'a':
			{
				auto double_param = param_value.toDouble(&ok);
				if (ok)
					fields.a = double_param;
			}
			break;
		case 'd':
			{
				auto spacing = param_value.toDouble(&ok);
				if (ok && spacing >= 0.001)
				{
					auto grid = map->getGrid();
					grid.setUnit(MapGrid::MetersInTerrain);
					grid.setHorizontalSpacing(spacing);
					grid.setVerticalSpacing(spacing);
					map->setGrid(grid);
				}
			}
			break;
		default:
			; // nothing
		}
	}
	
	fields.setupGeoref(georef, add_warning);

	map->setGeoreferencing(georef);
}

void OcdFileImport::importColors(const OcdFile<Ocd::FormatV8>& file)
{
	const Ocd::SymbolHeaderV8 & symbol_header = file.header()->symbol_header;
	
	if (symbol_header.cmyk_screen != Ocd::CmykScreenV8{})
	{
		qDebug("Ignoring unusual CMYK configuration");
	}
	
	auto num_separations = qMin(quint16(24), symbol_header.num_separations);
	if (num_separations > 0)
	{
		spot_colors.reserve(num_separations);
		for (auto i = 0u; i < num_separations; i++)
		{
			const auto& separation_info = symbol_header.separation_info[i];
			const auto name = convertOcdString(separation_info.name);
			auto color = new MapColor(name, MapColor::Undefined);
			color->setSpotColorName(name);
			// OCD stores CMYK values as integers from 0-200.
			const MapColorCmyk cmyk(
			  0.005f * separation_info.cmyk.cyan,
			  0.005f * separation_info.cmyk.magenta,
			  0.005f * separation_info.cmyk.yellow,
			  0.005f * separation_info.cmyk.black );
			color->setCmyk(cmyk);
			color->setOpacity(1.0f);
			color->setScreenAngle(0.1 * separation_info.raster_angle);
			color->setScreenFrequency(0.1 * separation_info.raster_freq);
			spot_colors.push_back(color);
		}
	}
	
	auto num_colors = symbol_header.num_colors;
	for (auto i = 0u; i < num_colors && i < 256; i++)
	{
		const Ocd::ColorInfoV8& color_info = symbol_header.color_info[i];
		const QString name = convertOcdString(color_info.name);
		int color_pos = map->getNumColors();
		auto color = new MapColor(name, color_pos);
		
		// OC*D stores CMYK values as integers from 0-200.
		MapColorCmyk cmyk;
		cmyk.c = 0.005f * color_info.cmyk.cyan;
		cmyk.m = 0.005f * color_info.cmyk.magenta;
		cmyk.y = 0.005f * color_info.cmyk.yellow;
		cmyk.k = 0.005f * color_info.cmyk.black;
		color->setCmyk(cmyk);
		color->setOpacity(1.0f);
		
		SpotColorComponents components;
		for (auto j = 0u; j < num_separations; ++j)
		{
			const auto ocd_halftone = color_info.separations[j];
			if (ocd_halftone <= 200)
			{
				components.reserve(std::size_t(num_separations));  // reserves only once for same capacity
				components.push_back(SpotColorComponent(spot_colors[j], 0.005f * ocd_halftone));  // clazy:exclude=reserve-candidates
			}
		}
		if (!components.empty())
		{
			color->setSpotColorComposition(components);
			const MapColorCmyk cmyk(color->getCmyk());
			color->setCmykFromSpotColors();
			if (cmyk != color->getCmyk())
				// The color's CMYK was customized.
				color->setCmyk(cmyk);
		}
		
		if ((i == 0 && color->isBlack() && color->getName() == QLatin1String("Registration black"))
		    || (!components.empty() && components.size() == num_separations
		        && color_info.cmyk.cyan == 200
		        && color_info.cmyk.magenta == 200
		        && color_info.cmyk.yellow == 200
		        && color_info.cmyk.black == 200
		        && std::all_of(color_info.separations, color_info.separations + num_separations,
		                       [](const auto& s) { return s == 200; }) ) )
		{
			addWarning(tr("Color \"%1\" is imported as special color \"Registration black\".").arg(color->getName()));
			delete color;
			color_index[color_info.number] = Map::getRegistrationColor();
			// NOTE: This does not make a difference in output
			// as long as no spot colors are created,
			// but as a special color, it is protected from modification,
			// and it will be saved as number 0 in OCD export.
		}
		else
		{
			color->setNumber(color_info.number);  // foom
			map->addColor(color, color_pos);
			color_index[color_info.number] = color;
		}
	}
	
	// Insert the spot colors into the map
	for (auto i = 0u; i < num_separations; ++i)
	{
		map->addColor(spot_colors[i], map->getNumColors());
	}
}

template< class F >
void OcdFileImport::importColors(const OcdFile< F >& file)
{
	spot_colors.clear();
	spot_colors.reserve(10);
	handleStrings(file, { { 10, &OcdFileImport::importSpotColor } });
	handleStrings(file, { { 9, &OcdFileImport::importColor } });
	std::sort(begin(spot_colors), end(spot_colors), [](const auto a, const auto b) {
		return a->getPriority() < b->getPriority();
	});
	// Insert the spot colors into the map after (below) the regular colors.
	for (auto spot_color : spot_colors)
	{
		map->addColor(spot_color, map->getNumColors());
	}
}

void OcdFileImport::importSpotColor(const QString& param_string)
{
	OcdParameterStreamReader parameters(param_string);
	
	const QString name = parameters.value().toString();
	
	int number = -1;
	bool number_ok = false;
	MapColorCmyk cmyk { 0.0, 0.0, 0.0, 0.0 };
	double screen_angle = 45;
	double screen_frequency = 150;
	
	SpotColorComponents components;
	
	while (parameters.readNext())
	{
		float f_value;
		bool ok;
		QStringRef param_value = parameters.value();
		switch (parameters.key())
		{
		case 'n':
			number = param_value.toInt(&number_ok);
			break;
		case 'v':
			if (param_value != QLatin1String("1"))
				qInfo("Spot color %s: Unknown value v:%s", qUtf8Printable(name), qUtf8Printable(param_value.toString()));
			break;
		case 'c':
			f_value = param_value.toFloat(&ok);
			if (ok && f_value >= 0 && f_value <= 100)
				cmyk.c = 0.01f * f_value;
			break;
		case 'm':
			f_value = param_value.toFloat(&ok);
			if (ok && f_value >= 0 && f_value <= 100)
				cmyk.m = 0.01f * f_value;
			break;
		case 'y':
			f_value = param_value.toFloat(&ok);
			if (ok && f_value >= 0 && f_value <= 100)
				cmyk.y = 0.01f * f_value;
			break;
		case 'k':
			f_value = param_value.toFloat(&ok);
			if (ok && f_value >= 0 && f_value <= 100)
				cmyk.k = 0.01f * f_value;
			break;
		case 'f':
			f_value = param_value.toFloat(&ok);
			if (ok && f_value >= 0)
				screen_frequency = 0.1 * double(f_value);
			break;
		case 'a':
			f_value = param_value.toFloat(&ok);
			if (ok && f_value >= 0)
				screen_angle = double(f_value);
			break;
		default:
			; // nothing
		}
	}
	
	if (!number_ok)
		return;
	
	auto color = new MapColor(name, number);
	color->setSpotColorName(name);
	color->setCmyk(cmyk);
	color->setScreenAngle(screen_angle);
	color->setScreenFrequency(screen_frequency);
	spot_colors.push_back(color);
}

void OcdFileImport::importColor(const QString& param_string)
{
	OcdParameterStreamReader parameters(param_string);
	
	const QString name = parameters.value().toString();
	int number;
	bool number_ok = false;
	MapColorCmyk cmyk { 0.0, 0.0, 0.0, 0.0 };
	bool overprinting = false;
	float opacity = 1.0f;
	
	SpotColorComponents components;
	QString spot_color_name;
	
	while (parameters.readNext())
	{
		float f_value;
		int i_value;
		bool ok;
		QStringRef param_value = parameters.value();
		switch (parameters.key())
		{
		case 'n':
			number = param_value.toInt(&number_ok);
			break;
		case 'c':
			f_value = param_value.toFloat(&ok);
			if (ok && f_value >= 0 && f_value <= 100)
				cmyk.c = 0.01f * f_value;
			break;
		case 'm':
			f_value = param_value.toFloat(&ok);
			if (ok && f_value >= 0 && f_value <= 100)
				cmyk.m = 0.01f * f_value;
			break;
		case 'y':
			f_value = param_value.toFloat(&ok);
			if (ok && f_value >= 0 && f_value <= 100)
				cmyk.y = 0.01f * f_value;
			break;
		case 'k':
			f_value = param_value.toFloat(&ok);
			if (ok && f_value >= 0 && f_value <= 100)
				cmyk.k = 0.01f * f_value;
			break;
		case 'o':
			i_value = param_value.toInt(&ok);
			if (ok)
				overprinting = i_value;
			break;
		case 't':
			f_value = param_value.toFloat(&ok);
			if (ok && f_value >= 0.f && f_value <= 100.f)
				opacity = 0.01f * f_value;
			break;
		case 's':
			spot_color_name = param_value.toString();
			break;
		case 'p':
			if (!spot_color_name.isEmpty())
			{
				auto spot_color = std::find_if(begin(spot_colors), end(spot_colors), [&spot_color_name](const auto s) {
					return s->getName() == spot_color_name;
				});
				if (spot_color != end(spot_colors))
				{
					i_value = param_value.toInt(&ok);
					components.push_back({*spot_color, qBound(0, i_value, 100)/100.0f});
				}
			}
			spot_color_name.clear();
			break;
		default:
			; // nothing
		}
	}
	
	if (!number_ok)
		return;
		
	if ((cmyk.isBlack() && name == QLatin1String("Registration black"))
	    || (!components.empty() && components.size() == spot_colors.size()
	        && qFuzzyCompare(cmyk.c, 1)
	        && qFuzzyCompare(cmyk.m, 1)
	        && qFuzzyCompare(cmyk.y, 1)
	        && qFuzzyCompare(cmyk.k, 1)
	        && std::all_of(begin(components), end(components),
	                       [](const auto& c) { return qFuzzyCompare(c.factor, 1); }) ) )
	{
		color_index[number] = Map::getRegistrationColor();
		return;
	}
	
	int color_pos = map->getNumColors();
	auto color = new MapColor(name, color_pos);
	color->setCmyk(cmyk);
	color->setOpacity(opacity);
	color->setNumber(number);  // foom: the design notes, "Loading through a cross-reference table"
	map->addColor(color, color_pos);
	color_index[number] = color;

	if (!components.empty())
	{
		color->setSpotColorComposition(components);
		const MapColorCmyk cmyk(color->getCmyk());
		color->setCmykFromSpotColors();
		if (cmyk != color->getCmyk())
			// The color's CMYK was customized.
			color->setCmyk(cmyk);
		color->setKnockout(!overprinting);
	}
}

namespace {
	quint16 symbolType(const Ocd::BaseSymbolV8& ocd_symbol)
	{
		if (ocd_symbol.type == Ocd::SymbolTypeLine && ocd_symbol.type2 == 1)
			return Ocd::SymbolTypeLineText;
		return ocd_symbol.type;
	}
	
	template< class T >
	quint8 symbolType(const T& ocd_symbol)
	{
		return ocd_symbol.type;
	}
}

template< class F >
void OcdFileImport::importSymbols(const OcdFile< F >& file)
{
	for (auto ocd_symbol_entry : file.symbols())
	{
		auto& ocd_symbol = *ocd_symbol_entry.entity;
		
		// When extra symbols are created, we want to insert the main symbol
		// before them. That is why pos needs to be determined first.
		auto pos = map->getNumSymbols();
		
		Symbol* symbol = nullptr;
		switch (symbolType(ocd_symbol))
		{
		case Ocd::SymbolTypePoint:
			symbol = importPointSymbol(reinterpret_cast<const typename F::PointSymbol&>(ocd_symbol));
			break;
		case Ocd::SymbolTypeLine:
			symbol = importLineSymbol(reinterpret_cast<const typename F::LineSymbol&>(ocd_symbol));
			break;
		case Ocd::SymbolTypeArea:
			symbol = importAreaSymbol(reinterpret_cast<const typename F::AreaSymbol&>(ocd_symbol));
			break;
		case Ocd::SymbolTypeText:
			symbol = importTextSymbol(reinterpret_cast<const typename F::TextSymbol&>(ocd_symbol));
			break;
		case Ocd::SymbolTypeRectangle_V8:
		case Ocd::SymbolTypeRectangle_V9:
			symbol = importRectangleSymbol(reinterpret_cast<const typename F::RectangleSymbol&>(ocd_symbol));
			break;
		case Ocd::SymbolTypeLineText:
			symbol = importLineTextSymbol(reinterpret_cast<const typename F::LineTextSymbol&>(ocd_symbol));
			break;
		default:
			addWarning(OcdFileImport::tr("Unable to import symbol %1.%2 \"%3\": %4") .
			           arg(ocd_symbol.number / F::BaseSymbol::symbol_number_factor) .
			           arg(ocd_symbol.number % F::BaseSymbol::symbol_number_factor) .
			           arg(convertOcdString(ocd_symbol.description)).
			           arg(OcdFileImport::tr("Unsupported type \"%1\".").arg(ocd_symbol.type)) );
			continue;
		}
		
		map->addSymbol(symbol, pos);
		symbol_index[ocd_symbol.number] = symbol;
	}
	resolveSubsymbols();
	
	for (auto ocd_symbol_entry : file.symbols())
	{
		auto& ocd_symbol = *ocd_symbol_entry.entity;
		if (symbol_index.contains(ocd_symbol.number))
			dropRedundantIcon(symbol_index[ocd_symbol.number], ocd_symbol);
	}
}

void OcdFileImport::resolveSubsymbols()
{
	for (auto i = 0; i < map->getNumSymbols(); ++i)
	{
		auto symbol = map->getSymbol(i);
		if (symbol->getType() == Symbol::Combined)
		{
			auto combined = symbol->asCombined();
			if (combined->getNumParts() == 2)
			{
				auto number = combined->getPart(1)->getNumberComponent(2);
				if (number >= 0 && symbol_index.contains(static_cast<unsigned int>(number)))
				{
					combined->setPart(1, symbol_index[static_cast<unsigned int>(number)], false);
				}
			}
		}
	}
	
	// foom: attached point symbols of text symbols (version 10 and later, number factor 1000)
	for (auto it = frame_symbol_numbers.constBegin(); it != frame_symbol_numbers.constEnd(); ++it)
	{
		auto const* symbol = symbol_index.value(it.value(), nullptr);
		if (symbol && symbol->getType() == Symbol::Point)
			it.key()->setFrameSymbol(symbol->asPoint());
		else
			addSymbolWarning(it.key(), tr("Skipping unsupported reference to point symbol '%1.%2'.").arg(it.value() / 1000).arg(it.value() % 1000));
	}
	frame_symbol_numbers.clear();
}


void OcdFileImport::importObjects(const OcdFile<Ocd::FormatV8>& file)
{
	MapPart* part = map->getCurrentPart();
	FILEFORMAT_ASSERT(part);
	
	for (auto ocd_object : file.objects())
	{
		if (ocd_object.entry->symbol)
		{
			if (auto object = importObject(*ocd_object.entity, part))
				part->addObject(object, part->getNumObjects());
		}
	}
}

namespace {

// foom: object timestamps, in the object header since OCD version 12.
// See the design notes, "Object identity and timestamps".
template< class O >
void importObjectDates(const O& /*ocd_object*/, Object* /*object*/)
{
	// nothing before version 12
}

void importObjectDates(const Ocd::ObjectV12& ocd_object, Object* object)
{
	object->setIdentity(object->uuid(),
	                    Ocd::fromDelphiDateTime(Ocd::delphiDateTimeFromBits(ocd_object.creation_date)),
	                    Ocd::fromDelphiDateTime(Ocd::delphiDateTimeFromBits(ocd_object.modification_date)));
}

// foom: the height and the object string of the version 12 object header,
// see the design notes, "OCD 2018 writer".
template< class O, class E >
void importObjectFieldsV12(const O& /*ocd_object*/, const E& /*entry*/, Object* /*object*/)
{
	// nothing before version 12
}

/// The object string of an object, and whether the record holds one.
bool objectStringV12(const Ocd::ObjectV12& ocd_object, const Ocd::ObjectV12::IndexEntryType& entry, QString& text)
{
	if (!ocd_object.object_string_length)
		return false;
	auto const header_size = sizeof(Ocd::ObjectV12) - sizeof(Ocd::OcdPoint32);
	auto const offset = header_size + sizeof(Ocd::OcdPoint32) * (std::size_t(ocd_object.num_items) + ocd_object.num_text);
	// The length counts units of 8 bytes, as the coordinates before the string
	// are counted. The string is UTF-16, little endian, and ends with a zero.
	auto const area = std::size_t(ocd_object.object_string_length) * sizeof(Ocd::OcdPoint32);
	if (offset + area > entry.size)
		return false;
	auto const* bytes = reinterpret_cast<const uchar*>(&ocd_object) + offset;
	text.clear();
	for (std::size_t i = 0; i + 1 < area; i += 2)
	{
		auto const unit = ushort(bytes[i] | (bytes[i + 1] << 8));
		if (!unit)
			break;
		text.append(QChar(unit));
	}
	return true;
}

void importObjectFieldsV12(const Ocd::ObjectV12& ocd_object, const Ocd::ObjectV12::IndexEntryType& entry, Object* object)
{
	if (ocd_object.height)
		object->setTag(QStringLiteral("ele"), QString::number(ocd_object.height / 256000.0, 'g', 10));
	QString text;
	if (objectStringV12(ocd_object, entry, text))
	{
		object->setTag(QStringLiteral("ocd:object_string_type"), QString::number(ocd_object.object_string_type));
		object->setTag(QStringLiteral("ocd:object_string"), text);
	}
}


// foom: the graphic and layout objects, which have no symbol.
// See the design notes, "Graphic and layout objects in OCD files".
struct GraphicSymbol
{
	std::unique_ptr<Symbol> symbol;
	TextObject::HorizontalAlignment alignment = TextObject::AlignLeft;
	bool layout = false;  ///< The object belongs to the layout part.
	bool skip = false;    ///< An empty layout text: nothing to import.
};

template< class O, class E >
GraphicSymbol graphicSymbolFor(const O& /*ocd_object*/, const E& /*entry*/, Map& /*map*/, const QHash<int, const MapColor*>& /*colors*/)
{
	return {};  // nothing before version 12
}

GraphicSymbol graphicSymbolFor(const Ocd::ObjectV12& ocd_object, const Ocd::ObjectV12::IndexEntryType& entry, Map& map, const QHash<int, const MapColor*>& colors)
{
	GraphicSymbol result;
	if (ocd_object.symbol == ocd_graphic_object_symbol)
	{
		// The colour field is the number of a colour of the map.
		auto const* color = colors.value(int(ocd_object.color), nullptr);
		if (color)
			result.symbol = makeOcdGraphicSymbol(ocd_object.type, color, ocd_object.line_width, nullptr, QStringLiteral("Graphic object"));
	}
	else if (ocd_object.symbol == ocd_layout_object_symbol)
	{
		result.layout = true;
		if (ocd_object.type == 4 && ocd_object.num_text == 0)
		{
			result.skip = true;
			return result;
		}
		QString string;
		auto style = OcdLayoutStyle {};
		if (objectStringV12(ocd_object, entry, string) && ocd_object.object_string_type == ocd_layout_object_string_type)
			style = parseOcdLayoutStyle(string);
		auto const cmyk = ocdLayoutCmyk(quint32(ocd_object.color));
		auto const name = ocd_object.type == 2 ? QStringLiteral("Line object")
		                : ocd_object.type == 3 ? QStringLiteral("Area object")
		                : QStringLiteral("Text object");
		if (ocd_object.type >= 2 && ocd_object.type <= 4)
			result.symbol = makeOcdGraphicSymbol(ocd_object.type, ocdLayoutColor(map, cmyk, style.opacity), ocd_object.line_width, &style, name);
		// A justified text without a box stands as a left-aligned one.
		result.alignment = style.alignment == 1 ? TextObject::AlignHCenter
		                 : style.alignment == 2 ? TextObject::AlignRight
		                 : TextObject::AlignLeft;
	}
	return result;
}

MapPart* ensureLayoutPart(Map& map)
{
	auto index = map.findPartIndexWithRole(MapPart::Role::Layout);
	if (index < 0)
	{
		auto* part = new MapPart(QCoreApplication::translate("OpenOrienteering::LayoutPart", "Layout"), &map);
		part->setRole(MapPart::Role::Layout);
		index = map.getNumParts();
		map.addPart(part, std::size_t(index));
	}
	return map.getPart(std::size_t(index));
}

}  // namespace


template< class F >
void OcdFileImport::importObjects(const OcdFile< F >& file)
{
	MapPart* part = map->getCurrentPart();
	FILEFORMAT_ASSERT(part);
	
	for (auto ocd_object : file.objects())
	{
		if ( ocd_object.entry->symbol
		     && ocd_object.entry->status != Ocd::ObjectDeleted
		     && ocd_object.entry->status != Ocd::ObjectDeletedForUndo )
		{
			// foom: a graphic or layout object gets a private symbol of its own
			auto graphic = graphicSymbolFor(*ocd_object.entity, *ocd_object.entry, *map, color_index);
			if (graphic.skip)
				continue;
			if (graphic.symbol)
			{
				pending_graphic_symbol = graphic.symbol.release();
				map->addSymbol(pending_graphic_symbol, map->getNumSymbols());
				text_halign_map[pending_graphic_symbol] = graphic.alignment;
				text_valign_map[pending_graphic_symbol] = TextObject::AlignBaseline;
			}
			auto object = importObject(*ocd_object.entity, part);
			pending_graphic_symbol = nullptr;
			if (object)
			{
				importObjectDates(*ocd_object.entity, object);  // foom
				importObjectFieldsV12(*ocd_object.entity, *ocd_object.entry, object);  // foom
				// foom: the object of a course's own graphics goes to the hidden part of the overrides,
				// see the design notes, "OCD files from the course data".
				auto* target = part;
				if (object->getTag(QStringLiteral("ocd:object_string_type")) == QLatin1String("2"))
					target = ensureCourseOverridesPart(*map);
				else if (graphic.layout)
					target = ensureLayoutPart(*map);
				target->addObject(object, target->getNumObjects());
			}
			else  // foom: keep the record for the round trip
			{
				auto const* data = reinterpret_cast<const char*>(ocd_object.entity);
				auto const offset = data - buffer.constData();
				auto const size = int(ocd_object.entry->size);
				if (offset >= 0 && size > 0 && offset + size <= buffer.size())
				{
					foreign_objects.push_back({ QByteArray(reinterpret_cast<const char*>(ocd_object.entry), int(sizeof(*ocd_object.entry))),
					                            QByteArray(data, size) });
				}
			}
		}
	}
}


template< class F >
void OcdFileImport::importTemplates(const OcdFile< F >& file)
{
	handleStrings(file, { { 8, &OcdFileImport::importTemplate } });
}

void OcdFileImport::importTemplate(const QString& param_string)
{
	OcdParameterStreamReader parameters(param_string);
	
	auto const filename = parameters.value().toString().replace(QLatin1Char('\\'), QLatin1Char('/'));
	auto const clean_path = QDir::cleanPath(filename);
	// Leave template type resolution to Importer::validate().
	auto* templ = new TemplatePlaceholder("", clean_path, map);
	
	// 8 or 9 or 10 ? Only tested with 8 and 11
	double scale_factor = (ocd_version <= 8) ? 0.01 : 1.0;
	unsigned int num_rotation_params = 0;
	double rotation = 0.0;
	double scale_x = 1.0;
	double scale_y = 1.0;
	int dimming = 0;
	bool visible = false;
	bool explicit_positioning = false;
	
	while (parameters.readNext())
	{
		double value;
		bool ok;
		QStringRef param_value = parameters.value();
		switch (parameters.key())
		{
		case 'x':
			explicit_positioning = true;
			value = param_value.toDouble(&ok);
			if (ok)
				templ->setTemplateX(qRound64(value*1000*scale_factor));
			break;
		case 'y':
			explicit_positioning = true;
			value = param_value.toDouble(&ok);
			if (ok)
				templ->setTemplateY(-qRound64(value*1000*scale_factor));
			break;
		case 'a':
		case 'b':
			// TODO: use the distinct angles correctly, not just the average
			explicit_positioning = true;
			rotation += param_value.toDouble(&ok);
			if (ok)
				++num_rotation_params;
			break;
		case 'u':
			explicit_positioning = true;
			value = param_value.toDouble(&ok);
			if (ok && qAbs(value) >= 0.0000000001)
				scale_x = value;
			break;
		case 'v':
			explicit_positioning = true;
			value = param_value.toDouble(&ok);
			if (ok && qAbs(value) >= 0.0000000001)
				scale_y = value;
			break;
		case 'd':
			dimming = param_value.toInt();
			break;
		case 's':
			visible = param_value.toInt();
			break;
		default:
			; // nothing
		}
	}
	
	if (explicit_positioning)
	{
		if (num_rotation_params)
			templ->setTemplateRotation(Georeferencing::degToRad(rotation / num_rotation_params));
		
		templ->setTemplateScaleX(scale_x * scale_factor);
		templ->setTemplateScaleY(scale_y * scale_factor);
	}
	else if (clean_path.endsWith(QLatin1String(".ocd"), Qt::CaseInsensitive))
	{
		// OCD templates must use the map's scale and georeferencing.
		// The transformation must be determined after loading the template.
		templ->setProperty(TemplateMap::ocdTransformProperty(), true);
	}
	
	auto const template_pos = std::min(0, map->getFirstFrontTemplate() - 1);
	map->addTemplate(template_pos, std::unique_ptr<Template>{templ});
	
	if (view)
	{
		auto opacity = qMax(0.0, qMin(1.0, 0.01 * (100 - dimming)));
		view->setTemplateVisibility(templ, { float(opacity), visible });
	}
}


void OcdFileImport::importExtras(const OcdFile<Ocd::FormatV8>& file)
{
	const Ocd::FileHeaderV8* header = file.header();
	map->setMapNotes(convertOcdString< Ocd::FormatV8::Encoding >(file.byteArray().data() + header->info_pos, header->info_size));
}

template< class F >
void OcdFileImport::importExtras(const OcdFile< F >& file)
{
	map->setMapNotes({ });
	handleStrings(file, extraStringHandlers);
}

const std::initializer_list<OcdFileImport::StringHandler> OcdFileImport::extraStringHandlers = {
    {   11, &OcdFileImport::appendNotes },
    { 1061, &OcdFileImport::appendNotes }
};

void OcdFileImport::appendNotes(const QString& param_string)
{
	auto notes = map->getMapNotes();
	notes.append(param_string);
	notes.replace(QLatin1String("\r\n"), QLatin1String("\n"));
	map->setMapNotes(notes);
}


void OcdFileImport::importView(const OcdFile<Ocd::FormatV8>& file)
{
	if (view)
	{
		const Ocd::FileHeaderV8* header = file.header();
		const Ocd::SetupV8* setup = Ocd::getBlockChecked<const Ocd::SetupV8>(file.byteArray(), header->setup_pos);
		if (Q_UNLIKELY(!setup))
			return;

		if (setup->zoom >= MapView::zoom_out_limit && setup->zoom <= MapView::zoom_in_limit)
			view->setZoom(setup->zoom);

		view->setCenter(convertOcdPoint(setup->center));
	}
}

template< class F >
void OcdFileImport::importView(const OcdFile< F >& file)
{
	handleStrings(file, { { 1030, &OcdFileImport::importView } });
}

// foom
template< class F >
void OcdFileImport::importBookmarks(const OcdFile< F >& file)
{
	handleStrings(file, { { 18, &OcdFileImport::importBookmark } });
}

// foom
void OcdFileImport::importBookmark(const QString& param_string)
{
	Bookmark bookmark;
	if (parseOcdBookmark(param_string, bookmark))
		map->addBookmark(bookmark);
}

// foom: the design notes, "OCD string type 26"
template< class F >
void OcdFileImport::importGuides(const OcdFile< F >& file)
{
	handleStrings(file, { { ocd_guide_string_type, &OcdFileImport::importGuide } });
}

// foom
void OcdFileImport::importGuide(const QString& param_string)
{
	Guide guide;
	if (!parseOcdGuide(param_string, guide))
		return;
	auto guides = map->guides();
	guides.list.push_back(guide);
	map->setGuides(std::move(guides));
}

// foom
template< class F >
void OcdFileImport::importLayoutImages(const OcdFile< F >& file)
{
	handleStrings(file, { { ocd_layout_image_string_type, &OcdFileImport::importLayoutImage } });
}

// foom
void OcdFileImport::importLayoutImage(const QString& param_string)
{
	// the design notes, "OCD embedded images".
	QString warning;
	auto image = ocdLayoutImageFromString(param_string, &warning);
	if (!warning.isEmpty())
		addWarning(warning);
	if (!image)
		return;
	
	auto index = map->findPartIndexWithRole(MapPart::Role::Layout);
	if (index < 0)
	{
		auto* part = new MapPart(QCoreApplication::translate("OpenOrienteering::LayoutPart", "Layout"), map);
		part->setRole(MapPart::Role::Layout);
		index = map->getNumParts();
		map->addPart(part, std::size_t(index));
	}
	map->addObject(image.release(), index);
}

void OcdFileImport::importView(const QString& param_string)
{
	OcdParameterStreamReader parameters(param_string);
	
	bool zoom_ok = false;
	double zoom=1.0, offset_x=0.0, offset_y=0.0;
	
	while (parameters.readNext())
	{
		QStringRef param_value = parameters.value();
		switch (parameters.key())
		{
		case 'x':
			offset_x = param_value.toDouble();
			break;
		case 'y':
			offset_y = param_value.toDouble();
			break;
		case 'z':
			zoom = param_value.toDouble(&zoom_ok);
			break;
		case 'h':
			map->setAreaHatchingEnabled(param_value.toInt() != 0);
			break;
		case 'k':
			map->setBaselineViewEnabled(param_value.toInt() != 0);
			break;
		default:
			; // nothing
		}
	}
	
	if (view)
	{
		view->setCenter(MapCoord(offset_x, -offset_y));
		if (zoom_ok)
		{
			view->setZoom(zoom);
		}
	}
}


namespace {

// foom: the course role of a symbol, the design notes, "Course role of symbols".
template< class OcdBaseSymbol >
void importSymbolCourseFields(const OcdBaseSymbol& ocd_base_symbol, Symbol* symbol)
{
	symbol->setCourseFields(ocd_base_symbol.cs_mode, ocd_base_symbol.cs_type, ocd_base_symbol.cd_flags);
}

void importSymbolCourseFields(const Ocd::BaseSymbolV8& /*ocd_base_symbol*/, Symbol* /*symbol*/)
{
	// The symbol record of version 8 has no place for it.
}

}  // namespace

template< class OcdBaseSymbol >
void OcdFileImport::setupBaseSymbol(Symbol* symbol, const OcdBaseSymbol& ocd_base_symbol)
{
	// common fields are name, number, description, helper_symbol, hidden/protected status
	symbol->setName(convertOcdString(ocd_base_symbol.description));
	symbol->setNumberComponent(0, ocd_base_symbol.number / OcdBaseSymbol::symbol_number_factor);
	symbol->setNumberComponent(1, ocd_base_symbol.number % OcdBaseSymbol::symbol_number_factor);
	symbol->setNumberComponent(2, -1);
	symbol->setIsHelperSymbol(false);
	symbol->setProtected(ocd_base_symbol.status & Ocd::SymbolProtected);
	symbol->setHidden(ocd_base_symbol.status & Ocd::SymbolHidden);
	importSymbolCourseFields(ocd_base_symbol, symbol);  // foom
	setupIcon(symbol, ocd_base_symbol);
}


template< >
void OcdFileImport::setupIcon<Ocd::BaseSymbolV8>(Symbol* symbol, const Ocd::BaseSymbolV8& ocd_base_symbol)
try
{
	if (ocd_base_symbol.flags & Ocd::SymbolIconCompressedV8)
		symbol->setCustomIcon(OcdIcon::toQImage(ocd_base_symbol.icon.uncompress()));
	else
		symbol->setCustomIcon(OcdIcon::toQImage(ocd_base_symbol.icon));
}
catch (std::logic_error& e)
{
	addWarning(tr(e.what()));
}

template< class OcdBaseSymbol >
void OcdFileImport::setupIcon(Symbol* symbol, const OcdBaseSymbol& ocd_base_symbol)
{
	symbol->setCustomIcon(OcdIcon::toQImage(ocd_base_symbol.icon));
}


template<>
void OcdFileImport::dropRedundantIcon<Ocd::BaseSymbolV8>(Symbol* symbol, const Ocd::BaseSymbolV8& ocd_base_symbol)
try
{
	const auto imported = symbol->getCustomIcon();
	if (imported.isNull())
		return;
	
	// The comparison is done in OCD format, due to the limited color palette.
	symbol->setCustomIcon({});
	auto ocd_icon = OcdIcon{*map, *symbol};
	if (ocd_base_symbol.flags & Ocd::SymbolIconCompressedV8)
	{
		if (ocd_base_symbol.icon.uncompress() != ocd_icon)
			symbol->setCustomIcon(imported);
	}
	else
	{
		if (ocd_base_symbol.icon != ocd_icon)
			symbol->setCustomIcon(imported);
	}
}
catch (std::logic_error&)
{
	// In general, ocd_base_symbol.icon.uncompress() can throw. But here,
	// it is called after successful icon import - which indicates that
	// uncompress() does not fail for ocd_base_symbol.icon.
}

template<class OcdBaseSymbol>
void OcdFileImport::dropRedundantIcon(Symbol* symbol, const OcdBaseSymbol& ocd_base_symbol)
{
	const auto imported = symbol->getCustomIcon();
	if (imported.isNull())
		return;
	
	// The comparison is done in OCD format, due to the limited color palette.
	symbol->setCustomIcon({});
	if (ocd_base_symbol.icon != OcdIcon{*map, *symbol})
		symbol->setCustomIcon(imported);
}


template< class S >
PointSymbol* OcdFileImport::importPointSymbol(const S& ocd_symbol)
{
	auto symbol = new OcdImportedPointSymbol();
	setupBaseSymbol(symbol, ocd_symbol.base);
	setupPointSymbolPattern(symbol, ocd_symbol.data_size, ocd_symbol.begin_of_elements);
	symbol->setRotatable(ocd_symbol.base.flags & Ocd::SymbolRotatable);
	return symbol;
}

template< class S >
Symbol* OcdFileImport::importLineSymbol(const S& ocd_symbol)
{
	using OcdLineSymbolCommon = Ocd::LineSymbolCommonV8;
	
	// Import a main line.
	auto main_line = new OcdImportedLineSymbol();
	setupBaseSymbol(main_line, ocd_symbol.base);
	importLineSymbolBase(main_line, ocd_symbol.common);
	setupLineSymbolPointSymbols(main_line, ocd_symbol.common, ocd_symbol.begin_of_elements);
	if (ocd_symbol.common.dec_mode == 1 || ocd_symbol.common.dec_mode == 2)  // foom: taper, same values
	{
		main_line->setTaperMode(ocd_symbol.common.dec_mode);
		main_line->setTaperEndPercent(qBound(0, int(ocd_symbol.common.dec_last), 100));
	}
	
	// Import a 'framing' line?
	OcdImportedLineSymbol* framing_line = nullptr;
	if (ocd_symbol.common.framing_width > 0 && ocd_version >= 7)
	{
		framing_line = main_line;
		if ((main_line->line_width && main_line->color)
		    || (main_line->mid_symbol && !main_line->mid_symbol->isEmpty()) )
		{ 
			framing_line = new OcdImportedLineSymbol();
			setupBaseSymbol(framing_line, ocd_symbol.base);
		}
		setupLineSymbolFraming(framing_line, ocd_symbol.common, main_line);
		if (framing_line == main_line)
			framing_line = nullptr;
	}
	
	// Import a 'double' line, including an optional filling?
	OcdImportedLineSymbol* double_line = nullptr;
	auto const is_visible_double_line = [](auto const& ocd_attributes) {
		return ocd_attributes.double_mode != OcdLineSymbolCommon::DoubleLineOff
		       && (ocd_attributes.double_width > 0
		           || ocd_attributes.double_left_width > 0
		           || ocd_attributes.double_right_width > 0);
	};
	if (is_visible_double_line(ocd_symbol.common))
	{
		double_line = main_line;
		if (main_line->dashed
		    || (main_line->line_width && main_line->color)
		    || (main_line->mid_symbol && !main_line->mid_symbol->isEmpty()) )
		{
			double_line = new OcdImportedLineSymbol();
			setupBaseSymbol(double_line, ocd_symbol.base);
		}
		setupLineSymbolDoubleBorder(double_line, ocd_symbol.common);
		if (double_line == main_line)
			double_line = nullptr;
	}
	
	if (ocd_symbol.common.double_flags & OcdLineSymbolCommon::DoubleFlagBackgroundColorOn)
	{
		addSymbolWarning(main_line,
		  OcdFileImport::tr("Unsupported line style '%1'.").arg(QLatin1String("LineStyle::DoubleBackgroundColorOn")) );
	}
	
	// TODO: taper fields (tmode and tlast)
	
	if (!double_line && !framing_line)
		return main_line;
	
	auto combined_line = new CombinedSymbol();
	setupBaseSymbol(combined_line, ocd_symbol.base);
	mergeLineSymbol(combined_line, main_line, framing_line, double_line);
	return combined_line;
}

void OcdFileImport::importLineSymbolBase(OcdImportedLineSymbol* symbol, const Ocd::LineSymbolCommonV8& attributes)
{
	using LineStyle = Ocd::LineSymbolCommonV8;
	
	// Basic line options
	symbol->line_width = convertLength(attributes.line_width);
	symbol->color = symbol->line_width ? convertColor(attributes.line_color) : nullptr;
	
	// Cap and join styles
	switch (attributes.line_style)
	{
	default:
		addSymbolWarning( symbol,
		                  tr("Unsupported line style '%1'.").
		                  arg(attributes.line_style) );
		Q_FALLTHROUGH();
	case LineStyle::BevelJoin_FlatCap:
		symbol->join_style = LineSymbol::BevelJoin;
		symbol->cap_style = LineSymbol::FlatCap;
		break;
	case LineStyle::RoundJoin_RoundCap:
		symbol->join_style = LineSymbol::RoundJoin;
		symbol->cap_style = LineSymbol::RoundCap;
		break;
	case LineStyle::BevelJoin_PointedCap:
		symbol->join_style = LineSymbol::BevelJoin;
		symbol->cap_style = LineSymbol::PointedCap;
		break;
	case LineStyle::RoundJoin_PointedCap:
		symbol->join_style = LineSymbol::RoundJoin;
		symbol->cap_style = LineSymbol::PointedCap;
		break;
	case LineStyle::MiterJoin_FlatCap:
		symbol->join_style = LineSymbol::MiterJoin;
		symbol->cap_style = LineSymbol::FlatCap;
		break;
	case LineStyle::MiterJoin_PointedCap:
		symbol->join_style = LineSymbol::MiterJoin;
		symbol->cap_style = LineSymbol::PointedCap;
		break;
	}
	
	symbol->start_offset = std::max(0, convertLength(attributes.dist_from_start));
	symbol->end_offset = std::max(0, convertLength(attributes.dist_from_end));
	if (symbol->cap_style == LineSymbol::PointedCap)
	{
		// Note: While the property in the file may be different
		// (cf. what is set in the first place), OC*D always
		// draws round joins if the line cap is pointed!
		symbol->join_style = LineSymbol::RoundJoin;
	}
	
	// Handle the dash pattern
	if (attributes.main_gap || attributes.sec_gap)
	{
		if (!attributes.main_length)
		{
			// Invalid dash pattern
			addSymbolWarning( symbol,
			  tr("The dash pattern cannot be imported correctly.") );
		}
		else if (attributes.sec_gap && !attributes.main_gap)
		{
			// Special case main_gap == 0
			symbol->dashed = true;
			symbol->dash_length = convertLength(attributes.main_length) - convertLength(attributes.sec_gap);
			symbol->break_length = convertLength(attributes.sec_gap);
			
			if (attributes.end_length)
			{
				if (qAbs(qint32(attributes.main_length) - 2*attributes.end_length) > 1)
				{
					// End length not equal to 0.5 * main length
					addSymbolWarning( symbol,
					  tr("The dash pattern's end length (%1 mm) cannot be imported correctly. Using %2 mm.").
					  arg(locale.toString(0.001f * convertLength(attributes.end_length)),
					      locale.toString(0.001f * symbol->dash_length)) );
				}
				if (attributes.end_gap)
				{
					addSymbolWarning( symbol,
					  tr("The dash pattern's end gap (%1 mm) cannot be imported correctly. Using %2 mm.").
					  arg(locale.toString(0.001f * convertLength(attributes.end_gap)),
					      locale.toString(0.001f * symbol->break_length)) );
				}
			}
		}
		else
		{
			// Standard case
			symbol->dashed = true;
			symbol->dash_length = convertLength(attributes.main_length);
			symbol->break_length = convertLength(attributes.main_gap);
			
			if (attributes.end_length && attributes.end_length != attributes.main_length)
			{
				if (attributes.main_length && 0.75 >= double(attributes.end_length) / double(attributes.main_length))
				{
					// End length max. 75 % of main length
					symbol->half_outer_dashes = true;
				}
				
				if (qAbs(qint32(attributes.main_length) - 2*attributes.end_length) > 1)
				{
					// End length not equal to 0.5 * main length
					addSymbolWarning( symbol,
					  tr("The dash pattern's end length (%1 mm) cannot be imported correctly. Using %2 mm.").
					  arg(locale.toString(0.001f * convertLength(attributes.end_length)),
					      locale.toString(0.001f * (symbol->half_outer_dashes ? (symbol->dash_length/2) : symbol->dash_length))) );
				}
			}
			
			if (attributes.sec_gap)
			{
				symbol->dashes_in_group = 2;
				symbol->in_group_break_length = convertLength(attributes.sec_gap);
				symbol->dash_length = (symbol->dash_length - symbol->in_group_break_length) / 2;
				
				if (attributes.end_length && attributes.end_gap != attributes.sec_gap)
				{
					addSymbolWarning( symbol,
					  tr("The dash pattern's end gap (%1 mm) cannot be imported correctly. Using %2 mm.").
					  arg(locale.toString(0.001f * convertLength(attributes.end_gap)),
					      locale.toString(0.001f * symbol->in_group_break_length)) );
				}
			}
		}
	} 
	else
	{
		symbol->segment_length = convertLength(attributes.main_length);
		symbol->end_length = convertLength(attributes.end_length);
	}
}

void OcdFileImport::setupLineSymbolFraming(OcdFileImport::OcdImportedLineSymbol* framing_line, const Ocd::LineSymbolCommonV8& attributes, const LineSymbol* main_line)
{
	using LineStyle = Ocd::LineSymbolCommonV8;
	
	// Basic line options
	framing_line->line_width = convertLength(attributes.framing_width);
	framing_line->color = framing_line->line_width ? convertColor(attributes.framing_color) : nullptr;
	
	// Cap and join styles
	switch (attributes.framing_style)
	{
	case LineStyle::BevelJoin_FlatCap:
		framing_line->join_style = LineSymbol::BevelJoin;
		framing_line->cap_style = LineSymbol::FlatCap;
		break;
	case LineStyle::RoundJoin_RoundCap:
		framing_line->join_style = LineSymbol::RoundJoin;
		framing_line->cap_style = LineSymbol::RoundCap;
		break;
	case LineStyle::MiterJoin_FlatCap:
		framing_line->join_style = LineSymbol::MiterJoin;
		framing_line->cap_style = LineSymbol::FlatCap;
		break;
	default:
		addSymbolWarning( main_line, 
		                  tr("Unsupported framing line style '%1'.").
		                  arg(attributes.framing_style) );
	}
}

void OcdFileImport::setupLineSymbolDoubleBorder(OcdFileImport::OcdImportedLineSymbol* double_line, const Ocd::LineSymbolCommonV8& attributes)
{
	using OcdLineSymbolCommon = Ocd::LineSymbolCommonV8;
	
	double_line->line_width = convertLength(attributes.double_width);
	if (double_line->line_width
	    && attributes.double_flags & OcdLineSymbolCommon::DoubleFlagFillColorOn)
		double_line->color = convertColor(attributes.double_color);
	else
		double_line->color = nullptr;
	double_line->cap_style = LineSymbol::FlatCap;
	double_line->join_style = LineSymbol::MiterJoin;
	
	double_line->have_border_lines = true;
	LineSymbolBorder& border = double_line->getBorder();
	LineSymbolBorder& right_border = double_line->getRightBorder();
	
	// Border color and width
	border.width = convertLength(attributes.double_left_width);
	border.color = border.width ? convertColor(attributes.double_left_color) : nullptr;
	border.shift = convertLength(attributes.double_left_width) / 2 + (convertLength(attributes.double_width) - double_line->line_width) / 2;
	
	right_border.width = convertLength(attributes.double_right_width);
	right_border.color = right_border.width ? convertColor(attributes.double_right_color) : nullptr;
	right_border.shift = convertLength(attributes.double_right_width) / 2 + (convertLength(attributes.double_width) - double_line->line_width) / 2;
	
	// The borders and the filling may be dashed
	if (attributes.double_gap > 0 && attributes.double_mode != OcdLineSymbolCommon::DoubleLineContinuous)
	{
		border.dashed = true;
		border.dash_length = convertLength(attributes.double_length);
		border.break_length = convertLength(attributes.double_gap);
		
		if (attributes.double_mode != OcdLineSymbolCommon::DoubleLineLeftBorderDashed)
		{
			right_border.dashed = border.dashed;
			right_border.dash_length = border.dash_length;
			right_border.break_length = border.break_length;
			
			if (attributes.double_mode == OcdLineSymbolCommon::DoubleLineAllDashed)
			{
				double_line->setDashed(true);
				double_line->setDashesInGroup(1);
				double_line->setDashLength(border.dash_length);
				double_line->setBreakLength(border.break_length);
				double_line->setHalfOuterDashes(false);
			}
		}
	}
}

void OcdFileImport::setupLineSymbolPointSymbols(OcdFileImport::OcdImportedLineSymbol* line_symbol, const Ocd::LineSymbolCommonV8& attributes, const Ocd::PointSymbolElementV8* elements)
{
	const Ocd::OcdPoint32* coords = reinterpret_cast<const Ocd::OcdPoint32*>(elements);
	
	// Special case main_gap == 0: swapped in Mapper
	auto gaps_swapped = attributes.sec_gap && !attributes.main_gap && attributes.main_length;
	if (attributes.primary_data_size > 0)
	{
		line_symbol->mid_symbol_placement = gaps_swapped ? LineSymbol::CenterOfDashGroup : LineSymbol::CenterOfGap;
		line_symbol->mid_symbols_per_spot = attributes.num_prim_sym;
		line_symbol->mid_symbol_distance = convertLength(attributes.prim_sym_dist);
		line_symbol->show_at_least_one_symbol = true;
		line_symbol->mid_symbol = new OcdImportedPointSymbol();
		setupPointSymbolPattern(line_symbol->mid_symbol, attributes.primary_data_size, elements);
		coords += attributes.primary_data_size;
		if (attributes.secondary_data_size > 0)  // foom: the secondary mid symbol
		{
			auto* secondary = new OcdImportedPointSymbol();
			setupPointSymbolPattern(secondary, attributes.secondary_data_size, reinterpret_cast<const Ocd::PointSymbolElementV8*>(coords));
			line_symbol->setMidSymbol2(secondary);
		}
	}
	else if (attributes.secondary_data_size > 0)
	{
		line_symbol->mid_symbol_placement = gaps_swapped ? LineSymbol::CenterOfGap : LineSymbol::CenterOfDashGroup;
		line_symbol->mid_symbols_per_spot = 1;
		line_symbol->show_at_least_one_symbol = true;
		line_symbol->mid_symbol = new OcdImportedPointSymbol();
		setupPointSymbolPattern(line_symbol->mid_symbol, attributes.secondary_data_size, elements);
	}
	// FIXME: not really sure how this translates... need test cases
	line_symbol->minimum_mid_symbol_count = 0; //1 + ocd_symbol->smin;
	line_symbol->minimum_mid_symbol_count_when_closed = 0; //1 + ocd_symbol->smin;
	coords += attributes.secondary_data_size;
	
	if (attributes.corner_data_size > 0)
	{
		line_symbol->dash_symbol = new OcdImportedPointSymbol();
		setupPointSymbolPattern(line_symbol->dash_symbol, attributes.corner_data_size, reinterpret_cast<const Ocd::PointSymbolElementV8*>(coords));
		line_symbol->dash_symbol->setName(QCoreApplication::translate("OpenOrienteering::LineSymbolSettings", "Dash symbol"));
		coords += attributes.corner_data_size;
	}
	if (attributes.start_data_size > 0)
	{
		line_symbol->start_symbol = new OcdImportedPointSymbol();
		setupPointSymbolPattern(line_symbol->start_symbol, attributes.start_data_size, reinterpret_cast<const Ocd::PointSymbolElementV8*>(coords));
		line_symbol->start_symbol->setName(QCoreApplication::translate("OpenOrienteering::LineSymbolSettings", "Start symbol"));
		coords += attributes.start_data_size;
	}
	if (attributes.end_data_size > 0)
	{
		line_symbol->end_symbol = new OcdImportedPointSymbol();
		setupPointSymbolPattern(line_symbol->end_symbol, attributes.end_data_size, reinterpret_cast<const Ocd::PointSymbolElementV8*>(coords));
		line_symbol->end_symbol->setName(QCoreApplication::translate("OpenOrienteering::LineSymbolSettings", "End symbol"));
	}
	
	// Suppress dash symbol at line ends if both start symbol and end symbol exist,
	// but don't create a warning unless a dash symbol is actually defined
	// and the line symbol is not Mapper's 799 Simple orienteering course.
	if (line_symbol->start_symbol && line_symbol->end_symbol)
	{
		line_symbol->setSuppressDashSymbolAtLineEnds(true);
		if (line_symbol->dash_symbol && line_symbol->getNumberComponent(0) != 799)
		{
			addSymbolWarning(line_symbol, tr("Suppressing dash symbol at line ends."));
		}
	}
}

void OcdFileImport::mergeLineSymbol(CombinedSymbol* full_line, LineSymbol* main_line, LineSymbol* framing_line, LineSymbol* double_line)
{
	full_line->setNumParts(3); // reserve
	
	int part = 0;
	full_line->setPart(part++, main_line, true);
	main_line->setHidden(false);
	main_line->setProtected(false);
	main_line->setName(main_line->getName() + tr(" - main line"));

	if (double_line)
	{
		full_line->setPart(part++, double_line, true);
		double_line->setHidden(false);
		double_line->setProtected(false);
		double_line->setName(double_line->getName() + tr(" - double line"));
	}
	if (framing_line)
	{
		full_line->setPart(part++, framing_line, true);
		framing_line->setHidden(false);
		framing_line->setProtected(false);
		framing_line->setName(framing_line->getName() + tr(" - framing"));
	}
	full_line->setNumParts(part);
}

Symbol* OcdFileImport::importAreaSymbol(const Ocd::AreaSymbolV8& ocd_symbol)
{
	FILEFORMAT_ASSERT(ocd_version <= 8);
	auto symbol = new OcdImportedAreaSymbol();
	setupBaseSymbol(symbol, ocd_symbol.base);
	setupAreaSymbolCommon(
	            symbol,
	            ocd_symbol.base.flags & Ocd::SymbolRotatable,
	            ocd_symbol.fill_on,
	            ocd_symbol.common,
	            ocd_symbol.data_size,
	            ocd_symbol.begin_of_elements);
	return symbol;
}

namespace {

// foom: the irregular pattern fields exist in version 12 only; see the design notes, "Area symbol properties".
template< class S >
void setAreaPatternVariation(AreaSymbol* /*symbol*/, const S& /*ocd_symbol*/)
{
	// nothing
}

void setAreaPatternVariation(AreaSymbol* symbol, const Ocd::AreaSymbolV12& ocd_symbol)
{
	if (!ocd_symbol.structure_variation_x && !ocd_symbol.structure_variation_y && !ocd_symbol.structure_minimum_dist)
		return;
	for (int i = 0; i < symbol->getNumFillPatterns(); ++i)
	{
		auto& pattern = symbol->getFillPattern(i);
		if (pattern.type != AreaSymbol::FillPattern::PointPattern)
			continue;
		pattern.variation_x = qBound(0, int(ocd_symbol.structure_variation_x), 100);
		pattern.variation_y = qBound(0, int(ocd_symbol.structure_variation_y), 100);
		pattern.minimum_distance = 10 * int(ocd_symbol.structure_minimum_dist);  // 0.01 mm to 1/1000 mm
	}
}

}  // namespace

template< class S >
Symbol* OcdFileImport::importAreaSymbol(const S& ocd_symbol)
{
	FILEFORMAT_ASSERT(ocd_version >= 9);
	auto symbol = new OcdImportedAreaSymbol();
	setupBaseSymbol(symbol, ocd_symbol.base);
	setupAreaSymbolCommon(
	            symbol,
	            ocd_symbol.base.flags & Ocd::SymbolRotatable,
	            ocd_symbol.common.fill_on_V9,
	            ocd_symbol.common,
	            ocd_symbol.data_size,
	            ocd_symbol.begin_of_elements);
	setAreaPatternVariation(symbol, ocd_symbol);  // foom
	if (!ocd_symbol.common.border_on_V9)
	{
		return symbol;
	}
	
	if (ocd_symbol.border_symbol == ocd_symbol.base.number)
	{
		addSymbolWarning(symbol, OcdFileImport::tr("The border of this symbol could not be loaded."));
		return symbol;
	}
	
	auto combined = new CombinedSymbol();
	setupBaseSymbol(combined, ocd_symbol.base);
	combined->setNumParts(2);
	combined->setPart(0, symbol, true);
	auto border = duplicate(*map->getUndefinedLine());
	border->setNumberComponent(0, symbol->getNumberComponent(0));
	border->setNumberComponent(1, symbol->getNumberComponent(1));
	border->setNumberComponent(2, static_cast<int>(ocd_symbol.border_symbol));
	combined->setPart(1, border.release(), true);
	return combined;
}

void OcdFileImport::setupAreaSymbolCommon(OcdImportedAreaSymbol* symbol, bool rotatable, bool fill_on, const Ocd::AreaSymbolCommonV8& ocd_symbol, std::size_t data_size, const Ocd::PointSymbolElementV8* elements)
{
	// Basic area symbol fields: minimum_area, color
	symbol->minimum_area = 0;
	symbol->color = fill_on ? convertColor(ocd_symbol.fill_color) : nullptr;
	symbol->setRotatable(rotatable);
	symbol->patterns.clear();
	symbol->patterns.reserve(4);
	
	// Hatching
	if (ocd_symbol.hatch_mode != Ocd::HatchNone && ocd_symbol.hatch_line_width)
	{
		AreaSymbol::FillPattern pattern;
		pattern.type = AreaSymbol::FillPattern::LinePattern;
		pattern.angle = convertAngle(ocd_symbol.hatch_angle_1);
		pattern.setRotatable(true);
		pattern.line_spacing = convertLength(ocd_symbol.hatch_dist);
		pattern.line_offset = 0;
		pattern.line_width = convertLength(ocd_symbol.hatch_line_width);
		pattern.line_color = pattern.line_width ? convertColor(ocd_symbol.hatch_color) : nullptr;
		if (ocd_version <= 8)
		{
			pattern.line_spacing += pattern.line_width;
		}
		symbol->patterns.push_back(pattern);
		
		if (ocd_symbol.hatch_mode == Ocd::HatchCross)
		{
			// Second hatch, same as the first, just a different angle
			pattern.angle = convertAngle(ocd_symbol.hatch_angle_2);
			symbol->patterns.push_back(pattern);
		}
	}
	
	if (ocd_symbol.structure_mode != Ocd::StructureNone && ocd_symbol.structure_height
	    && ocd_symbol.structure_width && data_size)
	{
		AreaSymbol::FillPattern pattern;
		pattern.type = AreaSymbol::FillPattern::PointPattern;
		pattern.angle = convertAngle(ocd_symbol.structure_angle);
		pattern.setRotatable(true);
		pattern.point_distance = convertLength(ocd_symbol.structure_width);
		pattern.line_spacing = convertLength(ocd_symbol.structure_height);
		pattern.line_offset = 0;
		pattern.offset_along_line = 0;
		// FIXME: somebody needs to own this symbol and be responsible for deleting it
		// Right now it looks like a potential memory leak
		pattern.point = new OcdImportedPointSymbol();
		setupPointSymbolPattern(pattern.point, data_size, elements);
		
		// OC*D 8 has a "staggered" pattern mode, where successive rows are shifted width/2 relative
		// to each other. We need to simulate this in Mapper with two overlapping patterns, each with
		// twice the height. The second is then offset by width/2, height/2.
		if (ocd_symbol.structure_mode == Ocd::StructureShiftedRows)
		{
			pattern.line_spacing *= 2;
			symbol->patterns.push_back(pattern);
			
			pattern.line_offset = pattern.line_spacing / 2;
			pattern.offset_along_line = pattern.point_distance / 2;
			pattern.point = duplicate(*pattern.point).release();
		}
		
		if (ocd_version >= 12)
		{
			switch (ocd_symbol.structure_draw_V12)
			{
			case 0:
				pattern.setClipping(AreaSymbol::FillPattern::Default);
				break;
			case 1:
				pattern.setClipping(AreaSymbol::FillPattern::NoClippingIfCompletelyInside);
				break;
			case 2:
				pattern.setClipping(AreaSymbol::FillPattern::NoClippingIfCenterInside);
				break;
			case 3:
				pattern.setClipping(AreaSymbol::FillPattern::NoClippingIfPartiallyInside);
				break;
			}
		}
		symbol->patterns.push_back(pattern);
	}
}

template< class S >
TextSymbol* OcdFileImport::importTextSymbol(const S& ocd_symbol)
{
	auto symbol = new OcdImportedTextSymbol();
	setupBaseSymbol(symbol, ocd_symbol.base);
	setBasicAttributes(symbol, convertOcdString(ocd_symbol.font_name), ocd_symbol.basic);
	setSpecialAttributes(symbol, ocd_symbol.special);
	setFraming(symbol, ocd_symbol.framing);
	if (ocd_version >= 10 && ocd_symbol.framing.point_symbol_on_V10)
		frame_symbol_numbers[symbol] = ocd_symbol.framing.point_symbol_number_V10;  // foom: resolved in resolveSubsymbols()
	symbol->setRotatable(ocd_symbol.base.flags & Ocd::SymbolRotatable);
	return symbol;
}

template< class S >
TextSymbol* OcdFileImport::importLineTextSymbol(const S& ocd_symbol)
{
	auto symbol = new OcdImportedTextSymbol();
	setupBaseSymbol(symbol, ocd_symbol.base);
	setBasicAttributes(symbol, convertOcdString(ocd_symbol.font_name), ocd_symbol.basic);
	setFraming(symbol, ocd_symbol.framing);
	
	// foom: text on a path, see the design notes.
	symbol->setFollowsPath(true);
	symbol->setPathAlignment(ocd_symbol.basic.alignment & 0x0f);
	return symbol;
}

template< class S >
LineSymbol* OcdFileImport::importRectangleSymbol(const S& ocd_symbol)
{
	auto symbol = new OcdImportedLineSymbol();
	setupBaseSymbol(symbol, ocd_symbol.base);
	
	symbol->line_width = convertLength(ocd_symbol.line_width);
	symbol->color = symbol->line_width ? convertColor(ocd_symbol.line_color) : nullptr;
	symbol->cap_style = LineSymbol::RoundCap;
	symbol->join_style = LineSymbol::RoundJoin;
	
	auto rect = RectangleInfo();
	rect.border_line = symbol;
	rect.corner_radius = 0.001 * convertLength(ocd_symbol.corner_radius);
	rect.has_grid = ocd_symbol.grid_flags & 1;
	
	if (rect.has_grid)
	{
		auto inner_line = new OcdImportedLineSymbol();
		setupBaseSymbol(inner_line, ocd_symbol.base);
		inner_line->setNumberComponent(2, 1);  // TODO: Dynamic
		inner_line->line_width = qRound(1000 * 0.15);
		inner_line->color = symbol->color;
		map->addSymbol(inner_line, map->getNumSymbols());
		
		auto text = new OcdImportedTextSymbol();
		setupBaseSymbol(text, ocd_symbol.base);
		text->setNumberComponent(2, 2);  // TODO: Dynamic
		text->font_family = QString::fromLatin1("Arial");
		text->font_size = qRound(1000 * (15 / 72.0 * 25.4));
		text->color = symbol->color;
		text->bold = true;
		text->updateQFont();
		map->addSymbol(text, map->getNumSymbols());
		
		rect.inner_line = inner_line;
		rect.text = text;
		rect.numbering_on = ocd_symbol.grid_flags & 2;
		rect.number_from_bottom = ocd_symbol.grid_flags & 4;
		rect.cell_width = 0.001 * convertLength(ocd_symbol.cell_width);
		rect.cell_height = 0.001 * convertLength(ocd_symbol.cell_height);
		rect.unnumbered_cells = ocd_symbol.unnumbered_cells;
		rect.unnumbered_text = convertOcdString(ocd_symbol.unnumbered_text);
	}
	/// \todo review symbol number signedness @symbol vs. @object
	rectangle_info.insert(int(ocd_symbol.base.number), rect);
	
	return symbol;
}

void OcdFileImport::setupPointSymbolPattern(PointSymbol* symbol, std::size_t data_size, const Ocd::PointSymbolElementV8* elements)
{
	FILEFORMAT_ASSERT(symbol);
	
	symbol->setRotatable(true);
	bool base_symbol_used = false;
	
	for (std::size_t i = 0; i < data_size; i += 2)
	{
		const Ocd::PointSymbolElementV8* element = reinterpret_cast<const Ocd::PointSymbolElementV8*>(&reinterpret_cast<const Ocd::OcdPoint32*>(elements)[i]);
		const Ocd::OcdPoint32* const coords = reinterpret_cast<const Ocd::OcdPoint32*>(elements) + i + 2;
		switch (element->type)
		{
		case Ocd::PointSymbolElementV8::TypeDot:
			if (element->diameter > 0)
			{
				bool can_use_base_symbol = (!base_symbol_used && (!element->num_coords || (!coords[0].x && !coords[0].y)));
				PointSymbol* working_symbol = can_use_base_symbol ? symbol : new PointSymbol();
				working_symbol->setInnerColor(convertColor(element->color));
				working_symbol->setInnerRadius(convertLength(element->diameter) / 2);
				working_symbol->setOuterColor(nullptr);
				working_symbol->setOuterWidth(0);
				if (can_use_base_symbol)
				{
					base_symbol_used = true;
				}
				else
				{
					working_symbol->setRotatable(false);
					auto element_object = new PointObject(working_symbol);
					if (element->num_coords)
					{
						const MapCoord coord = convertOcdPoint(coords[0]);
						element_object->setPosition(coord.nativeX(), coord.nativeY());
					}
					symbol->addElement(symbol->getNumElements(), element_object, working_symbol);
				}
			}
			break;
		case Ocd::PointSymbolElementV8::TypeCircle:
			{
				decltype(element->diameter) element_radius =
				        (ocd_version <= 8) ? (element->diameter / 2 - element->line_width)
				                           : ((element->diameter - element->line_width) / 2);
				if (element_radius > 0 && element->line_width > 0)
				{
					bool can_use_base_symbol = (!base_symbol_used && (!element->num_coords || (!coords[0].x && !coords[0].y)));
					PointSymbol* working_symbol = can_use_base_symbol ? symbol : new PointSymbol();
					working_symbol->setInnerColor(nullptr);
					working_symbol->setInnerRadius(convertLength(element_radius));
					working_symbol->setOuterColor(convertColor(element->color));
					working_symbol->setOuterWidth(convertLength(element->line_width));
					if (can_use_base_symbol)
					{
						base_symbol_used = true;
					}
					else
					{
						working_symbol->setRotatable(false);
						auto element_object = new PointObject(working_symbol);
						if (element->num_coords)
						{
							const MapCoord coord = convertOcdPoint(coords[0]);
							element_object->setPosition(coord.nativeX(), coord.nativeY());
						}
						symbol->addElement(symbol->getNumElements(), element_object, working_symbol);
					}
				}
				break;
			}
		case Ocd::PointSymbolElementV8::TypeLine:
			if (element->line_width > 0)
			{
				auto element_symbol = new OcdImportedLineSymbol();
				element_symbol->line_width = convertLength(element->line_width);
				element_symbol->color = convertColor(element->color);
				// The flags variable doesn't seem to contain individual flags.
				switch (element->flags)
				{
				default:
					qDebug("Ocd::PointSymbolElementV8: Unknown flags value %d", element->flags);
					Q_FALLTHROUGH();
				case Ocd::PointSymbolElementV8::NoFlags:
					element_symbol->setCapStyle(LineSymbol::FlatCap);
					element_symbol->setJoinStyle(LineSymbol::BevelJoin);
					break;
				case Ocd::PointSymbolElementV8::RoundStyle:
					element_symbol->setCapStyle(LineSymbol::RoundCap);
					element_symbol->setJoinStyle(LineSymbol::RoundJoin);
					break;
				case Ocd::PointSymbolElementV8::FlatMiterStyle:
					element_symbol->setCapStyle(LineSymbol::FlatCap);
					element_symbol->setJoinStyle(LineSymbol::MiterJoin);
					break;
				}
				auto element_object = new OcdImportedPathObject(element_symbol);
				fillPathCoords(element_object, false, element->num_coords, coords);
				element_object->recalculateParts();
				symbol->addElement(symbol->getNumElements(), element_object, element_symbol);
			}
			break;
		case Ocd::PointSymbolElementV8::TypeArea:
			{
				auto element_symbol = new OcdImportedAreaSymbol();
				element_symbol->color = convertColor(element->color);
				auto element_object = new OcdImportedPathObject(element_symbol);
				fillPathCoords(element_object, true, element->num_coords, coords);
				element_object->recalculateParts();
				symbol->addElement(symbol->getNumElements(), element_object, element_symbol);
			}
			break;
		default:
			; // TODO: not-supported warning
		}
		i += element->num_coords;
	}
}

template< class O >
Object* OcdFileImport::importObject(const O& ocd_object, MapPart* part)
{
	Symbol* symbol = nullptr;
	if (ocd_object.symbol >= 0)
	{
		symbol = symbol_index[ocd_object.symbol];
	}
	else if (pending_graphic_symbol)
	{
		symbol = pending_graphic_symbol;  // foom
	}

	if (!symbol)
	{
		switch (ocd_object.type)
{
		case 1:
			symbol = map->getUndefinedPoint();
			break;
		case 2:
		case 3:
			symbol = map->getUndefinedLine();
			break;
		case 4:
		case 5:
			symbol = map->getUndefinedText();
			break;
		default:
			addWarning(OcdFileImport::tr("Unable to load object"));
			qDebug() << "Undefined object type" << ocd_object.type << " for object of symbol" << ocd_object.symbol;
			return nullptr;
		}
	}
		
	if (symbol->getType() == Symbol::Line && rectangle_info.contains(ocd_object.symbol))
	{
		Object* object = importRectangleObject(ocd_object, part, rectangle_info[ocd_object.symbol]);
		if (!object)
			addWarning(OcdFileImport::tr("Unable to import rectangle object"));
		return object;
	}
	
	if (symbol->getType() == Symbol::Point)
	{
		auto p = new PointObject();
		p->setSymbol(symbol, true);
		
		// extra properties: rotation
		auto point_symbol = reinterpret_cast<PointSymbol*>(symbol);
		if (point_symbol->isRotatable())
		{
			p->setRotation(convertAngle(ocd_object.angle));
		}
		else if (ocd_object.angle != 0)
		{
			if (!point_symbol->isSymmetrical())
			{
				point_symbol->setRotatable(true);
				p->setRotation(convertAngle(ocd_object.angle));
			}
		}
		
		const MapCoord pos = convertOcdPoint(ocd_object.coords[0]);
		p->setPosition(pos.nativeX(), pos.nativeY());
		
		p->setMap(map);
		return p;
	}
	else if (symbol->getType() == Symbol::Text)
	{
		auto t = new TextObject(symbol);
		t->setText(getObjectText(ocd_object));
		t->setRotation(convertAngle(ocd_object.angle));
		t->setHorizontalAlignment(text_halign_map.value(symbol));
		// Vertical alignment is set in fillTextPathCoords().
		
		// foom: text on a path takes the whole coordinate list
		if (reinterpret_cast<TextSymbol*>(symbol)->followsPath())
		{
			OcdImportedPathObject anchor_path;
			fillPathCoords(&anchor_path, false, ocd_object.num_items, reinterpret_cast<const Ocd::OcdPoint32 *>(ocd_object.coords));
			t->setAnchorPath(anchor_path.getRawCoordinateVector());
			t->setVerticalAlignment(TextObject::AlignBaseline);
			t->setMap(map);
			return t;
		}
		// Text objects need special path translation
		if (!fillTextPathCoords(t, reinterpret_cast<TextSymbol*>(symbol), ocd_object.num_items, reinterpret_cast<const Ocd::OcdPoint32 *>(ocd_object.coords)))
		{
			addWarning(OcdFileImport::tr("Not importing text symbol, couldn't figure out path' (npts=%1): %2")
			           .arg(ocd_object.num_items).arg(t->getText()));
			delete t;
			return nullptr;
		}
		t->setMap(map);
		return t;
	}
	else if (symbol->getType() == Symbol::Line || symbol->getType() == Symbol::Area || symbol->getType() == Symbol::Combined)
	{
		auto p = new OcdImportedPathObject(symbol);
		p->setPatternRotation(convertAngle(ocd_object.angle));
		
		// Normal path
		fillPathCoords(p, symbol->getContainedTypes() & Symbol::Area, ocd_object.num_items, reinterpret_cast<const Ocd::OcdPoint32 *>(ocd_object.coords));
		p->recalculateParts();
		p->setMap(map);
		return p;
	}
	
	return nullptr;
}

QString OcdFileImport::getObjectText(const Ocd::ObjectV8& ocd_object) const
{
	auto input  = ocd_object.coords + ocd_object.num_items;
	auto maxlen = uint(sizeof(Ocd::OcdPoint32) * ocd_object.num_text);
	QString object_text;
	if (ocd_object.unicode && ocd_version >= 8)
	{
		object_text = convertOcdString(reinterpret_cast<const QChar*>(input), maxlen/2);
	}
	else
	{
		object_text = convertOcdString<Ocd::Custom8BitEncoding>(reinterpret_cast<const char*>(input), maxlen);
	}
	
	// Remove leading "\r\n"
	if (object_text.startsWith(QLatin1String("\r\n")))
	{
		object_text.remove(0, 2);
	}
	
	return object_text;
}

namespace {
	
QString fromRawOcdChars(const QChar* first, const QChar* last)
{
	if (first != last && first + 1 != last && first[0] == QChar::CarriageReturn && first[1] == QChar::LineFeed)
		first += 2;
	last = std::find(first, last, QChar::Null);
	return QString(first, std::distance(first, last));
}

}

template< class O >
QString OcdFileImport::getObjectText(const O& ocd_object) const
{
	auto* first = reinterpret_cast<const Ocd::OcdPoint32*>(ocd_object.coords) + ocd_object.num_items;
	auto* last = first + ocd_object.num_text;
	return fromRawOcdChars(reinterpret_cast<const QChar*>(first), reinterpret_cast<const QChar*>(last));
}


template< class O >
Object* OcdFileImport::importRectangleObject(const O& ocd_object, MapPart* part, const OcdFileImport::RectangleInfo& rect)
{
	if (ocd_object.num_items != 4)
	{
		qDebug() << "importRectangleObject called with num_items =" << ocd_object.num_items << "for object of symbol" << ocd_object.symbol;
		if (ocd_object.num_items != 5)  // 5 coords are handled like 4 coords now
			return nullptr;
	}
	return importRectangleObject(ocd_object.coords, part, rect);
}

Object* OcdFileImport::importRectangleObject(const Ocd::OcdPoint32* ocd_points, MapPart* part, const OcdFileImport::RectangleInfo& rect)
{
	// foom: through the rectangle box generator, see the design notes, "Rectangle objects".
	ops::RectangleBoxGeometry geometry;
	geometry.bottom_left = MapCoordF(convertOcdPoint(ocd_points[0]));
	geometry.top_right = MapCoordF(convertOcdPoint(ocd_points[2]));
	geometry.top_left = MapCoordF(convertOcdPoint(ocd_points[3]));
	
	ops::RectangleBoxParams params;
	params.frame = rect.border_line;
	params.corner_radius = rect.corner_radius;
	if (rect.has_grid)
	{
		params.grid = rect.inner_line;
		params.cell_width = rect.cell_width;
		params.cell_height = rect.cell_height;
		params.numbering = rect.numbering_on;
		params.number_from_bottom = rect.number_from_bottom;
		params.number_text = rect.text;
		params.unnumbered_cells = rect.unnumbered_cells;
		params.unnumbered_text = rect.unnumbered_text;
	}
	if (next_rectangle_group <= 0)
		next_rectangle_group = nextObjectGroupId(*map);
	params.group = next_rectangle_group++;
	
	auto result = ops::rectangleBox(*map, geometry, params);
	if (!result.error.isEmpty() || result.out.empty())
		return nullptr;
	
	// The frame is returned to the caller, which adds it; the rest is added here.
	for (std::size_t i = 1; i < result.out.size(); ++i)
	{
		auto* object = result.out[i].release();
		object->setMap(map);
		part->addObject(object, part->getNumObjects());
	}
	auto* frame = result.out.front().release();
	frame->setMap(map);
	return frame;
}

void OcdFileImport::setPathHolePoint(OcdImportedPathObject *object, quint32 pos)
{
	// Look for curve start points before the current point and apply hole point only if no such point is there.
	// This prevents hole points in the middle of a curve caused by incorrect map objects.
	if (pos >= 1 && object->coords[pos].isCurveStart())
		; //object->coords[i-1].setHolePoint(true);
	else if (pos >= 2 && object->coords[pos-1].isCurveStart())
		; //object->coords[i-2].setHolePoint(true);
	else if (pos >= 3 && object->coords[pos-2].isCurveStart())
		; //object->coords[i-3].setHolePoint(true);
	else if (pos > 0) // Don't start with hole point.
		object->coords[pos].setHolePoint(true);
}

void OcdFileImport::setPointFlags(OcdImportedPathObject* object, quint32 pos, bool is_area, const Ocd::OcdPoint32& ocd_point)
{
	// We can support CurveStart, HolePoint, DashPoint.
	// CurveStart needs to be applied to the main point though, not the control point, and
	// hole points need to bet set as the last point of a part of an area object instead of the first point of the next part
	if (ocd_point.x & Ocd::OcdPoint32::FlagCtl1 && pos > 0)
		object->coords[pos-1].setCurveStart(true);
	if ((ocd_point.y & Ocd::OcdPoint32::FlagDash) || (ocd_point.y & Ocd::OcdPoint32::FlagCorner))
		object->coords[pos].setDashPoint(true);
	if (ocd_point.y & Ocd::OcdPoint32::FlagCorner)
	{
		// foom: the design notes, "Corner and virtual gap vertices":
		// only where the export writes a dash point alone with the dash flag of the format
		auto const* line = object->getSymbol() && object->getSymbol()->getType() == Symbol::Line
		                   ? object->getSymbol()->asLine() : nullptr;
		if (line && line->isDashed() && (!line->getDashSymbol() || line->getDashSymbol()->isEmpty()))
			object->coords[pos].setCornerPoint(true);
	}
	if (ocd_point.y & Ocd::OcdPoint32::FlagHole && pos > 1 && is_area)
		setPathHolePoint(object, pos - 1);
}

/** Translates the OC*D path given in the last two arguments into an Object.
 */
void OcdFileImport::fillPathCoords(OcdImportedPathObject *object, bool is_area, quint32 num_points, const Ocd::OcdPoint32* ocd_points)
{
	object->coords.resize(num_points);
	for (auto i = 0u; i < num_points; i++)
	{
		object->coords[i] = convertOcdPoint(ocd_points[i]);
		setPointFlags(object, i, is_area, ocd_points[i]);
	}
	
	// For path objects, create closed parts where the position of the last point is equal to that of the first point
	if (object->getType() == Object::Path)
	{
		size_t start = 0;
		for (size_t i = 0; i < object->coords.size(); ++i)
		{
			if (!object->coords[i].isHolePoint() && i < object->coords.size() - 1)
				continue;
			
			auto coord = object->coords[start];
			coord.setHolePoint(object->coords[i].isHolePoint());
			coord.setClosePoint(true);
			coord.setCurveStart(false);
			if (object->coords[i].isPositionEqualTo(coord))
			{
				// This segment has the canonical closed form: The coordinates
				// of the last point are identical to the first point.
				object->coords[i].setFlags(coord.flags());
			}
			else if (is_area)
			{
				// We need to turn the segment into the canonical closed form
				// by inserting an extra end point.
				using difference_type = decltype(object->coords)::difference_type;
				auto const after_i = begin(object->coords) + static_cast<difference_type>(i + 1);
				object->coords.insert(after_i, coord);
				object->coords[i].setHolePoint(false);
				++i;
			}
			
			switch (i - start)
			{
			default:
				object->coords[i-2].setCurveStart(false);
				Q_FALLTHROUGH();
			case 1:
				object->coords[i-1].setCurveStart(false);
				Q_FALLTHROUGH();
			case 0:
				; // nothing
			}
			
			start = i + 1;
		}
	}
}

/** Translates an OCAD text object path into a Mapper text object specifier, if possible.
 *  If successful, sets either 1 or 2 coordinates in the text object and returns true.
 *  If the OCAD path was not importable, leaves the TextObject alone and returns false.
 */
bool OcdFileImport::fillTextPathCoords(TextObject *object, TextSymbol *symbol, quint32 npts, const Ocd::OcdPoint32 *ocd_points)
{
	// text objects either have 1 point (free anchor) or 2 (midpoint/size)
	// OCAD appears to always have 5 or 4 points (possible single anchor, then 4 corner coordinates going clockwise from anchor).
	if (npts == 0) return false;
	
	if (npts == 4)
	{
		// Box text
		MapCoord bottom_left = convertOcdPoint(ocd_points[0]);
		MapCoord top_right = convertOcdPoint(ocd_points[2]);
		MapCoord top_left = convertOcdPoint(ocd_points[3]);
		
		// According to Purple Pen source code: OC*D adds an extra internal leading (incorrectly).
		QFontMetricsF metrics = symbol->getFontMetrics();
		double top_adjust = -symbol->getFontSize() + (metrics.ascent() + metrics.descent() + 0.5) / symbol->calculateInternalScaling();
		
		MapCoordF adjust_vector = MapCoordF(top_adjust * sin(object->getRotation()), top_adjust * cos(object->getRotation()));
		top_left = MapCoord(top_left.x() + adjust_vector.x(), top_left.y() + adjust_vector.y());
		top_right = MapCoord(top_right.x() + adjust_vector.x(), top_right.y() + adjust_vector.y());
		
		object->setBox((bottom_left.nativeX() + top_right.nativeX()) / 2, (bottom_left.nativeY() + top_right.nativeY()) / 2,
		               top_left.distanceTo(top_right), top_left.distanceTo(bottom_left));
		object->setVerticalAlignment(TextObject::AlignTop);
	}
	else
	{
		// Single anchor text
		if (npts != 5)
			addWarning(tr("Trying to import a text object with unknown coordinate format"));
		
		// anchor point
		MapCoord coord = convertOcdPoint(ocd_points[0]);
		object->setAnchorPosition(coord.nativeX(), coord.nativeY());
		object->setVerticalAlignment(text_valign_map.value(symbol));
	}
	
	return true;
}

void OcdFileImport::setBasicAttributes(OcdFileImport::OcdImportedTextSymbol* symbol, const QString& font_name, const Ocd::BasicTextAttributesV8& attributes)
{
	symbol->font_family = font_name;
	symbol->color = convertColor(attributes.color);
	symbol->font_size = qRound(100.0 * attributes.font_size / 72.0 * 25.4);
	symbol->bold = (attributes.font_weight>= 550);
	symbol->italic = attributes.font_italic;
	symbol->underline = false;
	symbol->kerning = false;
	symbol->line_below = false;
	symbol->custom_tabs.resize(0);

	symbol->font_weight = (attributes.font_weight != 400 && attributes.font_weight != 700) ? attributes.font_weight : 0;  // foom
	
	switch (attributes.alignment & Ocd::HAlignMask)
	{
	case Ocd::HAlignLeft:
		text_halign_map[symbol] = TextObject::AlignLeft;
		break;
	case Ocd::HAlignRight:
		text_halign_map[symbol] = TextObject::AlignRight;
		break;
	case Ocd::HAlignJustified:
		text_halign_map[symbol] = TextObject::AlignJustify;  // foom
		break;
	default:
		text_halign_map[symbol] = TextObject::AlignHCenter;
	}
	
	switch (attributes.alignment & Ocd::VAlignMask)
	{
	case Ocd::VAlignTop:
		text_valign_map[symbol] = TextObject::AlignTop;
		break;
	case Ocd::VAlignMiddle:
		text_valign_map[symbol] = TextObject::AlignVCenter;
		break;
	default:
		addSymbolWarning(symbol, tr("Vertical alignment '%1' is not supported.").arg(attributes.alignment & Ocd::VAlignMask));
		Q_FALLTHROUGH();
	case Ocd::VAlignBottom:
		text_valign_map[symbol] = TextObject::AlignBaseline;
	}
	
	if (attributes.char_spacing != 0)
	{
		symbol->character_spacing = attributes.char_spacing / 100.0f;
		addSymbolWarning(symbol, tr("Custom character spacing may be incorrect."));
	}
	
	symbol->word_spacing = attributes.word_spacing / 100.0f;  // foom
	
	symbol->updateQFont();
}

void OcdFileImport::setSpecialAttributes(OcdFileImport::OcdImportedTextSymbol* symbol, const Ocd::SpecialTextAttributesV8& attributes)
{
	// Convert line spacing
	double absolute_line_spacing = 0.00001 * symbol->font_size * attributes.line_spacing;
	symbol->line_spacing = absolute_line_spacing / (symbol->getFontMetrics().lineSpacing() / symbol->calculateInternalScaling());
	symbol->paragraph_spacing = convertLength(attributes.para_spacing);
	
	symbol->line_below = attributes.line_below_on;
	symbol->line_below_width = convertLength(attributes.line_below_width);
	symbol->line_below_color = symbol->line_below_width ? convertColor(attributes.line_below_color) : nullptr;
	symbol->line_below_distance = convertLength(attributes.line_below_offset);
	
	symbol->custom_tabs.resize(attributes.num_tabs);
	for (auto i = 0u; i < attributes.num_tabs; ++i)
		symbol->custom_tabs[i] = convertLength<quint32, int>(attributes.tab_pos[i]);
	
	symbol->indent_first = convertLength(attributes.indent_first_line);  // foom
	symbol->indent_other = convertLength(attributes.indent_other_lines);
}

void OcdFileImport::setFraming(OcdFileImport::OcdImportedTextSymbol* symbol, const Ocd::FramingAttributesV8& framing)
{
	switch (framing.mode)
	{
	case Ocd::FramingShadow:
		symbol->framing = true;
		symbol->framing_mode = TextSymbol::ShadowFraming;
		symbol->framing_color = convertColor(framing.color);
		symbol->framing_shadow_x_offset = convertLength(framing.offset_x);
		symbol->framing_shadow_y_offset = -1 * convertLength(framing.offset_y);
		break;
	case Ocd::FramingLine: // since V7
		symbol->framing = true;
		symbol->framing_mode = TextSymbol::LineFraming;
		symbol->framing_color = convertColor(framing.color);
		symbol->framing_line_half_width = convertLength(framing.line_width);
		if (ocd_version >= 9)
		{
			if (framing.line_style_V9 != 0 && framing.line_style_V9 != 4)
				addSymbolWarning(symbol, tr("Ignoring text framing line style."));
		}
		break;
	case Ocd::FramingRectangle:  // foom: box framing
		symbol->framing = true;
		symbol->framing_mode = TextSymbol::BoxFraming;
		symbol->framing_color = convertColor(framing.color);
		if (ocd_version >= 9)
		{
			symbol->framing_box_left = convertLength(framing.border_left_V9);
			symbol->framing_box_right = convertLength(framing.border_right_V9);
			symbol->framing_box_top = convertLength(framing.border_top_V9);
			symbol->framing_box_bottom = convertLength(framing.border_bottom_V9);
		}
		break;
	default:
		addSymbolWarning(symbol, tr("Ignoring text framing (mode %1).").arg(framing.mode));
		Q_FALLTHROUGH();
	case Ocd::FramingNone:
		symbol->framing = false;
	}
}

bool OcdFileImport::importImplementation()
{
	buffer = device()->readAll();
	if (buffer.isEmpty())
		throw FileFormatException(device()->errorString());
	
	if (size_t(buffer.size()) < sizeof(Ocd::FileHeaderGeneric))
		throw FileFormatException(tr("Invalid data."));
	
	auto header = reinterpret_cast<const Ocd::FileHeaderGeneric*>(buffer.constData());
	if (header->vendor_mark != 0x0cad) // This also tests correct endianness...
		throw FileFormatException(tr("Invalid data."));
	
	ocd_version = header->version;
	map->setSymbolSetId(QStringLiteral("OCD"));
	map->setProperty(OcdFileFormat::versionProperty(), ocd_version);
	switch (ocd_version)
	{
	case 6:
	case 7:
	case 8:
		// Version 6 and 7 do have some differences from version 8, which need
		// to be handled in the version 8 implementation by looking up the
		// actual format version in the file header.
		importImplementation<Ocd::FormatV8>();
		break;
	case 9:
	case 10:
		Q_STATIC_ASSERT((std::is_same<Ocd::FormatV10, Ocd::FormatV9>::value));
		importImplementation<Ocd::FormatV9>();
		break;
	case 11:
		importImplementation<Ocd::FormatV11>();
		break;
	case 12:
		importImplementation<Ocd::FormatV12>();
		break;
	case 2018:  // foom: written back as 2018, see the design notes, "OCD 2018 writer"
		Q_STATIC_ASSERT((std::is_same<Ocd::FormatV2018, Ocd::FormatV12>::value));
		importImplementation<Ocd::FormatV12>();
		break;
	default:
		throw FileFormatException(tr("OCD files of version %1 are not supported!").arg(ocd_version));
	}
	return true;
}



template< class F >
void OcdFileImport::handleStrings(const OcdFile<F>& file, std::initializer_list<StringHandler> handlers)
{
	for (const auto& handler : handlers)  // foom
		handled_string_types.insert(handler.type);
	for (const auto ocd_string : file.strings())
	{
		for (const auto& handler : handlers)
		{
			if (ocd_string.entry->type == handler.type)
			{
				(this->*handler.callback)(convertOcdString<typename F::Encoding>(ocd_string));
			}
		}
	}
}


// foom
void OcdFileImport::importCourseData(bool course_file)
{
	// the design notes, "Storage".
	if (ocd_version < 12)
		return;
	// The named extents, which the canvases refer to; the design notes, "Canvases".
	OcdCourse::takeNamedExtents(*map);
	QStringList problems;
	auto data = OcdCourse::readFromImportedMap(*map, &problems);
	for (auto const& problem : problems)
		addWarning(problem);
	// In a file which says that it is a map, the options record alone does not make a course project.
	if (!course_file && data.objects.empty() && data.courses.empty() && data.classes.empty())
		return;
	if (data.isEmpty())
		return;
	// The routes of the file's route analysis become kept routes; "Route analysis: the routes in OCD files".
	OcdCourse::takeRoutes(*map, data.routes);
	map->setCourseData(std::move(data));
	// What became course data is not kept a second time.
	OcdCourse::removeReadRecords(*map);
}


// foom
template< class F >
void OcdFileImport::importForeignData(const OcdFile<F>& file)
{
	// the design notes, "OCD round trip".
	ForeignData foreign;
	foreign.format = QStringLiteral("OCD");
	foreign.version = ocd_version;
	for (const auto ocd_string : file.strings())
	{
		if (handled_string_types.count(ocd_string.entry->type))
			continue;
		auto const* data = reinterpret_cast<const char*>(ocd_string.entity);
		auto const offset = data - buffer.constData();
		auto const size = int(ocd_string.entry->size);
		if (offset < 0 || size <= 0 || offset + size > buffer.size())
			continue;
		ForeignString string;
		string.type = ocd_string.entry->type;
		string.object_index = ocd_string.entry->obj_index;
		string.data = QByteArray(data, size);
		if (string.type == ocd_dem_string_type)
		{
			// the design notes, "The embedded DEM in OCD files": the block becomes
			// the map's embedded grid, and the string is kept without the block's place.
			auto const dem = parseOcdDemString(string.data);
			if (dem.embedded || dem.offset >= 0 || dem.size >= 0)
			{
				if (dem.hasBlock() && dem.offset + dem.size <= buffer.size())
				{
					QString problem;
					auto grid = decodeOcdCourseDem(buffer.mid(int(dem.offset), int(dem.size)), map->getGeoreferencing().getProjectedCRSSpec(), &problem);
					if (grid)
						map->elevationSet().setEmbeddedGrid(std::move(grid));
					else
						addWarning(tr("The embedded DEM was not loaded: %1").arg(problem));
				}
				else if (dem.embedded)
				{
					addWarning(tr("The embedded DEM was not loaded: %1").arg(tr("its place lies outside the file.")));
				}
				if (dem.restIsEmpty())
					continue;
				string.data = dem.rest + '\0';
			}
		}
		foreign.strings.push_back(std::move(string));
	}
	foreign.objects = std::move(foreign_objects);
	foreign_objects.clear();
	map->setForeignData(std::move(foreign));
}


// explicit instantiation for tests
template QString OcdFileImport::getObjectText(const Ocd::FormatV12::Object& ocd_object) const;


}  // namespace OpenOrienteering
