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


#include "prj_file.h"

#include <proj.h>

#include <QCoreApplication>
#include <QFile>
#include <QIODevice>
#include <QLatin1String>


namespace OpenOrienteering {

namespace {

QString tr(const char* text)
{
	return QCoreApplication::translate("OpenOrienteering::PrjFile", text);
}

/** Owns a PJ object. */
struct Pj
{
	PJ* pj = nullptr;
	explicit Pj(PJ* pj) : pj(pj) {}
	Pj(const Pj&) = delete;
	Pj& operator=(const Pj&) = delete;
	~Pj() { if (pj) proj_destroy(pj); }
	void reset(PJ* other) { if (pj) proj_destroy(pj); pj = other; }
};

}  // namespace


PrjCrs crsFromWkt(const QByteArray& text)
{
	PrjCrs result;
	auto wkt = text;
	if (wkt.startsWith("\xef\xbb\xbf"))
		wkt.remove(0, 3);
	wkt = wkt.trimmed();

	PJ_CONTEXT* const ctx = PJ_DEFAULT_CTX;
	PROJ_STRING_LIST warnings = nullptr;
	PROJ_STRING_LIST errors = nullptr;
	Pj crs(wkt.isEmpty() ? nullptr : proj_create_from_wkt(ctx, wkt.constData(), nullptr, &warnings, &errors));
	proj_string_list_destroy(warnings);
	proj_string_list_destroy(errors);
	if (!crs.pj)
	{
		result.error = tr("The file holds no coordinate reference system in WKT.");
		return result;
	}

	// The horizontal part of a compound CRS, the source of a bound one.
	if (proj_get_type(crs.pj) == PJ_TYPE_COMPOUND_CRS)
		crs.reset(proj_crs_get_sub_crs(ctx, crs.pj, 0));
	if (crs.pj && proj_get_type(crs.pj) == PJ_TYPE_BOUND_CRS)
		crs.reset(proj_get_source_crs(ctx, crs.pj));
	if (!crs.pj || proj_get_type(crs.pj) != PJ_TYPE_PROJECTED_CRS)
	{
		result.error = tr("The coordinate reference system is not projected. The map needs one with coordinates in metres or feet.");
		return result;
	}
	if (auto const* name = proj_get_name(crs.pj))
		result.name = QString::fromUtf8(name);

	int* confidence = nullptr;
	if (auto* candidates = proj_identify(ctx, crs.pj, "EPSG", nullptr, &confidence))
	{
		if (proj_list_get_count(candidates) > 0 && confidence && confidence[0] >= 90)
		{
			Pj best(proj_list_get(ctx, candidates, 0));
			if (best.pj)
			{
				if (auto const* code = proj_get_id_code(best.pj, 0))
					result.epsg = QString::fromLatin1(code);
			}
		}
		proj_list_destroy(candidates);
	}
	proj_int_list_destroy(confidence);

	if (auto const* proj = proj_as_proj_string(ctx, crs.pj, PJ_PROJ_4, nullptr))
	{
		result.proj = QString::fromUtf8(proj).simplified();
		result.proj.remove(QLatin1String(" +type=crs"));
	}
	if (result.epsg.isEmpty() && result.proj.isEmpty())
		result.error = tr("The coordinate reference system cannot be written as a PROJ string.");
	return result;
}

PrjCrs readPrjFile(const QString& path)
{
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly))
	{
		PrjCrs result;
		result.error = tr("The file cannot be read.");
		return result;
	}
	return crsFromWkt(file.readAll());
}


}  // namespace OpenOrienteering
