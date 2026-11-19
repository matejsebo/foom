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


#include "elevation_set.h"

#include <QDir>
#include <QFileInfo>
#include <QLatin1String>

#include "elevation/elevation_model.h"


namespace OpenOrienteering {

ElevationSet::ElevationSet() = default;

ElevationSet::ElevationSet(const ElevationSet& other)
: hrefs(other.hrefs)
{
	// Models are reloaded on demand.
}

ElevationSet& ElevationSet::operator=(const ElevationSet& other)
{
	if (this != &other)
	{
		hrefs = other.hrefs;
		for (auto& model : models)
			model.reset();
	}
	return *this;
}

ElevationSet::~ElevationSet() = default;


bool ElevationSet::isEmpty() const
{
	for (auto const& href : hrefs)
		if (!href.isEmpty())
			return false;
	return true;
}


void ElevationSet::clear()
{
	for (auto& href : hrefs)
		href.clear();
	for (auto& model : models)
		model.reset();
}


void ElevationSet::setHref(Kind kind, const QString& href)
{
	hrefs[std::size_t(kind)] = href.isEmpty() ? QString{} : QFileInfo(href).absoluteFilePath();
	models[std::size_t(kind)].reset();
}


void ElevationSet::setModel(Kind kind, std::unique_ptr<ElevationModel> model, const QString& href)
{
	hrefs[std::size_t(kind)] = href.isEmpty() ? QString{} : QFileInfo(href).absoluteFilePath();
	models[std::size_t(kind)] = std::move(model);
}


const ElevationModel* ElevationSet::model(Kind kind, const QString& target_crs)
{
	auto& model = models[std::size_t(kind)];
	if (!model && !hrefs[std::size_t(kind)].isEmpty())
	{
		ElevationModel::OpenOptions options;
		options.target_crs = target_crs;
		model = ElevationModel::open(hrefs[std::size_t(kind)], options);
	}
	return model.get();
}


// static
QString ElevationSet::kindName(Kind kind)
{
	switch (kind)
	{
	case Surface:
		return QStringLiteral("surface");
	case Canopy:
		return QStringLiteral("canopy");
	case Terrain:
		break;
	}
	return QStringLiteral("terrain");
}


// static
bool ElevationSet::kindFromName(const QString& name, Kind& kind)
{
	if (name == QLatin1String("terrain") || name.isEmpty())
		kind = Terrain;
	else if (name == QLatin1String("surface"))
		kind = Surface;
	else if (name == QLatin1String("canopy"))
		kind = Canopy;
	else
		return false;
	return true;
}


// static
QString ElevationSet::relativeHref(const QString& href, const QString& map_path)
{
	if (href.isEmpty() || map_path.isEmpty())
		return href;
	auto const map_dir = QFileInfo(map_path).absoluteDir();
	auto const relative = map_dir.relativeFilePath(href);
	if (relative.startsWith(QLatin1String("..")) || QFileInfo(relative).isAbsolute())
		return href;
	return relative;
}


// static
QString ElevationSet::absoluteHref(const QString& href, const QString& map_path)
{
	if (href.isEmpty() || QFileInfo(href).isAbsolute() || map_path.isEmpty())
		return href;
	return QFileInfo(map_path).absoluteDir().absoluteFilePath(href);
}


}  // namespace OpenOrienteering
