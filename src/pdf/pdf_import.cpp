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

// the design notes, "PDF import"

#include "pdf_import.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include <QtGlobal>
#include <QtMath>
#include <QColor>
#include <QCoreApplication>
#include <QFile>
#include <QIODevice>
#include <QLatin1Char>
#include <QLatin1String>
#include <QLineF>
#include <QRegularExpression>

#include <Error.h>
#include <GfxFont.h>
#include <GfxState.h>
#include <GlobalParams.h>
#include <Object.h>
#include <OutputDev.h>
#include <PDFDoc.h>
#include <Page.h>
#include <Stream.h>
#include <goo/GooString.h>

#include "core/map.h"
#include "core/map_color.h"
#include "core/map_coord.h"
#include "core/objects/object.h"
#include "core/objects/text_object.h"
#include "core/symbols/area_symbol.h"
#include "core/symbols/line_symbol.h"
#include "core/symbols/symbol.h"
#include "core/symbols/text_symbol.h"


namespace OpenOrienteering {

namespace {

QString tr(const char* text, int n = -1)
{
	return QCoreApplication::translate("OpenOrienteering::PdfImport", text, nullptr, n);
}

constexpr double mm_per_point = 25.4 / 72.0;


void quietErrors(ErrorCategory /*category*/, Goffset /*pos*/, const char* /*msg*/)
{
}

void initPoppler()
{
	static std::once_flag once;
	std::call_once(once, [] {
		if (!globalParams)
			globalParams = std::make_unique<GlobalParams>();
		globalParams->setErrQuiet(true);
		setErrorCallback(&quietErrors);
	});
}


/// An open PDF document, read from memory so that any path works.
struct Document
{
	QByteArray bytes;
	std::unique_ptr<PDFDoc> doc;
	QString error;

	explicit Document(const QString& path)
	{
		initPoppler();
		QFile file(path);
		if (!file.open(QIODevice::ReadOnly))
		{
			error = tr("Cannot open the file %1: %2").arg(path, file.errorString());
			return;
		}
		bytes = file.readAll();
		// MemStream does not own the buffer: `bytes` is declared before `doc` and outlives it.
		auto stream = std::make_unique<MemStream>(bytes.constData(), 0, bytes.size(), ::Object::null());
		doc = std::make_unique<PDFDoc>(std::move(stream));
		if (!doc->isOk())
		{
			error = tr("The file %1 is not a PDF file which can be read (error %2).").arg(path).arg(doc->getErrorCode());
			doc.reset();
		}
	}
};


/// The size of a page in points, after its rotation.
QSizeF pageSizePt(Page* page)
{
	auto w = page->getCropWidth();
	auto h = page->getCropHeight();
	if (page->getRotate() % 180 != 0)
		std::swap(w, h);
	return { w, h };
}


QString hexName(const GfxRGB& rgb)
{
	return QColor::fromRgbF(qBound(0.0, colToDbl(rgb.r), 1.0),
	                        qBound(0.0, colToDbl(rgb.g), 1.0),
	                        qBound(0.0, colToDbl(rgb.b), 1.0)).name();
}

QString number(double value)
{
	return QString::number(value, 'g', 10);
}


/// Collects the vector content of a page as map objects.
class Collector : public OutputDev
{
public:
	Collector(Map& map, const PdfImportOptions& options, QSizeF page_pt, double scale_factor, int first_number)
	: map(map)
	, options(options)
	, page_pt(page_pt)
	, factor(mm_per_point * scale_factor)
	, next_number(first_number)
	{}

	PdfImportResult result;
	int long_dashes = 0;
	int missing_unicode = 0;

	bool upsideDown() override { return true; }
	bool useDrawChar() override { return true; }
	bool interpretType3Chars() override { return false; }
	bool useTilingPatternFill() override { return true; }
	bool useShadedFills(int /*type*/) override { return true; }

	bool tilingPatternFill(GfxState* /*state*/, Gfx* /*gfx*/, Catalog* /*cat*/, GfxTilingPattern* /*tPat*/, const std::array<double, 6>& /*mat*/, int /*x0*/, int /*y0*/, int /*x1*/, int /*y1*/, double /*xStep*/, double /*yStep*/) override
	{
		++result.patterns;
		return true;
	}
	bool functionShadedFill(GfxState* /*state*/, GfxFunctionShading* /*shading*/) override { ++result.shadings; return true; }
	bool axialShadedFill(GfxState* /*state*/, GfxAxialShading* /*shading*/, double /*tMin*/, double /*tMax*/) override { ++result.shadings; return true; }
	bool radialShadedFill(GfxState* /*state*/, GfxRadialShading* /*shading*/, double /*sMin*/, double /*sMax*/) override { ++result.shadings; return true; }
	bool gouraudTriangleShadedFill(GfxState* /*state*/, GfxGouraudTriangleShading* /*shading*/) override { ++result.shadings; return true; }
	bool patchMeshShadedFill(GfxState* /*state*/, GfxPatchMeshShading* /*shading*/) override { ++result.shadings; return true; }

	void drawImageMask(GfxState* state, ::Object* ref, Stream* str, int width, int height, bool invert, bool interpolate, bool inlineImg) override
	{
		++result.images;
		OutputDev::drawImageMask(state, ref, str, width, height, invert, interpolate, inlineImg);
	}
	void drawImage(GfxState* state, ::Object* ref, Stream* str, int width, int height, GfxImageColorMap* colorMap, bool interpolate, const int* maskColors, bool inlineImg) override
	{
		++result.images;
		OutputDev::drawImage(state, ref, str, width, height, colorMap, interpolate, maskColors, inlineImg);
	}
	void drawMaskedImage(GfxState* state, ::Object* ref, Stream* str, int width, int height, GfxImageColorMap* colorMap, bool interpolate, Stream* maskStr, int maskWidth, int maskHeight, bool maskInvert, bool maskInterpolate) override
	{
		++result.images;
		OutputDev::drawMaskedImage(state, ref, str, width, height, colorMap, interpolate, maskStr, maskWidth, maskHeight, maskInvert, maskInterpolate);
	}
	void drawSoftMaskedImage(GfxState* state, ::Object* ref, Stream* str, int width, int height, GfxImageColorMap* colorMap, bool interpolate, Stream* maskStr, int maskWidth, int maskHeight, GfxImageColorMap* maskColorMap, bool maskInterpolate) override
	{
		++result.images;
		OutputDev::drawSoftMaskedImage(state, ref, str, width, height, colorMap, interpolate, maskStr, maskWidth, maskHeight, maskColorMap, maskInterpolate);
	}

	void stroke(GfxState* state) override
	{
		auto coords = makeCoords(state, false);
		if (coords.empty())
			return;
		auto* symbol = lineSymbol(state);
		map.addObject(new PathObject(symbol, std::move(coords)));
		++result.lines;
	}

	void fill(GfxState* state) override
	{
		fillPath(state);
	}

	void eoFill(GfxState* state) override
	{
		fillPath(state);
	}

	void beginString(GfxState* state, const std::string& /*s*/) override
	{
		string_invisible = (state->getRender() & 3) == 3;
		string_had_glyphs = false;
	}

	void endString(GfxState* /*state*/) override
	{
		if (string_invisible && string_had_glyphs)
			++result.invisible_texts;
		string_invisible = false;
	}

	void drawChar(GfxState* state, double x, double y, double dx, double dy, double originX, double originY, CharCode /*code*/, int /*nBytes*/, const Unicode* u, int uLen) override
	{
		if (!options.text)
			return;
		string_had_glyphs = true;
		if (string_invisible)
			return;

		QString text;
		for (int i = 0; i < uLen; ++i)
		{
			if (u[i] == 0)
				continue;
			auto const ucs4 = char32_t(u[i]);
			auto const glyph = QString::fromUcs4(&ucs4, 1);
			// Some writers map the space glyph to a tab or another blank: one space.
			if (glyph.size() == 1 && glyph.at(0).isSpace())
				text.append(QLatin1Char(' '));
			else
				text.append(glyph);
		}
		if (text.isEmpty())
		{
			++missing_unicode;
			return;
		}

		double px, py, ex, ey;
		state->transform(x - originX, y - originY, &px, &py);
		state->transformDelta(dx, dy, &ex, &ey);
		auto const& tm = state->getTextMat();
		double bx, by;
		state->transformDelta(tm[0], tm[1], &bx, &by);
		auto const blen = std::hypot(bx, by);
		if (blen <= 0)
			return;
		QPointF const dir { bx / blen, by / blen };
		auto const em = state->getTransformedFontSize();
		if (em <= 0)
			return;

		auto* symbol = textSymbol(state, em);
		QPointF const pos { px, py };
		QPointF const end { px + ex, py + ey };

		if (run.symbol == symbol && !run.text.isEmpty())
		{
			auto const d = pos - run.end;
			auto const along = d.x() * run.dir.x() + d.y() * run.dir.y();
			auto const o = pos - run.origin;
			auto const across = std::abs(o.x() * run.dir.y() - o.y() * run.dir.x());
			auto const same_dir = std::abs(dir.x() * run.dir.y() - dir.y() * run.dir.x()) < 0.01;
			if (same_dir && across < 0.05 * em && along > -0.1 * em && along < 0.5 * em)
			{
				if (along > 0.2 * em && !run.text.endsWith(QLatin1Char(' ')) && text != QLatin1String(" "))
					run.text.append(QLatin1Char(' '));
				run.text.append(text);
				run.end = end;
				return;
			}
		}
		flushRun();
		run.symbol = symbol;
		run.text = text;
		run.origin = pos;
		run.end = end;
		run.dir = dir;
	}

	void finish()
	{
		flushRun();
	}

private:
	struct Run
	{
		TextSymbol* symbol = nullptr;
		QString text;
		QPointF origin;
		QPointF end;
		QPointF dir;
	};

	MapCoordF toMap(double device_x, double device_y) const
	{
		return MapCoordF(options.centre.x() + factor * (device_x - page_pt.width() / 2),
		                 options.centre.y() + factor * (device_y - page_pt.height() / 2));
	}

	/// The coordinates of the current path, with a part for each subpath.
	MapCoordVector makeCoords(GfxState* state, bool close_all) const
	{
		MapCoordVector coords;
		auto const* path = state->getPath();
		if (!path)
			return coords;
		for (int i = 0; i < path->getNumSubpaths(); ++i)
		{
			auto const* sub = path->getSubpath(i);
			auto const n = sub->getNumPoints();
			if (n < (close_all ? 3 : 2))
				continue;
			MapCoordVector part;
			part.reserve(std::size_t(n) + 1);
			for (int j = 0; j < n; ++j)
			{
				double dx, dy;
				state->transform(sub->getX(j), sub->getY(j), &dx, &dy);
				MapCoord coord(toMap(dx, dy));
				if (j + 1 < n && sub->getCurve(j + 1) && !sub->getCurve(j))
					coord.setCurveStart(true);
				part.push_back(coord);
			}
			auto const& first = part.front();
			auto& last = part.back();
			auto const ends_meet = std::abs(first.nativeX() - last.nativeX()) <= 1 && std::abs(first.nativeY() - last.nativeY()) <= 1;
			if (sub->isClosed() || close_all || ends_meet)
			{
				if (ends_meet && part.size() > 1)
				{
					last.setX(first.x());
					last.setY(first.y());
				}
				else
				{
					part.push_back(MapCoord(first.x(), first.y()));
				}
				if (part.size() < 3)
					continue;
				part.back().setClosePoint(true);
			}
			if (!coords.empty())
				coords.back().setHolePoint(true);
			coords.insert(coords.end(), part.begin(), part.end());
		}
		return coords;
	}

	void fillPath(GfxState* state)
	{
		auto coords = makeCoords(state, true);
		if (coords.empty())
			return;
		auto* symbol = areaSymbol(state);
		map.addObject(new PathObject(symbol, std::move(coords)));
		++result.areas;
	}

	/// The map colour for the fill or stroke colour of the state.
	MapColor* colour(GfxState* state, bool stroke)
	{
		auto* space = stroke ? state->getStrokeColorSpace() : state->getFillColorSpace();
		auto const& value = stroke ? state->getStrokeColor() : state->getFillColor();
		auto const opacity = stroke ? state->getStrokeOpacity() : state->getFillOpacity();

		GfxRGB rgb;
		space->getRGB(value, &rgb);
		auto name = hexName(rgb);
		QString key;
		QString spot;
		bool cmyk_space = false;
		GfxCMYK cmyk;
		space->getCMYK(value, &cmyk);
		if (space->getMode() == csSeparation && colToDbl(value.c[0]) > 0.999)
		{
			auto const* separation = static_cast<GfxSeparationColorSpace*>(space);
			if (separation->getName())
				spot = QString::fromUtf8(separation->getName()->c_str());
		}
		if (!spot.isEmpty())
		{
			name = spot;
			key = QLatin1String("spot ") + spot;
		}
		else if (space->getMode() == csDeviceCMYK || (space->getMode() == csICCBased && space->getNComps() == 4))
		{
			cmyk_space = true;
			key = QLatin1String("cmyk ") + number(colToDbl(cmyk.c)) + QLatin1Char(' ') + number(colToDbl(cmyk.m))
			      + QLatin1Char(' ') + number(colToDbl(cmyk.y)) + QLatin1Char(' ') + number(colToDbl(cmyk.k));
		}
		else
		{
			key = QLatin1String("rgb ") + name;
		}
		auto const rounded_opacity = qRound(qBound(0.0, opacity, 1.0) * 100);
		if (rounded_opacity < 100)
		{
			key += QLatin1String(" opacity ") + QString::number(rounded_opacity);
			name += QLatin1Char(' ') + QString::number(rounded_opacity) + QLatin1Char('%');
		}

		auto found = colours.find(key);
		if (found != colours.end())
			return found->second;

		auto* color = new MapColor(name, 0);
		MapColorCmyk const map_cmyk(float(colToDbl(cmyk.c)), float(colToDbl(cmyk.m)), float(colToDbl(cmyk.y)), float(colToDbl(cmyk.k)));
		if (!spot.isEmpty())
		{
			color->setSpotColorName(spot);
			color->setCmyk(map_cmyk);
			color->setRgbFromCmyk();
		}
		else if (cmyk_space)
		{
			color->setCmyk(map_cmyk);
			color->setRgbFromCmyk();
		}
		else
		{
			color->setRgb(QColor::fromRgbF(qBound(0.0, colToDbl(rgb.r), 1.0),
			                               qBound(0.0, colToDbl(rgb.g), 1.0),
			                               qBound(0.0, colToDbl(rgb.b), 1.0)));
			color->setCmykFromRgb();
		}
		if (rounded_opacity < 100)
			color->setOpacity(float(rounded_opacity / 100.0));
		// PDF paints later over earlier: a new colour goes on top.
		map.addColor(color, 0);
		colours.emplace(key, color);
		return color;
	}

	void addSymbol(Symbol* symbol, const QString& name)
	{
		symbol->setNumberComponent(0, next_number++);
		symbol->setName(name);
		map.addSymbol(symbol, map.getNumSymbols());
	}

	LineSymbol* lineSymbol(GfxState* state)
	{
		auto* color = colour(state, true);
		auto width = state->getTransformedLineWidth() * factor;
		if (width <= 0)
			width = 0.1;
		auto const width_um = qRound(width * 100) * 10;

		LineSymbol::CapStyle cap = LineSymbol::FlatCap;
		switch (state->getLineCap())
		{
		case GfxState::LineCapRound:      cap = LineSymbol::RoundCap; break;
		case GfxState::LineCapProjecting: cap = LineSymbol::SquareCap; break;
		default:                break;
		}
		LineSymbol::JoinStyle join = LineSymbol::MiterJoin;
		switch (state->getLineJoin())
		{
		case GfxState::LineJoinRound: join = LineSymbol::RoundJoin; break;
		case GfxState::LineJoinBevel: join = LineSymbol::BevelJoin; break;
		default:            break;
		}

		double start = 0;
		auto const& dash = state->getLineDash(&start);
		int dash_um = 0;
		int gap_um = 0;
		bool any_dash = false;
		for (auto value : dash)
			any_dash |= value > 0;
		if (any_dash)
		{
			auto const d = state->transformWidth(dash[0]) * factor;
			auto const g = state->transformWidth(dash.size() > 1 ? dash[1] : dash[0]) * factor;
			dash_um = qRound(d * 100) * 10;
			gap_um = qRound(g * 100) * 10;
			if (dash.size() > 2)
				++long_dashes;
		}

		QString const key = QString::number(quintptr(color)) + QLatin1Char(' ') + QString::number(width_um)
		                 + QLatin1Char(' ') + QString::number(int(cap)) + QLatin1Char(' ') + QString::number(int(join))
		                 + QLatin1Char(' ') + QString::number(dash_um) + QLatin1Char(' ') + QString::number(gap_um);
		auto found = line_symbols.find(key);
		if (found != line_symbols.end())
			return found->second;

		auto* symbol = new LineSymbol();
		symbol->setColor(color);
		symbol->setLineWidth(width_um / 1000.0);
		symbol->setCapStyle(cap);
		symbol->setJoinStyle(join);
		if (any_dash)
		{
			symbol->setDashed(true);
			symbol->setDashLength(dash_um);
			symbol->setBreakLength(gap_um);
		}
		addSymbol(symbol, QLatin1String("PDF line ") + number(width_um / 1000.0) + QLatin1String(" mm ") + color->getName());
		line_symbols.emplace(key, symbol);
		return symbol;
	}

	AreaSymbol* areaSymbol(GfxState* state)
	{
		auto* color = colour(state, false);
		auto found = area_symbols.find(color);
		if (found != area_symbols.end())
			return found->second;
		auto* symbol = new AreaSymbol();
		symbol->setColor(color);
		addSymbol(symbol, QLatin1String("PDF area ") + color->getName());
		area_symbols.emplace(color, symbol);
		return symbol;
	}

	TextSymbol* textSymbol(GfxState* state, double em_pt)
	{
		auto const render = state->getRender() & 3;
		auto* color = colour(state, render == 1);
		auto const size_um = qRound(em_pt * factor * 100) * 10;

		QString font_name;
		bool bold = false;
		bool italic = false;
		if (auto const& font = state->getFont())
		{
			if (font->getFamily())
				font_name = QString::fromStdString(*font->getFamily());
			auto const full = font->getName() ? QString::fromStdString(*font->getName()) : QString();
			static QRegularExpression const subset(QStringLiteral("^[A-Z]{6}\\+"));
			auto plain = full;
			plain.remove(subset);
			if (font_name.isEmpty())
			{
				font_name = plain.section(QLatin1Char('-'), 0, 0).section(QLatin1Char(','), 0, 0);
				for (auto suffix : { "PSMT", "MT", "PS" })
				{
					if (font_name.endsWith(QLatin1String(suffix)) && font_name.length() > int(qstrlen(suffix)))
					{
						font_name.chop(int(qstrlen(suffix)));
						break;
					}
				}
			}
			bold = font->isBold() || font->getWeight() >= GfxFont::W600 || plain.contains(QLatin1String("Bold"));
			italic = font->isItalic() || plain.contains(QLatin1String("Italic")) || plain.contains(QLatin1String("Oblique"));
		}
		if (font_name.isEmpty())
			font_name = QStringLiteral("Arial");

		QString const key = font_name + QLatin1Char(' ') + QString::number(int(bold)) + QString::number(int(italic))
		                 + QLatin1Char(' ') + QString::number(size_um) + QLatin1Char(' ') + QString::number(quintptr(color));
		auto found = text_symbols.find(key);
		if (found != text_symbols.end())
			return found->second;

		auto* symbol = new TextSymbol();
		symbol->setFontFamily(font_name);
		symbol->setFontSize(size_um);
		symbol->setBold(bold);
		symbol->setItalic(italic);
		symbol->setColor(color);
		QString name = QLatin1String("PDF text ") + font_name;
		if (bold)
			name += QLatin1String(" bold");
		if (italic)
			name += QLatin1String(" italic");
		name += QLatin1Char(' ') + number(size_um / 1000.0) + QLatin1String(" mm");
		addSymbol(symbol, name);
		text_symbols.emplace(key, symbol);
		return symbol;
	}

	void flushRun()
	{
		if (!run.symbol || run.text.trimmed().isEmpty())
		{
			run = {};
			return;
		}
		auto* object = new TextObject(run.symbol);
		object->setText(run.text.trimmed());
		object->setAnchorPosition(toMap(run.origin.x(), run.origin.y()));
		object->setHorizontalAlignment(TextObject::AlignLeft);
		object->setVerticalAlignment(TextObject::AlignBaseline);
		object->setRotation(std::atan2(-run.dir.y(), run.dir.x()));
		map.addObject(object);
		++result.texts;
		run = {};
	}

	Map& map;
	const PdfImportOptions& options;
	QSizeF page_pt;
	double factor;
	int next_number;

	std::map<QString, MapColor*> colours;
	std::map<QString, LineSymbol*> line_symbols;
	std::map<const MapColor*, AreaSymbol*> area_symbols;
	std::map<QString, TextSymbol*> text_symbols;

	Run run;
	bool string_invisible = false;
	bool string_had_glyphs = false;
};


int nextSymbolNumber(const Map& map)
{
	int highest = 0;
	for (int i = 0; i < map.getNumSymbols(); ++i)
		highest = std::max(highest, map.getSymbol(i)->getNumberComponent(0));
	return highest + 1;
}


PdfImportResult readPage(const QString& path, Map& map, const PdfImportOptions& options, int first_number)
{
	PdfImportResult result;
	Document document(path);
	if (!document.doc)
	{
		result.error = document.error;
		return result;
	}
	auto& doc = *document.doc;
	auto const pages = doc.getNumPages();
	if (options.page < 1 || options.page > pages)
	{
		result.error = tr("The file has no page %1; it has %n page(s).", pages).arg(options.page);
		return result;
	}
	auto* page = doc.getPage(options.page);
	if (!page)
	{
		result.error = tr("Cannot read page %1.").arg(options.page);
		return result;
	}
	auto const page_pt = pageSizePt(page);
	auto const pdf_scale = options.pdf_scale > 0 ? options.pdf_scale : int(map.getScaleDenominator());
	auto const scale_factor = double(pdf_scale) / map.getScaleDenominator();

	Collector collector(map, options, page_pt, scale_factor, first_number);
	doc.displayPage(&collector, options.page, 72, 72, 0, false, true, false);
	collector.finish();

	result = collector.result;
	result.page_size_mm = page_pt * mm_per_point;
	result.objects = result.areas + result.lines + result.texts;

	if (result.images > 0)
		result.warnings << tr("%n embedded image(s) skipped.", result.images);
	if (result.shadings > 0)
		result.warnings << tr("%n shading(s) skipped.", result.shadings);
	if (result.patterns > 0)
		result.warnings << tr("%n pattern fill(s) skipped.", result.patterns);
	if (result.invisible_texts > 0)
		result.warnings << tr("%n invisible text run(s) skipped.", result.invisible_texts);
	if (collector.long_dashes > 0)
		result.warnings << tr("%n dash pattern(s) with more than one dash and gap: the first dash and gap were taken.", collector.long_dashes);
	if (collector.missing_unicode > 0)
		result.warnings << tr("%n glyph(s) without a Unicode mapping were left out of the text.", collector.missing_unicode);
	if (result.objects == 0)
		result.warnings << tr("Page %1 has no vector content.").arg(options.page);
	result.ok = true;
	return result;
}

}  // namespace



std::vector<QSizeF> pdfPageSizes(const QString& path, QString* error)
{
	std::vector<QSizeF> sizes;
	Document document(path);
	if (!document.doc)
	{
		if (error)
			*error = document.error;
		return sizes;
	}
	auto const pages = document.doc->getNumPages();
	sizes.reserve(std::size_t(std::max(0, pages)));
	for (int i = 1; i <= pages; ++i)
	{
		auto* page = document.doc->getPage(i);
		sizes.push_back(page ? pageSizePt(page) * mm_per_point : QSizeF());
	}
	return sizes;
}


PdfImportResult readPdfPage(const QString& path, Map& map, const PdfImportOptions& options)
{
	return readPage(path, map, options, nextSymbolNumber(map));
}


PdfImportResult importPdf(Map& map, const QString& path, const PdfImportOptions& options)
{
	Map scratch;
	scratch.setScaleDenominator(map.getScaleDenominator());
	scratch.setGeoreferencing(map.getGeoreferencing());
	auto result = readPage(path, scratch, options, nextSymbolNumber(map));
	if (!result.ok)
		return result;
	auto const objects_before = map.getNumObjects();
	if (scratch.getNumObjects() > 0)
		map.importMap(scratch, Map::MinimalObjectImport | Map::GeorefImport);
	result.objects = map.getNumObjects() - objects_before;
	return result;
}



// ### PdfFileFormat ###

PdfFileFormat::PdfFileFormat()
: FileFormat(OgrFile, "PDF", ::OpenOrienteering::ImportExport::tr("PDF"), QStringLiteral("pdf"),
             Feature::FileImport | Feature::ReadingLossy)
{}

std::unique_ptr<Importer> PdfFileFormat::makeImporter(const QString& path, Map* map, MapView* view) const
{
	return std::make_unique<PdfFileImport>(path, map, view);
}



// ### PdfFileImport ###

PdfFileImport::PdfFileImport(const QString& path, Map* map, MapView* view)
: Importer(path, map, view)
{}

PdfFileImport::~PdfFileImport() = default;

bool PdfFileImport::supportsQIODevice() const noexcept
{
	return false;
}

bool PdfFileImport::importImplementation()
{
	auto const result = readPdfPage(path, *map, {});
	for (auto const& warning : result.warnings)
		addWarning(warning);
	if (!result.ok)
	{
		addWarning(result.error);
		return false;
	}
	return true;
}


}  // namespace OpenOrienteering
