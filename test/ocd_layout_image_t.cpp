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


#include <cmath>
#include <cstddef>
#include <memory>
#include <vector>

#include <QtGlobal>
#include <QtMath>
#include <QtTest>
#include <QBuffer>
#include <QByteArray>
#include <QColor>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QIODevice>
#include <QLatin1String>
#include <QObject>
#include <QPointF>
#include <QSizeF>
#include <QString>
#include <QStringList>

#include "global.h"
#include "core/foreign_data.h"
#include "core/map.h"
#include "core/map_coord.h"
#include "core/map_part.h"
#include "core/objects/image_object.h"
#include "core/objects/object.h"
#include "fileformats/file_format.h"
#include "fileformats/file_format_registry.h"
#include "fileformats/file_import_export.h"
#include "fileformats/ocd_file_format.h"
#include "fileformats/ocd_layout_image.h"

using namespace OpenOrienteering;


namespace {

QByteArray imageBytes(int width, int height, const char* format)
{
	QImage image(width, height, QImage::Format_RGB32);
	image.fill(QColor(200, 30, 30));
	QBuffer buffer;
	buffer.open(QIODevice::WriteOnly);
	image.save(&buffer, format);
	return buffer.data();
}

QString imageString(const QString& name, const QByteArray& bytes, const QString& rest)
{
	QString string = name;
	string += QLatin1String("\tF");
	string += QString::fromLatin1(bytes.toBase64());
	string += rest;
	return string;
}

}  // namespace



/**
 * Tests the embedded images of OCD files.
 *
 * See the design notes, "OCD embedded images".
 */
class OcdLayoutImageTest : public QObject
{
Q_OBJECT
private slots:
	void initTestCase()
	{
		doStaticInitializations();
	}
	
	/// Accept (1)
	void parseTest()
	{
		auto const png = imageBytes(20, 10, "PNG");
		QString warning;
		auto image = ocdLayoutImageFromString(imageString(QStringLiteral("C:\\pictures\\logo.png"), png, QStringLiteral("\tr1\ts1\tu0.500000\tv0.500000\tx100.000000\ty50.000000")), &warning);
		QVERIFY(image);
		QVERIFY2(warning.isEmpty(), qPrintable(warning));
		QCOMPARE(image->data(), png);
		QCOMPARE(image->href(), QStringLiteral("logo.png"));
		QVERIFY(!image->isLinked());
		QCOMPARE(image->size(), QSizeF(10, 5));
		QCOMPARE(QPointF(image->anchor()), QPointF(100, -50));
		QCOMPARE(image->imageRotation(), 0.0);
		auto const& corners = image->getRawCoordinateVector();
		QVERIFY(corners[0] == MapCoord(95.0, -52.5));
		QVERIFY(corners[2] == MapCoord(105.0, -47.5));
		
		auto const jpeg = imageBytes(16, 8, "JPEG");
		auto photo = ocdLayoutImageFromString(imageString(QStringLiteral("photo.jpg"), jpeg, QStringLiteral("\tr1\ts1\tu0.25\tv0.25\tx-10\ty-20")), &warning);
		QVERIFY(photo);
		QCOMPARE(photo->data(), jpeg);
		QCOMPARE(photo->format(), QStringLiteral("jpeg"));
		QCOMPARE(photo->size(), QSizeF(4, 2));
		QCOMPARE(QPointF(photo->anchor()), QPointF(-10, 20));
	}
	
	/// Accept (2)
	void refusalTest()
	{
		QString warning;
		auto broken = ocdLayoutImageFromString(imageString(QStringLiteral("broken.png"), QByteArray("not an image"), QStringLiteral("\tr1\ts1\tu0.5\tv0.5\tx0\ty0")), &warning);
		QVERIFY(!broken);
		QVERIFY(warning.contains(QLatin1String("broken.png")));
		
		warning.clear();
		auto const png = imageBytes(20, 10, "PNG");
		auto turned = ocdLayoutImageFromString(imageString(QStringLiteral("turned.png"), png, QStringLiteral("\tr1\ts1\tu0.5\tv0.5\tx0\ty0\ta30")), &warning);
		QVERIFY(turned);
		QCOMPARE(turned->imageRotation(), 0.0);
		QVERIFY(warning.contains(QLatin1String("turned.png")));
	}
	
	/// Accept (3)
	void writeTest()
	{
		auto const png = imageBytes(20, 10, "PNG");
		QString warning;
		auto image = ocdLayoutImageFromString(imageString(QStringLiteral("logo.png"), png, QStringLiteral("\tr1\ts1\tu0.5\tv0.5\tx100\ty50")), &warning);
		QVERIFY(image);
		
		auto const string = ocdLayoutImageToString(*image, MapCoord(0, 0), &warning);
		QVERIFY2(warning.isEmpty(), qPrintable(warning));
		auto const fields = string.split(QLatin1Char('\t'));
		QCOMPARE(fields.size(), 8);
		QCOMPARE(fields[0], QStringLiteral("logo.png"));
		QCOMPARE(fields[1], QString(QLatin1Char('F') + QString::fromLatin1(png.toBase64())));
		QCOMPARE(fields[2], QStringLiteral("r1"));
		QCOMPARE(fields[3], QStringLiteral("s1"));
		QCOMPARE(fields[4], QStringLiteral("u0.5000000000"));
		QCOMPARE(fields[5], QStringLiteral("v0.5000000000"));
		QCOMPARE(fields[6], QStringLiteral("x100.000000"));
		QCOMPARE(fields[7], QStringLiteral("y50.000000"));
		
		auto again = ocdLayoutImageFromString(string, &warning);
		QVERIFY(again);
		QVERIFY(again->equals(image.get(), true));
		
		ImageObject linked;
		linked.setLink(QStringLiteral("C:/nowhere/linked.png"));
		warning.clear();
		QVERIFY(ocdLayoutImageToString(linked, MapCoord(0, 0), &warning).isEmpty());
		QVERIFY(warning.contains(QLatin1String("linked.png")));
		
		image->setCorners(image->anchor(), image->size(), qDegreesToRadians(30.0));
		warning.clear();
		auto const upright = ocdLayoutImageToString(*image, MapCoord(0, 0), &warning);
		QVERIFY(!upright.isEmpty());
		QVERIFY(warning.contains(QLatin1String("logo.png")));
		auto restored = ocdLayoutImageFromString(upright, &warning);
		QVERIFY(restored);
		QCOMPARE(restored->size(), QSizeF(10, 5));
		QCOMPARE(QPointF(restored->anchor()), QPointF(100, -50));
	}
	
};


QTEST_MAIN(OcdLayoutImageTest)
#include "ocd_layout_image_t.moc"  // IWYU pragma: keep
