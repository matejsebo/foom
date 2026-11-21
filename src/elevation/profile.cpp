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

#include "profile.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

#include <QFile>
#include <QIODevice>
#include <QTextStream>

#include "elevation/elevation_model.h"


namespace OpenOrienteering {

Profile elevationProfile(const ElevationModel& grid, const std::vector<QPointF>& line, double step, double hysteresis)
{
	// the design notes, "Profile".
	Profile profile;
	if (line.empty())
		return profile;
	if (!(step > 0.0))
		step = grid.cellSize();
	auto const sample = [&grid, &profile](double distance, const QPointF& p) {
		ProfileSample s;
		s.distance = distance;
		s.projected = p;
		s.height = grid.at(p);
		profile.samples.push_back(s);
	};
	
	// Every step along the line, and its end.
	double travelled = 0.0;
	double next = 0.0;
	for (std::size_t k = 0; k + 1 < line.size(); ++k)
	{
		auto const& a = line[k];
		auto const& b = line[k + 1];
		auto const length = std::hypot(b.x() - a.x(), b.y() - a.y());
		while (next <= travelled + length + 1e-9)
		{
			auto const t = length > 0.0 ? (next - travelled) / length : 0.0;
			sample(next, a + (b - a) * std::min(1.0, t));
			next += step;
		}
		travelled += length;
	}
	if (profile.samples.empty() || profile.samples.back().distance < travelled - 1e-9)
		sample(travelled, line.back());
	profile.length = travelled;
	
	double reference = std::numeric_limits<double>::quiet_NaN();
	for (auto const& s : profile.samples)
	{
		if (std::isnan(s.height))
			continue;
		if (std::isnan(profile.start))
		{
			profile.start = profile.minimum = profile.maximum = reference = s.height;
		}
		profile.end = s.height;
		profile.minimum = std::min(profile.minimum, s.height);
		profile.maximum = std::max(profile.maximum, s.height);
		if (s.height - reference >= hysteresis)
		{
			profile.ascent += s.height - reference;
			reference = s.height;
		}
		else if (reference - s.height >= hysteresis)
		{
			profile.descent += reference - s.height;
			reference = s.height;
		}
	}
	return profile;
}


bool writeProfileCsv(const Profile& profile, const QString& path, QString* error)
{
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
	{
		if (error)
			*error = file.errorString();
		return false;
	}
	QTextStream out(&file);
	out << "distance;easting;northing;height\n";
	for (auto const& s : profile.samples)
	{
		out << QString::number(s.distance, 'f', 2) << ';' << QString::number(s.projected.x(), 'f', 2) << ';'
		    << QString::number(s.projected.y(), 'f', 2) << ';' << (std::isnan(s.height) ? QString{} : QString::number(s.height, 'f', 2)) << '\n';
	}
	out.flush();
	if (file.error() != QFileDevice::NoError)
	{
		if (error)
			*error = file.errorString();
		return false;
	}
	return true;
}


}  // namespace OpenOrienteering
