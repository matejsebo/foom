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

#include "ocd_course_dem.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <limits>
#include <vector>

#include <QCoreApplication>
#include <QList>
#include <QPointF>
#include <QRectF>
#include <QtEndian>

#include "elevation/elevation_model.h"

namespace OpenOrienteering {

namespace {

// the design notes, "The embedded DEM in OCD files".
constexpr char magic[] = "CS_DEM";         // 6 characters, then the 16-bit version
constexpr quint16 version = 1;
constexpr int header_size = 6 + 2 + 12 * 4;
constexpr quint16 void_value = 0xFFFF;
constexpr qint64 largest_value = 0xFFFE;
constexpr qint64 largest_grid = 400000000;  // cells, as the point cloud grids

class Tr
{
	Q_DECLARE_TR_FUNCTIONS(OpenOrienteering::OcdCourseDem)
};

qint32 centimetres(double metres)
{
	return qint32(std::llround(metres * 100.0));
}

/// The height unit of the block is the centimetre.
constexpr qint32 ocd_units_per_metre = 100;

}  // namespace



QByteArray encodeOcdCourseDem(const ElevationModel& grid)
{
	auto const info = grid.info();
	auto const all_void = std::isnan(info.minimum);
	// Centimetres while the heights fit into 16 bits; fewer units in a metre for higher ground.
	// (The heights of the header are divided by the ninth field, and the twelfth is the cell size;
	// both are 100 in blocks of 1 m cells.)
	auto units = ocd_units_per_metre;
	if (!all_void && (info.maximum - info.minimum) * units > double(largest_value))
		units = qint32(std::max(1.0, std::floor(double(largest_value) / (info.maximum - info.minimum))));
	auto const lowest = all_void ? qint32(0) : qint32(std::llround(info.minimum * units));
	auto const highest = all_void ? qint32(0) : qint32(std::llround(info.maximum * units));
	auto const extent = grid.extent();
	auto const cell = grid.cellSize();
	auto const width = grid.width();
	auto const height = grid.height();

	QByteArray raw(header_size + int(sizeof(quint16)) * width * height, '\0');
	auto* out = reinterpret_cast<uchar*>(raw.data());
	std::memcpy(out, magic, 6);
	qToLittleEndian<quint16>(version, out + 6);
	qint32 const fields[12] = {
	    width, height,
	    centimetres(extent.left() + cell / 2), centimetres(extent.left() + (width - 0.5) * cell),
	    centimetres(extent.top() + cell / 2), centimetres(extent.top() + (height - 0.5) * cell),
	    lowest, highest, units, lowest, 1, centimetres(cell),
	};
	for (int i = 0; i < 12; ++i)
		qToLittleEndian<qint32>(fields[i], out + 8 + 4 * i);
	auto* cells = out + header_size;
	for (int row = 0; row < height; ++row)
	{
		for (int col = 0; col < width; ++col)
		{
			auto const v = grid.value(col, row);
			auto q = void_value;
			if (!std::isnan(v))
				q = quint16(qBound<qint64>(0, std::llround(double(v) * units) - lowest, largest_value));
			qToLittleEndian<quint16>(q, cells);
			cells += 2;
		}
	}
	// qCompress() puts the length before the zlib stream; the block is the stream alone.
	return qCompress(raw).mid(4);
}



std::unique_ptr<ElevationModel> decodeOcdCourseDem(const QByteArray& block, const QString& crs_spec, QString* error)
{
	auto const fail = [error](const QString& message) {
		if (error)
			*error = message;
		return std::unique_ptr<ElevationModel>();
	};
	if (block.size() < 8 || uchar(block[0]) != 0x78)
		return fail(Tr::tr("The embedded DEM is damaged."));
	// qUncompress() wants the expected length first and enlarges its buffer when that was too small.
	QByteArray prefixed(4, '\0');
	qToBigEndian<quint32>(quint32(std::min<qint64>(qint64(block.size()) * 8, 1 << 28)), reinterpret_cast<uchar*>(prefixed.data()));
	prefixed.append(block);
	auto const raw = qUncompress(prefixed);
	if (raw.size() < header_size || std::memcmp(raw.constData(), magic, 6) != 0)
		return fail(Tr::tr("The embedded DEM is damaged."));
	auto const* in = reinterpret_cast<const uchar*>(raw.constData());
	auto const file_version = qFromLittleEndian<quint16>(in + 6);
	if (file_version != version)
		return fail(Tr::tr("The embedded DEM has an unknown version %1.").arg(file_version));
	qint32 fields[12];
	for (int i = 0; i < 12; ++i)
		fields[i] = qFromLittleEndian<qint32>(in + 8 + 4 * i);
	auto const width = fields[0];
	auto const height = fields[1];
	auto const units = double(fields[8] > 0 ? fields[8] : ocd_units_per_metre);
	// The cell size; a block without it has the distance of the first and the last cell centre.
	auto const cell = fields[11] > 0 ? fields[11] / 100.0
	                                 : (width > 1 ? (fields[3] - fields[2]) / 100.0 / (width - 1) : 0.0);
	auto const base = fields[9];
	if (width <= 0 || height <= 0 || qint64(width) * height > largest_grid || !(cell > 0)
	    || qint64(raw.size()) - header_size != qint64(width) * height * 2)
		return fail(Tr::tr("The embedded DEM is damaged."));

	auto const n = std::size_t(width) * std::size_t(height);
	std::vector<float> values(n);
	auto const* cells = in + header_size;
	for (std::size_t k = 0; k < n; ++k)
	{
		auto const q = qFromLittleEndian<quint16>(cells + 2 * k);
		values[k] = (q == void_value) ? std::numeric_limits<float>::quiet_NaN() : float((double(base) + q) / units);
	}
	// The header has the centres of the first and the last cell; the top edge is half a cell north of the north row.
	auto const left = fields[2] / 100.0 - cell / 2;
	auto const top = fields[5] / 100.0 + cell / 2;
	return std::make_unique<ElevationModel>(width, height, QPointF(left, top), cell, std::move(values), crs_spec);
}



bool OcdDemString::restIsEmpty() const
{
	for (auto const& field : rest.split('\t'))
	{
		if (!field.isEmpty() && field != "l1" && field != "l0")
			return false;
	}
	return true;
}



OcdDemString parseOcdDemString(const QByteArray& data)
{
	OcdDemString result;
	auto text = data;
	auto const zero = text.indexOf('\0');
	if (zero >= 0)
		text.truncate(zero);
	auto const fields = text.split('\t');
	QList<QByteArray> kept;
	for (int i = 0; i < fields.size(); ++i)
	{
		auto const& field = fields[i];
		// The first field is the file's name; the others begin with their letter.
		if (i > 0 && !field.isEmpty())
		{
			auto ok = false;
			auto const number = field.mid(1).toLongLong(&ok);
			if (field[0] == 'P' && ok)
			{
				result.offset = number;
				continue;
			}
			if (field[0] == 'S' && ok)
			{
				result.size = number;
				continue;
			}
			if (field[0] == 'e' && ok)
			{
				result.embedded = number != 0;
				continue;
			}
		}
		kept.push_back(field);
	}
	result.rest = kept.join('\t');
	return result;
}



QByteArray formatOcdDemString(const QByteArray& rest, qint64 offset, int digits, qint64 size, int* offset_position)
{
	// A string which names no file: an empty name, and the DEM is loaded, as a course project file has it.
	auto result = rest.isEmpty() ? QByteArray("\tl1") : rest;
	result += "\tP";
	if (offset_position)
		*offset_position = result.size();
	result += QByteArray::number(offset).rightJustified(digits, '0');
	result += "\tS" + QByteArray::number(size) + "\te1";
	return result;
}


}  // namespace OpenOrienteering
