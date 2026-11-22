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


#include "import_rule_choice.h"

#include <map>
#include <utility>
#include <vector>

#include <QByteArray>
#include <QDir>
#include <QFileInfo>
#include <QLatin1String>

#include <gdal.h>
#include <ogr_api.h>

#include "core/map.h"
#include "gdal/gdal_manager.h"


namespace OpenOrienteering {

namespace {

/// What recognition reads of one file: all names in lower case.
struct FileSignature
{
	QString driver;
	struct Layer
	{
		QString name;
		std::map<QString, OGRFieldType> fields;
	};
	std::vector<Layer> layers;
};

bool readSignature(const QString& path, FileSignature& signature)
{
	GdalManager().configure();
	auto* dataset = GDALOpenEx(path.toUtf8().constData(), GDAL_OF_VECTOR | GDAL_OF_READONLY, nullptr, nullptr, nullptr);
	if (!dataset)
		return false;
	if (auto* driver = GDALGetDatasetDriver(dataset))
		signature.driver = QString::fromUtf8(GDALGetDriverShortName(driver));
	auto const num_layers = GDALDatasetGetLayerCount(dataset);
	for (int i = 0; i < num_layers; ++i)
	{
		auto* layer = GDALDatasetGetLayer(dataset, i);
		auto* definition = layer ? OGR_L_GetLayerDefn(layer) : nullptr;
		if (!definition)
			continue;
		FileSignature::Layer entry;
		entry.name = QString::fromUtf8(OGR_L_GetName(layer)).toLower();
		for (int j = 0; j < OGR_FD_GetFieldCount(definition); ++j)
		{
			auto* field = OGR_FD_GetFieldDefn(definition, j);
			entry.fields[QString::fromUtf8(OGR_Fld_GetNameRef(field)).toLower()] = OGR_Fld_GetType(field);
		}
		signature.layers.push_back(std::move(entry));
	}
	GDALClose(dataset);
	return true;
}

/// The datasets, in the order in which they are tried.
QString recogniseFile(const FileSignature& s)
{
	if (s.driver == QLatin1String("OSM"))
		return QStringLiteral("OSM");
	return {};
}

}  // namespace



QString recogniseGeodataDataset(const QStringList& paths)
{
	QString result;
	for (auto const& path : paths)
	{
		FileSignature signature;
		if (!readSignature(path, signature))
			return {};
		auto const dataset = recogniseFile(signature);
		if (dataset.isEmpty() || (!result.isEmpty() && dataset != result))
			return {};
		result = dataset;
	}
	return result;
}


QString geodataDatasetTitle(const QString& dataset)
{
	if (dataset == QLatin1String("OSM"))
		return QStringLiteral("OpenStreetMap");
	return dataset;
}


ImportRuleChoice chooseImportRules(const QStringList& paths, const Map& map)
{
	ImportRuleChoice choice;
	choice.dataset = recogniseGeodataDataset(paths);
	if (choice.dataset.isEmpty())
		return choice;

	auto const id = map.symbolSetId();
	if (id.isEmpty())
		return choice;
	QString const name = QStringLiteral("symbol sets/") + choice.dataset + QLatin1Char('-') + id + QLatin1String(".crt");
	for (auto const& folder : QDir::searchPaths(QStringLiteral("data")))
	{
		QFileInfo const file(QDir(folder).filePath(name));
		if (file.isFile())
		{
			choice.crt_file = file.absoluteFilePath();
			break;
		}
	}
	return choice;
}


}  // namespace OpenOrienteering
