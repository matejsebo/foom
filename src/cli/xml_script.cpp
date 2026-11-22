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


#include "xml_script.h"

#include <map>
#include <utility>

#include <QJsonValue>
#include <QLatin1String>
#include <QStringList>
#include <QTextCodec>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

#include "job.h"


namespace OpenOrienteering {

namespace Cli {

namespace {

/// The text of a script: UTF-8, or else Windows-1252.
QString decode(const QByteArray& bytes)
{
	QTextCodec::ConverterState state;
	auto text = QTextCodec::codecForName("UTF-8")->toUnicode(bytes.constData(), bytes.size(), &state);
	if (state.invalidChars > 0 || state.remainingChars > 0)
	{
		auto* codec = QTextCodec::codecForName("Windows-1252");
		text = codec ? codec->toUnicode(bytes) : QString::fromLatin1(bytes);
	}
	return text;
}

/// The element at the reader, with everything in it, as XML text. The reader ends at its end.
QString elementXml(QXmlStreamReader& xml)
{
	QString text;
	QXmlStreamWriter writer(&text);
	auto depth = 0;
	do
	{
		if (xml.isStartElement())
			++depth;
		writer.writeCurrentToken(xml);
		if (xml.isEndElement() && --depth == 0)
			break;
	}
	while (!xml.atEnd() && (xml.readNext(), true));
	return text;
}

/// The parameters of a command: the text of its child elements, by name, in their order.
using Fields = std::map<QString, QStringList>;

Fields readFields(QXmlStreamReader& xml)
{
	Fields fields;
	while (xml.readNextStartElement())
	{
		auto const name = xml.name().toString();
		fields[name].push_back(xml.readElementText(QXmlStreamReader::IncludeChildElements).trimmed());
	}
	return fields;
}


/** Turns one command into steps. */
class Translator
{
public:
	Translator(XmlScript& script, const QString& command, int line, const Fields& fields)
	: script(script), command(command), line(line), fields(fields)
	{}

	/// Records the command as unsupported; returns false.
	bool unsupported(const QString& reason)
	{
		script.unsupported.push_back({ command, line, reason });
		return false;
	}

	/// Every field must be one of these; else the command is unsupported.
	bool only(std::initializer_list<const char*> names)
	{
		for (auto const& field : fields)
		{
			auto known = false;
			for (auto const* name : names)
				known = known || field.first == QLatin1String(name);
			if (!known)
				return unsupported(field.first);
		}
		return true;
	}

	QString text(const char* name) const
	{
		auto const found = fields.find(QLatin1String(name));
		return found == fields.end() || found->second.isEmpty() ? QString{} : found->second.front();
	}

	QStringList texts(const char* name) const
	{
		auto const found = fields.find(QLatin1String(name));
		return found == fields.end() ? QStringList{} : found->second;
	}

	bool has(const char* name) const { return fields.count(QLatin1String(name)) > 0; }

	/// The field as a number; records the command as unsupported when it is none.
	bool number(const char* name, double* value)
	{
		auto ok = false;
		*value = text(name).toDouble(&ok);
		if (!ok)
			unsupported(QString::fromLatin1("%1: not a number").arg(QLatin1String(name)));
		return ok;
	}

	bool requireFile()
	{
		if (text("FileName").isEmpty())
			return unsupported(QLatin1String("FileName missing"));
		return true;
	}

	void add(const char* step, QJsonObject params)
	{
		params.insert(QLatin1String("step"), QLatin1String(step));
		script.steps.append(params);
	}

	XmlScript& script;
	QString command;
	int line;
	const Fields& fields;
};


/// The raster formats by command; empty: by the extension of the file.
const std::map<QString, QString>& rasterCommands()
{
	static const std::map<QString, QString> commands = {
	    { QStringLiteral("ExportRaster"), QString{} },
	    { QStringLiteral("ExportBmp"), QStringLiteral("bmp") },
	    { QStringLiteral("ExportGif"), QStringLiteral("gif") },
	    { QStringLiteral("ExportJpg"), QStringLiteral("jpg") },
	    { QStringLiteral("ExportPng"), QStringLiteral("png") },
	    { QStringLiteral("ExportTiff"), QStringLiteral("tif") },
	};
	return commands;
}

}  // namespace



QJsonObject XmlScript::report(const QString& path) const
{
	QJsonArray list;
	for (auto const& entry : unsupported)
	{
		list.append(QJsonObject{
		    { QStringLiteral("command"), entry.command },
		    { QStringLiteral("line"), entry.line },
		    { QStringLiteral("reason"), entry.reason },
		});
	}
	return QJsonObject{
	    { QStringLiteral("script"), path },
	    { QStringLiteral("commands"), commands },
	    { QStringLiteral("steps"), steps.size() },
	    { QStringLiteral("unsupported"), list },
	};
}


bool isXmlScript(const QByteArray& bytes)
{
	auto text = bytes.trimmed();
	if (text.startsWith("\xEF\xBB\xBF"))
		text = text.mid(3).trimmed();
	if (text.startsWith("<?xml"))
	{
		auto const end = text.indexOf("?>");
		if (end < 0)
			return false;
		text = text.mid(end + 2).trimmed();
	}
	return text.startsWith("<OcadScript");
}


XmlScript readXmlScript(const QByteArray& bytes)
{
	XmlScript script;
	QXmlStreamReader xml(decode(bytes));
	if (!xml.readNextStartElement() || xml.name() != QLatin1String("OcadScript"))
		throw JobError(QLatin1String("the file is no XML script: its root is not OcadScript"));

	QString opened;  // the file of the last FileOpen
	while (xml.readNextStartElement())
	{
		++script.commands;
		auto const command = xml.name().toString();
		auto const line = int(xml.lineNumber());

		if (command == QLatin1String("CsCanvas"))
		{
			// The canvases of the course setting, with their parameters as the format has them.
			auto const text = elementXml(xml);
			script.steps.append(QJsonObject{ { QStringLiteral("step"), QStringLiteral("load-canvases") }, { QStringLiteral("xml"), text } });
			continue;
		}

		auto const fields = readFields(xml);
		Translator t(script, command, line, fields);

		if (command == QLatin1String("FileOpen"))
		{
			if (t.only({ "FileName" }) && t.requireFile())
			{
				opened = t.text("FileName");
				t.add("open", { { QStringLiteral("path"), opened } });
			}
		}
		else if (command == QLatin1String("FileSave"))
		{
			if (t.only({}))
			{
				if (opened.isEmpty())
					throw JobError(QString::fromLatin1("line %1: FileSave before any FileOpen").arg(line));
				t.add("save-as", { { QStringLiteral("path"), opened } });
			}
		}
		else if (command == QLatin1String("FileSaveAs"))
		{
			if (t.only({ "FileName", "Version" }) && t.requireFile())
			{
				QJsonObject params { { QStringLiteral("path"), t.text("FileName") } };
				auto version = 0.0;
				auto ok = true;
				if (t.has("Version"))
				{
					ok = t.number("Version", &version);
					if (ok && version >= 8 && version <= 12)
						params.insert(QStringLiteral("version"), int(version));
					else if (ok && int(version) != 2018)
						ok = t.unsupported(QLatin1String("Version"));
				}
				if (ok)
					t.add("save-as", params);
			}
		}
		else if (command == QLatin1String("FileClose") || command == QLatin1String("FileExit"))
		{
			t.only({});
		}
		else if (command == QLatin1String("MapChangeScale"))
		{
			auto scale = 0.0;
			if (t.only({ "Scale" }) && t.number("Scale", &scale))
				t.add("change-scale", { { QStringLiteral("scale"), int(scale) } });
		}
		else if (command == QLatin1String("Import"))
		{
			if (t.only({ "FileName", "CrtFile" }) && t.requireFile())
			{
				QJsonObject params { { QStringLiteral("path"), t.text("FileName") } };
				if (!t.text("CrtFile").isEmpty())
					params.insert(QStringLiteral("crt"), t.text("CrtFile"));
				t.add("import", params);
			}
		}
		else if (command == QLatin1String("BackgroundMapOpen"))
		{
			if (t.only({ "FileName" }) && t.requireFile())
				t.add("add-template", { { QStringLiteral("path"), t.text("FileName") } });
		}
		else if (command == QLatin1String("BackgroundMapRemove"))
		{
			if (t.only({ "FileName" }) && t.requireFile())
				t.add("remove-template", { { QStringLiteral("template"), t.text("FileName") } });
		}
		else if (command == QLatin1String("ExportPdf"))
		{
			auto scale = 0.0;
			if (t.only({ "FileName", "Scale" }) && t.requireFile() && (!t.has("Scale") || t.number("Scale", &scale)))
			{
				QJsonObject params { { QStringLiteral("path"), t.text("FileName") }, { QStringLiteral("format"), QStringLiteral("pdf") } };
				if (scale > 0)
					params.insert(QStringLiteral("scale"), int(scale));
				t.add("export", params);
			}
		}
		else if (rasterCommands().count(command))
		{
			auto const format = rasterCommands().at(command);
			auto scale = 0.0;
			auto dpi = 0.0;
			if (format == QLatin1String("gif"))
				t.unsupported(QLatin1String("GIF is not written by FOOM"));
			else if (t.only({ "FileName", "Resolution", "Scale" }) && t.requireFile()
			         && (!t.has("Resolution") || t.number("Resolution", &dpi))
			         && (!t.has("Scale") || t.number("Scale", &scale)))
			{
				QJsonObject params { { QStringLiteral("path"), t.text("FileName") } };
				if (!format.isEmpty())
					params.insert(QStringLiteral("format"), format);
				if (dpi > 0)
					params.insert(QStringLiteral("dpi"), int(dpi));
				if (scale > 0)
					params.insert(QStringLiteral("scale"), int(scale));
				t.add("export", params);
			}
		}
		else if (command == QLatin1String("LidarImport"))
		{
			auto cell = 0.0;
			if (t.only({ "FileName", "CellSize" }) && t.requireFile() && (!t.has("CellSize") || t.number("CellSize", &cell)))
			{
				QJsonObject params { { QStringLiteral("files"), QJsonArray::fromStringList(t.texts("FileName")) } };
				if (cell > 0)
					params.insert(QStringLiteral("cell"), cell);
				t.add("point-cloud", params);
			}
		}
		else
		{
			t.unsupported(QLatin1String("unknown command"));
		}
	}
	if (xml.hasError())
		throw JobError(QString::fromLatin1("the script is not well formed: line %1: %2").arg(QString::number(xml.lineNumber()), xml.errorString()));
	return script;
}


}  // namespace Cli

}  // namespace OpenOrienteering
