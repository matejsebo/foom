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


#include "steps.h"

#include <algorithm>
#include <cstddef>
#include <initializer_list>
#include <utility>
#include <vector>

#include <Qt>
#include <QtGlobal>
#include <QByteArray>  // foom
#include <QColor>
#include <QFile>
#include <QDateTime>  // foom
#include <QFileInfo>
#include <QImage>
#include <QIODevice>
#include <QJsonArray>
#include <QJsonDocument>  // foom
#include <QJsonObject>  // foom
#include <QJsonValue>
#include <QList>  // foom
#include <QSaveFile>  // foom
#include <QLatin1Char>
#include <QLatin1String>
#include <QPageSize>
#include <QPainter>
#include <QPointF>
#include <QPrinter>
#include <QRectF>
#include <QSizeF>
#include <QStringList>
#include <QTextStream>
#include <QUuid>  // foom

#include "job.h"
#include "background_steps.h"  // foom
#include "generator_steps.h"  // foom
#include "mapper_config.h"
#include "check/issue.h"  // foom
#include "check/legibility.h"  // foom
#include "check/map_check.h"  // foom
#include "core/crs_template.h"
#include "core/crt_rules.h"
#include "core/georeferencing.h"
#include "core/latlon.h"
#include "core/extent_printing.h"
#include "core/extents.h"
#include "course/course_map.h"  // foom
#include "course/course_interchange.h"  // foom
#include "course/course_report.h"  // foom
#include "course/route_course.h"  // foom
#include "course/route_print.h"  // foom
#include "course/route_store.h"  // foom
#include "elevation/elevation_model.h"  // foom
#include "elevation/elevation_set.h"  // foom
#include "elevation/raster_products.h"  // foom
#include "core/map.h"
#include "core/open_backup.h"  // foom
#include "core/map_coord.h"
#include "core/map_printer.h"
#include "core/print_output.h"
#include "core/raster_export.h"  // foom
#include "core/objects/object_query.h"
#include "core/objects/symbol_rule_set.h"
#include "core/symbols/private_symbol.h"
#include "core/symbols/symbol.h"
#include "core/sync_record.h"
#include "elevation/elevation_wizard.h"  // foom
#include "fileformats/file_format.h"
#include "fileformats/file_format_registry.h"
#include "fileformats/file_import_export.h"
#include "sync/field_copy.h"
#include "sync/merge.h"
#include "templates/template.h"  // foom
#ifdef MAPPER_USE_GDAL
#  include "gdal/cmyk_tiff.h"  // foom
#  include "gdal/gif_writer.h"  // foom
#  include "gdal/geodata_import.h"  // foom
#  include "gdal/import_rule_choice.h"  // foom
#  include "gdal/new_map.h"  // foom
#endif
#ifdef FOOM_WITH_POPPLER
#  include "pdf/pdf_import.h"  // foom
#endif


namespace OpenOrienteering {

namespace Cli {

static StepResult importPdfStep(Map& map, const Job& job, const Step& step);  // foom

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

// foom: adds one warning to those of the result
void appendWarning(StepResult& result, const QString& warning)
{
	auto array = result.details.value(QLatin1String("warnings")).toArray();
	array.append(warning);
	result.details.insert(QLatin1String("warnings"), array);
}

/** What an image or PDF leaves out because it is hidden, as warnings. */
std::vector<QString> hiddenWarnings(const Map& map)
{
	std::vector<QString> warnings;
	for (auto const& line : hiddenContent(map).lines())
	{
		QString warning = QStringLiteral("not in the output: ");
		warning += line;
		warnings.push_back(warning);
	}
	return warnings;
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
// foom: the design notes, "Headless runner", the step `check`.

enum Checker
{
	MapChecker,
	LegibilityChecker,
	CourseChecker,
	CheckerCount
};

const char* const checker_names[CheckerCount] = { "map", "legibility", "courses" };

/** The rules which a checker reports. */
std::vector<const char*> rulesOf(int checker)
{
	switch (checker)
	{
	case MapChecker:
		return { rule::text_empty, rule::path_degenerate, rule::symbol_undefined, rule::symbol_wrong_type,
		         rule::object_duplicate, rule::vertex_duplicate, rule::area_open, rule::symbol_unused,
		         rule::color_unused, rule::template_missing, rule::text_path_overflow };
	case LegibilityChecker:
		return { rule::line_short, rule::area_small, rule::gap };
	case CourseChecker:
		return { rule::course_start_missing, rule::course_finish_missing, rule::course_climb_zero,
		         rule::course_identical, rule::course_not_checked, rule::relay_structure, rule::course_part,
		         rule::control_unused, rule::control_too_close, rule::leg_opposite, rule::description_d_empty,
		         rule::description_de_missing, rule::description_fg_missing, rule::punch_consecutive,
		         rule::punch_repeated };
	default:
		return {};
	}
}

/** Which checkers run, and which of their rules are kept. */
struct CheckRules
{
	bool run[CheckerCount] = {};
	bool whole[CheckerCount] = {};
	QList<QByteArray> named;  ///< rule ids named alone
	
	bool keeps(int checker, const QByteArray& rule) const
	{
		return whole[checker] || named.contains(rule);
	}
};

CheckRules parseCheckRules(const Params& p)
{
	CheckRules result;
	if (!p.has("rules"))
	{
		result.run[MapChecker] = result.whole[MapChecker] = true;
		return result;
	}
	auto const what = QLatin1String("\"rules\" must be a non-empty array of checkers and rule ids");
	auto const v = p.value("rules");
	if (!v.isArray() || v.toArray().isEmpty())
		p.fail(what);
	for (auto const& item : v.toArray())
	{
		if (!item.isString())
			p.fail(what);
		auto const name = item.toString().toLatin1();
		auto found = false;
		for (int checker = 0; checker < CheckerCount && !found; ++checker)
		{
			if (name == checker_names[checker])
			{
				result.run[checker] = result.whole[checker] = found = true;
				break;
			}
			for (auto const* rule : rulesOf(checker))
			{
				if (name == rule)
				{
					result.run[checker] = found = true;
					result.named.push_back(name);
					break;
				}
			}
		}
		if (!found)
			p.fail(QString::fromLatin1("unknown rule \"%1\": expected \"map\", \"legibility\", \"courses\" or a rule id of them").arg(item.toString()));
	}
	return result;
}

/** True for JSON output, false for CSV. */
bool checkOutIsJson(const Params& p, const QString& out)
{
	auto const suffix = QFileInfo(out).suffix().toLower();
	if (suffix == QLatin1String("json"))
		return true;
	if (suffix != QLatin1String("csv"))
		p.fail(QString::fromLatin1("\"out\" of \"check\" must end in .csv or .json, not \"%1\"").arg(out));
	return false;
}

/** The least severity which makes the step fail, or -1 for none. */
int parseFailOn(const Params& p)
{
	if (!p.has("fail_on"))
		return -1;
	auto const v = p.requireString("fail_on");
	for (auto severity : { Issue::Info, Issue::Warning, Issue::Error })
	{
		if (v == severityName(severity))
			return int(severity);
	}
	p.fail(QString::fromLatin1("unknown fail_on \"%1\": expected \"error\", \"warning\" or \"info\"").arg(v));
}

double parseGap(const Params& p, const Map* map)
{
	if (!p.has("gap"))
		return map ? defaultGap(*map) : 0.0;
	auto const gap = p.requireNumber("gap");
	if (!(gap > 0))
		p.fail(QLatin1String("parameter \"gap\" must be a positive number of mm"));
	return gap;
}

/** The issues as the JSON file of the step `check`. */
QByteArray issuesJson(const std::vector<Issue>& issues)
{
	QJsonArray array;
	for (auto const& issue : issues)
	{
		QJsonArray objects;
		for (auto const& uuid : issue.objects)
			objects.append(uuid.toString(QUuid::WithoutBraces));
		array.append(QJsonObject{
			{ QStringLiteral("rule"), QString::fromLatin1(issue.rule) },
			{ QStringLiteral("severity"), severityName(issue.severity) },
			{ QStringLiteral("objects"), objects },
			{ QStringLiteral("x"), issue.where.x() },
			{ QStringLiteral("y"), issue.where.y() },
			{ QStringLiteral("measured"), issue.measured },
			{ QStringLiteral("limit"), issue.limit },
			{ QStringLiteral("message"), issue.message },
			{ QStringLiteral("subject"), issue.subject },
		});
	}
	return QJsonDocument(QJsonObject{ { QStringLiteral("issues"), array } }).toJson();
}

bool writeBytes(const QString& path, const QByteArray& bytes)
{
	QFile file(path);
	return file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(bytes) == bytes.size();
}

// foom: the design notes, "Headless runner", the step `export-courses`.
CourseFileKind parseCourseFileKind(const Params& p, const QString& path)
{
	auto format = p.optionalString("format");
	if (format.isEmpty())
	{
		format = QFileInfo(path).suffix().toLower();
		if (format == QLatin1String("xml"))
			format = QStringLiteral("iof");
	}
	if (format == QLatin1String("iof"))
		return CourseFileKind::IofXml;
	if (format == QLatin1String("gpx"))
		return CourseFileKind::Gpx;
	if (format == QLatin1String("kml"))
		return CourseFileKind::Kml;
	if (format == QLatin1String("kmz"))
		return CourseFileKind::Kmz;
	p.fail(QString::fromLatin1("unknown course format \"%1\": expected \"iof\" (.xml), \"gpx\", \"kml\" or \"kmz\"").arg(format));
}

// foom: the design notes, "Interchange: GPX and KML in, and a KMZ with the map"
CourseKmzOptions parseCourseKmzOptions(const Params& p, CourseFileKind kind)
{
	CourseKmzOptions options;
	if (kind != CourseFileKind::Kmz)
	{
		for (auto const* name : { "dpi", "tiles", "templates" })
		{
			if (p.has(name))
				p.fail(QString::fromLatin1("\"%1\" applies to KMZ only").arg(QLatin1String(name)));
		}
		return options;
	}
	if (p.has("dpi"))
		options.dpi = p.requirePositiveInt("dpi");
	options.tiles = p.optionalBool("tiles", true);
	options.templates = p.optionalBool("templates", true);
	return options;
}

void validateExportCourses(const Params& p)
{
	auto const kind = parseCourseFileKind(p, p.requireString("path"));
	auto const course = p.optionalString("course");
	auto const per_course = p.optionalBool("per_course", false);
	if (kind == CourseFileKind::IofXml && (!course.isEmpty() || per_course))
		p.fail(QLatin1String("\"course\" and \"per_course\" apply to GPX, KML and KMZ only: IOF XML holds all courses"));
	if (!course.isEmpty() && per_course)
		p.fail(QLatin1String("give \"course\" or \"per_course\", not both"));
	parseCourseKmzOptions(p, kind);
}

// foom: the step `import-courses`: IOF XML, or GPX, KML and KMZ.
CourseFileKind parseImportCourseKind(const Params& p, const QString& path)
{
	auto kind = CourseFileKind::IofXml;
	auto const suffix = QFileInfo(path).suffix().toLower();
	if (p.has("format") || suffix == QLatin1String("gpx") || suffix == QLatin1String("kml") || suffix == QLatin1String("kmz"))
		kind = parseCourseFileKind(p, path);
	if (kind != CourseFileKind::IofXml && p.has("classes"))
		p.fail(QLatin1String("\"classes\" applies to IOF XML only: GPX, KML and KMZ have no classes"));
	return kind;
}

/// The parameters of the step hill-shade; see the design notes, "Bicubic resampling for hill shading".
HillShadeParams parseHillShadeParams(const Params& p)
{
	HillShadeParams params;
	auto const method = p.optionalString("method", QStringLiteral("oblique"));
	if (method == QLatin1String("oblique"))
		params.method = HillShadeParams::Oblique;
	else if (method == QLatin1String("slope"))
		params.method = HillShadeParams::SlopeShading;
	else if (method == QLatin1String("combined"))
		params.method = HillShadeParams::Combined;
	else
		p.fail(QString::fromLatin1("parameter \"method\" must be \"oblique\", \"slope\" or \"combined\""));
	params.multi_directional = p.optionalBool("multi_directional", params.multi_directional);
	if (p.has("azimuth"))
		params.azimuth_deg = p.requireNumber("azimuth");
	if (p.has("elevation"))
	{
		params.elevation_deg = p.requireNumber("elevation");
		if (params.elevation_deg <= 0.0 || params.elevation_deg > 90.0)
			p.fail(QLatin1String("parameter \"elevation\" must be above 0 and at most 90"));
	}
	if (p.has("exaggeration"))
	{
		params.exaggeration = p.requireNumber("exaggeration");
		if (params.exaggeration <= 0.0)
			p.fail(QLatin1String("parameter \"exaggeration\" must be positive"));
	}
	if (p.has("cell_size"))
	{
		params.cell_size = p.requireNumber("cell_size");
		if (params.cell_size <= 0.0)
			p.fail(QLatin1String("parameter \"cell_size\" must be positive"));
	}
	auto const resampling = p.optionalString("resampling", QStringLiteral("bicubic"));
	if (resampling == QLatin1String("bicubic"))
		params.resampling = HillShadeParams::Bicubic;
	else if (resampling == QLatin1String("bilinear"))
		params.resampling = HillShadeParams::Bilinear;
	else
		p.fail(QString::fromLatin1("parameter \"resampling\" must be \"bicubic\" or \"bilinear\""));
	auto const gradient = p.optionalString("gradient", QStringLiteral("corners"));
	if (gradient == QLatin1String("corners"))
		params.gradient = GradientMethod::Corners;
	else if (gradient == QLatin1String("horn"))
		params.gradient = GradientMethod::Horn;
	else
		p.fail(QString::fromLatin1("parameter \"gradient\" must be \"corners\" or \"horn\""));
	return params;
}


QJsonArray jsonList(const QStringList& list)
{
	return QJsonArray::fromStringList(list);
}

// foom: the design notes, "The maps of the courses as PDF files"
QStringList parseCourseNames(const Params& p)
{
	QStringList names;
	if (!p.has("courses"))
		return names;
	auto const v = p.value("courses");
	if (!v.isArray())
		p.fail(QLatin1String("\"courses\" must be an array of course names"));
	for (auto const& item : v.toArray())
	{
		if (!item.isString())
			p.fail(QLatin1String("\"courses\" must be an array of course names"));
		names.push_back(item.toString());
	}
	return names;
}

RelayMaps parseRelayMaps(const Params& p)
{
	auto const name = p.optionalString("relay");
	if (name.isEmpty() || name == QLatin1String("variants"))
		return RelayMaps::Variants;
	if (name != QLatin1String("runners"))
		p.fail(QLatin1String("\"relay\" must be \"variants\" or \"runners\""));
	return RelayMaps::Runners;
}

// foom: the design notes, "Several courses in one file"
CourseMapPlacing parseCourseMapPlacing(const Params& p)
{
	auto const name = p.optionalString("placing");
	if (name.isEmpty() || name == QLatin1String("separate"))
		return CourseMapPlacing::Separate;
	if (name == QLatin1String("pages"))
		return CourseMapPlacing::Pages;
	if (name == QLatin1String("beside"))
		return CourseMapPlacing::Beside;
	if (name == QLatin1String("below"))
		return CourseMapPlacing::Below;
	if (name != QLatin1String("original"))
		p.fail(QLatin1String("\"placing\" must be \"separate\", \"pages\", \"beside\", \"below\" or \"original\""));
	return CourseMapPlacing::Original;
}

// foom: the design notes, "Course only, intensity, line width and the control descriptions"
void parseCourseMapPrint(const Params& p, CourseMapExportOptions& options)
{
	auto const content = p.optionalString("content");
	if (content.isEmpty() || content == QLatin1String("map"))
		options.content = CourseMapContent::Map;
	else if (content == QLatin1String("course-only"))
		options.content = CourseMapContent::CourseOnly;
	else if (content == QLatin1String("descriptions"))
		options.content = CourseMapContent::Descriptions;
	else
		p.fail(QLatin1String("\"content\" must be \"map\", \"course-only\" or \"descriptions\""));
	if (p.has("intensity"))
		options.intensity = qRound(p.requireNumber("intensity"));
	if (p.has("line_width"))
		options.line_width = qRound(p.requireNumber("line_width"));
	if (p.has("stretch_x"))
		options.stretch_x = p.requireNumber("stretch_x");
	if (p.has("stretch_y"))
		options.stretch_y = p.requireNumber("stretch_y");
	options.mirror = p.optionalBool("mirror", false);
	if (p.has("offset_x"))
		options.offset_x = p.requireNumber("offset_x");
	if (p.has("offset_y"))
		options.offset_y = p.requireNumber("offset_y");
	auto const error = courseMapOptionsError(options);
	if (!error.isEmpty())
		p.fail(error);
}

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

/** What `plates`, `depth`, `halftone` and `compression` ask for. */
struct PlateRequest
{
	enum Kind { None, Cmyk, Spot };
	Kind kind = None;
	PlateForm form = PlateForm::Grey;
	bool fax = false;  ///< CCITT group 4 instead of LZW
};

PlateRequest parsePlates(const Params& p, const QString& format)
{
	PlateRequest request;
	if (!p.has("plates"))
	{
		for (auto parameter : { "depth", "halftone", "compression" })
		{
			if (p.has(parameter))
				p.fail(QString::fromLatin1("parameter \"%1\" needs \"plates\": \"spot\"").arg(QLatin1String(parameter)));
		}
		return request;
	}
	
	auto const name = p.requireString("plates");
	if (name == QLatin1String("cmyk"))
	{
		request.kind = PlateRequest::Cmyk;
		if (format.compare(QLatin1String("tif"), Qt::CaseInsensitive) != 0 && format.compare(QLatin1String("tiff"), Qt::CaseInsensitive) != 0)
			p.fail(QLatin1String("\"plates\": \"cmyk\" is written as TIFF only"));
#ifndef MAPPER_USE_GDAL
		p.fail(QLatin1String("\"plates\": \"cmyk\" needs a build with GDAL"));
#endif
		for (auto parameter : { "depth", "halftone", "compression" })
		{
			if (p.has(parameter))
				p.fail(QString::fromLatin1("parameter \"%1\" needs \"plates\": \"spot\"").arg(QLatin1String(parameter)));
		}
	}
	else if (name == QLatin1String("spot"))
	{
		request.kind = PlateRequest::Spot;
		auto depth = 8u;
		if (p.has("depth"))
		{
			depth = p.requirePositiveInt("depth");
			if (depth != 8 && depth != 1)
				p.fail(QLatin1String("parameter \"depth\" must be 8 or 1"));
		}
		auto const halftone = p.optionalBool("halftone", false);
		if (halftone && depth != 1)
			p.fail(QLatin1String("\"halftone\" needs \"depth\": 1"));
		request.form = depth == 8 ? PlateForm::Grey : halftone ? PlateForm::Halftone : PlateForm::LineArt;
		// foom: the design notes, "Raster plates and CMYK raster"
		auto const compression = p.optionalString("compression", QStringLiteral("lzw"));
		if (compression == QLatin1String("fax"))
		{
			request.fax = true;
			if (depth != 1)
				p.fail(QLatin1String("\"compression\": \"fax\" needs \"depth\": 1"));
			if (format.compare(QLatin1String("tif"), Qt::CaseInsensitive) != 0 && format.compare(QLatin1String("tiff"), Qt::CaseInsensitive) != 0)
				p.fail(QLatin1String("\"compression\": \"fax\" is written as TIFF only"));
#ifndef MAPPER_USE_GDAL
			p.fail(QLatin1String("\"compression\": \"fax\" needs a build with GDAL"));
#endif
		}
		else if (compression != QLatin1String("lzw"))
		{
			p.fail(QString::fromLatin1("unknown compression \"%1\": expected \"lzw\" or \"fax\"").arg(compression));
		}
	}
	else
	{
		p.fail(QString::fromLatin1("unknown plates \"%1\": expected \"cmyk\" or \"spot\"").arg(name));
	}
	return request;
}

/**
 * foom: What `colours`, `tiles` and `world_file` ask for.
 * the design notes, "Print and raster output", fourth change
 */
struct RasterRequest
{
	RasterColours colours = RasterColours::Rgb;
	RasterTiles tiles;
	bool world_file = false;

	bool isPlain(const QString& format) const
	{
		return colours == RasterColours::Rgb && !tiles.isOn() && !world_file && format != QLatin1String("gif");
	}
};

RasterRequest parseRaster(const Params& p, const QString& format)
{
	RasterRequest request;
	auto const gif = format == QLatin1String("gif");
	auto const jpeg = format == QLatin1String("jpg") || format == QLatin1String("jpeg");
	if (p.has("plates"))
	{
		for (auto parameter : { "colours", "tiles", "world_file" })
		{
			if (p.has(parameter))
				p.fail(QString::fromLatin1("parameter \"%1\" does not apply to \"plates\"").arg(QLatin1String(parameter)));
		}
		if (gif)
			p.fail(QLatin1String("\"plates\" are written as TIFF or PNG"));
		return request;
	}

	auto const colours = p.optionalString("colours", gif ? QStringLiteral("256") : QStringLiteral("rgb"));
	if (colours == QLatin1String("rgb"))
		request.colours = RasterColours::Rgb;
	else if (colours == QLatin1String("256"))
		request.colours = RasterColours::Palette;
	else if (colours == QLatin1String("grey"))
		request.colours = RasterColours::Grey;
	else
		p.fail(QString::fromLatin1("unknown colours \"%1\": expected \"rgb\", \"256\" or \"grey\"").arg(colours));
	if (gif && request.colours == RasterColours::Rgb)
		p.fail(QLatin1String("a GIF has \"colours\" \"256\" or \"grey\""));
	if (jpeg && request.colours == RasterColours::Palette)
		p.fail(QLatin1String("a JPEG has \"colours\" \"rgb\" or \"grey\""));
#ifndef MAPPER_USE_GDAL
	if (gif)
		p.fail(QLatin1String("GIF export needs a build with GDAL"));
#endif

	if (p.has("tiles"))
	{
		auto const v = p.value("tiles");
		if (v.isArray())
		{
			auto const size = p.numberArray(v, 2, QStringLiteral("parameter \"tiles\""));
			if (size[0] < 1 || size[1] < 1 || size[0] != double(int(size[0])) || size[1] != double(int(size[1])))
				p.fail(QLatin1String("parameter \"tiles\" must be positive integers"));
			request.tiles = { int(size[0]), int(size[1]) };
		}
		else
		{
			auto const size = int(p.requirePositiveInt("tiles"));
			request.tiles = { size, size };
		}
	}
	request.world_file = p.optionalBool("world_file", false);
	return request;
}

/** foom: What `crt`, `crt_only` and `write_crt` ask for; DXF only. */
void parseDxfCrt(const Params& p, const QString& format)
{
	auto const any = p.has("crt") || p.has("crt_only") || p.has("write_crt");
	if (!any)
		return;
	if (format != QLatin1String("dxf"))
		p.fail(QLatin1String("parameters \"crt\", \"crt_only\" and \"write_crt\" apply to DXF export only"));
	p.optionalString("crt");
	p.optionalString("write_crt");
	if (p.has("crt_only") && !p.has("crt"))
		p.fail(QLatin1String("parameter \"crt_only\" needs \"crt\""));
	p.optionalBool("crt_only", false);
}

/** What `paper`, `orientation` and `overlap` ask for. */
struct PaperRequest
{
	bool has_paper = false;
	QPageSize::PageSizeId id = QPageSize::Custom;
	QSizeF size;  ///< For a custom paper, in mm.
	bool has_orientation = false;
	MapPrinterPageFormat::Orientation orientation = MapPrinterPageFormat::Portrait;
	bool has_overlap = false;
	qreal h_overlap = 0;
	qreal v_overlap = 0;
};

PaperRequest parsePaper(const Params& p)
{
	PaperRequest request;
	auto const pair = [&p](const char* name, double minimum, double& first, double& second) {
		auto const array = p.value(name).toArray();
		if (!p.value(name).isArray() || array.size() != 2 || !array.at(0).isDouble() || !array.at(1).isDouble()
		    || array.at(0).toDouble() < minimum || array.at(1).toDouble() < minimum)
			return false;
		first = array.at(0).toDouble();
		second = array.at(1).toDouble();
		return true;
	};
	if (p.has("paper"))
	{
		request.has_paper = true;
		auto const v = p.value("paper");
		auto width = 0.0;
		auto height = 0.0;
		if (v.isString())
		{
			request.id = pageSizeFromKey(v.toString());
			if (request.id == QPageSize::Custom)
				p.fail(QString::fromLatin1("unknown paper \"%1\": expected the key of a page size, such as \"A4\", or [width, height] in mm").arg(v.toString()));
		}
		else if (pair("paper", 1, width, height))
		{
			request.size = QSizeF(width, height);
		}
		else
		{
			p.fail(QLatin1String("parameter \"paper\" must be the key of a page size or [width, height] in mm"));
		}
	}
	if (p.has("orientation"))
	{
		auto const name = p.requireString("orientation");
		if (name == QLatin1String("landscape"))
			request.orientation = MapPrinterPageFormat::Landscape;
		else if (name != QLatin1String("portrait"))
			p.fail(QString::fromLatin1("unknown orientation \"%1\": expected \"portrait\" or \"landscape\"").arg(name));
		if (!request.has_paper || request.id == QPageSize::Custom)
			p.fail(QLatin1String("\"orientation\" needs \"paper\" with the key of a page size"));
		request.has_orientation = true;
	}
	if (p.has("overlap"))
	{
		if (!pair("overlap", 0, request.h_overlap, request.v_overlap))
			p.fail(QLatin1String("parameter \"overlap\" must be [horizontal, vertical] in mm"));
		request.has_overlap = true;
	}
	return request;
}

/**
 * foom: Loads the files of the templates which are not loaded. In the editor
 * the view loads them; the runner has none, and what prints the templates
 * or reads the background maps needs them. A template whose file cannot be
 * loaded stays as it is. the design notes, "Headless runner".
 */
void loadTemplateFiles(Map& map)
{
	for (int i = 0; i < map.getNumTemplates(); ++i)
	{
		auto* temp = map.getTemplate(i);
		if (temp->getTemplateState() == Template::Unloaded)
			temp->loadTemplateFile();
	}
}

/** foom: The same when the printer prints the templates: by the parameter `templates`, or else by the print settings of the map. */
void loadTemplateFilesFor(Map& map, const MapPrinter& printer, const Params& p)
{
	if (p.has("templates") ? p.optionalBool("templates", false) : printer.getOptions().show_templates)
		loadTemplateFiles(map);
}

/**
 * Applies the printing parameters to the printer: `scale`, `dpi`, `mode`,
 * `templates`, `grid`, `overprinting`, `paper`, `orientation`, `overlap`, `extent`.
 */
ExportExtent configurePrinter(MapPrinter& printer, const Map& map, const Params& p, MapPrinterOptions::MapPrinterMode default_mode)
{
	if (p.has("scale"))
		printer.setScale(p.requirePositiveInt("scale"));
	if (p.has("dpi"))
		printer.setResolution(int(p.requirePositiveInt("dpi")));
	printer.setMode(parseMode(p, default_mode));
	// the design notes, "Print settings as a job"
	if (p.has("templates"))
		printer.setPrintTemplates(p.optionalBool("templates", false));
	if (p.has("grid"))
		printer.setPrintGrid(p.optionalBool("grid", false));
	if (p.has("overprinting"))
		printer.setSimulateOverprinting(p.optionalBool("overprinting", false));
	auto const paper = parsePaper(p);
	if (paper.has_paper && paper.id != QPageSize::Custom)
		printer.setPageSize(paper.id);
	else if (paper.has_paper)
		printer.setCustomPageSize(paper.size);
	if (paper.has_orientation)
		printer.setPageOrientation(paper.orientation);
	if (paper.has_overlap)
		printer.setOverlap(paper.h_overlap, paper.v_overlap);
	
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
			// Scale, page, margin and back sides: the design notes, "Extents in print and export"
			applyExtentToPrinter(printer, map, *named);
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
	static const char* const image_formats[] = { "png", "bmp", "tif", "tiff", "jpg", "jpeg", "gif" };  // foom: gif
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



// foom: for the steps of background_steps.cpp
QString crsSpecOf(const Step& step, const QString& text)
{
	Params p(step);
	return parseCrs(p, text).spec.trimmed();
}


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
		if (p.value("crt") != QJsonValue(false))  // foom: false, the default symbols
			p.optionalString("crt");
		parseImportLayers(p);
		if (p.has("clip"))
			p.numberArray(p.value("clip"), 4, QLatin1String("\"clip\""));
	}
	else if (name == QLatin1String("import-pdf"))  // foom: the design notes, "PDF import"
	{
		p.requireMap(map_open);
		p.checkKnown({ "path", "page", "scale", "centre", "text" });
		p.requireString("path");
		if (p.has("page"))
			p.requirePositiveInt("page");
		if (p.has("scale"))
			p.requirePositiveInt("scale");
		if (p.has("centre"))
			p.numberArray(p.value("centre"), 2, QLatin1String("\"centre\""));
		p.optionalBool("text", true);
#ifndef FOOM_WITH_POPPLER
		p.fail(QLatin1String("step \"import-pdf\" needs Poppler"));
#endif
	}
	else if (name == QLatin1String("new-map"))  // foom
	{
		p.checkKnown({ "box", "symbol_set", "scale", "crs", "declination", "rotate_to_magnetic", "osm", "osm_config", "endpoint", "crt", "tile_service", "elevation", "elevation_cell", "path" });
		if (!p.has("box"))
			p.fail(QLatin1String("needs \"box\": [south, west, north, east] in degrees"));
		p.numberArray(p.value("box"), 4, QLatin1String("\"box\""));
		p.requireString("symbol_set");
		p.requireString("path");
		if (p.has("scale"))
			p.requirePositiveInt("scale");
		p.optionalString("crs");
		if (p.has("declination"))
			p.requireNumber("declination");
		p.optionalBool("rotate_to_magnetic", true);
		for (auto parameter : { "osm", "osm_config", "endpoint", "crt", "tile_service" })
			p.optionalString(parameter);
		p.optionalString("elevation");  // foom
		if (p.has("elevation_cell"))
			p.requireNumber("elevation_cell");
#ifndef MAPPER_USE_GDAL
		p.fail(QLatin1String("step \"new-map\" needs GDAL"));
#endif
		map_open = true;
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
	else if (name == QLatin1String("check"))  // foom
	{
		p.requireMap(map_open);
		p.checkKnown({ "rules", "out", "gap", "between_contours", "fail_on" });
		parseCheckRules(p);
		if (p.has("out"))
			checkOutIsJson(p, p.requireString("out"));
		parseGap(p, nullptr);
		p.optionalBool("between_contours", false);
		parseFailOn(p);
	}
	else if (name == QLatin1String("course-statistics"))  // foom
	{
		p.requireMap(map_open);
		p.checkKnown({ "out" });
		p.requireString("out");
	}
	else if (name == QLatin1String("export-courses"))  // foom
	{
		p.requireMap(map_open);
		p.checkKnown({ "path", "format", "course", "per_course", "dpi", "tiles", "templates" });
		validateExportCourses(p);
	}
	else if (name == QLatin1String("hill-shade"))  // foom
	{
		p.requireMap(map_open);
		p.checkKnown({ "out", "method", "multi_directional", "azimuth", "elevation", "exaggeration", "cell_size", "resampling", "gradient", "attach" });
		parseHillShadeParams(p);
		p.optionalString("out");
		p.optionalBool("attach", false);
	}
	else if (name == QLatin1String("import-courses"))  // foom
	{
		p.requireMap(map_open);
		p.checkKnown({ "path", "format", "objects", "courses", "classes" });
		parseImportCourseKind(p, p.requireString("path"));
		for (auto parameter : { "objects", "courses", "classes" })
			p.optionalBool(parameter, true);
	}
	else if (name == QLatin1String("export"))
	{
		p.requireMap(map_open);
		p.checkKnown({ "format", "path", "extent", "scale", "dpi", "mode", "course",
		               "plates", "depth", "halftone", "compression", "templates", "grid", "overprinting", "paper", "orientation", "overlap",
		               "colours", "tiles", "world_file", "crt", "crt_only", "write_crt" });  // foom
		auto const path = p.requireString("path");
		p.unsupported("course", "the course data are written by the step \"export-courses\", the maps of the courses by \"export-course-maps\"");  // foom
		auto const target = resolveExportFormat(p, path, p.optionalString("format"));
		switch (target.kind)
		{
		case ExportTarget::Image:
			if (parseMode(p, MapPrinterOptions::Raster) != MapPrinterOptions::Raster)
				p.fail(QString::fromLatin1("\"%1\" images are raster output: \"mode\" must be \"raster\" or absent").arg(target.format));
			parsePlates(p, target.format);
			parseRaster(p, target.format);  // foom
			for (auto parameter : { "paper", "orientation", "overlap" })
			{
				if (p.has(parameter))
					p.fail(QString::fromLatin1("parameter \"%1\" applies to PDF export only").arg(QLatin1String(parameter)));
			}
			Q_FALLTHROUGH();
		case ExportTarget::Pdf:
			if (target.kind == ExportTarget::Pdf)
			{
				for (auto parameter : { "plates", "depth", "halftone", "compression", "colours", "tiles", "world_file" })  // foom: colours, tiles, world_file
				{
					if (p.has(parameter))
						p.fail(QString::fromLatin1("parameter \"%1\" applies to image export only").arg(QLatin1String(parameter)));
				}
				parsePaper(p);
			}
			for (auto parameter : { "templates", "grid", "overprinting" })
				p.optionalBool(parameter, false);
			if (p.has("scale"))
				p.requirePositiveInt("scale");
			if (p.has("dpi"))
				p.requirePositiveInt("dpi");
			parseMode(p, MapPrinterOptions::Vector);
			parseExtent(p);
			parseDxfCrt(p, target.format);  // foom: rejects the parameters of the table
			break;
		case ExportTarget::Registry:
			for (auto parameter : { "extent", "scale", "dpi", "mode", "plates", "depth", "halftone", "compression", "templates", "grid", "overprinting", "paper", "orientation", "overlap",
			                        "colours", "tiles", "world_file" })  // foom: colours, tiles, world_file
			{
				if (p.has(parameter))
					p.fail(QString::fromLatin1("parameter \"%1\" applies to image and PDF export only, not to \"%2\"").arg(QLatin1String(parameter), target.format));
			}
			parseDxfCrt(p, target.format);  // foom
			break;
		}
	}
	else if (name == QLatin1String("export-course-maps"))  // foom
	{
		p.requireMap(map_open);
		p.checkKnown({ "folder", "courses", "relay", "extent", "scale", "canvases", "routes", "format", "placing",
		               "content", "intensity", "line_width", "stretch_x", "stretch_y", "mirror", "offset_x", "offset_y" });
		p.requireString("folder");
		parseCourseNames(p);
		parseRelayMaps(p);
		CourseMapExportOptions options;
		options.placing = parseCourseMapPlacing(p);
		parseCourseMapPrint(p, options);
		if (p.has("scale"))
			p.requirePositiveInt("scale");
	}
	else if (name == QLatin1String("analyse-routes"))  // foom
	{
		p.requireMap(map_open);
		p.checkKnown({ "out", "alternatives", "cache" });
		p.optionalBool("cache", false);
		p.optionalString("out");
		if (p.has("alternatives"))
		{
			// the design notes, "Route analysis: alternative routes"
			auto const v = p.value("alternatives");
			if (!v.isDouble() || v.toDouble() != double(v.toInt()) || v.toInt() < 0 || v.toInt() > 5)
				p.fail(QLatin1String("parameter \"alternatives\" must be a whole number from 0 to 5"));
		}
	}
	else if (name == QLatin1String("export-routes-map"))  // foom
	{
		p.requireMap(map_open);
		p.checkKnown({ "out", "courses", "extent", "captions" });
		p.requireString("out");
		parseCourseNames(p);
		p.optionalString("extent");
		p.optionalBool("captions", true);
	}
	else if (name == QLatin1String("save-as"))
	{
		p.requireMap(map_open);
		p.checkKnown({ "path", "format", "version" });
		auto const path = p.requireString("path");
		auto const version = p.has("version") ? p.requirePositiveInt("version") : 0u;
		resolveSaveFormat(p, path, p.optionalString("format"), version);
	}
	else if (name == QLatin1String("field-copy-create"))
	{
		// the design notes, "Merge changes from file"
		p.requireMap(map_open);
		p.checkKnown({ "path", "extent", "templates_dpi", "review" });
		p.requireString("path");
		if (p.has("extent"))
		{
			auto const numbers = p.numberArray(p.value("extent"), 4, QLatin1String("\"extent\""));
			if (numbers[2] <= 0 || numbers[3] <= 0)
				p.fail(QLatin1String("the extent needs a positive width and height"));
		}
		if (p.has("templates_dpi"))
			p.requirePositiveInt("templates_dpi");
		p.optionalBool("review", false);
	}
	else if (name == QLatin1String("merge-from-file"))
	{
		p.requireMap(map_open);
		p.checkKnown({ "path", "conflicts", "definitions", "transform" });
		p.optionalBool("transform", false);
		p.requireString("path");
		auto const conflicts = p.optionalString("conflicts", QStringLiteral("fail"));
		if (!QStringList{ QStringLiteral("fail"), QStringLiteral("mine"), QStringLiteral("theirs"), QStringLiteral("both"), QStringLiteral("prefilled") }.contains(conflicts))
			p.fail(QString::fromLatin1("unknown conflicts \"%1\": expected \"fail\", \"mine\", \"theirs\", \"both\" or \"prefilled\"").arg(conflicts));
		auto const definitions = p.optionalString("definitions", QStringLiteral("mine"));
		if (definitions != QLatin1String("mine") && definitions != QLatin1String("theirs"))
			p.fail(QString::fromLatin1("unknown definitions \"%1\": expected \"mine\" or \"theirs\"").arg(definitions));
	}
	else if (name == QLatin1String("field-copy-close"))
	{
		p.requireMap(map_open);
		p.checkKnown({ "id" });
		p.optionalString("id");
	}
	else if (name == QLatin1String("backup"))  // foom
	{
		// the design notes, "Manual backup"
		p.requireMap(map_open);
		p.checkKnown({ "keep", "compress" });
		if (p.has("keep"))
			p.requirePositiveInt("keep");
		p.optionalBool("compress", false);
	}
	else if (name == QLatin1String("elevation-wizard"))  // foom: the design notes, "Wizard"
	{
		p.requireMap(map_open);
		ElevationWizardJob wizard_job;
		QString message;
		if (!elevationWizardJobFromJson(step.params, &wizard_job, &message))
			p.fail(message);
	}
	else if (isGeneratorStep(name))  // foom
	{
		validateGeneratorStep(step, map_open);
	}
	else if (isBackgroundStep(name))  // foom: the design notes, "Background maps and point clouds in the runner"
	{
		validateBackgroundStep(step, map_open);
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
	if (name == QLatin1String("import-pdf"))  // foom
		return importPdfStep(*map, job, step);
	if (name == QLatin1String("new-map"))
		return newMap(step);
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
	if (name == QLatin1String("export-course-maps"))  // foom
		return exportCourseMapsStep(step);
	if (name == QLatin1String("analyse-routes"))  // foom
		return analyseRoutesStep(step);
	if (name == QLatin1String("export-routes-map"))  // foom
		return exportRoutesMapStep(step);
	if (name == QLatin1String("check"))  // foom
		return checkStep(step);
	if (name == QLatin1String("course-statistics"))  // foom
		return courseStatisticsStep(step);
	if (name == QLatin1String("export-courses"))  // foom
		return exportCoursesStep(step);
	if (name == QLatin1String("import-courses"))  // foom
		return importCoursesStep(step);
	if (name == QLatin1String("hill-shade"))  // foom
		return hillShadeStep(step);
	if (name == QLatin1String("elevation-wizard"))  // foom
		return elevationWizardStep(step);
	if (isGeneratorStep(name))  // foom
		return runGeneratorStep(*map, step);
	if (isBackgroundStep(name))  // foom
		return runBackgroundStep(*map, map_path, job, step);
	if (name == QLatin1String("field-copy-create"))
		return fieldCopyCreate(step);
	if (name == QLatin1String("merge-from-file"))
		return mergeFromFile(step);
	if (name == QLatin1String("field-copy-close"))
		return fieldCopyClose(step);
	if (name == QLatin1String("backup"))  // foom
		return backupStep(step);
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
	map_path = path;
	StepResult result;
	result.details.insert(QLatin1String("path"), path);
	result.details.insert(QLatin1String("objects"), map->getNumObjects());
	result.details.insert(QLatin1String("scale"), int(map->getScaleDenominator()));
	addWarnings(result, importer->warnings());
	return result;
}


// foom: the design notes, "PDF import"
static StepResult importPdfStep(Map& map, const Job& job, const Step& step)
{
#ifdef FOOM_WITH_POPPLER
	Params p(step);
	auto const path = job.resolvePath(p.requireString("path"));
	PdfImportOptions options;
	if (p.has("page"))
		options.page = int(p.requirePositiveInt("page"));
	if (p.has("scale"))
		options.pdf_scale = int(p.requirePositiveInt("scale"));
	if (p.has("centre"))
	{
		auto const centre = p.numberArray(p.value("centre"), 2, QLatin1String("\"centre\""));
		options.centre = { centre[0], centre[1] };
	}
	options.text = p.optionalBool("text", true);
	auto const imported = importPdf(map, path, options);
	if (!imported.ok)
		return failure(imported.error);
	StepResult result;
	result.details.insert(QLatin1String("path"), path);
	result.details.insert(QLatin1String("objects"), imported.objects);
	result.details.insert(QLatin1String("areas"), imported.areas);
	result.details.insert(QLatin1String("lines"), imported.lines);
	result.details.insert(QLatin1String("texts"), imported.texts);
	result.details.insert(QLatin1String("images"), imported.images);
	result.details.insert(QLatin1String("shadings"), imported.shadings);
	result.details.insert(QLatin1String("patterns"), imported.patterns);
	for (auto const& warning : imported.warnings)
		appendWarning(result, warning);
	return result;
#else
	Q_UNUSED(map)
	Q_UNUSED(job)
	Q_UNUSED(step)
	return failure(QLatin1String("step \"import-pdf\" needs Poppler"));
#endif
}


#ifdef MAPPER_USE_GDAL
// foom: the import step with the import options
static StepResult importGeodataStep(Map& map, const Job& job, const Params& p, const QString& path, const QString& crt,
                                    const ImportRuleChoice* choice = nullptr)
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
	if (choice)  // foom: "Choosing the table from the dataset"
	{
		options.crt_file = choice->crt_file;
		options.layer_tag = choice->layer_tag;
	}
	
	auto const outcome = importGeodata(map, { path }, options);
	if (!outcome.ok)
		return failure(outcome.error);
	StepResult result;
	result.details.insert(QLatin1String("path"), path);
	result.details.insert(QLatin1String("objects"), outcome.objects);
	if (choice)
	{
		result.details.insert(QLatin1String("dataset"), choice->dataset);
		result.details.insert(QLatin1String("crt"), choice->crt_file);
	}
	addWarnings(result, std::vector<QString>(outcome.warnings.begin(), outcome.warnings.end()));
	return result;
}
#endif


StepResult StepRunner::import(const Step& step)
{
	Params p(step);
	auto const path = job.resolvePath(p.requireString("path"));
	auto const default_symbols = p.value("crt") == QJsonValue(false);  // foom
	auto const crt = default_symbols ? QString() : p.optionalString("crt");
	
#ifdef MAPPER_USE_GDAL
	// foom: without "crt", the shipped table chosen from the dataset
	QString no_table;
	if (!p.has("crt") && !default_symbols)
	{
		auto const* format = FileFormats.findFormatForFilename(path, &FileFormat::supportsFileImport);
		if (format && qstrcmp(format->id(), "OGR") == 0)
		{
			auto const choice = chooseImportRules({ path }, *map);
			if (!choice.crt_file.isEmpty())
				return importGeodataStep(*map, job, p, path, {}, &choice);
			if (!choice.dataset.isEmpty())
				no_table = QString::fromLatin1("No rule table for %1 and the symbol set \"%2\": default symbols")
				           .arg(choice.dataset, map->symbolSetId());
		}
	}
	if (p.has("layer") || p.has("clip"))
	{
		auto result = importGeodataStep(*map, job, p, path, crt);  // foom: the import options
		if (!no_table.isEmpty())
			appendWarning(result, no_table);
		return result;
	}
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
#ifdef MAPPER_USE_GDAL
	if (!no_table.isEmpty())
		appendWarning(result, no_table);  // foom
#endif
	
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


// foom: the design notes, "New map flow"
StepResult StepRunner::newMap(const Step& step)
{
#ifdef MAPPER_USE_GDAL
	Params p(step);
	NewMapParams params;
	auto const box = p.numberArray(p.value("box"), 4, QLatin1String("\"box\""));
	params.box = { box[0], box[1], box[2], box[3] };
	params.symbol_set_path = job.resolvePath(p.requireString("symbol_set"));
	params.map_path = job.resolvePath(p.requireString("path"));
	if (p.has("scale"))
		params.scale = p.requirePositiveInt("scale");
	params.crs_spec = p.optionalString("crs");
	if (p.has("declination"))
		params.declination = p.requireNumber("declination");
	params.rotate_to_magnetic = p.optionalBool("rotate_to_magnetic", true);
	auto const osm = p.optionalString("osm");
	if (!osm.isEmpty())
		params.osm_path = job.resolvePath(osm);
	auto const osm_config = p.optionalString("osm_config");
	if (!osm_config.isEmpty())
		params.osm_config = job.resolvePath(osm_config);
	params.endpoint = p.optionalString("endpoint");
	auto const crt = p.optionalString("crt");
	if (!crt.isEmpty())
		params.crt_file = job.resolvePath(crt);
	params.tile_service = p.optionalString("tile_service");
	auto const elevation = p.optionalString("elevation");
	if (!elevation.isEmpty())
		params.elevation_path = job.resolvePath(elevation);
	if (p.has("elevation_cell"))
		params.elevation_cell = p.requireNumber("elevation_cell");
	
	auto outcome = createNewMap(params);
	if (!outcome.ok)
		return failure(outcome.error);
	map = std::move(outcome.map);
	map_path.clear();
	StepResult result;
	result.details.insert(QLatin1String("path"), params.map_path);
	result.details.insert(QLatin1String("osm"), outcome.osm_path);
	result.details.insert(QLatin1String("objects"), outcome.objects);
	addWarnings(result, std::vector<QString>(outcome.warnings.begin(), outcome.warnings.end()));
	return result;
#else
	Q_UNUSED(step)
	return failure(QString::fromLatin1("step \"new-map\" needs GDAL"));
#endif
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
	// foom: the design notes, "DXF export through a cross-reference table"
	if (p.has("crt"))
		exporter->setOption(QStringLiteral("foom dxf crt"), job.resolvePath(p.requireString("crt")));
	if (p.has("crt_only"))
		exporter->setOption(QStringLiteral("foom dxf crt only"), p.optionalBool("crt_only", false));
	if (p.has("write_crt"))
		exporter->setOption(QStringLiteral("foom dxf write crt"), job.resolvePath(p.requireString("write_crt")));
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
	loadTemplateFilesFor(*map, printer, p);  // foom
	configurePrinter(printer, *map, p, MapPrinterOptions::Raster);
	
	// As PrintWidget::exportToImage()
	auto const pixel_per_mm = printer.getOptions().resolution / 25.4;
	auto const width = qRound(printer.getPrintAreaPaperSize().width() * pixel_per_mm);
	auto const height = qRound(printer.getPrintAreaPaperSize().height() * pixel_per_mm);
	if (width < 1 || height < 1)
		return failure(QLatin1String("the extent is empty"));
	
	// Plates instead of one RGB image. the design notes, "Print and raster output"
	auto const plates = parsePlates(p, format);
	if (plates.kind != PlateRequest::None)
	{
		auto const area = printer.getPrintArea();
		auto const resolution = printer.getOptions().resolution;
		QStringList files;
		QString error;
#ifdef MAPPER_USE_GDAL
		// foom: TIFF through GDAL, with the georeference. the design notes, "Raster plates and CMYK raster"
		auto const georeference = plateGeoreference(*map, area, resolution, printer.getScaleAdjustment());
		PlateWriter writer;
		if (format.startsWith(QLatin1String("tif"), Qt::CaseInsensitive))
		{
			auto const compression = plates.fax ? PlateCompression::Fax : PlateCompression::Lzw;
			writer = [resolution, compression, &georeference](const QString& file, const QImage& image, QString* error) {
				return writePlateTiff(file, image, resolution, compression, &georeference, error);
			};
		}
#else
		PlateWriter writer;
#endif
		if (plates.kind == PlateRequest::Spot)
		{
			if (!saveSpotPlates(*map, area, resolution, printer.getScaleAdjustment(), path, plates.form, &files, &error,
			                    printer.getOptions().show_templates, nullptr, writer))  // foom: templates on their plates
				return failure(error);
		}
		else
		{
#ifdef MAPPER_USE_GDAL
			if (!writeCmykTiff(path, renderProcessPlates(*map, area, resolution, printer.getScaleAdjustment()), resolution, &error, &georeference))
				return failure(error);
			files.append(path);
#else
			return failure(QLatin1String("CMYK plates need a build with GDAL"));
#endif
		}
		StepResult result;
		result.details.insert(QLatin1String("path"), path);
		result.details.insert(QLatin1String("format"), format);
		result.details.insert(QLatin1String("plates"), plates.kind == PlateRequest::Spot ? QLatin1String("spot") : QLatin1String("cmyk"));
		result.details.insert(QLatin1String("files"), QJsonArray::fromStringList(files));
		result.details.insert(QLatin1String("width"), width);
		result.details.insert(QLatin1String("height"), height);
		result.details.insert(QLatin1String("dpi"), resolution);
		addWarnings(result, hiddenWarnings(*map));
		return result;
	}
	
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

	// foom: colours, tiles, world file. the design notes, "Print and raster output", fourth change
	auto raster = parseRaster(p, format);
	std::vector<QString> raster_warnings;
	QStringList files;
	if (raster.isPlain(format))
	{
		if (!image.save(path))
			return failure(QString::fromLatin1("cannot write the %1 image \"%2\"").arg(format.toUpper(), path));
	}
	else
	{
		RasterSaveOptions options;
		options.colours = raster.colours;
		options.tiles = raster.tiles;
		if (raster.world_file)
		{
			if (map->getGeoreferencing().toProjectedCoords(MapCoordF{}).isNull())
				raster_warnings.push_back(QLatin1String("no world file: the map has no projected georeferencing"));
			else
				options.world_file = true;
			options.world = rasterWorldTransform(*map, printer.getPrintArea(), printer.getOptions().resolution, printer.getScaleAdjustment());
		}
#ifdef MAPPER_USE_GDAL
		if (format == QLatin1String("gif"))
			options.writer = &writeGif;
#endif
		QString error;
		if (!saveRasterImage(image, *map, path, options, &files, &error))
			return failure(error);
	}

	StepResult result;
	result.details.insert(QLatin1String("path"), path);
	result.details.insert(QLatin1String("format"), format);
	result.details.insert(QLatin1String("colours"), raster.colours == RasterColours::Rgb ? QStringLiteral("rgb") : raster.colours == RasterColours::Grey ? QStringLiteral("grey") : QStringLiteral("256"));
	if (raster.tiles.isOn())
		result.details.insert(QLatin1String("files"), QJsonArray::fromStringList(files));
	result.details.insert(QLatin1String("width"), width);
	result.details.insert(QLatin1String("height"), height);
	result.details.insert(QLatin1String("dpi"), printer.getOptions().resolution);
	addWarnings(result, hiddenWarnings(*map));
	for (auto const& warning : raster_warnings)  // foom
		appendWarning(result, warning);
	return result;
}


StepResult StepRunner::exportPdf(const Step& step, const QString& path)
{
	Params p(step);
	MapPrinter printer(*map, nullptr);
	printer.setTarget(MapPrinter::pdfTarget());
	loadTemplateFilesFor(*map, printer, p);  // foom
	auto const extent = configurePrinter(printer, *map, p, MapPrinterOptions::Vector);
	if (extent.kind != ExportExtent::PrintArea && extent.kind != ExportExtent::Named && !p.has("paper"))
	{
		// An explicit extent goes onto a single page of its size. A named extent brings its own page,
		// and with `paper` the extent is laid on pages of that paper.
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
	result.details.insert(QLatin1String("pages"), int(printer.horizontalPagePositions().size() * printer.verticalPagePositions().size() + printer.backSides().size()));
	addWarnings(result, hiddenWarnings(*map));
	return result;
}


// foom: the design notes, "The maps of the courses as PDF files"
StepResult StepRunner::exportCourseMapsStep(const Step& step)
{
	Params p(step);
	CourseMapExportOptions options;
	options.folder = job.resolvePath(p.requireString("folder"));
	options.base_name = map_path.isEmpty() ? QStringLiteral("map") : QFileInfo(map_path).completeBaseName();
	options.courses = parseCourseNames(p);
	options.relay_maps = parseRelayMaps(p);
	options.extent = p.optionalString("extent");
	options.scale = p.has("scale") ? p.requirePositiveInt("scale") : 0u;
	options.use_canvases = p.optionalBool("canvases", false);
	options.with_routes = p.optionalBool("routes", false);
	options.placing = parseCourseMapPlacing(p);
	parseCourseMapPrint(p, options);
	// foom: the design notes, "Course maps as map files"
	auto const file_format = p.optionalString("format").toLower();
	if (!file_format.isEmpty() && file_format != QLatin1String("pdf"))
	{
		auto const* format = findFormat(file_format, &FileFormat::supportsFileSaveAs);
		if (!format)
			p.fail(QString::fromLatin1("unknown map file format \"%1\"").arg(file_format));
		// "ocd" is OCD 2018.
		options.map_format = file_format == QLatin1String("ocd") ? QStringLiteral("OCD2018") : QString::fromLatin1(format->id());
	}
	// The maps of the courses have their background maps.
	loadTemplateFiles(*map);
	auto const report = exportCourseMaps(*map, options);
	if (!report.ok)
		return failure(report.error);
	
	StepResult result;
	result.details.insert(QLatin1String("folder"), options.folder);
	result.details.insert(QLatin1String("files"), report.files.size());
	if (!report.skipped.isEmpty())
		result.details.insert(QLatin1String("skipped"), QJsonArray::fromStringList(report.skipped));
	return result;
}


// foom: the design notes, "Route analysis: all courses, and the routes in the file"
StepResult StepRunner::analyseRoutesStep(const Step& step)
{
	Params p(step);
	auto const out = p.optionalString("out");
	auto const alternatives = p.has("alternatives") ? p.value("alternatives").toInt() : 0;
	// the design notes, "Route analysis: the cache of the rasters"
	auto const cache = p.optionalBool("cache", false);
	// The terrain is that of the map and of its background maps.
	loadTemplateFiles(*map);
	StoredRoutes store;
	RouteTerrainSource source = RouteTerrainSource::Made;
	auto const all = cache ? analyseAllRoutesOfMap(*map, store, alternatives, routeTerrainCachePath(map_path), &source)
	                       : analyseAllRoutesOfMap(*map, store, alternatives);
	auto missing = 0;
	for (auto const& route : store.routes)
		missing += route.found ? 0 : 1;
	if (!out.isEmpty())
	{
		auto const path = job.resolvePath(out);
		// With a byte order mark, as the format has it.
		QByteArray bytes("\xef\xbb\xbf");
		bytes += allRoutesText(map->courseData(), store, all).toUtf8();
		QFile file(path);
		if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) || file.write(bytes) != bytes.size())
			return failure(QString::fromLatin1("cannot write \"%1\"").arg(path));
	}
	// The routes are kept in the map: a save-as after this step writes them.
	auto data = map->courseData();
	data.routes = store;
	map->setCourseData(std::move(data));
	
	StepResult result;
	result.details.insert(QLatin1String("tables"), int(all.tables.size()));
	result.details.insert(QLatin1String("routes"), int(store.routes.size()));
	auto const outside = routeLegsOutside(all);
	result.details.insert(QLatin1String("missing"), missing - outside);
	if (outside > 0)
		result.details.insert(QLatin1String("outside"), outside);
	if (alternatives > 0)
		result.details.insert(QLatin1String("alternatives"), all.alternatives);
	if (cache)
		result.details.insert(QLatin1String("terrain"), source == RouteTerrainSource::Memory ? QStringLiteral("memory")
		                                                 : source == RouteTerrainSource::File ? QStringLiteral("file") : QStringLiteral("made"));
	if (!all.skipped.isEmpty())
		result.details.insert(QLatin1String("skipped"), QJsonArray::fromStringList(all.skipped));
	return result;
}


// foom: the design notes, "Route analysis: a print of the routes"
StepResult StepRunner::exportRoutesMapStep(const Step& step)
{
	Params p(step);
	RouteMapOptions options;
	options.path = job.resolvePath(p.requireString("out"));
	options.courses = parseCourseNames(p);
	options.extent = p.optionalString("extent");
	options.captions = p.optionalBool("captions", true);
	// The routes are printed over the background maps.
	loadTemplateFiles(*map);
	auto const report = exportRoutesMap(*map, options);
	if (!report.ok)
		return failure(report.error);
	StepResult result;
	result.details.insert(QLatin1String("path"), options.path);
	result.details.insert(QLatin1String("pages"), report.pages);
	return result;
}


// foom: the design notes, "Headless runner", the step `check`.
StepResult StepRunner::checkStep(const Step& step)
{
	Params p(step);
	auto const rules = parseCheckRules(p);
	auto const fail_on = parseFailOn(p);
	
	std::vector<Issue> issues;
	auto const keep = [&rules, &issues](std::vector<Issue> found, int checker) {
		for (auto& issue : found)
		{
			if (rules.keeps(checker, issue.rule))
				issues.push_back(std::move(issue));
		}
	};
	if (rules.run[MapChecker])
	{
		// Without a view, nothing has tried the files of the templates yet.
		loadTemplateFiles(*map);
		keep(checkMap(*map), MapChecker);
	}
	if (rules.run[LegibilityChecker])
	{
		LegibilityParams params;
		params.gap_mm = parseGap(p, map.get());
		params.between_contours = p.optionalBool("between_contours", false);
		keep(checkLegibility(*map, params), LegibilityChecker);
	}
	if (rules.run[CourseChecker])
		keep(checkCourses(*map, map->courseData()), CourseChecker);
	
	StepResult result;
	auto const out = p.optionalString("out");
	if (!out.isEmpty())
	{
		auto const path = job.resolvePath(out);
		QByteArray bytes;
		if (checkOutIsJson(p, out))
		{
			bytes = issuesJson(issues);
		}
		else
		{
			IssueModel model;
			model.setIssues(issues);
			bytes = model.toCsv();
		}
		if (!writeBytes(path, bytes))
			return failure(QString::fromLatin1("cannot write \"%1\"").arg(path));
		result.details.insert(QLatin1String("path"), path);
	}
	
	int count[3] = {};
	QJsonObject by_rule;
	auto failing = 0;
	for (auto const& issue : issues)
	{
		++count[issue.severity];
		auto const rule = QString::fromLatin1(issue.rule);
		by_rule.insert(rule, by_rule.value(rule).toInt() + 1);
		if (fail_on >= 0 && int(issue.severity) >= fail_on)
			++failing;
	}
	result.details.insert(QLatin1String("issues"), int(issues.size()));
	result.details.insert(QLatin1String("errors"), count[Issue::Error]);
	result.details.insert(QLatin1String("warnings"), count[Issue::Warning]);
	result.details.insert(QLatin1String("infos"), count[Issue::Info]);
	result.details.insert(QLatin1String("rules"), by_rule);
	if (failing > 0)
	{
		result.ok = false;
		result.message = QString::fromLatin1("%1 issue(s) of severity %2 or higher").arg(QString::number(failing), severityName(Issue::Severity(fail_on)));
	}
	return result;
}


// foom: the design notes, "Statistics and the consistency check"
// foom: the design notes, "Bicubic resampling for hill shading"
StepResult StepRunner::hillShadeStep(const Step& step)
{
	Params p(step);
	auto const params = parseHillShadeParams(p);
	auto& set = map->elevationSet();
	auto const href = set.href(ElevationSet::Terrain);
	if (href.isEmpty())
		return failure(QLatin1String("the map has no terrain grid"));
	auto const* model = set.model(ElevationSet::Terrain, map->getGeoreferencing().getProjectedCRSSpec());
	if (!model)
		return failure(QString::fromLatin1("cannot open the terrain grid \"%1\": %2").arg(href, ElevationModel::lastError()));
	auto const path = p.has("out") ? job.resolvePath(p.requireString("out")) : productPath(map_path, href, QStringLiteral("hillshade"));
	auto const product = hillShade(*model, params);
	if (!product.error.isEmpty())
		return failure(product.error);
	QString error;
	if (!writeGeoTiff(product, path, &error))
		return failure(QString::fromLatin1("cannot write \"%1\": %2").arg(path, error));
	if (p.optionalBool("attach", false) && !attachProductTemplate(*map, path, &error))
		return failure(QString::fromLatin1("cannot attach \"%1\": %2").arg(path, error));

	StepResult result;
	result.details.insert(QLatin1String("path"), path);
	result.details.insert(QLatin1String("width"), product.image.width());
	result.details.insert(QLatin1String("height"), product.image.height());
	return result;
}


StepResult StepRunner::courseStatisticsStep(const Step& step)
{
	Params p(step);
	auto const path = job.resolvePath(p.requireString("out"));
	auto const statistics = courseStatistics(*map, map->courseData());
	if (!writeBytes(path, courseStatisticsText(statistics).toUtf8()))
		return failure(QString::fromLatin1("cannot write \"%1\"").arg(path));
	
	StepResult result;
	result.details.insert(QLatin1String("path"), path);
	result.details.insert(QLatin1String("courses"), statistics.course_count);
	result.details.insert(QLatin1String("controls"), statistics.control_count);
	result.details.insert(QLatin1String("classes"), statistics.class_count);
	return result;
}


// foom: the design notes, "Wizard"
StepResult StepRunner::elevationWizardStep(const Step& step)
{
	ElevationWizardJob wizard_job;
	QString message;
	if (!elevationWizardJobFromJson(step.params, &wizard_job, &message))
		return failure(message);  // validate() prevents this
	for (auto& file : wizard_job.files)
		file = job.resolvePath(file);
	auto const outcome = runElevationWizard(*map, map_path, wizard_job);
	if (!outcome.ok)
		return failure(outcome.error);
	
	StepResult result;
	result.details.insert(QLatin1String("contours"), outcome.contours);
	result.details.insert(QLatin1String("features"), outcome.features);
	result.details.insert(QLatin1String("grids"), QJsonArray::fromStringList(outcome.grids));
	result.details.insert(QLatin1String("files"), QJsonArray::fromStringList(outcome.files));
	return result;
}


// foom: the design notes, "Interchange: IOF XML, GPX and KML"
StepResult StepRunner::exportCoursesStep(const Step& step)
{
	Params p(step);
	auto const path = job.resolvePath(p.requireString("path"));
	auto const kind = parseCourseFileKind(p, path);
	auto const course = p.optionalString("course");
	
	auto const kmz = parseCourseKmzOptions(p, kind);

	CourseFilesReport report;
	if (course.isEmpty())
	{
		report = writeCourseFiles(*map, path, kind, p.optionalBool("per_course", false), kmz);
	}
	else if (kind == CourseFileKind::Kmz)
	{
		auto options = kmz;
		options.course = course;
		auto const written = writeCoursesKmz(*map, path, options);
		if (!written.ok)
			return failure(written.error);
		report.ok = true;
		report.files.push_back(path);
		report.skipped_courses = written.skipped_courses;
	}
	else
	{
		// One course: validate() lets this through for GPX and KML only.
		QSaveFile file(path);
		if (!file.open(QIODevice::WriteOnly))
			return failure(QString::fromLatin1("cannot write \"%1\"").arg(path));
		auto const written = kind == CourseFileKind::Gpx ? writeCoursesGpx(*map, file, course) : writeCoursesKml(*map, file, course);
		if (!written.ok)
		{
			file.cancelWriting();
			return failure(written.error);
		}
		if (!file.commit())
			return failure(QString::fromLatin1("cannot write \"%1\"").arg(path));
		report.ok = true;
		report.files.push_back(path);
		report.skipped_courses = written.skipped_courses;
	}
	if (!report.ok)
		return failure(report.error);
	
	StepResult result;
	result.details.insert(QLatin1String("files"), jsonList(report.files));
	if (!report.skipped_courses.isEmpty())
		result.details.insert(QLatin1String("skipped"), jsonList(report.skipped_courses));
	return result;
}


// foom: the design notes, "Interchange: IOF XML, GPX and KML"
StepResult StepRunner::importCoursesStep(const Step& step)
{
	Params p(step);
	auto const path = job.resolvePath(p.requireString("path"));
	auto const kind = parseImportCourseKind(p, path);
	if (kind != CourseFileKind::IofXml)
	{
		// foom: the design notes, "Interchange: GPX and KML in, and a KMZ with the map"
		CourseGeodataImportOptions options;
		options.objects = p.optionalBool("objects", true);
		options.courses = p.optionalBool("courses", true);
		CourseGeodataImportReport report;
		if (kind == CourseFileKind::Kmz)
		{
			report = importCourseFile(*map, path, options);
		}
		else
		{
			QFile file(path);
			if (!file.open(QIODevice::ReadOnly))
				return failure(QString::fromLatin1("cannot read \"%1\"").arg(path));
			report = kind == CourseFileKind::Gpx ? importCoursesGpx(*map, file, options) : importCoursesKml(*map, file, options);
		}
		if (!report.ok)
			return failure(report.error);
		
		StepResult result;
		result.details.insert(QLatin1String("path"), path);
		result.details.insert(QLatin1String("objects_added"), report.objects_added);
		result.details.insert(QLatin1String("objects_found"), report.objects_found);
		result.details.insert(QLatin1String("courses_added"), report.courses_added);
		result.details.insert(QLatin1String("relay_variants"), report.relay_variants);
		for (auto const& list : { std::make_pair("objects_not_made", &report.objects_not_made),
		                          std::make_pair("courses_existing", &report.courses_existing),
		                          std::make_pair("tracks_not_courses", &report.tracks_not_courses) })
		{
			if (!list.second->isEmpty())
				result.details.insert(QLatin1String(list.first), jsonList(*list.second));
		}
		return result;
	}
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly))
		return failure(QString::fromLatin1("cannot read \"%1\"").arg(path));
	IofImportOptions options;
	options.objects = p.optionalBool("objects", true);
	options.courses = p.optionalBool("courses", true);
	options.classes = p.optionalBool("classes", true);
	auto const report = importIofCourseData(*map, file, options);
	if (!report.ok)
		return failure(report.error);
	
	StepResult result;
	result.details.insert(QLatin1String("path"), path);
	result.details.insert(QLatin1String("objects_added"), report.objects_added);
	result.details.insert(QLatin1String("objects_found"), report.objects_found);
	result.details.insert(QLatin1String("courses_added"), report.courses_added);
	result.details.insert(QLatin1String("classes_added"), report.classes_added);
	result.details.insert(QLatin1String("relay_variants"), report.relay_variants);
	result.details.insert(QLatin1String("taped_routes"), report.taped_routes);
	auto const insertList = [&result](const char* name, const QStringList& list) {
		if (!list.isEmpty())
			result.details.insert(QLatin1String(name), jsonList(list));
	};
	insertList("objects_not_made", report.objects_not_made);
	insertList("courses_existing", report.courses_existing);
	insertList("codes_missing", report.codes_missing);
	insertList("classes_skipped", report.classes_skipped);
	return result;
}


StepResult StepRunner::saveAs(const Step& step)
{
	Params p(step);
	auto const path = job.resolvePath(p.requireString("path"));
	auto const version = p.has("version") ? p.requirePositiveInt("version") : 0u;
	auto const* format = resolveSaveFormat(p, path, p.optionalString("format"), version);
	
	purgePrivateSymbols(*map);
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


// ### Field copies. the design notes, "Merge changes from file" ###

namespace {

/** Saves the working map to the file it was opened from. Returns a message on failure. */
QString saveWorkingMap(Map& map, const QString& path, bool check_only = false)
{
	if (path.isEmpty())
		return QStringLiteral("needs a map opened from a file");
	auto const* format = findFormat(QFileInfo(path).suffix().toLower(), &FileFormat::supportsFileSaveAs);
	if (!format || qstrcmp(format->id(), "XML") != 0)
		return QString::fromLatin1("the working map must be a .foom, .omap or .xmap file, not \"%1\"").arg(path);
	if (check_only)
		return {};
	purgePrivateSymbols(map);
	auto exporter = format->makeExporter(path, &map, nullptr);
	if (!exporter || !exporter->doExport())
		return QString::fromLatin1("cannot write \"%1\"").arg(path);
	return {};
}

}  // namespace

StepResult StepRunner::fieldCopyCreate(const Step& step)
{
	Params p(step);
	auto const path = job.resolvePath(p.requireString("path"));
	auto problem = saveWorkingMap(*map, map_path, true);
	if (!problem.isEmpty())
		return failure(problem);
	
	FieldCopyOptions options;
	if (p.has("extent"))
	{
		auto const numbers = p.numberArray(p.value("extent"), 4, QLatin1String("\"extent\""));
		options.extent = QRectF(numbers[0], numbers[1], numbers[2], numbers[3]);
	}
	if (p.has("templates_dpi"))
		options.template_dpi = int(p.requirePositiveInt("templates_dpi"));
	options.review = p.optionalBool("review", false);
	QString error;
	auto const id = createFieldCopy(*map, map_path, path, options, &error);
	if (id.isEmpty())
		return failure(error);
	problem = saveWorkingMap(*map, map_path);
	if (!problem.isEmpty())
		return failure(problem);
	
	StepResult result;
	result.details.insert(QLatin1String("path"), path);
	result.details.insert(QLatin1String("id"), id);
	result.details.insert(QLatin1String("base"), baseFilePath(map_path, id));
	return result;
}

StepResult StepRunner::mergeFromFile(const Step& step)
{
	Params p(step);
	auto const path = job.resolvePath(p.requireString("path"));
	auto const conflicts = p.optionalString("conflicts", QStringLiteral("fail"));
	auto problem = saveWorkingMap(*map, map_path, true);
	if (!problem.isEmpty())
		return failure(problem);
	
	QString error;
	MergeInput input;
	if (!loadForMerge(*map, map_path, path, input, &error, p.optionalBool("transform", false)))
		return failure(input.other_frame ? error + QLatin1String(" Set \"transform\" to true to do so.") : error);
	auto plan = compareMaps(input.base.get(), *map, *input.theirs);
	auto const conflict_count = plan.count(MergeRow::Conflict);
	if (p.optionalString("definitions", QStringLiteral("mine")) == QLatin1String("theirs"))
		plan.resolveDefinitions(MergeSymbolRow::KeepTheirs);
	if (conflicts == QLatin1String("mine"))
		plan.resolveConflicts(MergeRow::KeepMine);
	else if (conflicts == QLatin1String("theirs"))
		plan.resolveConflicts(MergeRow::KeepTheirs);
	else if (conflicts == QLatin1String("both"))
		plan.resolveConflicts(MergeRow::KeepBoth);
	else if (conflicts == QLatin1String("prefilled"))
		plan.acceptPrefilled();
	else if (plan.unresolved() > 0)
		return failure(QString::fromLatin1("%1 conflict(s)%2: set \"conflicts\" to \"mine\", \"theirs\", \"both\" or \"prefilled\"")
		               .arg(plan.unresolved()).arg(plan.two_way ? QLatin1String(" (the base file is missing, so every difference is one)") : QLatin1String("")));
	
	MergeResult merged;
	if (!applyMerge(*map, *input.theirs, plan, &merged, &error))
		return failure(error);
	// The working map first: a base which is ahead of an unsaved merge would hide the merge.
	problem = saveWorkingMap(*map, map_path);
	if (!problem.isEmpty())
		return failure(problem);
	if (!refreshBase(map_path, path, input.copy, &error))
		return failure(error);
	
	StepResult result;
	result.details.insert(QLatin1String("path"), path);
	result.details.insert(QLatin1String("id"), input.copy.id);
	result.details.insert(QLatin1String("two_way"), plan.two_way);
	result.details.insert(QLatin1String("transformed"), input.transformed);
	result.details.insert(QLatin1String("incoming"), plan.count(MergeRow::Incoming));
	result.details.insert(QLatin1String("outgoing"), plan.count(MergeRow::Outgoing));
	result.details.insert(QLatin1String("conflicts"), conflict_count);
	result.details.insert(QLatin1String("added"), merged.added);
	result.details.insert(QLatin1String("replaced"), merged.replaced);
	result.details.insert(QLatin1String("deleted"), merged.deleted);
	result.details.insert(QLatin1String("symbols"), merged.symbols);
	result.details.insert(QLatin1String("parts"), merged.parts);
	result.details.insert(QLatin1String("colors"), merged.colors);
	result.details.insert(QLatin1String("definitions"), merged.definitions);
	std::vector<QString> warnings;
	for (auto const& symbol : plan.symbols)
	{
		if (symbol.status == MergeSymbolRow::Differs && symbol.action == MergeSymbolRow::KeepMine)
			warnings.push_back(QString::fromLatin1("symbol %1 %2 differs in the copy; the map's definition is kept").arg(symbol.number, symbol.name));
	}
	for (auto const& color : plan.colors)
	{
		if (color.status == MergeColorRow::Differs && color.action == MergeSymbolRow::KeepMine)
			warnings.push_back(QString::fromLatin1("colour %1 differs in the copy; the map's definition is kept").arg(color.name));
	}
	addWarnings(result, warnings);
	return result;
}

StepResult StepRunner::fieldCopyClose(const Step& step)
{
	Params p(step);
	auto problem = saveWorkingMap(*map, map_path, true);
	if (!problem.isEmpty())
		return failure(problem);
	QString error;
	if (!closeFieldCopy(*map, map_path, p.optionalString("id"), &error))
		return failure(error);
	problem = saveWorkingMap(*map, map_path);
	if (!problem.isEmpty())
		return failure(problem);
	StepResult result;
	result.details.insert(QLatin1String("open_copies"), int(map->syncCopies().size()));
	return result;
}


// foom: the design notes, "Manual backup"
StepResult StepRunner::backupStep(const Step& step)
{
	Params p(step);
	if (map_path.isEmpty())
		return failure(QStringLiteral("needs a map opened from a file"));
	OpenBackupOptions options;
	if (p.has("keep"))
		options.keep = int(p.requirePositiveInt("keep"));
	options.compress = p.optionalBool("compress", false);
	auto const* format = findFormat(QFileInfo(map_path).suffix().toLower(), &FileFormat::supportsFileSave);
	QString error;
	std::vector<QString> warnings;
	auto const backup = makeMapBackup(*map, nullptr, map_path, format, options, QDateTime::currentDateTime(), &error, &warnings);
	if (backup.isEmpty())
		return failure(error);
	StepResult result;
	result.details.insert(QLatin1String("path"), backup);
	addWarnings(result, warnings);
	return result;
}

}  // namespace Cli

}  // namespace OpenOrienteering
