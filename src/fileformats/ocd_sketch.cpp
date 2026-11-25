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


#include "ocd_sketch.h"

#include <cstddef>
#include <utility>

#include <QtEndian>


namespace OpenOrienteering {

namespace {

constexpr int sketch_header_size = 22;
constexpr int layer_name_offset = 8;
constexpr int layer_name_bytes = 512;
constexpr int layer_index_offset = 546;
constexpr int stroke_header_size = 56;
constexpr int limit = 100000;  // of layers, of indexes and of strokes in an index: no file has that many

quint32 u32(const QByteArray& file, qint64 at)
{
	return qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(file.constData()) + at);
}

bool holds(const QByteArray& file, qint64 at, qint64 size)
{
	return at > 0 && size >= 0 && at + size <= file.size();
}

}  // namespace


std::vector<OcdSketchStroke> readOcdSketch(const QByteArray& file, bool* damaged)
{
	std::vector<OcdSketchStroke> strokes;
	if (damaged)
		*damaged = false;
	auto const fail = [damaged]() {
		if (damaged)
			*damaged = true;
	};
	if (file.size() < ocd_sketch_header_field + 4)
		return strokes;
	auto const header = qint64(u32(file, ocd_sketch_header_field));
	if (header == 0)
		return strokes;
	if (!holds(file, header, sketch_header_size))
	{
		fail();
		return strokes;
	}
	
	auto layers = 0;
	for (auto layer = qint64(u32(file, header + sketch_header_size - 4)); layer != 0; layer = qint64(u32(file, layer)))
	{
		if (!holds(file, layer, layer_index_offset + 4) || ++layers > limit)
		{
			fail();
			break;
		}
		auto const name = QString::fromUtf16(reinterpret_cast<const char16_t*>(file.constData() + layer + layer_name_offset),
		                                     layer_name_bytes / 2).trimmed();
		auto indexes = 0;
		for (auto index = qint64(u32(file, layer + layer_index_offset)); index != 0; index = qint64(u32(file, index)))
		{
			if (!holds(file, index, 12) || ++indexes > limit)
			{
				fail();
				break;
			}
			auto const count = qint64(u32(file, index + 8));
			if (count > limit || !holds(file, index + 12, 8 * count))
			{
				fail();
				break;
			}
			for (qint64 k = 0; k < count; ++k)
			{
				auto const at = qint64(u32(file, index + 12 + 8 * k));
				if (at == 0)
					continue;  // a stroke which was erased
				if (!holds(file, at, stroke_header_size))
				{
					fail();
					continue;
				}
				auto const points = qint64(u32(file, at + 40));
				if (points > 10 * limit || !holds(file, at + stroke_header_size, 8 * points))
				{
					fail();
					continue;
				}
				OcdSketchStroke stroke;
				stroke.layer = name;
				stroke.color = QColor(quint8(file[int(at)]), quint8(file[int(at + 1)]), quint8(file[int(at + 2)]));
				stroke.width = int(u32(file, at + 4));
				stroke.points.reserve(std::size_t(points));
				for (qint64 i = 0; i < points; ++i)
				{
					stroke.points.emplace_back(int(u32(file, at + stroke_header_size + 8 * i)),
					                           int(u32(file, at + stroke_header_size + 8 * i + 4)));
				}
				strokes.push_back(std::move(stroke));
			}
		}
	}
	return strokes;
}


}  // namespace OpenOrienteering
