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


#include "course_canvas.h"

#include <algorithm>
#include <cmath>
#include <iterator>

#include <QtGlobal>
#include <QChar>
#include <QLatin1Char>
#include <QLatin1String>
#include <QRectF>
#include <QStringRef>
#include <QTextCodec>
#include <QXmlStreamAttributes>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

#include "core/extents.h"

namespace OpenOrienteering {

namespace {

/// The units of a named extent in an OCD file, per mm.
constexpr double extent_units = 25600.0;

/// The name of the entry of all controls in the courses or the classes of a canvas.
const QLatin1String all_controls("All controls");

QString boolText(bool value)
{
	return value ? QStringLiteral("true") : QStringLiteral("false");
}

/// The texts of the child elements with the name; with an attribute "visible" of false they go to hidden.
void readNames(QXmlStreamReader& xml, const QLatin1String& child, QStringList* all, QStringList* hidden)
{
	while (xml.readNextStartElement())
	{
		if (xml.name() != child)
		{
			xml.skipCurrentElement();
			continue;
		}
		auto const visible = xml.attributes().value(QLatin1String("visible")) != QLatin1String("false");
		auto const name = xml.readElementText();
		if (all)
			all->push_back(name);
		if (hidden && !visible)
			hidden->push_back(name);
	}
}

CourseCanvas readOcdCanvas(QXmlStreamReader& xml)
{
	CourseCanvas canvas;
	while (xml.readNextStartElement())
	{
		auto const name = xml.name();
		if (name == QLatin1String("Name"))
		{
			canvas.name = xml.readElementText();
		}
		else if (name == QLatin1String("ExportRectangleName"))
		{
			auto const status = xml.attributes().value(QLatin1String("pageStatus")).toString();
			if (!status.isEmpty())
				canvas.page_status = status;
			canvas.extent = xml.readElementText();
		}
		else if (name == QLatin1String("ExportScale"))
		{
			canvas.scale = xml.readElementText().toUInt();
		}
		else if (name == QLatin1String("CdSymbolsSize"))
		{
			canvas.box_size = xml.readElementText().toDouble();
		}
		else if (name == QLatin1String("CdMaxLength"))
		{
			canvas.max_rows = xml.readElementText().toInt();
		}
		else if (name == QLatin1String("BackgroundMaps"))
		{
			readNames(xml, QLatin1String("BackgroundMap"), nullptr, &canvas.hidden_templates);
		}
		else if (name == QLatin1String("Symbols"))
		{
			readNames(xml, QLatin1String("Symbol"), nullptr, &canvas.hidden_symbols);
		}
		else if (name == QLatin1String("Courses") || name == QLatin1String("Classes"))
		{
			// By courses or by classes; a class has no part.
			auto const child = QLatin1String(name == QLatin1String("Courses") ? "Course" : "Class");
			while (xml.readNextStartElement())
			{
				if (xml.name() != child)
				{
					xml.skipCurrentElement();
					continue;
				}
				CanvasCourse course;
				auto const attributes = xml.attributes();
				auto const number = attributes.value(QLatin1String("mapNumber")).toInt();
				if (number > 0)
				{
					course.part = number - 1;
					course.flip = attributes.value(QLatin1String("courseType")) == QLatin1String("MapFlip");
				}
				course.course = xml.readElementText();
				if (course.course == all_controls)
					course.course.clear();
				canvas.courses.push_back(course);
			}
		}
		else
		{
			// The rectangle of the extent, which the map has by its name, and what is not known.
			xml.skipCurrentElement();
		}
	}
	return canvas;
}

void append(QString& text, const char* element, const QString& content, const QString& attributes = {})
{
	text += QLatin1Char('<') + QLatin1String(element) + attributes + QLatin1Char('>')
	        + content.toHtmlEscaped()
	        + QLatin1String("</") + QLatin1String(element) + QLatin1String(">\r\n");
}

}  // namespace



bool operator==(const CanvasCourse& lhs, const CanvasCourse& rhs)
{
	return lhs.course == rhs.course && lhs.part == rhs.part && lhs.flip == rhs.flip;
}

bool operator==(const CourseCanvas& lhs, const CourseCanvas& rhs)
{
	return lhs.name == rhs.name
	       && lhs.extent == rhs.extent
	       && lhs.page_status == rhs.page_status
	       && lhs.scale == rhs.scale
	       && qAbs(lhs.box_size - rhs.box_size) < 0.0005
	       && lhs.max_rows == rhs.max_rows
	       && lhs.hidden_symbols == rhs.hidden_symbols
	       && lhs.hidden_templates == rhs.hidden_templates
	       && lhs.courses == rhs.courses;
}

bool operator==(const CourseCanvases& lhs, const CourseCanvases& rhs)
{
	return lhs.active == rhs.active
	       && lhs.by_classes == rhs.by_classes
	       && lhs.used_symbols == rhs.used_symbols
	       && lhs.used_templates == rhs.used_templates
	       && lhs.canvases == rhs.canvases;
}



const CourseCanvas* canvasFor(const CourseCanvases& canvases, const QString& course, int part)
{
	const CourseCanvas* of_course = nullptr;
	for (auto const& canvas : canvases.canvases)
	{
		for (auto const& entry : canvas.courses)
		{
			if (entry.course != course)
				continue;
			if (entry.part == part)
				return &canvas;
			if (entry.part < 0 && !of_course)
				of_course = &canvas;
		}
	}
	return of_course;
}


QString canvasSymbolNumber(int first, int second)
{
	return QStringLiteral("%1.%2").arg(first).arg(std::max(second, 0), 3, 10, QLatin1Char('0'));
}



void saveCanvases(const CourseCanvases& canvases, QXmlStreamWriter& xml)
{
	xml.writeStartElement(QStringLiteral("canvases"));
	if (canvases.active)
		xml.writeAttribute(QStringLiteral("active"), QStringLiteral("true"));
	if (canvases.by_classes)
		xml.writeAttribute(QStringLiteral("by_classes"), QStringLiteral("true"));
	for (auto const& number : canvases.used_symbols)
	{
		xml.writeEmptyElement(QStringLiteral("used_symbol"));
		xml.writeAttribute(QStringLiteral("number"), number);
	}
	for (auto const& name : canvases.used_templates)
	{
		xml.writeEmptyElement(QStringLiteral("used_template"));
		xml.writeAttribute(QStringLiteral("name"), name);
	}
	for (auto const& canvas : canvases.canvases)
	{
		xml.writeStartElement(QStringLiteral("canvas"));
		xml.writeAttribute(QStringLiteral("name"), canvas.name);
		if (!canvas.extent.isEmpty())
			xml.writeAttribute(QStringLiteral("extent"), canvas.extent);
		if (canvas.page_status != QLatin1String("psSingle"))
			xml.writeAttribute(QStringLiteral("page_status"), canvas.page_status);
		if (canvas.scale > 0)
			xml.writeAttribute(QStringLiteral("scale"), QString::number(canvas.scale));
		if (canvas.box_size > 0.0)
			xml.writeAttribute(QStringLiteral("box"), QString::number(canvas.box_size, 'f', 3));
		if (canvas.max_rows > 0)
			xml.writeAttribute(QStringLiteral("rows"), QString::number(canvas.max_rows));
		for (auto const& number : canvas.hidden_symbols)
		{
			xml.writeEmptyElement(QStringLiteral("hidden_symbol"));
			xml.writeAttribute(QStringLiteral("number"), number);
		}
		for (auto const& name : canvas.hidden_templates)
		{
			xml.writeEmptyElement(QStringLiteral("hidden_template"));
			xml.writeAttribute(QStringLiteral("name"), name);
		}
		for (auto const& course : canvas.courses)
		{
			xml.writeEmptyElement(QStringLiteral("course"));
			xml.writeAttribute(QStringLiteral("name"), course.course);
			if (course.part >= 0)
			{
				xml.writeAttribute(QStringLiteral("part"), QString::number(course.part));
				if (course.flip)
					xml.writeAttribute(QStringLiteral("flip"), QStringLiteral("true"));
			}
		}
		xml.writeEndElement();
	}
	xml.writeEndElement();
}


CourseCanvases loadCanvases(QXmlStreamReader& xml)
{
	CourseCanvases canvases;
	auto const isTrue = [](const QStringRef& text) { return text == QLatin1String("true"); };
	canvases.active = isTrue(xml.attributes().value(QLatin1String("active")));
	canvases.by_classes = isTrue(xml.attributes().value(QLatin1String("by_classes")));
	while (xml.readNextStartElement())
	{
		auto const attributes = xml.attributes();
		if (xml.name() == QLatin1String("used_symbol"))
		{
			canvases.used_symbols.push_back(attributes.value(QLatin1String("number")).toString());
			xml.skipCurrentElement();
		}
		else if (xml.name() == QLatin1String("used_template"))
		{
			canvases.used_templates.push_back(attributes.value(QLatin1String("name")).toString());
			xml.skipCurrentElement();
		}
		else if (xml.name() == QLatin1String("canvas"))
		{
			CourseCanvas canvas;
			canvas.name = attributes.value(QLatin1String("name")).toString();
			canvas.extent = attributes.value(QLatin1String("extent")).toString();
			if (attributes.hasAttribute(QLatin1String("page_status")))
				canvas.page_status = attributes.value(QLatin1String("page_status")).toString();
			canvas.scale = attributes.value(QLatin1String("scale")).toUInt();
			canvas.box_size = attributes.value(QLatin1String("box")).toDouble();
			canvas.max_rows = attributes.value(QLatin1String("rows")).toInt();
			while (xml.readNextStartElement())
			{
				auto const inner = xml.attributes();
				if (xml.name() == QLatin1String("hidden_symbol"))
				{
					canvas.hidden_symbols.push_back(inner.value(QLatin1String("number")).toString());
				}
				else if (xml.name() == QLatin1String("hidden_template"))
				{
					canvas.hidden_templates.push_back(inner.value(QLatin1String("name")).toString());
				}
				else if (xml.name() == QLatin1String("course"))
				{
					CanvasCourse course;
					course.course = inner.value(QLatin1String("name")).toString();
					if (inner.hasAttribute(QLatin1String("part")))
					{
						course.part = inner.value(QLatin1String("part")).toInt();
						course.flip = isTrue(inner.value(QLatin1String("flip")));
					}
					canvas.courses.push_back(course);
				}
				xml.skipCurrentElement();
			}
			canvases.canvases.push_back(canvas);
		}
		else
		{
			xml.skipCurrentElement();
		}
	}
	return canvases;
}



bool canvasesFromOcdXml(const QByteArray& bytes, CourseCanvases& canvases)
{
	auto const end = bytes.indexOf('\0');
	auto const size = end < 0 ? bytes.size() : end;
	QTextCodec::ConverterState state;
	auto text = QTextCodec::codecForName("UTF-8")->toUnicode(bytes.constData(), size, &state);
	if (state.invalidChars > 0 || state.remainingChars > 0)
	{
		auto* codec = QTextCodec::codecForName("Windows-1252");
		text = codec ? codec->toUnicode(bytes.constData(), size) : QString::fromLatin1(bytes.constData(), size);
	}
	
	QXmlStreamReader xml(text);
	while (!xml.atEnd())
	{
		xml.readNext();
		if (!xml.isStartElement() || xml.name() != QLatin1String("CsCanvas"))
			continue;
		CourseCanvases result;
		while (xml.readNextStartElement())
		{
			auto const name = xml.name();
			if (name == QLatin1String("Activate"))
				result.active = xml.readElementText() == QLatin1String("true");
			else if (name == QLatin1String("IsCourses"))
				result.by_classes = xml.readElementText() != QLatin1String("true");
			else if (name == QLatin1String("UsedSymbols"))
				readNames(xml, QLatin1String("Symbol"), &result.used_symbols, nullptr);
			else if (name == QLatin1String("UsedBackgroundMaps"))
				readNames(xml, QLatin1String("BackgroundMap"), &result.used_templates, nullptr);
			else if (name == QLatin1String("Canvas"))
				result.canvases.push_back(readOcdCanvas(xml));
			else
				xml.skipCurrentElement();  // the export folder, a path of another machine
		}
		canvases = result;
		return true;
	}
	return false;
}


QString canvasesToOcdXml(const CourseCanvases& canvases)
{
	auto text = QStringLiteral("<?xml version=\"1.0\"?>\r\n<OcadScript>\r\n<CsCanvas>\r\n");
	append(text, "ExportFolder", {});
	append(text, "Activate", boolText(canvases.active));
	append(text, "IsCourses", boolText(!canvases.by_classes));
	text += QLatin1String("<UsedSymbols>\r\n");
	for (auto const& number : canvases.used_symbols)
		append(text, "Symbol", number);
	text += QLatin1String("</UsedSymbols>\r\n<UsedBackgroundMaps>\r\n");
	for (auto const& name : canvases.used_templates)
		append(text, "BackgroundMap", name);
	text += QLatin1String("</UsedBackgroundMaps>\r\n");
	for (auto const& canvas : canvases.canvases)
	{
		text += QLatin1String("<Canvas>\r\n");
		append(text, "Name", canvas.name);
		append(text, "ExportRectangleName", canvas.extent, QLatin1String(" pageStatus=\"") + canvas.page_status.toHtmlEscaped() + QLatin1Char('"'));
		append(text, "ExportScale", QString::number(canvas.scale));
		append(text, "CdSymbolsSize", QString::number(canvas.box_size, 'f', 1));
		append(text, "CdMaxLength", QString::number(canvas.max_rows));
		text += QLatin1String("<BackgroundMaps>\r\n");
		for (auto const& name : canvases.used_templates)
			append(text, "BackgroundMap", name, QLatin1String(" visible=\"") + boolText(!canvas.hidden_templates.contains(name)) + QLatin1Char('"'));
		text += QLatin1String("</BackgroundMaps>\r\n<Symbols>\r\n");
		for (auto const& number : canvases.used_symbols)
			append(text, "Symbol", number, QLatin1String(" visible=\"") + boolText(!canvas.hidden_symbols.contains(number)) + QLatin1Char('"'));
		text += QLatin1String(canvases.by_classes ? "</Symbols>\r\n<Classes>\r\n" : "</Symbols>\r\n<Courses>\r\n");
		for (auto const& course : canvas.courses)
		{
			QString attributes;
			if (course.part >= 0 && !canvases.by_classes)
			{
				attributes = QLatin1String(" courseType=\"") + QLatin1String(course.flip ? "MapFlip" : "MapExchange")
				             + QLatin1String("\" mapNumber=\"") + QString::number(course.part + 1) + QLatin1Char('"');
			}
			auto const entry = course.course.isEmpty() ? QString(all_controls) : course.course;
			append(text, canvases.by_classes ? "Class" : "Course", entry, attributes);
		}
		text += QLatin1String(canvases.by_classes ? "</Classes>\r\n</Canvas>\r\n" : "</Courses>\r\n</Canvas>\r\n");
	}
	text += QLatin1String("</CsCanvas>\r\n</OcadScript>\r\n");
	return text;
}


bool extentFromOcdRecord(const QString& record, Extent& extent)
{
	auto const fields = record.split(QLatin1Char('\t'));
	if (fields.isEmpty() || fields.front().isEmpty())
		return false;
	double left = 0.0;
	double bottom = 0.0;
	double right = 0.0;
	double top = 0.0;
	auto found = 0;
	for (int i = 1; i < fields.size(); ++i)
	{
		if (fields[i].isEmpty())
			continue;
		auto ok = false;
		auto const value = fields[i].mid(1).toDouble(&ok) / extent_units;
		if (!ok)
			continue;
		switch (fields[i].at(0).unicode())
		{
		case 'l':  left = value; ++found; break;
		case 'b':  bottom = value; ++found; break;
		case 'r':  right = value; ++found; break;
		case 't':  top = value; ++found; break;
		default:   break;
		}
	}
	if (found != 4 || right <= left || top <= bottom)
		return false;
	extent = {};
	extent.name = fields.front();
	// The y axis of the file points up.
	extent.rect_mm = QRectF(left, -top, right - left, top - bottom);
	return true;
}


QString extentToOcdRecord(const Extent& extent)
{
	auto const units = [](double mm) { return QString::number(qRound64(mm * extent_units)); };
	auto const& rect = extent.rect_mm;
	return extent.name
	       + QLatin1String("\tl") + units(rect.left())
	       + QLatin1String("\tb") + units(-rect.bottom())
	       + QLatin1String("\tr") + units(rect.right())
	       + QLatin1String("\tt") + units(-rect.top());
}


}  // namespace OpenOrienteering
