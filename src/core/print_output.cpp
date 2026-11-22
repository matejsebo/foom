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


#include "print_output.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <functional>
#include <map>

#include <QtGlobal>
#include <QtMath>
#include <QColor>
#include <QCoreApplication>
#include <QFileInfo>
#include <QImageIOHandler>
#include <QImageWriter>
#include <QLatin1Char>
#include <QPainter>
#include <QRgb>
#include <QVector>

#include "core/map.h"
#include "core/map_color.h"
#include "core/map_view.h"
#include "core/map_part.h"
#include "core/objects/object.h"
#include "core/renderables/renderable.h"
#include "core/symbols/symbol.h"
#include "templates/template.h"


namespace OpenOrienteering {

namespace {

QString tr(const char* text, int n = -1)
{
	return QCoreApplication::translate("OpenOrienteering::PrintOutput", text, nullptr, n);
}

void setError(QString* error, const QString& message)
{
	if (error)
		*error = message;
}

/** Draws the area onto white and returns it as ink. */
QImage renderInk(const Map& map, const QRectF& area, double dpi, double scale_adjustment, const std::function<void(QPainter&, RenderConfig&)>& draw)
{
	auto const scale = dpi / 25.4 * scale_adjustment;  // pixels per mm of the map
	auto const width = qRound(area.width() * scale);
	auto const height = qRound(area.height() * scale);
	if (width < 1 || height < 1)
		return {};
	QImage image(width, height, QImage::Format_RGB32);
	if (image.isNull())
		return {};
	image.fill(QColor(Qt::white));
	{
		QPainter painter(&image);
		painter.setRenderHint(QPainter::Antialiasing);
		painter.setRenderHint(QPainter::SmoothPixmapTransform);
		painter.scale(scale, scale);
		painter.translate(-area.topLeft());
		RenderConfig config = { map, area, scale, RenderConfig::NoOptions, 1.0 };
		config.blend_modes = true;
		draw(painter, config);
	}
	auto ink = image.convertToFormat(QImage::Format_Grayscale8);
	ink.invertPixels();
	auto const dots_per_meter = qRound(dpi / 0.0254);
	ink.setDotsPerMeterX(dots_per_meter);
	ink.setDotsPerMeterY(dots_per_meter);
	return ink;
}

QImage monoImage(const QImage& like)
{
	QImage result(like.size(), QImage::Format_Mono);
	if (result.isNull())
		return result;
	result.setColorTable(QVector<QRgb>{ qRgb(255, 255, 255), qRgb(0, 0, 0) });
	result.fill(0);
	result.setDotsPerMeterX(like.dotsPerMeterX());
	result.setDotsPerMeterY(like.dotsPerMeterY());
	return result;
}

/** The round-dot threshold at a place in the screen, given as angles: 1 in the middle of a dot. */
double dotThreshold(double u, double v)
{
	return 0.5 * (1 + 0.5 * (std::cos(u) + std::cos(v)));
}

constexpr int share_steps = 1024;

int shareIndex(double threshold)
{
	return qBound(0, int(threshold * (share_steps - 1) + 0.5), share_steps - 1);
}

/**
 * The share of a screen cell in which the threshold is at least i / 1023.
 * Inking the places whose share is not above the ink value makes the share
 * of inked pixels equal to the ink value, which the plain threshold does not.
 */
const std::vector<double>& dotShare()
{
	static const std::vector<double> table = [] {
		constexpr int samples = 256;
		std::vector<double> counts(share_steps, 0.0);
		for (int i = 0; i < samples; ++i)
		{
			for (int j = 0; j < samples; ++j)
				counts[std::size_t(shareIndex(dotThreshold(2 * M_PI * (i + 0.5) / samples, 2 * M_PI * (j + 0.5) / samples)))] += 1;
		}
		std::vector<double> share(share_steps, 0.0);
		auto sum = 0.0;
		for (int k = share_steps - 1; k >= 0; --k)
		{
			sum += counts[std::size_t(k)];
			share[std::size_t(k)] = sum / (double(samples) * samples);
		}
		return share;
	}();
	return table;
}

}  // namespace


QStringList HiddenContent::lines() const
{
	QStringList result;
	for (auto const& entry : symbols)
		result.append(tr("Hidden symbol %1: %n object(s)", entry.objects).arg(entry.name));
	for (auto const& entry : parts)
		result.append(tr("Hidden part %1: %n object(s)", entry.objects).arg(entry.name));
	return result;
}

HiddenContent hiddenContent(const Map& map)
{
	HiddenContent content;
	std::map<const Symbol*, int> counts;
	for (int p = 0; p < map.getNumParts(); ++p)
	{
		auto const* part = map.getPart(std::size_t(p));
		if (!part->isVisible())
		{
			if (part->getNumObjects() > 0)
				content.parts.push_back({ part->getName(), part->getNumObjects() });
			continue;
		}
		for (int i = 0; i < part->getNumObjects(); ++i)
		{
			auto const* symbol = part->getObject(i)->getSymbol();
			if (symbol && symbol->isHidden() && !symbol->isHelperSymbol())
				++counts[symbol];
		}
	}
	// In the order of the symbol list.
	for (int i = 0; i < map.getNumSymbols(); ++i)
	{
		auto const* symbol = map.getSymbol(i);
		auto const found = counts.find(symbol);
		if (found == counts.end())
			continue;
		QString name = symbol->getNumberAsString();
		name += QLatin1Char(' ');
		name += symbol->getPlainTextName();
		content.symbols.push_back({ name.trimmed(), found->second });
	}
	return content;
}


void drawPlateTemplates(QPainter* painter, const Map& map, const MapColor* color, const QRectF& area, const MapView* view)
{
	if (!color)
		return;
	for (int i = 0; i < map.getNumTemplates(); ++i)
	{
		auto const* temp = map.getTemplate(i);
		if (temp->separationColor() != color || temp->getTemplateState() != Template::Loaded)
			continue;
		auto visibility = view ? view->getTemplateVisibility(temp) : TemplateVisibility{ 1.0f, true };
		if (!visibility.visible || visibility.opacity <= 0)
			continue;
		auto const scale = std::max(temp->getTemplateScaleX(), temp->getTemplateScaleY()) * (view ? view->getZoom() : 1.0);
		painter->save();
		temp->drawTemplate(painter, area, scale, false, visibility.opacity);
		painter->restore();
	}
}

std::vector<RasterPlate> renderSpotPlates(Map& map, const QRectF& area, double dpi, double scale_adjustment, bool with_templates, const MapView* view)
{
	std::vector<RasterPlate> plates;
	for (int i = map.getNumColors() - 1; i >= 0; --i)
	{
		auto const* color = map.getColor(i);
		if (color->getSpotColorMethod() != MapColor::SpotColor)
			continue;
		RasterPlate plate;
		plate.name = color->getSpotColorName();
		plate.screen_frequency = color->getScreenFrequency();
		plate.screen_angle = color->getScreenAngle();
		plate.ink = renderInk(map, area, dpi, scale_adjustment, [&map, color, &area, with_templates, view](QPainter& painter, RenderConfig& config) {
			if (with_templates)
				drawPlateTemplates(&painter, map, color, area, view);  // under the map's objects
			map.drawColorSeparation(&painter, config, color);
		});
		plates.push_back(plate);
	}
	return plates;
}

std::vector<RasterPlate> renderProcessPlates(Map& map, const QRectF& area, double dpi, double scale_adjustment)
{
	std::vector<RasterPlate> plates;
	const char* const names[4] = { "Cyan", "Magenta", "Yellow", "Black" };
	const double angles[4] = { 15, 75, 0, 45 };
	for (int channel = 0; channel < 4; ++channel)
	{
		RasterPlate plate;
		plate.name = QLatin1String(names[channel]);
		plate.screen_angle = angles[channel];
		plate.ink = renderInk(map, area, dpi, scale_adjustment, [&map, channel](QPainter& painter, RenderConfig& config) {
			config.process_channel = channel;
			map.draw(&painter, config);
		});
		plates.push_back(plate);
	}
	return plates;
}

QImage processPlateOfImage(const QImage& image, int channel)
{
	auto result = image.convertToFormat(QImage::Format_ARGB32);
	for (int y = 0; y < result.height(); ++y)
	{
		auto* line = reinterpret_cast<QRgb*>(result.scanLine(y));
		for (int x = 0; x < result.width(); ++x)
		{
			auto const pixel = line[x];
			auto const black = 255 - std::max({ qRed(pixel), qGreen(pixel), qBlue(pixel) });
			auto value = black;
			if (channel < 3)
			{
				auto const component = channel == 0 ? qRed(pixel) : channel == 1 ? qGreen(pixel) : qBlue(pixel);
				value = black < 255 ? (255 - component - black) * 255 / (255 - black) : 0;
			}
			auto const grey = 255 - value;
			line[x] = qRgba(grey, grey, grey, qAlpha(pixel));
		}
	}
	return result;
}

QImage plateAsGrey(const QImage& ink)
{
	auto result = ink.convertToFormat(QImage::Format_Grayscale8);
	result.invertPixels();
	return result;
}

QImage plateAsLineArt(const QImage& ink)
{
	auto result = monoImage(ink);
	for (int y = 0; y < ink.height(); ++y)
	{
		auto const* in = ink.constScanLine(y);
		auto* out = result.scanLine(y);
		for (int x = 0; x < ink.width(); ++x)
		{
			if (in[x] >= 128)
				out[x >> 3] |= 0x80 >> (x & 7);
		}
	}
	return result;
}

namespace {

/** Returns false with a message when a cell of the screen would be less than 8 pixels wide. */
bool screenFitsResolution(double dpi, double frequency, QString* error)
{
	if (frequency <= 0 || dpi / frequency >= 8 - 1e-9)
		return true;
	setError(error, tr("A screen of %1 lines per inch needs a resolution of %2 dpi or more.").arg(frequency).arg(8 * frequency));
	return false;
}

}  // namespace

QImage plateAsHalftone(const QImage& ink, double dpi, double frequency, double angle, QString* error)
{
	if (frequency <= 0)
	{
		frequency = std::min(150.0, dpi / 8);
		angle = 45;
	}
	if (!screenFitsResolution(dpi, frequency, error))
		return {};
	auto const cell = dpi / frequency;  // pixels
	
	auto result = monoImage(ink);
	auto const cos_a = std::cos(qDegreesToRadians(angle));
	auto const sin_a = std::sin(qDegreesToRadians(angle));
	auto const turn = 2 * M_PI / cell;
	auto const& share = dotShare();
	for (int y = 0; y < ink.height(); ++y)
	{
		auto const* in = ink.constScanLine(y);
		auto* out = result.scanLine(y);
		auto const py = y + 0.5;
		for (int x = 0; x < ink.width(); ++x)
		{
			auto const value = in[x];
			if (value == 0)
				continue;
			auto inked = value == 255;
			if (!inked)
			{
				// A round dot which grows from the middle of each cell of the turned screen.
				auto const px = x + 0.5;
				auto const u = (px * cos_a + py * sin_a) * turn;
				auto const v = (py * cos_a - px * sin_a) * turn;
				inked = share[std::size_t(shareIndex(dotThreshold(u, v)))] <= value / 255.0;
			}
			if (inked)
				out[x >> 3] |= 0x80 >> (x & 7);
		}
	}
	return result;
}

QString plateFileName(const QString& path, const QString& plate_name)
{
	QString name;
	for (auto c : plate_name.trimmed())
	{
		auto const bad = c.unicode() < 32 || QStringLiteral("\\/:*?\"<>|").contains(c);
		name += bad ? QLatin1Char('_') : c;
	}
	if (name.isEmpty())
		name = QStringLiteral("plate");
	
	const QFileInfo info(path);
	QString result = info.path();
	result += QLatin1Char('/');
	result += info.completeBaseName();
	result += QLatin1Char('-');
	result += name;
	if (!info.suffix().isEmpty())
	{
		result += QLatin1Char('.');
		result += info.suffix();
	}
	return result;
}

bool saveSpotPlates(Map& map, const QRectF& area, double dpi, double scale_adjustment, const QString& path, PlateForm form, QStringList* files, QString* error,
                    bool with_templates, const MapView* view, const PlateWriter& writer)
{
	auto const plates = renderSpotPlates(map, area, dpi, scale_adjustment, with_templates, view);
	if (plates.empty())
	{
		setError(error, tr("The map has no spot colours."));
		return false;
	}
	if (form == PlateForm::Halftone)
	{
		// Before the first file is written: one screen which is too fine fails the whole set.
		for (auto const& plate : plates)
		{
			if (!screenFitsResolution(dpi, plate.screen_frequency, error))
				return false;
		}
	}
	for (auto const& plate : plates)
	{
		if (plate.ink.isNull())
		{
			setError(error, tr("The area is empty or too large for an image."));
			return false;
		}
		QImage image;
		switch (form)
		{
		case PlateForm::Grey:
			image = plateAsGrey(plate.ink);
			break;
		case PlateForm::LineArt:
			image = plateAsLineArt(plate.ink);
			break;
		case PlateForm::Halftone:
			image = plateAsHalftone(plate.ink, dpi, plate.screen_frequency, plate.screen_angle, error);
			break;
		}
		if (image.isNull())
			return false;
		
		auto const file = plateFileName(path, plate.name);
		if (writer)
		{
			if (!writer(file, image, error))
				return false;
		}
		else
		{
			QImageWriter image_writer(file);
			if (image_writer.supportsOption(QImageIOHandler::CompressionRatio) && QFileInfo(file).suffix().startsWith(QLatin1String("tif"), Qt::CaseInsensitive))
				image_writer.setCompression(1);  // LZW
			if (!image_writer.write(image))
			{
				setError(error, tr("Cannot write %1: %2").arg(file, image_writer.errorString()));
				return false;
			}
		}
		if (files)
			files->append(file);
	}
	return true;
}


}  // namespace OpenOrienteering
