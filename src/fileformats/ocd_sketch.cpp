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

#include <algorithm>
#include <cstddef>
#include <utility>

#include <QtEndian>
#include <QChar>
#include <QLatin1Char>

#include "fileformats/ocd_datetime.h"


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

double f64(const QByteArray& file, qint64 at)
{
	return Ocd::delphiDateTimeFromBits(qFromLittleEndian<quint64>(reinterpret_cast<const uchar*>(file.constData()) + at));
}

void put32(QByteArray& file, int at, quint32 value)
{
	qToLittleEndian<quint32>(value, reinterpret_cast<uchar*>(file.data()) + at);
}

void putDate(QByteArray& file, int at, double value)
{
	qToLittleEndian<quint64>(Ocd::delphiDateTimeToBits(value), reinterpret_cast<uchar*>(file.data()) + at);
}

// As the format has it.
constexpr int layer_size = 550;
constexpr int index_capacity = 256;
constexpr int index_size = 12 + 8 * index_capacity;
constexpr quint32 header_field_2 = 1400;  // its meaning is not known

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
				stroke.created = f64(file, at + 8);
				stroke.modified = f64(file, at + 16);
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


void appendOcdSketch(QByteArray& file, const std::vector<OcdSketchStroke>& strokes)
{
	if (strokes.empty() || file.size() < ocd_sketch_header_field + 4)
		return;
	auto const header = file.size();
	file.append(QByteArray(sketch_header_size, '\0'));
	file[header] = 1;
	file[header + 1] = 1;
	put32(file, header + 2, header_field_2);
	// The pen which is set: that of the last stroke.
	file[header + 6] = char(strokes.back().color.red());
	file[header + 7] = char(strokes.back().color.green());
	file[header + 8] = char(strokes.back().color.blue());
	
	auto const layer = file.size();
	put32(file, header + sketch_header_size - 4, quint32(layer));
	file.append(QByteArray(layer_size, '\0'));
	put32(file, layer + 4, 1);
	auto name = strokes.front().layer;
	if (name.isEmpty())
		name = QStringLiteral("Sketch-1");
	name = name.leftJustified(layer_name_bytes / 2, QLatin1Char(' '), true);
	for (int i = 0; i < layer_name_bytes / 2; ++i)
		qToLittleEndian<quint16>(name.at(i).unicode(), reinterpret_cast<uchar*>(file.data()) + layer + layer_name_offset + 2 * i);
	auto created = strokes.front().created;
	auto modified = strokes.front().modified;
	for (auto const& stroke : strokes)
	{
		if (stroke.created > 0 && (created <= 0 || stroke.created < created))
			created = stroke.created;
		if (stroke.modified > modified)
			modified = stroke.modified;
	}
	putDate(file, layer + 520, created);
	putDate(file, layer + 528, modified);
	put32(file, layer + 536, 1);
	
	auto link = layer + layer_index_offset;
	for (std::size_t first = 0; first < strokes.size(); first += index_capacity)
	{
		auto const index = file.size();
		put32(file, link, quint32(index));
		link = index;
		file.append(QByteArray(index_size, '\0'));
		auto const count = int(std::min(strokes.size() - first, std::size_t(index_capacity)));
		put32(file, index + 8, quint32(count));
		for (int k = 0; k < count; ++k)
		{
			auto const& stroke = strokes[first + std::size_t(k)];
			auto const at = file.size();
			auto const size = stroke_header_size + 8 * int(stroke.points.size());
			put32(file, index + 12 + 8 * k, quint32(at));
			put32(file, index + 16 + 8 * k, quint32(size));
			file.append(QByteArray(size, '\0'));
			file[at] = char(stroke.color.red());
			file[at + 1] = char(stroke.color.green());
			file[at + 2] = char(stroke.color.blue());
			put32(file, at + 4, quint32(stroke.width));
			putDate(file, at + 8, stroke.created);
			putDate(file, at + 16, stroke.modified);
			if (!stroke.points.empty())
			{
				// The box holds the ink: the points and half the pen's width around them.
				auto min_x = stroke.points.front().x();
				auto max_x = min_x;
				auto min_y = stroke.points.front().y();
				auto max_y = min_y;
				for (auto const& point : stroke.points)
				{
					min_x = std::min(min_x, point.x());
					max_x = std::max(max_x, point.x());
					min_y = std::min(min_y, point.y());
					max_y = std::max(max_y, point.y());
				}
				auto const half = 128 * stroke.width;  // half of 1/100 mm shifted left by eight bits
				put32(file, at + 24, quint32(min_x - half));
				put32(file, at + 28, quint32(min_y - half));
				put32(file, at + 32, quint32(max_x + half));
				put32(file, at + 36, quint32(max_y + half));
			}
			put32(file, at + 40, quint32(stroke.points.size()));
			put32(file, at + 44, 1);
			for (std::size_t i = 0; i < stroke.points.size(); ++i)
			{
				put32(file, at + stroke_header_size + 8 * int(i), quint32(stroke.points[i].x()));
				put32(file, at + stroke_header_size + 8 * int(i) + 4, quint32(stroke.points[i].y()));
			}
		}
	}
	put32(file, ocd_sketch_header_field, quint32(header));
}


}  // namespace OpenOrienteering
