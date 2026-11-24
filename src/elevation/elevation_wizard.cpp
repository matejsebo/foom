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

#include "elevation_wizard.h"

#include <array>
#include <cmath>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <utility>

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QLatin1String>

#include "core/georeferencing.h"
#include "core/map.h"
#include "core/objects/object.h"
#include "elevation/elevation_commands.h"
#include "elevation/elevation_set.h"

namespace OpenOrienteering {

namespace {

QString tr(const char* text)
{
	return QCoreApplication::translate("OpenOrienteering::ElevationWizard", text);
}

}  // namespace


// ### ElevationWizardJob ###

ElevationWizardJob ElevationWizardJob::defaults(Source source)
{
	// the design notes, "Wizard": the ticks for each source.
	ElevationWizardJob job;
	job.source = source;
	if (source == PointCloud)
	{
		job.vegetation_height = true;
		job.cloud.vegetation_map = true;
		job.cloud.intensity_map = true;
	}
	return job;
}


LandformParams ElevationWizardJob::effectiveLandforms() const
{
	auto params = landforms;
	if (!features)
	{
		params.knolls = false;
		params.depressions = false;
		params.trees = false;
	}
	// The cliffs are on the slope page and take its threshold.
	params.cliffs = params.cliffs && slope;
	params.cliff_threshold = slope_params.threshold_deg;
	return params;
}


bool ElevationWizardJob::makesObjects() const
{
	auto const params = effectiveLandforms();
	return (contours && !contour_sets.empty()) || params.knolls || params.depressions || params.cliffs || params.trees;
}



// ### runElevationWizard ###

ElevationWizardResult runElevationWizard(Map& map, const QString& map_path, const ElevationWizardJob& job, const TileProgress& progress)
{
	ElevationWizardResult result;
	auto const fail = [&result](const QString& message) {
		result.ok = false;
		result.error = message;
		return result;
	};
	if (job.files.isEmpty())
		return fail(tr("No file was chosen."));
	auto const templates = map.getNumTemplates();
	auto const landforms = job.effectiveLandforms();
	auto const attach = job.attach;
	QString error;

	// 1. The grids.
	if (job.source == ElevationWizardJob::Grid)
	{
		if (job.files.size() == 1)
		{
			QString href;
			if (!ElevationCommands::importGrid(map, map_path, job.files.front(), ElevationSet::Terrain, job.open_options, &href, &error))
				return fail(error);
			result.grids.push_back(href);
		}
		else
		{
			if (!ElevationCommands::importMergedGrids(map, map_path, job.files, job.open_options, &error))
				return fail(error);
			result.grids.push_back(map.elevationSet().href(ElevationSet::Terrain));
		}
	}
	else
	{
		// 2. The rasters of the cloud; the vegetation height comes from the map's grids below.
		auto gridding = job.gridding;
		gridding.dsm = gridding.dsm || job.vegetation_height || landforms.trees;
		if (progress)
			gridding.progress = progress;
		auto products = job.cloud;
		products.vegetation_height = false;
		if (!ElevationCommands::importPointClouds(map, map_path, job.files, gridding, products, attach, &result.grids, &result.files, &error))
			return fail(error);
		if (job.vegetation_height)
		{
			auto const path = ElevationCommands::writeVegetationHeightMap(map, map_path, job.vegetation_height_options, attach, &error);
			if (path.isEmpty())
				return fail(error);
			result.files.push_back(path);
		}
	}

	auto const* model = map.elevationSet().model(ElevationSet::Terrain, map.getGeoreferencing().getProjectedCRSSpec());
	if (!model)
		return fail(tr("The map has no terrain grid."));

	// 3. The rasters of the terrain grid.
	auto const product = [&](const RasterProduct& raster, const char* suffix) {
		auto const path = ElevationCommands::writeTerrainProduct(map, map_path, raster, QString::fromLatin1(suffix), attach, &error);
		if (!path.isEmpty())
			result.files.push_back(path);
		return !path.isEmpty();
	};
	if (job.hill_shading && !product(hillShade(*model, job.hill_shade), "hillshade"))
		return fail(error);
	if (job.slope && !product(slopeRaster(*model, job.slope_params), "slope"))
		return fail(error);
	if (job.hypsometric)
	{
		auto const path = ElevationCommands::writeHypsometricMap(map, map_path, job.hypsometric_params, attach, &error);
		if (path.isEmpty())
			return fail(error);
		result.files.push_back(path);
	}

	// 4. The objects, in one undo step.
	std::vector<std::unique_ptr<Object>> objects;
	if (job.contours)
	{
		for (auto const& params : job.contour_sets)
		{
			auto contours = makeContourObjects(map, *model, params, contourSymbols(map, params.smoothed));
			if (!contours.error.isEmpty())
				return fail(contours.error);
			std::move(contours.objects.begin(), contours.objects.end(), std::back_inserter(objects));
		}
	}
	result.contours = int(objects.size());
	if (landforms.knolls || landforms.depressions || landforms.cliffs || landforms.trees)
	{
		auto features = ElevationCommands::findFeatures(map, landforms);
		result.features = int(features.objects.size());
		std::move(features.objects.begin(), features.objects.end(), std::back_inserter(objects));
	}
	if (!objects.empty())
		addContours(map, std::move(objects));

	map.setHasUnsavedChanges(true);
	result.templates = map.getNumTemplates() - templates;
	result.ok = true;
	return result;
}



// ### elevationWizardJobFromJson ###

namespace {

/**
 * Reads one JSON object, with the member names checked.
 */
class Reader
{
public:
	Reader(const QJsonObject& object, QString context, QString* error)
	    : object(object), context(std::move(context)), error(error)
	{}

	bool ok() const { return okay; }

	void fail(const QString& message)
	{
		if (okay && error)
			*error = context + message;
		okay = false;
	}

	void known(std::initializer_list<const char*> names)
	{
		for (auto it = object.begin(); it != object.end(); ++it)
		{
			auto found = false;
			for (auto const* name : names)
				found = found || it.key() == QLatin1String(name);
			if (!found)
				fail(QString::fromLatin1("unknown member \"%1\"").arg(it.key()));
		}
	}

	void boolean(const char* name, bool& value)
	{
		auto const v = object.value(QLatin1String(name));
		if (v.isUndefined())
			return;
		if (!v.isBool())
			return fail(QString::fromLatin1("\"%1\" must be true or false").arg(QLatin1String(name)));
		value = v.toBool();
	}

	void number(const char* name, double& value, double minimum, double maximum)
	{
		auto const v = object.value(QLatin1String(name));
		if (v.isUndefined())
			return;
		if (!v.isDouble() || !(v.toDouble() >= minimum && v.toDouble() <= maximum))
			return fail(QString::fromLatin1("\"%1\" must be a number from %2 to %3").arg(QLatin1String(name)).arg(minimum).arg(maximum));
		value = v.toDouble();
	}

	void integer(const char* name, int& value, int minimum, int maximum)
	{
		auto number = double(value);
		this->number(name, number, minimum, maximum);
		if (okay && number != std::floor(number))
			return fail(QString::fromLatin1("\"%1\" must be a whole number").arg(QLatin1String(name)));
		value = int(number);
	}

	template <std::size_t n>
	void numbers(const char* name, std::array<double, n>& values, double minimum, double maximum)
	{
		auto const v = object.value(QLatin1String(name));
		if (v.isUndefined())
			return;
		auto const array = v.toArray();
		if (!v.isArray() || array.size() != int(n))
			return fail(QString::fromLatin1("\"%1\" must be a list of %2 numbers").arg(QLatin1String(name)).arg(n));
		for (std::size_t i = 0; i < n; ++i)
		{
			auto const item = array.at(int(i));
			if (!item.isDouble() || !(item.toDouble() >= minimum && item.toDouble() <= maximum))
				return fail(QString::fromLatin1("\"%1\" must hold numbers from %2 to %3").arg(QLatin1String(name)).arg(minimum).arg(maximum));
			values[i] = item.toDouble();
		}
	}

	/// One of the names; returns its index, or -1 when absent.
	int choice(const char* name, std::initializer_list<const char*> names)
	{
		auto const v = object.value(QLatin1String(name));
		if (v.isUndefined())
			return -1;
		auto index = 0;
		for (auto const* candidate : names)
		{
			if (v.isString() && v.toString() == QLatin1String(candidate))
				return index;
			++index;
		}
		QStringList list;
		for (auto const* candidate : names)
			list.push_back(QLatin1String(candidate));
		fail(QString::fromLatin1("\"%1\" must be one of %2").arg(QLatin1String(name), list.join(QLatin1String(", "))));
		return -1;
	}

	/// The member as an object; an absent member gives an empty object.
	QJsonObject child(const char* name)
	{
		auto const v = object.value(QLatin1String(name));
		if (v.isUndefined())
			return {};
		if (!v.isObject())
			fail(QString::fromLatin1("\"%1\" must be an object").arg(QLatin1String(name)));
		return v.toObject();
	}

	QJsonValue value(const char* name) const { return object.value(QLatin1String(name)); }

	QString childContext(const char* name) const { return context + QLatin1Char('"') + QLatin1String(name) + QLatin1String("\": "); }

	/// Reads a child object with its own reader, passing on a failure.
	template <class Function>
	void nested(const char* name, Function function)
	{
		auto const o = child(name);
		if (!okay)
			return;
		QString message;
		Reader reader(o, childContext(name), &message);
		function(reader);
		if (!reader.ok())
		{
			if (okay && error)
				*error = message;
			okay = false;
		}
	}

private:
	QJsonObject object;
	QString context;
	QString* error;
	bool okay = true;
};

}  // namespace


bool elevationWizardJobFromJson(const QJsonObject& object, ElevationWizardJob* job, QString* error)
{
	// the design notes, "Wizard", the runner step.
	Reader r(object, {}, error);
	r.known({ "source", "files", "products", "attach", "units", "cell", "dtm", "dsm", "dtm_classes", "dtm_returns", "dsm_classes", "dsm_returns", "fill_cells", "edge_cells", "contour_sets", "hill_shading",
	          "slope_gradient", "hypsometric_map", "feature_extraction", "vegetation_height", "vegetation_map", "feature_map",
	          "memory_limit_mb" });
	auto const source = r.choice("source", { "grid", "point-cloud" });
	if (!r.ok())
		return false;
	if (source < 0)
	{
		r.fail(QString::fromLatin1("\"source\" is required: grid or point-cloud"));
		return false;
	}
	auto result = ElevationWizardJob::defaults(source == 0 ? ElevationWizardJob::Grid : ElevationWizardJob::PointCloud);

	auto const files = r.value("files");
	if (!files.isArray() || files.toArray().isEmpty())
		r.fail(QString::fromLatin1("\"files\" must be a list of one or more paths"));
	for (auto const& file : files.toArray())
	{
		if (!file.isString() || file.toString().isEmpty())
			r.fail(QString::fromLatin1("\"files\" must hold paths"));
		result.files.push_back(file.toString());
	}

	auto const products = r.value("products");
	if (!products.isUndefined())
	{
		if (!products.isArray())
			r.fail(QString::fromLatin1("\"products\" must be a list"));
		result.contours = result.hill_shading = result.slope = result.hypsometric = result.features = false;
		result.vegetation_height = result.cloud.vegetation_map = result.cloud.feature_map = false;
		result.cloud.intensity_map = result.cloud.classification_map = false;
		for (auto const& product : products.toArray())
		{
			auto const name = product.toString();
			if (name == QLatin1String("contours"))
				result.contours = true;
			else if (name == QLatin1String("hill-shading"))
				result.hill_shading = true;
			else if (name == QLatin1String("slope"))
				result.slope = true;
			else if (name == QLatin1String("hypsometric"))
				result.hypsometric = true;
			else if (name == QLatin1String("features"))
				result.features = true;
			else if (source == 1 && name == QLatin1String("vegetation-height"))
				result.vegetation_height = true;
			else if (source == 1 && name == QLatin1String("vegetation-map"))
				result.cloud.vegetation_map = true;
			else if (source == 1 && name == QLatin1String("feature-map"))
				result.cloud.feature_map = true;
			else if (source == 1 && name == QLatin1String("intensity"))
				result.cloud.intensity_map = true;
			else if (source == 1 && name == QLatin1String("classification"))
				result.cloud.classification_map = true;
			else
				r.fail(QString::fromLatin1("unknown product \"%1\" for this source").arg(product.isString() ? name : QString::fromLatin1("(not a name)")));
		}
	}
	r.boolean("attach", result.attach);

	r.nested("units", [&result](Reader& u) {
		u.known({ "feet", "millimetres", "shift" });
		u.boolean("feet", result.open_options.feet_to_metres);
		u.boolean("millimetres", result.open_options.millimetres_to_metres);
		u.boolean("shift", result.open_options.shift_below_sea_level);
	});
	r.number("cell", result.gridding.cell, 0.1, 100.0);
	result.cloud.vegetation.cell = result.gridding.cell;
	r.boolean("dtm", result.gridding.dtm);
	r.boolean("dsm", result.gridding.dsm);
	// "Filling voids: inside the data and past its edge".
	r.integer("fill_cells", result.gridding.fill_cells, 0, 1000);
	r.integer("edge_cells", result.gridding.edge_cells, 0, 1000);
	// "The points of the models": the classes as numbers or as the names of the rows, and the returns.
	auto const points = [&r](const char* classes_name, const char* returns_name, PointChoice& choice) {
		auto const list = r.value(classes_name);
		if (!list.isUndefined())
		{
			if (!list.isArray())
			{
				r.fail(QString::fromLatin1("\"%1\" must be a list of class numbers or names").arg(QLatin1String(classes_name)));
				return;
			}
			choice.classes.fill(false);
			for (auto const& entry : list.toArray())
			{
				auto group = -1;
				for (int g = 0; entry.isString() && g < PointChoice::group_count; ++g)
				{
					if (entry.toString() == QLatin1String(PointChoice::groupId(g)))
						group = g;
				}
				auto const number = entry.isDouble() ? entry.toDouble(-1) : -1.0;
				if (group >= 0)
					choice.setGroup(group, true);
				else if (number >= 0 && number <= 255 && number == std::floor(number))
					choice.classes[std::size_t(number)] = true;
				else
				{
					r.fail(QString::fromLatin1("\"%1\": a class is a number from 0 to 255 or one of unclassified, ground, low-vegetation, "
					                           "medium-vegetation, high-vegetation, building, low-point, water, overlap, other").arg(QLatin1String(classes_name)));
					return;
				}
			}
		}
		auto const returns = r.choice(returns_name, { "first", "last", "all" });
		if (returns >= 0)
			choice.returns = returns == 0 ? ReturnChoice::First : returns == 1 ? ReturnChoice::Last : ReturnChoice::All;
	};
	points("dtm_classes", "dtm_returns", result.gridding.dtm_points);
	points("dsm_classes", "dsm_returns", result.gridding.dsm_points);
	// "Grids on disk, a tile cache, and a progress bar".
	double memory_mb = 0.0;
	r.number("memory_limit_mb", memory_mb, 1.0, 1.0e7);
	if (memory_mb > 0.0)
		result.gridding.memory_limit = qint64(memory_mb * 1048576.0);

	auto const sets = r.value("contour_sets");
	if (!sets.isUndefined())
	{
		if (!sets.isArray())
			r.fail(QString::fromLatin1("\"contour_sets\" must be a list of objects"));
		result.contour_sets.clear();
		for (auto const& set : sets.toArray())
		{
			if (!set.isObject())
			{
				r.fail(QString::fromLatin1("\"contour_sets\" must be a list of objects"));
				break;
			}
			QString message;
			Reader s(set.toObject(), QString::fromLatin1("\"contour_sets\": "), &message);
			s.known({ "interval", "main_interval", "index_interval", "smoothed", "curves", "depressions" });
			ContourParams params;
			s.boolean("smoothed", params.smoothed);
			if (params.smoothed)
			{
				// The smoothed set's defaults, as the contour page's.
				params.interval = 2.5;
				params.curves = true;
			}
			s.number("interval", params.interval, 0.01, 1000.0);
			s.number("main_interval", params.main_interval, 0.01, 10000.0);
			s.number("index_interval", params.index_interval, 0.01, 10000.0);
			s.boolean("curves", params.curves);
			s.boolean("depressions", params.depression_symbols);
			if (!s.ok())
			{
				r.fail(message);
				break;
			}
			result.contour_sets.push_back(params);
		}
	}

	r.nested("hill_shading", [&result](Reader& h) {
		h.known({ "method", "multi_directional", "azimuth", "elevation", "exaggeration", "cell" });
		auto const method = h.choice("method", { "slope", "oblique", "combined" });
		if (method >= 0)
			result.hill_shade.method = HillShadeParams::Method(method);
		h.boolean("multi_directional", result.hill_shade.multi_directional);
		h.number("azimuth", result.hill_shade.azimuth_deg, 0.0, 360.0);
		h.number("elevation", result.hill_shade.elevation_deg, 1.0, 90.0);
		h.number("exaggeration", result.hill_shade.exaggeration, 1.0, 10.0);
		h.number("cell", result.hill_shade.cell_size, 0.0, 1000.0);
	});
	r.nested("slope_gradient", [&result](Reader& s) {
		s.known({ "binary", "threshold", "darkest", "cell", "cliffs", "min_area", "min_length" });
		s.number("darkest", result.slope_params.darkest, 0.0, 1.0);
		s.boolean("binary", result.slope_params.binary);
		s.number("threshold", result.slope_params.threshold_deg, 1.0, 89.0);
		s.number("cell", result.slope_params.cell_size, 0.0, 1000.0);
		s.boolean("cliffs", result.landforms.cliffs);
		s.integer("min_area", result.landforms.cliff_min_area, 1, 1000);
		s.integer("min_length", result.landforms.cliff_min_length, 1, 1000);
	});
	r.nested("hypsometric_map", [&result](Reader& h) {
		h.known({ "ramp", "interval", "white_at_zero" });
		auto const ramp = h.choice("ramp", { "greyscale", "red-green-blue", "red-yellow-green", "red-yellow-blue",
		                                     "brown-yellow-green", "brown-yellow", "red-yellow", "greyscale-interval" });
		if (ramp >= 0)
			result.hypsometric_params.ramp = HypsometricParams::Ramp(ramp);
		h.number("interval", result.hypsometric_params.interval, 1.0, 10000.0);
		h.boolean("white_at_zero", result.hypsometric_params.white_at_zero);
	});
	r.nested("feature_extraction", [&result](Reader& f) {
		f.known({ "knolls", "min_height", "depressions", "min_depth", "max_area", "trees", "tree_min_height" });
		f.boolean("knolls", result.landforms.knolls);
		f.number("min_height", result.landforms.min_height, 0.1, 100.0);
		f.boolean("depressions", result.landforms.depressions);
		f.number("min_depth", result.landforms.min_depth, 0.1, 100.0);
		f.number("max_area", result.landforms.max_area, 10.0, 1000000.0);
		f.boolean("trees", result.landforms.trees);
		f.number("tree_min_height", result.landforms.tree_min_height, 1.0, 100.0);
	});
	r.nested("vegetation_height", [&result](Reader& v) {
		v.known({ "greyscale", "mapping", "max_height" });
		v.boolean("greyscale", result.vegetation_height_options.greyscale);
		auto const mapping = v.choice("mapping", { "linear", "quadratic-negative", "quadratic-positive" });
		if (mapping >= 0)
			result.vegetation_height_options.mapping = VegetationHeightOptions::GreyMapping(mapping);
		v.number("max_height", result.vegetation_height_options.max_height, 0.1, 1000.0);
	});
	r.nested("vegetation_map", [&result](Reader& v) {
		auto& p = result.cloud.vegetation;
		v.known({ "undergrowth_from", "undergrowth_to", "undergrowth_kernel", "undergrowth_thresholds",
		          "vegetation_from", "vegetation_to", "vegetation_kernel", "vegetation_thresholds", "no_data_red" });
		v.number("undergrowth_from", p.undergrowth_from, 0.0, 100.0);
		v.number("undergrowth_to", p.undergrowth_to, 0.0, 100.0);
		v.integer("undergrowth_kernel", p.undergrowth_kernel, 1, 51);
		v.numbers("undergrowth_thresholds", p.undergrowth_thresholds, -1.0, 1.0);
		v.number("vegetation_from", p.vegetation_from, 0.0, 100.0);
		v.number("vegetation_to", p.vegetation_to, 0.0, 100.0);
		v.integer("vegetation_kernel", p.vegetation_kernel, 1, 51);
		v.numbers("vegetation_thresholds", p.vegetation_thresholds, -1.0, 1.0);
		v.boolean("no_data_red", p.no_data_red);
	});
	r.nested("feature_map", [&result](Reader& f) {
		f.known({ "from", "to", "legacy" });
		f.number("from", result.cloud.feature_from, 0.0, 100.0);
		f.number("to", result.cloud.feature_to, 0.0, 100.0);
		f.boolean("legacy", result.cloud.feature_legacy);
	});

	if (!r.ok())
		return false;
	*job = std::move(result);
	return true;
}

}  // namespace OpenOrienteering
