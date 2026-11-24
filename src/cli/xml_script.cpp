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

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <initializer_list>
#include <map>
#include <utility>
#include <vector>

#include <Qt>
#include <QtGlobal>
#include <QByteArray>
#include <QChar>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QLatin1Char>
#include <QLatin1String>
#include <QString>
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


/// An element of a command, with the elements in it.
struct Node
{
	QString name;
	QString text;                 ///< The text of the element and of those in it, trimmed.
	QString raw;                  ///< The same as it stands in the file.
	int line = 0;
	std::vector<Node> children;
	mutable bool used = false;    ///< A reader asked for it by its name.

	/// The first element of this name in the node; it then counts as known.
	const Node* child(const char* child_name) const
	{
		for (auto const& node : children)
		{
			if (node.name == QLatin1String(child_name))
			{
				node.used = true;
				return &node;
			}
		}
		return nullptr;
	}

	const Node* child(const QString& child_name) const { return child(child_name.toLatin1().constData()); }

	/// The node and all in it count as known.
	void useAll() const
	{
		used = true;
		for (auto const& node : children)
			node.useAll();
	}
};

/// The element at the reader. The reader ends at its end.
Node readNode(QXmlStreamReader& xml)
{
	Node node;
	node.name = xml.name().toString();
	node.line = int(xml.lineNumber());
	while (!xml.atEnd())
	{
		auto const token = xml.readNext();
		if (token == QXmlStreamReader::StartElement)
		{
			node.children.push_back(readNode(xml));
			node.raw += node.children.back().raw;
		}
		else if (token == QXmlStreamReader::Characters || token == QXmlStreamReader::EntityReference)
		{
			node.raw += xml.text();
		}
		else if (token == QXmlStreamReader::EndElement)
		{
			break;
		}
	}
	node.text = node.raw.trimmed();
	return node;
}


/// The parameters of a command: the text of its child elements, by name, in their order.
using Fields = std::map<QString, QStringList>;

Fields fieldsOf(const Node& command)
{
	Fields fields;
	for (auto const& node : command.children)
		fields[node.name].push_back(node.text);
	return fields;
}


/// Adds a step with the warnings of its command.
int appendStep(XmlScript& script, const char* step, QJsonObject params, const QStringList& warnings = {})
{
	params.insert(QLatin1String("step"), QLatin1String(step));
	script.steps.append(params);
	script.warnings.push_back(warnings);
	return script.steps.size() - 1;
}


/** Turns one command of FOOM's names of 2026-11-22 into steps. */
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

	void add(const char* step, const QJsonObject& params)
	{
		appendStep(script, step, params);
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


// ### The format's commands. the design notes, "The commands of the LiDAR script" ###

/// The script's rows of the classes of a model, and FOOM's names of them (PointChoice::groupId()).
const char* const class_rows[][2] = {
    { "Unclassified", "unclassified" },
    { "Ground", "ground" },
    { "LowVegetation", "low-vegetation" },
    { "MeanVegetation", "medium-vegetation" },
    { "HighVegetation", "high-vegetation" },
    { "Building", "building" },
    { "LowPoint", "low-point" },
    { "Water", "water" },
    { "OverlapPoints", "overlap" },
    { "Other", "other" },
};


/**
 * Reads the format's commands (File.Open, Dem.DemImport, ...) into steps.
 *
 * It keeps what a step cannot know of the steps before it: the file which
 * is open, and what the script has put into the folders it names.
 */
class ScriptCommands
{
public:
	ScriptCommands(XmlScript& script, const QDir& folder) : script(script), folder(folder) {}

	/// The file of the last File.Open or File.SaveAs; empty after File.Close.
	QString current;

	void read(const Node& command)
	{
		node = &command;
		warnings.clear();
		main_step = -1;
		note_index = -1;

		auto const* enabled = command.child("Enabled");
		if (enabled && enabled->text.compare(QLatin1String("false"), Qt::CaseInsensitive) == 0)
		{
			note(QLatin1String("Enabled is false: left out"));
			return;
		}

		auto const& name = command.name;
		auto is_file_command = true;
		if (name == QLatin1String("File.Open"))
			fileOpen();
		else if (name == QLatin1String("File.SaveAs"))
			fileSaveAs();
		else if (name == QLatin1String("File.Save"))
			fileSave();
		else if (name == QLatin1String("File.Close"))
			fileClose();
		else if (name == QLatin1String("File.Exit"))
			only({});
		else if (name == QLatin1String("File.Import.Ocd"))
			importOcd();
		else
			is_file_command = false;
		if (is_file_command)
		{
			finish();
			return;
		}

		if (name == QLatin1String("Dem.DemImport"))
			demImport();
		else if (name == QLatin1String("Dem.MatchTiles"))
			tiles("TileSize", "FOOM grids the tiles together: nothing to match");
		else if (name == QLatin1String("Dem.MergeTiles"))
			tiles("TileSize", "FOOM grids the tiles together: nothing to merge");
		else if (name == QLatin1String("Dem.CropTiles"))
			tiles("TileSize", "FOOM grids the tiles together: nothing to crop");
		else if (name == QLatin1String("Dem.MergeRasterMapTiles"))
			tiles("TileSizeMeter", "FOOM writes one raster: nothing to merge");
		else if (name == QLatin1String("Dem.CreateContourLines"))
			contourLines();
		else if (name == QLatin1String("Dem.CreateVegetationBaseMap"))
			vegetationBaseMap();
		else
		{
			unsupported(QLatin1String("unknown command"));
			return;
		}
		// What a Dem command has besides: named, and the command runs.
		unknownFields(command, {});
		finish();
	}

private:
	/// What Dem.DemImport put into a folder, for the later commands which name the folder.
	struct Dem
	{
		QStringList clouds;     ///< The point clouds, absolute.
		QJsonObject terrain;    ///< dtm_classes and dtm_returns of the import.
		QString grid;           ///< The terrain grid which FOOM writes; empty when the import makes none.
	};

	XmlScript& script;
	QDir folder;
	std::map<QString, Dem> dems;               ///< By folder.
	std::map<QString, QStringList> maps;       ///< By folder: the map files which the script writes, absolute.

	const Node* node = nullptr;
	QStringList warnings;
	int main_step = -1;
	int note_index = -1;


	// The command

	bool unsupported(const QString& reason)
	{
		script.unsupported.push_back({ node->name, node->line, reason });
		return false;
	}

	void note(const QString& text)
	{
		script.notes.push_back({ node->name, node->line, text });
		note_index = int(script.notes.size()) - 1;
	}

	int add(const char* step, const QJsonObject& params)
	{
		return appendStep(script, step, params);
	}

	/// The warnings go to the step of the command, or to its note.
	void finish()
	{
		if (warnings.isEmpty())
			return;
		if (main_step >= 0)
		{
			script.warnings[std::size_t(main_step)] += warnings;
		}
		else if (note_index >= 0)
		{
			auto& text = script.notes[std::size_t(note_index)].note;
			text += QLatin1String("; ") + warnings.join(QLatin1String("; "));
		}
	}

	/// A File command: every parameter must be one of these (and Enabled); else it is unsupported.
	bool only(std::initializer_list<const char*> names)
	{
		for (auto const& field : node->children)
		{
			auto known = field.name == QLatin1String("Enabled");
			for (auto const* name : names)
				known = known || field.name == QLatin1String(name);
			if (!known)
				return unsupported(field.name);
		}
		return true;
	}

	/// The parameters which no reader asked for, as warnings.
	void unknownFields(const Node& parent, const QString& prefix)
	{
		for (auto const& field : parent.children)
		{
			if (!field.used)
				warnings.push_back(prefix + field.name + QLatin1String(": not known to FOOM"));
			else
				unknownFields(field, prefix + field.name + QLatin1Char('/'));
		}
	}

	void needsFile() const
	{
		if (current.isEmpty())
			throw JobError(QString::fromLatin1("line %1: %2 while no file is open").arg(QString::number(node->line), node->name));
	}


	// Values

	static QString text(const Node* field) { return field ? field->text : QString{}; }

	void noCounterpart(const QString& name, const Node* field, const char* what)
	{
		warnings.push_back(name + QLatin1Char(' ') + text(field) + QLatin1String(": ") + QLatin1String(what));
	}

	/// true or false; anything else is a warning and the fallback.
	bool flag(const QString& name, const Node* field, bool fallback)
	{
		if (!field)
			return fallback;
		if (field->text.compare(QLatin1String("true"), Qt::CaseInsensitive) == 0)
			return true;
		if (field->text.compare(QLatin1String("false"), Qt::CaseInsensitive) == 0)
			return false;
		noCounterpart(name, field, "neither true nor false");
		return fallback;
	}

	/// A number from minimum to maximum; anything else is a warning, and false.
	bool number(const QString& name, const Node* field, double minimum, double maximum, double* value)
	{
		if (!field)
			return false;
		auto ok = false;
		auto const number = field->text.toDouble(&ok);
		if (!ok || !(number >= minimum && number <= maximum))
		{
			warnings.push_back(QString::fromLatin1("%1 %2: FOOM takes a number from %3 to %4 and keeps its own value")
			                   .arg(name, field->text).arg(minimum).arg(maximum));
			return false;
		}
		*value = number;
		return true;
	}

	bool number(const char* name, double minimum, double maximum, double* value)
	{
		return number(QLatin1String(name), node->child(name), minimum, maximum, value);
	}

	/// The number of a field which FOOM does not use, 0 when it has none.
	static double plainNumber(const Node* field)
	{
		return field ? field->text.toDouble() : 0.0;
	}


	// Paths

	static QString slashes(QString path)
	{
		return path.trimmed().replace(QLatin1Char('\\'), QLatin1Char('/'));
	}

	QString absolute(const QString& path) const
	{
		return QDir::cleanPath(folder.absoluteFilePath(slashes(path)));
	}

	/// What names a folder or a file whatever the case of its letters.
	QString key(const QString& path) const { return absolute(path).toLower(); }

	QString folderOf(const QString& file) const { return QFileInfo(absolute(file)).absolutePath(); }

	/// The files of the folder which match, absolute, in the order of their names.
	QStringList filesIn(const QString& dir, const QStringList& patterns) const
	{
		QDir const directory(absolute(dir));
		QStringList result;
		for (auto const& name : directory.entryList(patterns, QDir::Files, QDir::Name | QDir::IgnoreCase))
			result.push_back(QDir::cleanPath(directory.absoluteFilePath(name)));
		return result;
	}

	const Dem* demOf(const QString& dir) const
	{
		auto const found = dems.find(key(dir));
		return found == dems.end() ? nullptr : &found->second;
	}


	// The commands of the files

	void fileOpen()
	{
		if (!only({ "File", "IgnoreMissingBackgroundMaps", "IgnoreMissingDatasets" }))
			return;
		auto const file = slashes(text(node->child("File")));
		if (file.isEmpty())
		{
			unsupported(QLatin1String("File missing"));
			return;
		}
		current = file;
		add("open", { { QStringLiteral("path"), file } });
	}

	void fileSaveAs()
	{
		if (!only({ "File" }))
			return;
		auto const file = slashes(text(node->child("File")));
		if (file.isEmpty())
		{
			unsupported(QLatin1String("File missing"));
			return;
		}
		needsFile();
		// A script goes on in the saved file: the DEM import writes beside it, and File.Save writes it.
		add("save-as", { { QStringLiteral("path"), file }, { QStringLiteral("current"), true } });
		maps[key(folderOf(file))].push_back(absolute(file));
		current = file;
	}

	void fileSave()
	{
		if (!only({}))
			return;
		needsFile();
		add("save-as", { { QStringLiteral("path"), current } });
	}

	void fileClose()
	{
		if (only({}))
			current.clear();
	}

	void importOcd()
	{
		if (!only({ "File", "Directory", "SymbolOption", "ColorOption" }))
			return;
		needsFile();
		QStringList files;
		auto const file = slashes(text(node->child("File")));
		auto const directory = text(node->child("Directory"));
		if (!file.isEmpty())
		{
			files.push_back(file);
		}
		else if (!directory.isEmpty())
		{
			// What is there now, and what the script will have written by then.
			files = filesIn(directory, { QStringLiteral("*.ocd") });
			auto const written = maps.find(key(directory));
			if (written != maps.end())
			{
				for (auto const& map_file : written->second)
				{
					auto const known = std::any_of(files.cbegin(), files.cend(), [&map_file](const QString& other) {
						return other.compare(map_file, Qt::CaseInsensitive) == 0;
					});
					if (!known && map_file.endsWith(QLatin1String(".ocd"), Qt::CaseInsensitive))
						files.push_back(map_file);
				}
			}
			auto const open_file = key(current);
			files.erase(std::remove_if(files.begin(), files.end(), [&open_file](const QString& other) { return other.toLower() == open_file; }), files.end());
			std::sort(files.begin(), files.end(), [](const QString& a, const QString& b) { return a.compare(b, Qt::CaseInsensitive) < 0; });
			if (files.isEmpty())
			{
				unsupported(QLatin1String("Directory: no .ocd file in ") + absolute(directory));
				return;
			}
		}
		else
		{
			unsupported(QLatin1String("File or Directory missing"));
			return;
		}
		for (auto const* option : { "SymbolOption", "ColorOption" })
		{
			if (auto const* field = node->child(option))
				noCounterpart(QLatin1String(option), field, "what the number means is not known; the files are imported as the step import does");
		}
		for (auto const& path : qAsConst(files))
		{
			auto const index = add("import", { { QStringLiteral("path"), path } });
			if (main_step < 0)
				main_step = index;
		}
	}


	// The commands of the DEM

	/// The classes and returns of a model of Dem.DemImport, into the members of the step.
	void model(const Node& las, const char* name, const char* member, QJsonObject& params, QJsonArray& products)
	{
		QString const prefix = QString::fromLatin1(name) + QLatin1Char('/');
		if (auto const* enabled = las.child("Enabled"))
			params.insert(QLatin1String(member), flag(prefix + QLatin1String("Enabled"), enabled, true));
		if (auto const* classification = las.child("Classification"))
		{
			QJsonArray classes;
			for (auto const& row : class_rows)
			{
				QString const row_name = prefix + QLatin1String("Classification/") + QLatin1String(row[0]);
				if (flag(row_name, classification->child(row[0]), false))
					classes.append(QLatin1String(row[1]));
			}
			params.insert(QString::fromLatin1(member) + QLatin1String("_classes"), classes);
		}
		if (auto const* returns = las.child("ReturnType"))
		{
			// 1 is the last returns and 2 all returns. 0 follows from the order of the values.
			QString const member_name = QString::fromLatin1(member) + QLatin1String("_returns");
			if (returns->text == QLatin1String("1"))
				params.insert(member_name, QLatin1String("last"));
			else if (returns->text == QLatin1String("2"))
				params.insert(member_name, QLatin1String("all"));
			else if (returns->text == QLatin1String("0"))
			{
				params.insert(member_name, QLatin1String("first"));
				noCounterpart(prefix + QLatin1String("ReturnType"), returns, "taken as the first returns");
			}
			else
				noCounterpart(prefix + QLatin1String("ReturnType"), returns, "not a return type FOOM knows (1 last, 2 all, 0 first); FOOM's own choice is kept");
		}
		if (flag(prefix + QLatin1String("CreateIntensityMap"), las.child("CreateIntensityMap"), false)
		    && !products.contains(QLatin1String("intensity")))
			products.append(QLatin1String("intensity"));
		if (flag(prefix + QLatin1String("CreateClassificationMap"), las.child("CreateClassificationMap"), false)
		    && !products.contains(QLatin1String("classification")))
			products.append(QLatin1String("classification"));
	}

	void demImport()
	{
		needsFile();
		auto const* las = node->child("LasImport");
		auto const directory = text(las ? las->child("Directory") : nullptr);
		if (directory.isEmpty())
		{
			unsupported(QLatin1String("LasImport/Directory missing"));
			return;
		}
		Dem dem;
		dem.clouds = filesIn(directory, { QStringLiteral("*.las"), QStringLiteral("*.laz") });
		if (dem.clouds.isEmpty())
		{
			unsupported(QLatin1String("LasImport/Directory: no .las or .laz file in ") + absolute(directory));
			return;
		}
		if (auto const* ocd_las = node->child("CreateOcdLas"))
			ocd_las->useAll();  // FOOM reads the point clouds again when it needs them.

		QJsonObject params { { QStringLiteral("files"), QJsonArray::fromStringList(dem.clouds) } };
		QJsonArray products;
		auto const* create = node->child("CreateOcdDem");
		auto const grids = create && flag(QLatin1String("CreateOcdDem/Enabled"), create->child("Enabled"), true);
		if (create && !grids)
			create->useAll();
		if (grids)
		{
			if (auto const* size = create->child("Cellsize100"))
			{
				auto ok = false;
				auto const cell = size->text.toDouble(&ok) / 100.0;
				if (!ok || !(cell >= 0.1 && cell <= 100.0))
				{
					unsupported(QString::fromLatin1("CreateOcdDem/Cellsize100 %1: FOOM grids cells of 0.1 to 100 m").arg(size->text));
					return;
				}
				params.insert(QLatin1String("cell"), cell);
			}
			if (flag(QLatin1String("CreateOcdDem/CreateVegetationBaseMap"), create->child("CreateVegetationBaseMap"), false))
				products.append(QLatin1String("vegetation-map"));
			if (auto const* dtm = create->child("LasDtm"))
				model(*dtm, "LasDtm", "dtm", params, products);
			if (auto const* dsm = create->child("LasDsm"))
				model(*dsm, "LasDsm", "dsm", params, products);
		}
		params.insert(QLatin1String("products"), products);

		auto const makes_dtm = grids && params.value(QLatin1String("dtm")).toBool(true);
		auto const makes_dsm = grids && params.value(QLatin1String("dsm")).toBool(true);
		for (auto const* member : { "dtm_classes", "dtm_returns" })
		{
			if (params.contains(QLatin1String(member)))
				dem.terrain.insert(QLatin1String(member), params.value(QLatin1String(member)));
		}
		if (makes_dtm)
		{
			// The name of ElevationCommands::importPointClouds(): productPath() of the map.
			QFileInfo const map_file(absolute(current));
			dem.grid = QDir::cleanPath(map_file.absoluteDir().absoluteFilePath(map_file.completeBaseName() + QLatin1String("_DTM.tif")));
		}
		dems[key(folderOf(current))] = dem;

		if (!makes_dtm && !makes_dsm && products.isEmpty())
		{
			note(QLatin1String("no grid and no raster is asked for: nothing to do (FOOM reads the point clouds again when a later command needs them)"));
			return;
		}
		main_step = add("point-cloud", params);
	}

	/// Dem.MatchTiles, MergeTiles, CropTiles, MergeRasterMapTiles: the output folder holds what the input folder held.
	void tiles(const char* size_field, const char* what)
	{
		auto const input = text(node->child("InputFolder"));
		auto const output = text(node->child("OutputFolder"));
		node->child(size_field);
		if (!input.isEmpty() && !output.isEmpty())
		{
			if (auto const* dem = demOf(input))
				dems[key(output)] = Dem(*dem);
		}
		note(QLatin1String(what));
	}

	/// The terrain grids of a folder which the script did not fill.
	QStringList gridsIn(const QString& dir) const
	{
		auto files = filesIn(dir, { QStringLiteral("*_DTM.tif"), QStringLiteral("*_DTM.ocdDem") });
		if (!files.isEmpty())
			return files;
		for (auto const& file : filesIn(dir, { QStringLiteral("*.ocdDem") }))
		{
			auto const base = QFileInfo(file).completeBaseName();
			if (!base.endsWith(QLatin1String("_DSM"), Qt::CaseInsensitive) && !base.endsWith(QLatin1String("_Diff"), Qt::CaseInsensitive))
				files.push_back(file);
		}
		return files;
	}

	/// One set of contours of Dem.CreateContourLines; infix is empty or "Tpi". False when the command cannot run.
	bool contourSet(const char* infix, const char* made_field, bool smoothed, bool depressions, QJsonArray& sets)
	{
		auto const name = [infix](const char* before, const QString& after) {
			return QString(QString::fromLatin1(before) + QLatin1String(infix) + after);
		};
		auto const made = flag(QLatin1String(made_field), node->child(made_field), false);
		// A level: its interval and the numbers of its symbols as a map read from an OCD file shows them ("101.0").
		struct Level
		{
			double interval;
			QString symbol;
			QString depression_symbol;
		};
		auto const symbolText = [](const Node* field) {
			auto const number = qRound64(plainNumber(field));
			return number > 0 ? QString(QString::number(number / 1000) + QLatin1Char('.') + QString::number(number % 1000)) : QString();
		};
		std::vector<Level> levels;
		std::vector<double> intervals;
		for (int level = 1; level <= 3; ++level)
		{
			auto const level_name = QString::fromLatin1("Level%1").arg(level);
			auto const interval = plainNumber(node->child(name("Equidistance", level_name))) / 100.0;
			auto const symbol_name = name("SymbolNumber", level_name);
			auto const* symbol = node->child(symbol_name);
			auto const* depression_symbol = node->child(name("SymbolNumber", level_name + QLatin1String("Depr")));
			if (!(interval > 0.0))
				continue;
			intervals.push_back(interval);
			levels.push_back({ interval, symbolText(symbol), depressions ? symbolText(depression_symbol) : QString() });
			if (made && symbol && levels.back().symbol.isEmpty())
				noCounterpart(symbol_name, symbol, "no symbol: FOOM draws the lines of this level with its own contour symbol");
		}
		auto const original_name = name("Equidistance", QLatin1String("Original"));
		auto const* original = node->child(original_name);
		if (!made)
			return true;

		std::sort(intervals.begin(), intervals.end());
		intervals.erase(std::unique(intervals.begin(), intervals.end()), intervals.end());
		if (intervals.empty() || intervals.front() < 0.01 || intervals.front() > 1000.0 || intervals.back() > 10000.0)
			return unsupported(name("Equidistance", QLatin1String("Level1")) + QLatin1String(": the set needs intervals from 1 cm to 1,000 m, the largest up to 10,000 m"));
		if (original && std::abs(plainNumber(original) / 100.0 - intervals.front()) > 1e-9)
			noCounterpart(original_name, original, "its meaning is not known; the lines are made at the intervals of the levels");

		QJsonObject set {
		    { QStringLiteral("smoothed"), smoothed },
		    { QStringLiteral("interval"), intervals.front() },
		    { QStringLiteral("main_interval"), intervals.size() == 3 ? intervals[1] : intervals.front() },
		    { QStringLiteral("index_interval"), intervals.size() == 1 ? 10000.0 : intervals.back() },
		    { QStringLiteral("depressions"), depressions },
		};
		auto const* curves = node->child("ConvertTpiContoursToBezier");
		if (smoothed && curves)
			set.insert(QLatin1String("curves"), flag(QLatin1String("ConvertTpiContoursToBezier"), curves, true));

		// The symbols go with the intervals: the classes take the levels as the intervals above do.
		std::stable_sort(levels.begin(), levels.end(), [](const Level& a, const Level& b) { return a.interval < b.interval; });
		levels.erase(std::unique(levels.begin(), levels.end(), [](const Level& a, const Level& b) { return a.interval == b.interval; }), levels.end());
		auto const count = levels.size();
		const Level* const classes[3] = { &levels.front(), count == 3 ? &levels[1] : &levels.front(), &levels.back() };
		QJsonObject symbols;
		QJsonObject depression_symbols;
		auto cls = 0;
		for (auto const* member : { "smallest", "main", "index" })
		{
			if (!classes[cls]->symbol.isEmpty())
				symbols.insert(QLatin1String(member), classes[cls]->symbol);
			if (!classes[cls]->depression_symbol.isEmpty())
				depression_symbols.insert(QLatin1String(member), classes[cls]->depression_symbol);
			++cls;
		}
		if (!symbols.isEmpty())
			set.insert(QLatin1String("symbols"), symbols);
		if (!depression_symbols.isEmpty())
			set.insert(QLatin1String("depression_symbols"), depression_symbols);
		sets.append(set);
		return true;
	}

	void contourLines()
	{
		auto const template_file = slashes(text(node->child("TemplateFile")));
		auto const input = text(node->child("InputFolder"));
		auto const output = text(node->child("OutputFolder"));
		for (auto const* required : { "TemplateFile", "InputFolder", "OutputFolder" })
		{
			if (text(node->child(required)).isEmpty())
			{
				unsupported(QString::fromLatin1(required) + QLatin1String(" missing"));
				return;
			}
		}
		if (!current.isEmpty())
		{
			unsupported(QLatin1String("a file is open: FOOM makes the contours in a copy of TemplateFile (File.Close first)"));
			return;
		}

		QStringList grids;
		if (auto const* dem = demOf(input))
		{
			if (dem->grid.isEmpty())
			{
				unsupported(QLatin1String("InputFolder: the Dem.DemImport of this folder makes no terrain model"));
				return;
			}
			grids.push_back(dem->grid);
		}
		else
		{
			grids = gridsIn(input);
			if (grids.isEmpty())
			{
				unsupported(QLatin1String("InputFolder: no terrain grid of this script, and none in ") + absolute(input));
				return;
			}
		}

		auto const depressions = flag(QLatin1String("UseDifferentSymbolsForDepressions"), node->child("UseDifferentSymbolsForDepressions"), false);
		QJsonArray sets;
		if (!contourSet("", "CreateNotSmoothedContours", false, depressions, sets)
		    || !contourSet("Tpi", "CreateSmoothedContoursUsingTpi", true, depressions, sets))
			return;
		node->child("ConvertTpiContoursToBezier");

		// FOOM writes one map with the lines joined, whatever the tiles.
		node->child("DeleteCombinedDemFile");
		if (auto const* cropping = node->child("TileSizeCropping"))
		{
			if (plainNumber(cropping) != 0.0)
				noCounterpart(QLatin1String("TileSizeCropping"), cropping, "FOOM writes one map of contours, not tiles");
		}
		if (auto const* merging = node->child("Merging"))
		{
			if (!flag(QLatin1String("Merging"), merging, true))
				noCounterpart(QLatin1String("Merging"), merging, "FOOM joins the lines always");
		}
		for (auto const* limit : { "MinimumContourValue", "MaximumContourValue", "MaxContourLength" })
		{
			auto const* field = node->child(limit);
			if (plainNumber(field) != 0.0)
				noCounterpart(QLatin1String(limit), field, "the step has no such limit");
		}
		if (auto const* limited = node->child("IsMaxContourLength"))
		{
			if (flag(QLatin1String("IsMaxContourLength"), limited, false))
				noCounterpart(QLatin1String("IsMaxContourLength"), limited, "the step has no such limit");
		}

		if (sets.isEmpty())
		{
			note(QLatin1String("no set of contours is asked for: nothing to do"));
			return;
		}

		auto suffix = QFileInfo(template_file).suffix();
		if (suffix.isEmpty())
			suffix = QStringLiteral("foom");
		auto const contours_file = QDir::cleanPath(QDir(absolute(output)).absoluteFilePath(QLatin1String("Contours.") + suffix));
		add("open", { { QStringLiteral("path"), template_file } });
		main_step = add("elevation-wizard", {
		    { QStringLiteral("source"), QStringLiteral("grid") },
		    { QStringLiteral("files"), QJsonArray::fromStringList(grids) },
		    { QStringLiteral("products"), QJsonArray{ QStringLiteral("contours") } },
		    { QStringLiteral("contour_sets"), sets },
		});
		add("save-as", { { QStringLiteral("path"), contours_file } });
		maps[key(output)].push_back(contours_file);
	}

	void vegetationBaseMap()
	{
		needsFile();
		auto const input = text(node->child("InputFolder"));
		auto const* dem = demOf(input);
		if (!dem)
		{
			if (input.isEmpty())
				unsupported(QLatin1String("InputFolder missing"));
			else
				unsupported(QLatin1String("InputFolder: no Dem.DemImport of this script filled ") + absolute(input)
				            + QLatin1String(" (FOOM reads the point clouds again, not the point files of the script)"));
			return;
		}

		QJsonObject params {
		    { QStringLiteral("files"), QJsonArray::fromStringList(dem->clouds) },
		    { QStringLiteral("dtm"), true },
		    { QStringLiteral("dsm"), false },
		    { QStringLiteral("products"), QJsonArray{ QStringLiteral("vegetation-map") } },
		};
		for (auto it = dem->terrain.begin(); it != dem->terrain.end(); ++it)
			params.insert(it.key(), it.value());
		auto value = 0.0;
		if (number("CellSize", 0.1, 100.0, &value))
			params.insert(QLatin1String("cell"), value);

		QJsonObject vegetation;
		const struct { const char* field; const char* member; double minimum; double maximum; } numbers[] = {
		    { "NeighborhoodKernelUndergrowth", "undergrowth_kernel", 1.0, 51.0 },
		    { "ThresholdUndergrowthLow", "undergrowth_from", 0.0, 100.0 },
		    { "ThresholdUndergrowthHigh", "undergrowth_to", 0.0, 100.0 },
		    { "NeighborhoodKernelVegetation", "vegetation_kernel", 1.0, 51.0 },
		    { "ThresholdVegetationLow", "vegetation_from", 0.0, 100.0 },
		    { "ThresholdVegetationHigh", "vegetation_to", 0.0, 100.0 },
		};
		for (auto const& entry : numbers)
		{
			if (number(entry.field, entry.minimum, entry.maximum, &value))
				vegetation.insert(QLatin1String(entry.member), value);
		}
		// The thresholds of a group go together; one which the script has not is FOOM's.
		auto const thresholds = [this, &vegetation](const char* member, std::initializer_list<std::pair<const char*, double>> fields) {
			QJsonArray list;
			auto any = false;
			for (auto const& field : fields)
			{
				auto threshold = field.second;
				any = number(field.first, -1.0, 1.0, &threshold) || any;
				list.append(threshold);
			}
			if (any)
				vegetation.insert(QLatin1String(member), list);
		};
		thresholds("undergrowth_thresholds", { { "ThresholdUndergrowthSlow", -0.80 }, { "ThresholdUndergrowthDifficult", -0.60 } });
		thresholds("vegetation_thresholds", { { "ThresholdVegetationSlow", -0.60 }, { "ThresholdVegetationDifficult", -0.30 }, { "ThresholdVegetationVeryDifficult", -0.10 } });
		if (auto const* no_data = node->child("ShowAreaWithNoData"))
			vegetation.insert(QLatin1String("no_data_red"), flag(QLatin1String("ShowAreaWithNoData"), no_data, true));
		if (!vegetation.isEmpty())
			params.insert(QLatin1String("vegetation_map"), vegetation);

		if (auto const* filter = node->child("UseFilter"))
		{
			if (flag(QLatin1String("UseFilter"), filter, false))
				noCounterpart(QLatin1String("UseFilter"), filter, "FOOM's vegetation base map has no filter");
		}
		if (auto const* overlap = node->child("ThresholdOverlapPointsLow"))
			noCounterpart(QLatin1String("ThresholdOverlapPointsLow"), overlap, "FOOM has no height for overlap points");
		if (auto const* classification = node->child("LasClassification"))
		{
			classification->useAll();
			warnings.push_back(QLatin1String("LasClassification: FOOM counts every return in the bands, and the ground and water returns, whatever the rows"));
		}
		if (auto const* output = node->child("OutputFolder"))
		{
			if (!output->text.isEmpty() && key(output->text) != key(folderOf(current)))
				noCounterpart(QLatin1String("OutputFolder"), output, "FOOM writes the raster beside the open file");
		}
		main_step = add("point-cloud", params);
	}
};

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
	QJsonObject result {
	    { QStringLiteral("script"), path },
	    { QStringLiteral("commands"), commands },
	    { QStringLiteral("steps"), steps.size() },
	    { QStringLiteral("unsupported"), list },
	};
	if (!notes.empty())
	{
		QJsonArray note_list;
		for (auto const& entry : notes)
		{
			note_list.append(QJsonObject{
			    { QStringLiteral("command"), entry.command },
			    { QStringLiteral("line"), entry.line },
			    { QStringLiteral("note"), entry.note },
			});
		}
		result.insert(QStringLiteral("notes"), note_list);
	}
	return result;
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
	// A script may begin with a comment.
	while (text.startsWith("<!--"))
	{
		auto const end = text.indexOf("-->");
		if (end < 0)
			return false;
		text = text.mid(end + 3).trimmed();
	}
	return text.startsWith("<OcadScript");
}


XmlScript readXmlScript(const QByteArray& bytes, const QDir& folder)
{
	XmlScript script;
	QXmlStreamReader xml(decode(bytes));
	if (!xml.readNextStartElement() || xml.name() != QLatin1String("OcadScript"))
		throw JobError(QLatin1String("the file is no XML script: its root is not OcadScript"));

	ScriptCommands ocd(script, folder);
	auto& opened = ocd.current;  // the file of the last FileOpen
	while (xml.readNextStartElement())
	{
		++script.commands;
		auto const command = xml.name().toString();
		auto const line = int(xml.lineNumber());

		if (command == QLatin1String("CsCanvas"))
		{
			// The canvases of the course setting, with their parameters as the format has them.
			auto const text = elementXml(xml);
			appendStep(script, "load-canvases", { { QStringLiteral("xml"), text } });
			continue;
		}

		auto const node = readNode(xml);
		if (command.contains(QLatin1Char('.')))
		{
			// The format's own names: a group, a dot, the command.
			ocd.read(node);
			continue;
		}

		auto const fields = fieldsOf(node);
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
