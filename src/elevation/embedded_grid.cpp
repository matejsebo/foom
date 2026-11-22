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

#include "embedded_grid.h"

#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

#include <QtGlobal>
#include <QCoreApplication>
#include <QDataStream>
#include <QIODevice>
#include <QPointF>

#include "elevation/elevation_model.h"

namespace OpenOrienteering {

namespace {

// the design notes, "The embedded grid for course setting".
constexpr char magic[] = "FOOMDEM";       // 7 characters, then the version
constexpr quint8 version = 1;
constexpr quint16 void_value = 0xFFFF;
constexpr quint16 largest_value = 0xFFFE;  // 6553.4 m above the base
constexpr double step = 0.1;              // metres per unit
constexpr qint64 largest_grid = 400000000; // cells, as the point cloud grids

class Tr
{
	Q_DECLARE_TR_FUNCTIONS(OpenOrienteering::EmbeddedGrid)
};

}  // namespace



QByteArray encodeEmbeddedGrid(const ElevationModel& grid)
{
	auto const info = grid.info();
	auto const base = std::isnan(info.minimum) ? 0.0 : std::floor(info.minimum);

	QByteArray raw;
	{
		QDataStream out(&raw, QIODevice::WriteOnly);
		out.setVersion(QDataStream::Qt_5_0);
		out.setByteOrder(QDataStream::LittleEndian);
		out.setFloatingPointPrecision(QDataStream::DoublePrecision);
		out.writeRawData(magic, 7);
		out << version;
		auto const extent = grid.extent();
		out << qint32(grid.width()) << qint32(grid.height())
		    << extent.left() << extent.top() << grid.cellSize() << base  // top(): the southern edge
		    << grid.crsSpec();
		for (int row = 0; row < grid.height(); ++row)
		{
			quint16 previous = 0;
			for (int col = 0; col < grid.width(); ++col)
			{
				auto const z = grid.value(col, row);
				quint16 q = void_value;
				if (!std::isnan(z))
					q = quint16(qBound(0LL, qint64(std::llround((double(z) - base) / step)), qint64(largest_value)));
				// Differences along the row compress far better than the heights.
				out << quint16(q - previous);
				previous = q;
			}
		}
	}
	return qCompress(raw, 9).toBase64();
}


std::unique_ptr<ElevationModel> decodeEmbeddedGrid(const QByteArray& text, QString* error)
{
	auto const fail = [error](const QString& message) {
		if (error)
			*error = message;
		return std::unique_ptr<ElevationModel>();
	};
	auto const raw = qUncompress(QByteArray::fromBase64(text));
	if (raw.size() < 8 || !raw.startsWith(QByteArray::fromRawData(magic, 7)))
		return fail(Tr::tr("The embedded elevation grid is damaged."));
	QDataStream in(raw);
	in.setVersion(QDataStream::Qt_5_0);
	in.setByteOrder(QDataStream::LittleEndian);
	in.setFloatingPointPrecision(QDataStream::DoublePrecision);
	in.skipRawData(7);
	quint8 file_version = 0;
	in >> file_version;
	if (file_version != version)
		return fail(Tr::tr("The embedded elevation grid has an unknown version %1.").arg(file_version));
	qint32 width = 0, height = 0;
	double left = 0, bottom = 0, cell = 0, base = 0;
	QString crs;
	in >> width >> height >> left >> bottom >> cell >> base >> crs;
	if (in.status() != QDataStream::Ok || width <= 0 || height <= 0 || qint64(width) * height > largest_grid || !(cell > 0))
		return fail(Tr::tr("The embedded elevation grid is damaged."));
	auto const n = std::size_t(width) * std::size_t(height);
	if (qint64(raw.size()) - in.device()->pos() != qint64(n) * 2)
		return fail(Tr::tr("The embedded elevation grid is damaged."));
	std::vector<float> values(n);
	auto k = std::size_t(0);
	for (int row = 0; row < height; ++row)
	{
		quint16 q = 0;
		for (int col = 0; col < width; ++col)
		{
			quint16 d = 0;
			in >> d;
			q = quint16(q + d);
			values[k++] = (q == void_value) ? std::numeric_limits<float>::quiet_NaN() : float(base + q * step);
		}
	}
	return std::make_unique<ElevationModel>(width, height, QPointF(left, bottom + height * cell), cell, std::move(values), crs);
}


}  // namespace OpenOrienteering
