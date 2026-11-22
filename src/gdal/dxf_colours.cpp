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

#include "dxf_colours.h"

#include <array>
#include <mutex>

#include <QtGlobal>
#include <QByteArray>
#include <QRegularExpression>
#include <QString>

#include <cpl_vsi.h>
#include <gdal.h>
#include <ogr_api.h>


namespace OpenOrienteering {

namespace {

/**
 * Reads GDAL's table: a DXF with one line per index, in GDAL's memory files,
 * read back through OGR, whose style strings carry the colours.
 */
std::array<int, 256> readAciTable()
{
	std::array<int, 256> table;
	table.fill(-1);

	QByteArray dxf = "0\nSECTION\n2\nENTITIES\n";
	for (int index = 1; index < 256; ++index)
	{
		dxf += "0\nLINE\n8\n0\n62\n";
		dxf += QByteArray::number(index);
		dxf += "\n10\n0\n20\n";
		dxf += QByteArray::number(index);
		dxf += "\n11\n1\n21\n";
		dxf += QByteArray::number(index);
		dxf += "\n";
	}
	dxf += "0\nENDSEC\n0\nEOF\n";

	GDALAllRegister();
	const char* const name = "/vsimem/foom_aci_colours.dxf";
	auto* file = VSIFileFromMemBuffer(name, reinterpret_cast<GByte*>(dxf.data()), vsi_l_offset(dxf.size()), FALSE);
	if (!file)
		return table;
	VSIFCloseL(file);

	const char* const drivers[] = { "DXF", nullptr };
	auto dataset = GDALOpenEx(name, GDAL_OF_VECTOR, drivers, nullptr, nullptr);
	if (dataset && GDALDatasetGetLayerCount(dataset) > 0)
	{
		static const QRegularExpression colour(QStringLiteral("c:#([0-9a-fA-F]{6})"));
		auto layer = GDALDatasetGetLayer(dataset, 0);
		OGR_L_ResetReading(layer);
		while (auto feature = OGR_L_GetNextFeature(layer))
		{
			// The line's y holds the index.
			auto geometry = OGR_F_GetGeometryRef(feature);
			auto const* style = OGR_F_GetStyleString(feature);
			if (geometry && style)
			{
				auto const index = qRound(OGR_G_GetY(geometry, 0));
				auto const match = colour.match(QString::fromLatin1(style));
				if (index >= 1 && index <= 255 && match.hasMatch())
					table[std::size_t(index)] = match.captured(1).toInt(nullptr, 16);
			}
			OGR_F_Destroy(feature);
		}
	}
	if (dataset)
		GDALClose(dataset);
	VSIUnlink(name);
	return table;
}

const std::array<int, 256>& aciTable()
{
	static std::once_flag once;
	static std::array<int, 256> table;
	std::call_once(once, []() { table = readAciTable(); });
	return table;
}

}  // namespace


bool dxfAciColour(int index, QRgb* rgb)
{
	if (index < 1 || index > 255)
		return false;
	auto const value = aciTable()[std::size_t(index)];
	if (value < 0)
		return false;
	if (rgb)
		*rgb = QRgb(value) | 0xff000000u;
	return true;
}

QByteArray dxfAciStyleColour(int index)
{
	QRgb rgb = 0;
	if (!dxfAciColour(index, &rgb))
		return {};
	return "#" + QByteArray::number(rgb & 0xffffff, 16).rightJustified(6, '0');
}


}  // namespace OpenOrienteering
