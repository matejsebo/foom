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


#include "background_steps.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <utility>
#include <vector>

#include <QtGlobal>
#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QLatin1Char>
#include <QLatin1String>
#include <QRectF>
#include <QStringList>

#include "job.h"
#include "core/extents.h"
#include "core/georeferencing.h"
#include "core/map.h"
#include "core/map_coord.h"
#include "core/symbols/symbol.h"
#include "course/course_canvas.h"
#include "course/course_data.h"
#include "elevation/elevation_commands.h"
#include "elevation/elevation_wizard.h"
#include "elevation/point_cloud.h"
#include "sensors/gnss_file.h"
#include "templates/template.h"
#include "templates/template_image.h"
#include "util/transformation.h"


namespace OpenOrienteering {

namespace Cli {

namespace {

const QLatin1String add_template_step("add-template");
const QLatin1String remove_template_step("remove-template");
const QLatin1String georeference_template_step("georeference-template");
const QLatin1String point_cloud_step("point-cloud");
const QLatin1String load_canvases_step("load-canvases");
const QLatin1String import_gnss_step("import-gnss");

constexpr double pi = 3.14159265358979323846;


/** The parameters of a step, checked. */
class Args
{
public:
	explicit Args(const Step& step) : step(step) {}

	Q_NORETURN void fail(const QString& what) const
	{
		throw JobError(step.prefix() + what);
	}

	bool has(const char* name) const { return step.params.contains(QLatin1String(name)); }
	QJsonValue value(const char* name) const { return step.params.value(QLatin1String(name)); }

	void known(std::initializer_list<const char*> names) const
	{
		for (auto const& key : step.params.keys())
		{
			if (std::none_of(names.begin(), names.end(), [&key](const char* name) { return key == QLatin1String(name); }))
				fail(QString::fromLatin1("unknown parameter \"%1\"").arg(key));
		}
	}

	QString string(const char* name) const
	{
		auto const v = value(name);
		if (!v.isString() || v.toString().isEmpty())
			fail(QString::fromLatin1("parameter \"%1\" must be a non-empty string").arg(QLatin1String(name)));
		return v.toString();
	}

	bool boolean(const char* name, bool default_value) const
	{
		if (!has(name))
			return default_value;
		if (!value(name).isBool())
			fail(QString::fromLatin1("parameter \"%1\" must be true or false").arg(QLatin1String(name)));
		return value(name).toBool();
	}

	double number(const char* name) const
	{
		if (!value(name).isDouble())
			fail(QString::fromLatin1("parameter \"%1\" must be a number").arg(QLatin1String(name)));
		return value(name).toDouble();
	}

	double positive(const char* name) const
	{
		auto const v = number(name);
		if (!(v > 0))
			fail(QString::fromLatin1("parameter \"%1\" must be a positive number").arg(QLatin1String(name)));
		return v;
	}

	std::vector<double> numbers(const QJsonValue& v, int count, const QString& what) const
	{
		auto const array = v.toArray();
		if (!v.isArray() || array.size() != count)
			fail(QString::fromLatin1("%1 must be an array of %2 numbers").arg(what, QString::number(count)));
		std::vector<double> result;
		for (auto const& item : array)
		{
			if (!item.isDouble())
				fail(QString::fromLatin1("%1 must be an array of %2 numbers").arg(what, QString::number(count)));
			result.push_back(item.toDouble());
		}
		return result;
	}

	const Step& step;
};


StepResult failure(const QString& message)
{
	StepResult result;
	result.ok = false;
	result.message = message;
	return result;
}


// ### Templates ###

/// A template named by its file name, its path or its place from 1 at the top.
void checkSelector(const Args& a, const QJsonValue& v)
{
	auto const ok = (v.isString() && !v.toString().isEmpty())
	                || (v.isDouble() && v.toDouble() >= 1 && v.toDouble() == std::floor(v.toDouble()));
	if (!ok)
		a.fail(QLatin1String("\"template\" must be a file name or a number from 1"));
}

/// The index of the template in the map, or -1.
int findTemplate(const Map& map, const Job& job, const QJsonValue& v)
{
	auto const count = map.getNumTemplates();
	if (v.isDouble())
	{
		auto const number = int(v.toDouble());
		return number >= 1 && number <= count ? count - number : -1;
	}
	auto const name = v.toString();
	auto const path = job.resolvePath(name);
	for (int i = count - 1; i >= 0; --i)
	{
		auto const* temp = map.getTemplate(i);
		if (temp->getTemplateFilename().compare(name, Qt::CaseInsensitive) == 0
		    || QDir::cleanPath(temp->getTemplatePath()).compare(path, Qt::CaseInsensitive) == 0)
			return i;
	}
	return -1;
}

QString selectorText(const QJsonValue& v)
{
	return v.isDouble() ? QString::number(int(v.toDouble())) : v.toString();
}

void validatePlacement(const Args& a)
{
	if (a.has("center"))
		a.numbers(a.value("center"), 2, QLatin1String("\"center\""));
	if (a.has("meters_per_pixel"))
		a.positive("meters_per_pixel");
	if (a.has("dpi"))
		a.positive("dpi");
	if (a.has("meters_per_pixel") && a.has("dpi"))
		a.fail(QLatin1String("give \"meters_per_pixel\" or \"dpi\", not both"));
	if (a.has("rotation"))
		a.number("rotation");
}

bool hasPlacement(const Args& a)
{
	return a.has("center") || a.has("meters_per_pixel") || a.has("dpi") || a.has("rotation");
}

/// Places a template which is not georeferenced. Returns a message on a fault.
QString place(Template& temp, const Map& map, const Args& a)
{
	if (auto* image = qobject_cast<TemplateImage*>(&temp))
	{
		auto mm_per_pixel = 0.0;
		if (a.has("meters_per_pixel"))
			mm_per_pixel = a.value("meters_per_pixel").toDouble() * 1000.0 / map.getScaleDenominator();
		else if (a.has("dpi"))
			mm_per_pixel = 25.4 / a.value("dpi").toDouble();
		if (mm_per_pixel > 0)
		{
			image->setTemplateScaleX(mm_per_pixel);
			image->setTemplateScaleY(mm_per_pixel);
			image->setTemplateShear(0.0);
		}
	}
	else if (a.has("meters_per_pixel") || a.has("dpi"))
	{
		return QLatin1String("\"meters_per_pixel\" and \"dpi\" are for images");
	}
	if (a.has("rotation"))
		temp.setTemplateRotation(a.value("rotation").toDouble() * pi / 180.0);
	if (a.has("center"))
	{
		auto const xy = a.numbers(a.value("center"), 2, QLatin1String("\"center\""));
		temp.setTemplatePosition(MapCoord(xy[0], xy[1]));
	}
	return {};
}

/// Georeferences or un-georeferences a loaded template. Returns a message on a fault.
QString setGeoreferenced(Template& temp, const Map& map, bool value, const QString& crs_spec)
{
	if (value && map.getGeoreferencing().getState() != Georeferencing::Geospatial)
		return QLatin1String("the map is not georeferenced: add a \"set-georeferencing\" step first");
	if (value == temp.isTemplateGeoreferenced() && crs_spec.isEmpty())
		return {};
	if (auto* image = qobject_cast<TemplateImage*>(&temp))
	{
		if (value)
			return image->setGeoreferencedByFile(crs_spec) ? QString{} : QLatin1String("the file brings no georeferencing");
		return image->trySetTemplateGeoreferenced(false, nullptr) ? QString{} : QLatin1String("the template cannot be made not georeferenced");
	}
	if (!temp.canChangeTemplateGeoreferenced() || !temp.trySetTemplateGeoreferenced(value, nullptr))
		return value ? QLatin1String("the template cannot be georeferenced") : QLatin1String("the template cannot be made not georeferenced");
	return {};
}


StepResult addTemplate(Map& map, const Job& job, const Args& a, const QString& crs_spec)
{
	auto const path = job.resolvePath(a.string("path"));
	if (!QFileInfo::exists(path))
		return failure(QString::fromLatin1("there is no file \"%1\"").arg(path));
	auto temp = Template::templateForPath(path, &map);
	if (!temp)
		return failure(QString::fromLatin1("\"%1\" cannot be opened as a background map").arg(path));
	auto const type = QString::fromLatin1(temp->getTemplateType());
	auto const map_georeferenced = map.getGeoreferencing().getState() == Georeferencing::Geospatial;
	if (type == QLatin1String("TemplateTrack"))
		return failure(QLatin1String("tracks are opened as background maps only in the editor; use \"import-gnss\" or a vector file"));
	if (type == QLatin1String("OgrTemplate") && !map_georeferenced)
		return failure(QLatin1String("the map is not georeferenced: add a \"set-georeferencing\" step before a vector background map"));
	if (a.boolean("georeferenced", false) && !map_georeferenced)
		return failure(QLatin1String("the map is not georeferenced: add a \"set-georeferencing\" step first"));

	if (!temp->preLoadSetup(nullptr) || !temp->loadTemplateFile())
		return failure(QString::fromLatin1("cannot load \"%1\": %2").arg(path, temp->errorString()));

	auto* image = qobject_cast<TemplateImage*>(temp.get());
	auto default_georeferenced = temp->isTemplateGeoreferenced();
	if (image)
		default_georeferenced = map_georeferenced && image->canChangeTemplateGeoreferenced();
	auto const georeferenced = a.boolean("georeferenced", default_georeferenced);
	auto message = setGeoreferenced(*temp, map, georeferenced, georeferenced ? crs_spec : QString{});
	if (message.isEmpty() && !georeferenced)
	{
		if (image && !a.has("meters_per_pixel") && !a.has("dpi"))
			message = QLatin1String("an image which is not georeferenced needs \"meters_per_pixel\" or \"dpi\"");
		else
			message = place(*temp, map, a);
	}
	if (!message.isEmpty())
		return failure(message);

	temp->setTemplateState(Template::Loaded);
	auto* added = temp.get();
	map.addTemplate(-1, std::move(temp));
	map.setTemplateAreaDirty(map.findTemplateIndex(added));
	map.setTemplatesDirty();

	StepResult result;
	result.details.insert(QLatin1String("path"), path);
	result.details.insert(QLatin1String("type"), type);
	result.details.insert(QLatin1String("georeferenced"), added->isTemplateGeoreferenced());
	result.details.insert(QLatin1String("templates"), map.getNumTemplates());
	return result;
}


StepResult removeTemplate(Map& map, const Job& job, const Args& a)
{
	auto const v = a.value("template");
	auto const selectors = v.isArray() ? v.toArray() : QJsonArray{ v };
	std::vector<Template*> found;
	for (auto const& selector : selectors)
	{
		auto const index = findTemplate(map, job, selector);
		if (index < 0)
			return failure(QString::fromLatin1("the map has no template \"%1\"").arg(selectorText(selector)));
		auto* temp = map.getTemplate(index);
		if (std::find(found.begin(), found.end(), temp) == found.end())
			found.push_back(temp);
	}
	for (auto* temp : found)
		map.deleteTemplate(map.findTemplateIndex(temp));
	map.setTemplatesDirty();

	StepResult result;
	result.details.insert(QLatin1String("removed"), int(found.size()));
	result.details.insert(QLatin1String("templates"), map.getNumTemplates());
	return result;
}


StepResult georeferenceTemplate(Map& map, const Job& job, const Args& a, const QString& crs_spec)
{
	auto const index = findTemplate(map, job, a.value("template"));
	if (index < 0)
		return failure(QString::fromLatin1("the map has no template \"%1\"").arg(selectorText(a.value("template"))));
	auto* temp = map.getTemplate(index);
	if (temp->getTemplateState() != Template::Loaded && !temp->loadTemplateFile())
		return failure(QString::fromLatin1("cannot load \"%1\": %2").arg(temp->getTemplatePath(), temp->errorString()));
	map.setTemplateAreaDirty(index);

	StepResult result;
	if (a.has("georeferenced"))
	{
		auto const value = a.value("georeferenced").toBool();
		auto const message = setGeoreferenced(*temp, map, value, value ? crs_spec : QString{});
		if (!message.isEmpty())
			return failure(message);
	}
	else if (a.has("pass_points"))
	{
		if (temp->isTemplateGeoreferenced())
		{
			auto const message = setGeoreferenced(*temp, map, false, {});
			if (!message.isEmpty())
				return failure(message);
		}
		temp->clearPassPoints();
		auto position = 0;
		for (auto const& item : a.value("pass_points").toArray())
		{
			auto const object = item.toObject();
			auto const from = a.numbers(object.value(QLatin1String("from")), 2, QLatin1String("\"from\""));
			auto const to = a.numbers(object.value(QLatin1String("to")), 2, QLatin1String("\"to\""));
			PassPoint point;
			point.src_coords = MapCoordF(from[0], from[1]);
			point.dest_coords = MapCoordF(to[0], to[1]);
			point.calculated_coords = point.dest_coords;
			point.error = -1;
			temp->addPassPoint(point, position++);
		}
		TemplateTransform transform;
		temp->getTransform(transform);
		if (!temp->getPassPointList().estimateSimilarityTransformation(&transform))
			return failure(QLatin1String("the pass points give no transformation"));
		auto error = 0.0;
		for (auto const& point : temp->getPassPointList())
			error = std::max(error, std::max(0.0, point.error));
		temp->setTransform(transform);
		temp->clearPassPoints();
		result.details.insert(QLatin1String("error"), error);
	}
	else
	{
		if (temp->isTemplateGeoreferenced())
		{
			auto const message = setGeoreferenced(*temp, map, false, {});
			if (!message.isEmpty())
				return failure(message);
		}
		auto const message = place(*temp, map, a);
		if (!message.isEmpty())
			return failure(message);
	}
	map.setTemplateAreaDirty(index);
	map.emitTemplateChanged(temp);
	map.setTemplatesDirty();
	result.details.insert(QLatin1String("georeferenced"), temp->isTemplateGeoreferenced());
	return result;
}


// ### Point clouds ###

const char* const cloud_products[] = { "vegetation-height", "vegetation-map", "feature-map", "intensity", "classification" };

/// The parameters of the elevation wizard's step which give the same.
QJsonObject wizardParams(const Args& a)
{
	QJsonObject wizard;
	wizard.insert(QLatin1String("source"), QLatin1String("point-cloud"));
	for (auto const* name : { "files", "cell", "dtm", "dsm", "vegetation_height", "vegetation_map", "feature_map", "attach" })
	{
		if (a.has(name))
			wizard.insert(QLatin1String(name), a.value(name));
	}
	QJsonArray products;
	if (a.has("products"))
	{
		auto const v = a.value("products");
		if (!v.isArray())
			a.fail(QLatin1String("\"products\" must be a list"));
		for (auto const& product : v.toArray())
		{
			auto const name = product.toString();
			if (std::none_of(std::begin(cloud_products), std::end(cloud_products), [&name](const char* known) { return name == QLatin1String(known); }))
				a.fail(QString::fromLatin1("unknown product \"%1\": expected vegetation-height, vegetation-map, feature-map, intensity or classification").arg(name));
			products.append(name);
		}
	}
	else
	{
		products = QJsonArray{ QStringLiteral("vegetation-height"), QStringLiteral("vegetation-map"), QStringLiteral("intensity") };
	}
	wizard.insert(QLatin1String("products"), products);
	return wizard;
}

ElevationWizardJob cloudJob(const Args& a)
{
	ElevationWizardJob job;
	QString error;
	if (!elevationWizardJobFromJson(wizardParams(a), &job, &error))
		a.fail(error);
	if (a.has("box"))
	{
		auto const box = a.numbers(a.value("box"), 4, QLatin1String("\"box\""));
		if (!(box[2] > box[0] && box[3] > box[1]))
			a.fail(QLatin1String("\"box\" must be [E min, N min, E max, N max] with the maximum above the minimum"));
		job.gridding.box = QRectF(box[0], box[1], box[2] - box[0], box[3] - box[1]);
	}
	return job;
}

StepResult pointCloud(Map& map, const QString& map_path, const Job& job, const Args& a)
{
	if (!PointCloud::supported())
		return failure(QLatin1String("this build of FOOM has no LASzip"));
	auto cloud = cloudJob(a);
	for (auto& file : cloud.files)
		file = job.resolvePath(file);
	auto gridding = cloud.gridding;
	gridding.dsm = gridding.dsm || cloud.vegetation_height;
	if (!gridding.box.isEmpty())
	{
		auto const info = PointCloud::analyse(cloud.files);
		if (!info.error.isEmpty())
			return failure(info.error);
		if (!info.extent.intersects(gridding.box))
			return failure(QLatin1String("the box holds no point of the clouds"));
	}
	auto products = cloud.cloud;
	products.vegetation_height = false;
	QStringList grids;
	QStringList files;
	QString error;
	if (!ElevationCommands::importPointClouds(map, map_path, cloud.files, gridding, products, cloud.attach, &grids, &files, &error))
		return failure(error);
	if (cloud.vegetation_height)
	{
		auto const path = ElevationCommands::writeVegetationHeightMap(map, map_path, cloud.vegetation_height_options, cloud.attach, &error);
		if (path.isEmpty())
			return failure(error);
		files.push_back(path);
	}
	StepResult result;
	result.details.insert(QLatin1String("grids"), QJsonArray::fromStringList(grids));
	result.details.insert(QLatin1String("files"), QJsonArray::fromStringList(files));
	return result;
}


// ### Canvases ###

StepResult loadCanvases(Map& map, const Job& job, const Args& a)
{
	QByteArray bytes;
	if (a.has("path"))
	{
		auto const path = job.resolvePath(a.string("path"));
		QFile file(path);
		if (!file.open(QIODevice::ReadOnly))
			return failure(QString::fromLatin1("cannot read \"%1\": %2").arg(path, file.errorString()));
		bytes = file.readAll();
	}
	else
	{
		bytes = a.string("xml").toUtf8();
	}
	auto data = map.courseData();
	std::vector<Extent> extents;
	if (!canvasesFromXmlScript(bytes, data.canvases, extents))
		return failure(QLatin1String("the text has no canvases (CsCanvas)"));
	map.setCourseData(data);
	if (!extents.empty())
		importExtents(map, extents);
	StepResult result;
	result.details.insert(QLatin1String("canvases"), int(data.canvases.canvases.size()));
	result.details.insert(QLatin1String("extents"), int(extents.size()));
	return result;
}


// ### GNSS files ###

GnssImportKind gnssKind(const Args& a)
{
	auto const kind = a.has("as") ? a.string("as") : QStringLiteral("track");
	if (kind == QLatin1String("track"))
		return GnssImportKind::Track;
	if (kind == QLatin1String("waypoints"))
		return GnssImportKind::Waypoints;
	if (kind == QLatin1String("positions"))
		return GnssImportKind::Positions;
	a.fail(QString::fromLatin1("unknown \"as\" \"%1\": expected track, waypoints or positions").arg(kind));
}

QString symbolNumber(const Args& a)
{
	auto const v = a.value("symbol");
	if (v.isDouble())
		return QString::number(v.toDouble());
	if (v.isString() && !v.toString().isEmpty())
		return v.toString();
	a.fail(QLatin1String("\"symbol\" must be the number of a symbol"));
}

StepResult importGnss(Map& map, const Job& job, const Args& a)
{
	auto const path = job.resolvePath(a.string("path"));
	auto const kind = gnssKind(a);
	auto const number = symbolNumber(a);
	const Symbol* symbol = nullptr;
	for (int i = 0; i < map.getNumSymbols() && !symbol; ++i)
	{
		if (map.getSymbol(i)->getNumberAsString() == number)
			symbol = map.getSymbol(i);
	}
	if (!symbol)
		return failure(QString::fromLatin1("the map has no symbol %1").arg(number));
	auto const data = readGnssFile(path);
	if (!data.ok())
		return failure(data.error);
	auto made = gnssObjects(map, data, kind, symbol);
	if (!made.error.isEmpty())
		return failure(made.error);
	auto const count = addGnssObjects(map, std::move(made.objects));
	StepResult result;
	result.details.insert(QLatin1String("path"), path);
	result.details.insert(QLatin1String("objects"), count);
	result.details.insert(QLatin1String("fixes"), int(data.fixes.size()));
	result.details.insert(QLatin1String("waypoints"), int(data.waypoints.size()));
	result.details.insert(QLatin1String("skipped"), data.skipped);
	result.details.insert(QLatin1String("bad_checksums"), data.bad_checksums);
	return result;
}

}  // namespace



bool isBackgroundStep(const QString& name)
{
	return name == add_template_step || name == remove_template_step || name == georeference_template_step
	       || name == point_cloud_step || name == load_canvases_step || name == import_gnss_step;
}


void validateBackgroundStep(const Step& step, bool map_open)
{
	Args a(step);
	if (!map_open)
		a.fail(QLatin1String("needs an open map: add an \"open\" step first"));
	auto const& name = step.name;
	if (name == add_template_step)
	{
		a.known({ "path", "georeferenced", "crs", "center", "meters_per_pixel", "dpi", "rotation" });
		a.string("path");
		a.boolean("georeferenced", false);
		if (a.has("crs"))
			crsSpecOf(step, a.string("crs"));
		validatePlacement(a);
	}
	else if (name == remove_template_step)
	{
		a.known({ "template" });
		if (!a.has("template"))
			a.fail(QLatin1String("needs \"template\""));
		auto const v = a.value("template");
		if (v.isArray())
		{
			if (v.toArray().isEmpty())
				a.fail(QLatin1String("\"template\" must not be an empty list"));
			for (auto const& item : v.toArray())
				checkSelector(a, item);
		}
		else
		{
			checkSelector(a, v);
		}
	}
	else if (name == georeference_template_step)
	{
		a.known({ "template", "georeferenced", "crs", "pass_points", "center", "meters_per_pixel", "dpi", "rotation" });
		if (!a.has("template"))
			a.fail(QLatin1String("needs \"template\""));
		checkSelector(a, a.value("template"));
		auto const ways = int(a.has("georeferenced")) + int(a.has("pass_points")) + int(hasPlacement(a));
		if (ways != 1)
			a.fail(QLatin1String("needs exactly one of \"georeferenced\", \"pass_points\" or a place (\"center\", \"meters_per_pixel\", \"dpi\", \"rotation\")"));
		auto const georeferenced = a.boolean("georeferenced", false);
		if (a.has("crs"))
		{
			if (!georeferenced)
				a.fail(QLatin1String("\"crs\" goes with \"georeferenced\": true"));
			crsSpecOf(step, a.string("crs"));
		}
		if (a.has("pass_points"))
		{
			auto const points = a.value("pass_points");
			if (!points.isArray() || points.toArray().isEmpty())
				a.fail(QLatin1String("\"pass_points\" must be a list of at least one {\"from\": [x, y], \"to\": [x, y]}"));
			for (auto const& item : points.toArray())
			{
				auto const object = item.toObject();
				if (!item.isObject() || object.size() != 2)
					a.fail(QLatin1String("a pass point must be {\"from\": [x, y], \"to\": [x, y]}"));
				a.numbers(object.value(QLatin1String("from")), 2, QLatin1String("\"from\""));
				a.numbers(object.value(QLatin1String("to")), 2, QLatin1String("\"to\""));
			}
		}
		validatePlacement(a);
	}
	else if (name == point_cloud_step)
	{
		a.known({ "files", "cell", "box", "dtm", "dsm", "products", "vegetation_height", "vegetation_map", "feature_map", "attach" });
		cloudJob(a);
	}
	else if (name == load_canvases_step)
	{
		a.known({ "path", "xml" });
		if (a.has("path") == a.has("xml"))
			a.fail(QLatin1String("needs one of \"path\" and \"xml\""));
		a.has("path") ? a.string("path") : a.string("xml");
	}
	else if (name == import_gnss_step)
	{
		a.known({ "path", "as", "symbol" });
		a.string("path");
		gnssKind(a);
		if (!a.has("symbol"))
			a.fail(QLatin1String("needs \"symbol\": the number of a line or point symbol"));
		symbolNumber(a);
	}
}


StepResult runBackgroundStep(Map& map, const QString& map_path, const Job& job, const Step& step)
{
	Args a(step);
	auto const& name = step.name;
	auto const crs_spec = a.has("crs") ? crsSpecOf(step, a.string("crs")) : QString{};
	if (name == add_template_step)
		return addTemplate(map, job, a, crs_spec);
	if (name == remove_template_step)
		return removeTemplate(map, job, a);
	if (name == georeference_template_step)
		return georeferenceTemplate(map, job, a, crs_spec);
	if (name == point_cloud_step)
		return pointCloud(map, map_path, job, a);
	if (name == load_canvases_step)
		return loadCanvases(map, job, a);
	if (name == import_gnss_step)
		return importGnss(map, job, a);
	return failure(QString::fromLatin1("unknown step \"%1\"").arg(name));
}


}  // namespace Cli

}  // namespace OpenOrienteering
