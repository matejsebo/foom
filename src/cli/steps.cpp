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


#include "steps.h"

#include <algorithm>
#include <cstddef>
#include <initializer_list>
#include <utility>
#include <vector>

#include <Qt>
#include <QtGlobal>
#include <QColor>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QIODevice>
#include <QJsonArray>
#include <QJsonValue>
#include <QLatin1Char>
#include <QLatin1String>
#include <QPainter>
#include <QPointF>
#include <QPrinter>
#include <QRectF>
#include <QSizeF>
#include <QStringList>
#include <QTextStream>

#include "job.h"
#include "mapper_config.h"
#include "core/crs_template.h"
#include "core/crt_rules.h"
#include "core/georeferencing.h"
#include "core/latlon.h"
#include "core/map.h"
#include "core/map_coord.h"
#include "core/map_printer.h"
#include "core/objects/object_query.h"
#include "core/objects/symbol_rule_set.h"
#include "core/symbols/symbol.h"
#include "fileformats/file_format.h"
#include "fileformats/file_format_registry.h"
#include "fileformats/file_import_export.h"
#ifdef MAPPER_USE_GDAL
#  include "gdal/geodata_import.h"  // foom
#endif


namespace OpenOrienteering {

namespace Cli {

namespace {

/**
 * Access to a step's parameters, with validation.
 *
 * Every problem is reported as a JobError naming the step.
 */
class Params
{
public:
	explicit Params(const Step& step) : step(step) {}
	
	Q_NORETURN void fail(const QString& what) const
	{
		throw JobError(step.prefix() + what);
	}
	
	bool has(const char* name) const
	{
		return step.params.contains(QLatin1String(name));
	}
	
	QJsonValue value(const char* name) const
	{
		return step.params.value(QLatin1String(name));
	}
	
	/**
	 * Rejects parameters which are not in the list. A typo must not pass silently.
	 */
	void checkKnown(std::initializer_list<const char*> names) const
	{
		for (auto const& key : step.params.keys())
		{
			auto known = false;
			for (auto name : names)
				known = known || key == QLatin1String(name);
			if (!known)
				fail(QString::fromLatin1("unknown parameter \"%1\"").arg(key));
		}
	}
	
	/**
	 * Rejects a parameter whose feature is not built yet.
	 */
	void unsupported(const char* name, const char* why) const
	{
		if (has(name))
			fail(QString::fromLatin1("parameter \"%1\" is not supported yet: %2").arg(QLatin1String(name), QLatin1String(why)));
	}
	
	void requireMap(bool map_open) const
	{
		if (!map_open)
			fail(QLatin1String("needs an open map: add an \"open\" step first"));
	}
	
	QString requireString(const char* name) const
	{
		auto const v = value(name);
		if (!v.isString() || v.toString().isEmpty())
			fail(QString::fromLatin1("parameter \"%1\" must be a non-empty string").arg(QLatin1String(name)));
		return v.toString();
	}
	
	QString optionalString(const char* name, const QString& default_value = {}) const
	{
		if (!has(name))
			return default_value;
		return requireString(name);
	}
	
	bool optionalBool(const char* name, bool default_value) const
	{
		if (!has(name))
			return default_value;
		auto const v = value(name);
		if (!v.isBool())
			fail(QString::fromLatin1("parameter \"%1\" must be true or false").arg(QLatin1String(name)));
		return v.toBool();
	}
	
	double requireNumber(const char* name) const
	{
		auto const v = value(name);
		if (!v.isDouble())
			fail(QString::fromLatin1("parameter \"%1\" must be a number").arg(QLatin1String(name)));
		return v.toDouble();
	}
	
	unsigned int requirePositiveInt(const char* name) const
	{
		auto const v = requireNumber(name);
		if (v < 1 || v != double(int(v)))
			fail(QString::fromLatin1("parameter \"%1\" must be a positive integer").arg(QLatin1String(name)));
		return unsigned(v);
	}
	
	/**
	 * Returns the numbers of an array value of the given length.
	 */
	std::vector<double> numberArray(const QJsonValue& v, int count, const QString& what) const
	{
		if (!v.isArray() || v.toArray().size() != count)
			fail(QString::fromLatin1("%1 must be an array of %2 numbers").arg(what, QString::number(count)));
		std::vector<double> result;
		result.reserve(std::size_t(count));
		for (auto const& item : v.toArray())
		{
			if (!item.isDouble())
				fail(QString::fromLatin1("%1 must be an array of %2 numbers").arg(what, QString::number(count)));
			result.push_back(item.toDouble());
		}
		return result;
	}

private:
	const Step& step;
};


StepResult failure(const QString& message)
{
	StepResult result;
	result.ok = false;
	result.message = message;
	return result;
}

QString lastWarning(const std::vector<QString>& warnings, const QString& fallback)
{
	return warnings.empty() ? fallback : warnings.back();
}

void addWarnings(StepResult& result, const std::vector<QString>& warnings)
{
	if (warnings.empty())
		return;
	QJsonArray array;
	for (auto const& warning : warnings)
		array.append(warning);
	result.details.insert(QLatin1String("warnings"), array);
}

QString cannotOpen(const QString& path)
{
	return QString::fromLatin1("cannot open \"%1\": the file does not exist or its format is not supported").arg(path);
}



// ### CRS ###

struct CrsChoice
{
	QString id;
	QString spec;
	std::vector<QString> params;
};

/**
 * Parses the `crs` parameter: `local`, a PROJ string (starting with '+'),
 * or `<template id>:<parameters>` with the parameters separated by commas,
 * e.g. `EPSG:26913` or `UTM:13 N`.
 */
CrsChoice parseCrs(const Params& p, const QString& text)
{
	if (text.compare(QLatin1String("local"), Qt::CaseInsensitive) == 0)
		return { QString::fromLatin1("Local"), {}, {} };
	
	if (text.startsWith(QLatin1Char('+')))
		return { QString::fromLatin1("PROJ.4"), text, { text } };
	
	CRSTemplateRegistry registry;
	for (auto const& crs_template : registry.list())
	{
		auto const id = crs_template->id();
		if (!text.startsWith(id + QLatin1Char(':'), Qt::CaseInsensitive))
			continue;
		
		auto values = text.mid(id.length() + 1).split(QLatin1Char(','));
		for (auto& value : values)
			value = value.trimmed();
		if (values.size() != int(crs_template->parameters().size()))
			p.fail(QString::fromLatin1("CRS \"%1\" takes %2 parameter(s)").arg(id, QString::number(crs_template->parameters().size())));
		
		if (id == QLatin1String("UTM"))
		{
			// Accept "33N", "33 n" and "33": the UTM zone parameter wants "33 N".
			auto& zone = values[0];
			zone = zone.toUpper().remove(QLatin1Char(' '));
			if (zone.endsWith(QLatin1Char('N')) || zone.endsWith(QLatin1Char('S')))
				zone.insert(zone.length() - 1, QLatin1Char(' '));
			else
				zone.append(QLatin1String(" N"));
		}
		
		auto spec = crs_template->specificationTemplate();
		auto value = values.begin();
		for (auto const& parameter : crs_template->parameters())
		{
			for (auto const& spec_value : parameter->specValues(*value))
				spec = spec.arg(spec_value);
			++value;
		}
		if (spec.isEmpty())
			spec = QStringLiteral(" ");  // intentionally non-empty: enforce non-local state, as the dialog does
		
		std::vector<QString> params(values.begin(), values.end());
		return { id, spec, std::move(params) };
	}
	
	p.fail(QString::fromLatin1("unknown CRS \"%1\": expected \"local\", \"EPSG:<code>\", \"UTM:<zone>\" or a PROJ string").arg(text));
}


/**
 * Validates the `ref_point` parameter: an object with optional `map` [x, y]
 * (millimetres on paper, y growing downwards, as OOM's map coordinates do)
 * and at most one of `projected` [east, north] and `geographic` [lat, lon].
 */
QJsonObject parseRefPoint(const Params& p)
{
	auto const v = p.value("ref_point");
	if (!v.isObject())
		p.fail(QLatin1String("parameter \"ref_point\" must be an object with \"map\", \"projected\" or \"geographic\" coordinates"));
	
	auto const object = v.toObject();
	for (auto const& key : object.keys())
	{
		if (key != QLatin1String("map") && key != QLatin1String("projected") && key != QLatin1String("geographic"))
			p.fail(QString::fromLatin1("unknown member \"%1\" in \"ref_point\"").arg(key));
	}
	if (object.isEmpty())
		p.fail(QLatin1String("parameter \"ref_point\" is empty"));
	if (object.contains(QLatin1String("projected")) && object.contains(QLatin1String("geographic")))
		p.fail(QLatin1String("\"ref_point\" takes either \"projected\" or \"geographic\" coordinates, not both"));
	
	for (auto const& key : object.keys())
		p.numberArray(object.value(key), 2, QLatin1String("\"ref_point\".") + key);
	return object;
}



// ### Printing ###

struct ExportExtent
{
	enum Kind { PrintArea, DrawnMap, Box, Named };
	Kind kind = PrintArea;
	QRectF box;
	QString name;  // foom: a named extent of the map
};

/**
 * Parses the `extent` parameter: `print-area` (default), `map`, or a box
 * [x, y, width, height] in millimetres on paper.
 */
/**
 * Returns the layer names of the import step's "layer": a string or an array of strings.
 */
QStringList parseImportLayers(const Params& p)
{
	QStringList layers;
	if (!p.has("layer"))
		return layers;
	auto const v = p.value("layer");
	if (v.isString())
	{
		layers.push_back(v.toString());
	}
	else if (v.isArray())
	{
		for (auto const& item : v.toArray())
		{
			if (!item.isString())
				p.fail(QLatin1String("\"layer\" must be a layer name or an array of layer names"));
			layers.push_back(item.toString());
		}
	}
	else
	{
		p.fail(QLatin1String("\"layer\" must be a layer name or an array of layer names"));
	}
	return layers;
}


ExportExtent parseExtent(const Params& p)
{
	ExportExtent extent;
	if (!p.has("extent"))
		return extent;
	
	auto const v = p.value("extent");
	if (v.isString())
	{
		auto const name = v.toString();
		if (name == QLatin1String("print-area"))
			extent.kind = ExportExtent::PrintArea;
		else if (name == QLatin1String("map"))
			extent.kind = ExportExtent::DrawnMap;
		else if (!name.isEmpty())
		{
			extent.kind = ExportExtent::Named;
			extent.name = name;
		}
		else
			p.fail(QString::fromLatin1("unknown extent \"%1\": expected \"print-area\", \"map\", a named extent or [x, y, width, height]").arg(name));
	}
	else
	{
		auto const numbers = p.numberArray(v, 4, QLatin1String("\"extent\""));
		extent.kind = ExportExtent::Box;
		extent.box = QRectF(numbers[0], numbers[1], numbers[2], numbers[3]);
		if (extent.box.width() <= 0 || extent.box.height() <= 0)
			p.fail(QLatin1String("the extent box needs a positive width and height"));
	}
	return extent;
}

MapPrinterOptions::MapPrinterMode parseMode(const Params& p, MapPrinterOptions::MapPrinterMode default_mode)
{
	if (!p.has("mode"))
		return default_mode;
	
	auto const name = p.requireString("mode");
	if (name == QLatin1String("vector"))
		return MapPrinterOptions::Vector;
	if (name == QLatin1String("raster"))
		return MapPrinterOptions::Raster;
	if (name == QLatin1String("separations"))
		return MapPrinterOptions::Separations;
	p.fail(QString::fromLatin1("unknown mode \"%1\": expected \"vector\", \"raster\" or \"separations\"").arg(name));
}

/**
 * Applies the printing parameters to the printer: `scale`, `dpi`, `mode`, `extent`.
 */
ExportExtent configurePrinter(MapPrinter& printer, const Map& map, const Params& p, MapPrinterOptions::MapPrinterMode default_mode)
{
	if (p.has("scale"))
		printer.setScale(p.requirePositiveInt("scale"));
	if (p.has("dpi"))
		printer.setResolution(int(p.requirePositiveInt("dpi")));
	printer.setMode(parseMode(p, default_mode));
	
	auto const extent = parseExtent(p);
	switch (extent.kind)
	{
	case ExportExtent::PrintArea:
		break;
	case ExportExtent::DrawnMap:
		printer.setPrintArea(map.calculateExtent(false, printer.getOptions().show_templates));
		break;
	case ExportExtent::Box:
		printer.setPrintArea(extent.box);
		break;
	case ExportExtent::Named:
		{
			auto const& extents = map.extents();
			auto const named = std::find_if(begin(extents), end(extents), [&extent](const OpenOrienteering::Extent& e) { return e.name == extent.name; });
			if (named == end(extents))
				p.fail(QString::fromLatin1("unknown extent \"%1\": the map has no extent of that name").arg(extent.name));
			printer.setPrintArea(named->rect_mm);
			if (named->scale > 0)
				printer.setScale(named->scale);
		}
		break;
	}
	return extent;
}



// ### Formats ###

bool matchesFormat(const FileFormat& format, const QString& name)
{
	return name.compare(QLatin1String(format.id()), Qt::CaseInsensitive) == 0
	       || format.fileExtensions().contains(name, Qt::CaseInsensitive);
}

const FileFormat* findFormat(const QString& name, bool (FileFormat::*predicate)() const)
{
	return FileFormats.findFormat([&name, predicate](const FileFormat* format) {
		return (format->*predicate)() && matchesFormat(*format, name);
	});
}

struct ExportTarget
{
	enum Kind { Image, Pdf, Registry };
	Kind kind;
	QString format;                    ///< lower-case format name, e.g. "png"
	const FileFormat* file_format;     ///< for Registry
};

bool isImageFormat(const QString& format)
{
	static const char* const image_formats[] = { "png", "bmp", "tif", "tiff", "jpg", "jpeg" };
	for (auto image_format : image_formats)
	{
		if (format == QLatin1String(image_format))
			return true;
	}
	return false;
}

/**
 * Resolves the format of an `export` step from the `format` parameter or
 * the output path's extension.
 */
ExportTarget resolveExportFormat(const Params& p, const QString& path, const QString& format_param)
{
	auto const format = (format_param.isEmpty() ? QFileInfo(path).suffix() : format_param).toLower();
	if (format.isEmpty())
		p.fail(QLatin1String("the export format is given neither by \"format\" nor by the extension of \"path\""));
	
	if (isImageFormat(format))
		return { ExportTarget::Image, format, nullptr };
	if (format == QLatin1String("pdf"))
		return { ExportTarget::Pdf, format, nullptr };
	
	if (auto const* file_format = findFormat(format, &FileFormat::supportsFileExport))
		return { ExportTarget::Registry, format, file_format };
	if (findFormat(format, &FileFormat::supportsFileSaveAs))
		p.fail(QString::fromLatin1("\"%1\" is a map file format: use the \"save-as\" step").arg(format));
	p.fail(QString::fromLatin1("unknown export format \"%1\"").arg(format));
}

/**
 * Resolves the format of a `save-as` step from the `format` and `version`
 * parameters or the output path's extension.
 */
const FileFormat* resolveSaveFormat(const Params& p, const QString& path, const QString& format_param, unsigned int version)
{
	auto const format = (format_param.isEmpty() ? QFileInfo(path).suffix() : format_param).toLower();
	if (format.isEmpty())
		p.fail(QLatin1String("the file format is given neither by \"format\" nor by the extension of \"path\""));
	
	auto const* file_format = findFormat(format, &FileFormat::supportsFileSaveAs);
	if (!file_format)
	{
		if (findFormat(format, &FileFormat::supportsFileExport))
			p.fail(QString::fromLatin1("\"%1\" is an export format: use the \"export\" step").arg(format));
		p.fail(QString::fromLatin1("unknown file format \"%1\"").arg(format));
	}
	
	if (version > 0)
	{
		auto const id = QLatin1String(file_format->id());
		if (!id.startsWith(QLatin1String("OCD")))
			p.fail(QLatin1String("parameter \"version\" applies to OCD files only"));
		auto const versioned_id = QString::fromLatin1("OCD%1").arg(version);
		// A conflict needs a format name which itself names a version, such as "ocd12";
		// "ocd" alone matches by extension and may resolve to any OCD version.
		auto const named_version = format.size() > 3;
		if (named_version && id != QLatin1String("OCD") && versioned_id != id)
			p.fail(QString::fromLatin1("format \"%1\" conflicts with version %2").arg(format, QString::number(version)));
		file_format = FileFormats.findFormat(versioned_id.toLatin1().constData());
		if (!file_format)
			p.fail(QString::fromLatin1("OCD version %1 is not supported").arg(version));
	}
	return file_format;
}





}  // namespace



StepRunner::StepRunner(const Job& job)
: job(job)
{
	// nothing else
}

StepRunner::~StepRunner() = default;


// static
void StepRunner::validate(const Step& step, bool& map_open)
{
	Params p(step);
	auto const& name = step.name;
	if (name == QLatin1String("open"))
	{
		p.checkKnown({ "path" });
		p.requireString("path");
		map_open = true;
	}
	else if (name == QLatin1String("import"))
	{
		p.requireMap(map_open);
		p.checkKnown({ "path", "crt", "layer", "clip" });
		p.requireString("path");
		p.optionalString("crt");
		parseImportLayers(p);
		if (p.has("clip"))
			p.numberArray(p.value("clip"), 4, QLatin1String("\"clip\""));
	}
	else if (name == QLatin1String("apply-crt"))
	{
		p.requireMap(map_open);
		p.checkKnown({ "path" });
		p.requireString("path");
	}
	else if (name == QLatin1String("replace-symbols"))
	{
		p.requireMap(map_open);
		p.checkKnown({ "path", "match" });
		p.requireString("path");
		auto const match = p.optionalString("match", QStringLiteral("number"));
		if (match != QLatin1String("number") && match != QLatin1String("name"))
			p.fail(QString::fromLatin1("unknown matching mode \"%1\": expected \"number\" or \"name\"").arg(match));
	}
	else if (name == QLatin1String("change-scale"))
	{
		p.requireMap(map_open);
		p.checkKnown({ "scale", "scale_symbols" });
		p.requirePositiveInt("scale");
		p.optionalBool("scale_symbols", true);
	}
	else if (name == QLatin1String("set-georeferencing"))
	{
		p.requireMap(map_open);
		p.checkKnown({ "crs", "ref_point", "declination" });
		if (!p.has("crs") && !p.has("ref_point") && !p.has("declination"))
			p.fail(QLatin1String("needs at least one of \"crs\", \"ref_point\" and \"declination\""));
		if (p.has("crs"))
			parseCrs(p, p.requireString("crs"));
		if (p.has("ref_point"))
			parseRefPoint(p);
		if (p.has("declination"))
			p.requireNumber("declination");
	}
	else if (name == QLatin1String("check"))
	{
		p.requireMap(map_open);
		p.checkKnown({ "rules", "out" });
		p.fail(QLatin1String("step \"check\" is not supported yet: the map check is not built"));
	}
	else if (name == QLatin1String("export"))
	{
		p.requireMap(map_open);
		p.checkKnown({ "format", "path", "extent", "scale", "dpi", "mode", "course" });
		auto const path = p.requireString("path");
		p.unsupported("course", "course setting is not built yet");
		auto const target = resolveExportFormat(p, path, p.optionalString("format"));
		switch (target.kind)
		{
		case ExportTarget::Image:
			if (parseMode(p, MapPrinterOptions::Raster) != MapPrinterOptions::Raster)
				p.fail(QString::fromLatin1("\"%1\" images are raster output: \"mode\" must be \"raster\" or absent").arg(target.format));
			Q_FALLTHROUGH();
		case ExportTarget::Pdf:
			if (p.has("scale"))
				p.requirePositiveInt("scale");
			if (p.has("dpi"))
				p.requirePositiveInt("dpi");
			parseMode(p, MapPrinterOptions::Vector);
			parseExtent(p);
			break;
		case ExportTarget::Registry:
			for (auto parameter : { "extent", "scale", "dpi", "mode" })
			{
				if (p.has(parameter))
					p.fail(QString::fromLatin1("parameter \"%1\" applies to image and PDF export only, not to \"%2\"").arg(QLatin1String(parameter), target.format));
			}
			break;
		}
	}
	else if (name == QLatin1String("save-as"))
	{
		p.requireMap(map_open);
		p.checkKnown({ "path", "format", "version" });
		auto const path = p.requireString("path");
		auto const version = p.has("version") ? p.requirePositiveInt("version") : 0u;
		resolveSaveFormat(p, path, p.optionalString("format"), version);
	}
	else
	{
		p.fail(QString::fromLatin1("unknown step \"%1\"").arg(name));
	}
}


StepResult StepRunner::run(const Step& step)
{
	auto const& name = step.name;
	if (name == QLatin1String("open"))
		return open(step);
	if (name == QLatin1String("import"))
		return import(step);
	if (name == QLatin1String("apply-crt"))
		return applyCrt(step);
	if (name == QLatin1String("replace-symbols"))
		return replaceSymbols(step);
	if (name == QLatin1String("change-scale"))
		return changeScale(step);
	if (name == QLatin1String("set-georeferencing"))
		return setGeoreferencing(step);
	if (name == QLatin1String("export"))
		return exportMap(step);
	if (name == QLatin1String("save-as"))
		return saveAs(step);
	return failure(QString::fromLatin1("unknown step \"%1\"").arg(name));  // validate() prevents this
}


StepResult StepRunner::open(const Step& step)
{
	Params p(step);
	auto const path = job.resolvePath(p.requireString("path"));
	
	auto new_map = std::make_unique<Map>();
	auto importer = FileFormats.makeImporter(path, *new_map, nullptr);
	if (!importer)
		return failure(cannotOpen(path));
	if (!importer->doImport())
		return failure(lastWarning(importer->warnings(), cannotOpen(path)));
	
	map = std::move(new_map);
	StepResult result;
	result.details.insert(QLatin1String("path"), path);
	result.details.insert(QLatin1String("objects"), map->getNumObjects());
	result.details.insert(QLatin1String("scale"), int(map->getScaleDenominator()));
	addWarnings(result, importer->warnings());
	return result;
}


#ifdef MAPPER_USE_GDAL
// foom: the import step with the import options
static StepResult importGeodataStep(Map& map, const Job& job, const Params& p, const QString& path, const QString& crt)
{
	auto const* format = FileFormats.findFormatForFilename(path, &FileFormat::supportsFileImport);
	if (!format || qstrcmp(format->id(), "OGR") != 0)
		return failure(QString::fromLatin1("\"layer\" and \"clip\" apply to geodata files, not to \"%1\"").arg(path));
	
	GeodataImportOptions options;
	options.layers = parseImportLayers(p);
	if (p.has("clip"))
	{
		auto const box = p.numberArray(p.value("clip"), 4, QLatin1String("\"clip\""));
		options.clip = geodataClipBox(map, QRectF(QPointF(box[0], box[1]), QPointF(box[2], box[3])));
	}
	if (!crt.isEmpty())
		options.crt_file = job.resolvePath(crt);
	
	auto const outcome = importGeodata(map, { path }, options);
	if (!outcome.ok)
		return failure(outcome.error);
	StepResult result;
	result.details.insert(QLatin1String("path"), path);
	result.details.insert(QLatin1String("objects"), outcome.objects);
	addWarnings(result, std::vector<QString>(outcome.warnings.begin(), outcome.warnings.end()));
	return result;
}
#endif


StepResult StepRunner::import(const Step& step)
{
	Params p(step);
	auto const path = job.resolvePath(p.requireString("path"));
	auto const crt = p.optionalString("crt");
	
#ifdef MAPPER_USE_GDAL
	if (p.has("layer") || p.has("clip"))
		return importGeodataStep(*map, job, p, path, crt);  // foom: the import options
#endif
	
	Map imported;
	imported.setScaleDenominator(map->getScaleDenominator());  // for non-scaled geodata
	auto importer = FileFormats.makeImporter(path, imported, nullptr);
	if (!importer)
		return failure(cannotOpen(path));
	if (!importer->doImport())
		return failure(lastWarning(importer->warnings(), cannotOpen(path)));
	
	StepResult result;
	result.details.insert(QLatin1String("path"), path);
	addWarnings(result, importer->warnings());
	
	if (!crt.isEmpty())
	{
		SymbolRuleSet rules;
		QString error;
		if (!loadCrtRules(job.resolvePath(crt), *map, imported, rules, error))
			return failure(error);
		result.details.insert(QLatin1String("rules"), int(rules.squeezed().size()));
		std::move(rules).apply(imported, *map, {});
	}
	
	if (imported.getNumSymbols() > 0
	    && imported.getScaleDenominator() != map->getScaleDenominator())
	{
		// The editor asks; the runner rescales, as the editor's default answer does.
		result.details.insert(QLatin1String("rescaled_from"), int(imported.getScaleDenominator()));
		imported.changeScale(map->getScaleDenominator(), 1.0, MapCoord(0, 0), true, true, true, true);
	}
	
	auto const objects_before = map->getNumObjects();
	map->importMap(imported, Map::MinimalObjectImport | Map::GeorefImport);
	result.details.insert(QLatin1String("objects"), map->getNumObjects() - objects_before);
	return result;
}


StepResult StepRunner::applyCrt(const Step& step)
{
	Params p(step);
	auto const path = job.resolvePath(p.requireString("path"));
	
	SymbolRuleSet rules;
	QString error;
	if (!loadCrtRules(path, *map, *map, rules, error))
		return failure(error);
	
	StepResult result;
	result.details.insert(QLatin1String("path"), path);
	result.details.insert(QLatin1String("rules"), int(rules.squeezed().size()));
	std::move(rules).apply(*map, *map, {});
	return result;
}


StepResult StepRunner::replaceSymbols(const Step& step)
{
	Params p(step);
	auto const path = job.resolvePath(p.requireString("path"));
	auto const match = p.optionalString("match", QStringLiteral("number"));
	
	Map symbol_set;
	auto importer = FileFormats.makeImporter(path, symbol_set, nullptr);
	if (!importer)
		return failure(cannotOpen(path));
	importer->setLoadSymbolsOnly(true);
	if (!importer->doImport())
		return failure(lastWarning(importer->warnings(), cannotOpen(path)));
	
	auto rules = SymbolRuleSet::forUsedSymbols(*map);
	if (match == QLatin1String("name"))
		rules.matchQuerySymbolName(symbol_set);
	else
		rules.matchQuerySymbolNumber(symbol_set);
	
	StepResult result;
	result.details.insert(QLatin1String("path"), path);
	addWarnings(result, importer->warnings());
	QJsonArray unmatched;
	for (auto const& rule : rules)
	{
		if (rule.type == SymbolRule::NoAssignment && rule.query.symbolOperand())
			unmatched.append(rule.query.symbolOperand()->getNumberAsString());
	}
	result.details.insert(QLatin1String("replaced"), int(rules.size()) - unmatched.size());
	if (!unmatched.isEmpty())
		result.details.insert(QLatin1String("unmatched"), unmatched);
	
	// The options of the editor's "Replace symbol set" dialog, as it opens.
	std::move(rules).apply(*map, symbol_set,
	                       SymbolRuleSet::ImportAllSymbols | SymbolRuleSet::PreserveSymbolState
	                       | SymbolRuleSet::RemoveUnusedSymbols | SymbolRuleSet::RemoveUnusedColors);
	map->setSymbolSetId(symbol_set.symbolSetId());
	return result;
}


StepResult StepRunner::changeScale(const Step& step)
{
	Params p(step);
	auto const scale = p.requirePositiveInt("scale");
	auto const scale_symbols = p.optionalBool("scale_symbols", true);
	
	StepResult result;
	result.details.insert(QLatin1String("from"), int(map->getScaleDenominator()));
	result.details.insert(QLatin1String("scale"), int(scale));
	// Objects, georeferencing and templates follow the scale change, centred on the origin,
	// as the editor's scale dialog does by default.
	map->changeScale(scale, 1.0, MapCoord(0, 0), scale_symbols, true, true, true);
	return result;
}


StepResult StepRunner::setGeoreferencing(const Step& step)
{
	Params p(step);
	Georeferencing georef(map->getGeoreferencing());
	
	auto const ref_point = p.has("ref_point") ? parseRefPoint(p) : QJsonObject();
	if (p.has("crs"))
	{
		auto const crs = parseCrs(p, p.requireString("crs"));
		if (!georef.setProjectedCRS(crs.id, crs.spec, crs.params))
			return failure(QString::fromLatin1("invalid CRS \"%1\": %2").arg(p.requireString("crs"), georef.getErrorText()));
		if (georef.getState() != Georeferencing::Local
		    && !ref_point.contains(QLatin1String("projected"))
		    && !ref_point.contains(QLatin1String("geographic")))
		{
			// Keep the geographic position of the reference point, as the dialog does.
			georef.setGeographicRefPoint(georef.getGeographicRefPoint());
		}
	}
	
	if (ref_point.contains(QLatin1String("map")))
	{
		auto const xy = p.numberArray(ref_point.value(QLatin1String("map")), 2, QLatin1String("\"ref_point\".map"));
		georef.setMapRefPoint(MapCoord(xy[0], xy[1]));
	}
	if (ref_point.contains(QLatin1String("projected")))
	{
		auto const en = p.numberArray(ref_point.value(QLatin1String("projected")), 2, QLatin1String("\"ref_point\".projected"));
		georef.setProjectedRefPoint(QPointF(en[0], en[1]));
	}
	if (ref_point.contains(QLatin1String("geographic")))
	{
		auto const lat_lon = p.numberArray(ref_point.value(QLatin1String("geographic")), 2, QLatin1String("\"ref_point\".geographic"));
		georef.setGeographicRefPoint(LatLon(lat_lon[0], lat_lon[1]));
	}
	
	if (p.has("declination"))
		georef.setDeclination(p.requireNumber("declination"));
	
	if (georef.getState() == Georeferencing::BrokenGeospatial)
		return failure(QString::fromLatin1("the georeferencing is broken: %1").arg(georef.getErrorText()));
	
	map->setGeoreferencing(georef);
	
	StepResult result;
	result.details.insert(QLatin1String("crs"), georef.getProjectedCRSId());
	result.details.insert(QLatin1String("declination"), georef.getDeclination());
	result.details.insert(QLatin1String("grivation"), georef.getGrivation());
	return result;
}


StepResult StepRunner::exportMap(const Step& step)
{
	Params p(step);
	auto const path = job.resolvePath(p.requireString("path"));
	auto const target = resolveExportFormat(p, path, p.optionalString("format"));
	
	switch (target.kind)
	{
	case ExportTarget::Image:
		return exportImage(step, path, target.format);
	case ExportTarget::Pdf:
		return exportPdf(step, path);
	case ExportTarget::Registry:
		break;
	}
	
	auto exporter = target.file_format->makeExporter(path, map.get(), nullptr);
	if (!exporter)
		return failure(QString::fromLatin1("export as %1 is not supported").arg(target.file_format->description()));
	if (!exporter->doExport())
		return failure(lastWarning(exporter->warnings(), QString::fromLatin1("cannot write \"%1\"").arg(path)));
	
	StepResult result;
	result.details.insert(QLatin1String("path"), path);
	result.details.insert(QLatin1String("format"), QLatin1String(target.file_format->id()));
	addWarnings(result, exporter->warnings());
	return result;
}


StepResult StepRunner::exportImage(const Step& step, const QString& path, const QString& format)
{
	Params p(step);
	MapPrinter printer(*map, nullptr);
	printer.setTarget(MapPrinter::imageTarget());
	configurePrinter(printer, *map, p, MapPrinterOptions::Raster);
	
	// As PrintWidget::exportToImage()
	auto const pixel_per_mm = printer.getOptions().resolution / 25.4;
	auto const width = qRound(printer.getPrintAreaPaperSize().width() * pixel_per_mm);
	auto const height = qRound(printer.getPrintAreaPaperSize().height() * pixel_per_mm);
	if (width < 1 || height < 1)
		return failure(QLatin1String("the extent is empty"));
	
	QImage image(width, height, QImage::Format_ARGB32_Premultiplied);
	if (image.isNull())
		return failure(QString::fromLatin1("cannot allocate an image of %1 x %2 pixels").arg(width).arg(height));
	
	auto const dots_per_meter = qRound(pixel_per_mm * 1000);
	image.setDotsPerMeterX(dots_per_meter);
	image.setDotsPerMeterY(dots_per_meter);
	image.fill(QColor(Qt::white));
	{
		QPainter painter(&image);
		printer.drawPage(&painter, printer.getPrintArea(), &image);
	}
	if (!image.save(path))
		return failure(QString::fromLatin1("cannot write the %1 image \"%2\"").arg(format.toUpper(), path));
	
	StepResult result;
	result.details.insert(QLatin1String("path"), path);
	result.details.insert(QLatin1String("format"), format);
	result.details.insert(QLatin1String("width"), width);
	result.details.insert(QLatin1String("height"), height);
	result.details.insert(QLatin1String("dpi"), printer.getOptions().resolution);
	return result;
}


StepResult StepRunner::exportPdf(const Step& step, const QString& path)
{
	Params p(step);
	MapPrinter printer(*map, nullptr);
	printer.setTarget(MapPrinter::pdfTarget());
	auto const extent = configurePrinter(printer, *map, p, MapPrinterOptions::Vector);
	if (extent.kind != ExportExtent::PrintArea)
	{
		// An explicit extent goes onto a single page of its size.
		printer.setCustomPageSize(printer.getPrintAreaPaperSize());
	}
	
	auto qprinter = printer.makePrinter();
	if (!qprinter)
		return failure(QLatin1String("cannot prepare the PDF export"));
	qprinter->setOutputFormat(QPrinter::PdfFormat);
	qprinter->setOutputFileName(path);
	qprinter->setCreator(QString::fromLatin1("FOOM " APP_VERSION));
	qprinter->setDocName(QFileInfo(path).completeBaseName());
	
	if (!printer.printMap(qprinter.get()))
	{
		QFile::remove(path);
		return failure(QString::fromLatin1("cannot write the PDF \"%1\"").arg(path));
	}
	
	StepResult result;
	result.details.insert(QLatin1String("path"), path);
	result.details.insert(QLatin1String("format"), QStringLiteral("pdf"));
	result.details.insert(QLatin1String("pages"), int(printer.horizontalPagePositions().size() * printer.verticalPagePositions().size()));
	return result;
}


StepResult StepRunner::saveAs(const Step& step)
{
	Params p(step);
	auto const path = job.resolvePath(p.requireString("path"));
	auto const version = p.has("version") ? p.requirePositiveInt("version") : 0u;
	auto const* format = resolveSaveFormat(p, path, p.optionalString("format"), version);
	
	auto exporter = format->makeExporter(path, map.get(), nullptr);
	if (!exporter)
		return failure(QString::fromLatin1("saving as %1 is not supported").arg(format->description()));
	if (!exporter->doExport())
		return failure(lastWarning(exporter->warnings(), QString::fromLatin1("cannot write \"%1\"").arg(path)));
	
	StepResult result;
	result.details.insert(QLatin1String("path"), path);
	result.details.insert(QLatin1String("format"), QLatin1String(format->id()));
	addWarnings(result, exporter->warnings());
	return result;
}


}  // namespace Cli

}  // namespace OpenOrienteering
