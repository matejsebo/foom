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


#include "ocd_layout_image.h"

#include <cmath>

#include <QtGlobal>
#include <QByteArray>
#include <QChar>
#include <QCoreApplication>
#include <QFileInfo>
#include <QImage>
#include <QLatin1Char>
#include <QLatin1String>
#include <QLineF>
#include <QPointF>
#include <QPolygonF>
#include <QSizeF>
#include <QStringRef>

#include "core/map_coord.h"
#include "core/objects/image_object.h"
#include "fileformats/ocd_parameter_stream_reader.h"


namespace OpenOrienteering {

namespace {

QString tr(const char* text)
{
	return QCoreApplication::translate("OpenOrienteering::OcdLayoutImage", text);
}

void setWarning(QString* warning, const QString& message)
{
	if (warning)
		*warning = message;
}

}  // namespace


std::unique_ptr<ImageObject> ocdLayoutImageFromString(const QString& param_string, QString* warning)
{
	OcdParameterStreamReader parameters(param_string);
	auto const path = parameters.value().toString().replace(QLatin1Char('\\'), QLatin1Char('/'));
	auto const name = QFileInfo(path).fileName();
	
	QByteArray bytes;
	auto u = 0.0;
	auto v = 0.0;
	auto x = 0.0;
	auto y = 0.0;
	auto rotated = false;
	auto vector_object = false;
	while (parameters.readNext())
	{
		auto const value = parameters.value();
		switch (parameters.key())
		{
		case 'n':
			// The entry of a layout line, area or text: n is its place in the object index.
			// The object itself is imported with the objects.
			vector_object = true;
			break;
		case 'F':
			bytes = QByteArray::fromBase64(value.toLatin1());
			break;
		case 'u':
			u = value.toDouble();
			break;
		case 'v':
			v = value.toDouble();
			break;
		case 'x':
			x = value.toDouble();
			break;
		case 'y':
			y = value.toDouble();
			break;
		case 'a':
		case 'b':
			rotated = rotated || !qIsNull(value.toDouble());
			break;
		default:
			;  // r, s: not decoded
		}
	}
	
	if (vector_object && bytes.isEmpty())
		return {};

	auto image = std::make_unique<ImageObject>();
	image->setData(bytes, name);
	auto const pixels = image->image().size();
	if (bytes.isEmpty() || image->format().isEmpty() || pixels.isEmpty() || u <= 0 || v <= 0)
	{
		setWarning(warning, tr("The embedded image %1 cannot be read.").arg(name));
		return {};
	}
	
	image->setCorners(MapCoordF(x, -y), QSizeF(pixels.width() * u, pixels.height() * v));
	if (rotated)
		setWarning(warning, tr("The rotation of the embedded image %1 is not imported.").arg(name));
	return image;
}

QString ocdLayoutImageToString(const ImageObject& image, const MapCoord& area_offset, QString* warning)
{
	auto name = image.href();
	if (image.isLinked())
	{
		setWarning(warning, tr("The linked image %1 is not saved: only embedded images can be saved in an OCD file.").arg(QFileInfo(image.linkedFile()).fileName()));
		return {};
	}
	auto const pixels = image.image().size();
	if (pixels.isEmpty() || image.data().isEmpty())
	{
		setWarning(warning, tr("The image %1 cannot be read and is not saved.").arg(name));
		return {};
	}
	if (name.isEmpty())
		name = QLatin1String("image.") + (image.format() == QLatin1String("jpeg") ? QLatin1String("jpg") : QLatin1String("png"));
	
	// Upright and rectangular: the top side points to the right, the left side down.
	auto const quad = image.quad();
	auto const top = quad[1] - quad[0];
	auto const left = quad[3] - quad[0];
	auto const size = image.size();
	if (qAbs(top.y()) > 0.0005 || qAbs(left.x()) > 0.0005 || top.x() < 0 || left.y() < 0)
		setWarning(warning, tr("The image %1 is saved upright: an OCD file cannot hold its rotation.").arg(name));
	
	auto const centre = QPointF(image.anchor()) - QPointF(MapCoordF(area_offset));
	return name
	       + QLatin1String("\tF") + QString::fromLatin1(image.data().toBase64())
	       + QLatin1String("\tr1\ts1")
	       + QLatin1String("\tu") + QString::number(size.width() / pixels.width(), 'f', 10)
	       + QLatin1String("\tv") + QString::number(size.height() / pixels.height(), 'f', 10)
	       + QLatin1String("\tx") + QString::number(centre.x(), 'f', 6)
	       + QLatin1String("\ty") + QString::number(-centre.y(), 'f', 6);
}


}  // namespace OpenOrienteering
