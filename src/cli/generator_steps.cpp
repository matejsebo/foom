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


#include "generator_steps.h"

#include <cmath>
#include <cstddef>
#include <vector>

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QLatin1Char>
#include <QLatin1String>
#include <QStringList>

#include "job.h"
#include "core/map.h"
#include "core/map_coord.h"
#include "core/map_part.h"
#include "core/objects/object.h"
#include "core/symbols/symbol.h"
#include "layout/generator.h"


namespace OpenOrienteering {

namespace Cli {

namespace {

const QLatin1String generate_step("generate");
const QLatin1String regenerate_step("regenerate");

Q_NORETURN void fail(const Step& step, const QString& what)
{
	throw JobError(step.prefix() + what);
}

QString quoted(const QString& name)
{
	return QLatin1Char('"') + name + QLatin1Char('"');
}

bool isNumberPair(const QJsonValue& value)
{
	auto const array = value.toArray();
	return value.isArray() && array.size() == 2 && array.at(0).isDouble() && array.at(1).isDouble();
}

bool hasClip(const std::vector<GeneratorField>& fields)
{
	for (auto const& field : fields)
	{
		if (field.key == QLatin1String("clip"))
			return true;
	}
	return false;
}

int symbolTypes(GeneratorField::Kind kind)
{
	switch (kind)
	{
	case GeneratorField::LineSymbol:
		return Symbol::Line | Symbol::Combined;
	case GeneratorField::AreaSymbol:
		return Symbol::Area | Symbol::Combined;
	default:
		return Symbol::Text;
	}
}

QString symbolKindName(GeneratorField::Kind kind)
{
	switch (kind)
	{
	case GeneratorField::LineSymbol:
		return QStringLiteral("line");
	case GeneratorField::AreaSymbol:
		return QStringLiteral("area");
	default:
		return QStringLiteral("text");
	}
}

/// A symbol number given as a string or as a number.
QString symbolNumber(const QJsonValue& value)
{
	if (value.isDouble())
		return QString::number(value.toDouble(), 'g', 10);
	return value.toString();
}

bool hasPublicSymbol(const Map& map, const QString& number, int types)
{
	for (int i = 0; i < map.getNumPublicSymbols(); ++i)
	{
		auto const* symbol = map.getSymbol(i);
		if ((symbol->getType() & types) && symbol->getNumberAsString() == number)
			return true;
	}
	return false;
}

void validateField(const Step& step, const QString& id, const GeneratorField& field, const QJsonValue& value)
{
	auto const name = quoted(field.key);
	switch (field.kind)
	{
	case GeneratorField::Bool:
		if (!value.isBool())
			fail(step, QString::fromLatin1("parameter %1 must be true or false").arg(name));
		break;
	case GeneratorField::Int:
	case GeneratorField::Double:
		{
			if (!value.isDouble())
				fail(step, QString::fromLatin1("parameter %1 must be a number").arg(name));
			auto const v = value.toDouble();
			if (field.kind == GeneratorField::Int && v != std::floor(v))
				fail(step, QString::fromLatin1("parameter %1 must be an integer").arg(name));
			if (v < field.minimum || v > field.maximum)
				fail(step, QString::fromLatin1("parameter %1 must be from %2 to %3").arg(name, QString::number(field.minimum), QString::number(field.maximum)));
		}
		break;
	case GeneratorField::Choice:
		if (!value.isString())
			fail(step, QString::fromLatin1("parameter %1 must be a string").arg(name));
		// The extent of the trim marks is the name of one of the map's extents, checked when it runs.
		if (!(id == QLatin1String("gen-trim-marks") && field.key == QLatin1String("extent"))
		    && !field.choices.contains(value.toString()))
		{
			QStringList choices;
			for (auto const& choice : field.choices)
				choices.append(quoted(choice));
			fail(step, QString::fromLatin1("unknown %1 %2: expected %3").arg(name, quoted(value.toString()), choices.join(QLatin1String(", "))));
		}
		break;
	case GeneratorField::String:
		if (!value.isString())
			fail(step, QString::fromLatin1("parameter %1 must be a string").arg(name));
		break;
	case GeneratorField::LineSymbol:
	case GeneratorField::AreaSymbol:
	case GeneratorField::TextSymbol:
		if (!value.isString() && !value.isDouble())
			fail(step, QString::fromLatin1("parameter %1 must be a symbol number, as a string or a number").arg(name));
		break;
	}
}

}  // namespace


bool isGeneratorStep(const QString& name)
{
	return name == generate_step || name == regenerate_step;
}


void validateGeneratorStep(const Step& step, bool map_open)
{
	if (!map_open)
		fail(step, QLatin1String("needs an open map: add an \"open\" step first"));
	auto const ids = generatorIds();
	auto const& params = step.params;

	if (step.name == regenerate_step)
	{
		for (auto const& key : params.keys())
		{
			if (key != QLatin1String("generators"))
				fail(step, QString::fromLatin1("unknown parameter %1").arg(quoted(key)));
		}
		if (params.contains(QLatin1String("generators")))
		{
			auto const value = params.value(QLatin1String("generators"));
			if (!value.isArray())
				fail(step, QLatin1String("parameter \"generators\" must be a list of generator ids"));
			for (auto const& item : value.toArray())
			{
				if (!item.isString() || !ids.contains(item.toString()))
					fail(step, QString::fromLatin1("unknown generator %1: expected one of %2").arg(quoted(item.toString()), ids.join(QLatin1String(", "))));
			}
		}
		return;
	}

	auto const generator = params.value(QLatin1String("generator"));
	if (!generator.isString() || generator.toString().isEmpty())
		fail(step, QLatin1String("needs \"generator\", the id of a generator such as \"gen-scale-bar\""));
	auto const id = generator.toString();
	if (!ids.contains(id))
		fail(step, QString::fromLatin1("unknown generator %1: expected one of %2").arg(quoted(id), ids.join(QLatin1String(", "))));

	auto const fields = generatorFields(id);
	auto const clip = hasClip(fields);
	for (auto const& key : params.keys())
	{
		if (key == QLatin1String("generator") || key == QLatin1String("at") || (clip && key == QLatin1String("outline")))
			continue;
		auto known = false;
		for (auto const& field : fields)
		{
			if (field.key == key)
			{
				validateField(step, id, field, params.value(key));
				known = true;
			}
		}
		if (!known)
			fail(step, QString::fromLatin1("unknown parameter %1 of %2").arg(quoted(key), id));
	}

	if (generatorIsPlaced(id))
	{
		if (!isNumberPair(params.value(QLatin1String("at"))))
			fail(step, QString::fromLatin1("%1 needs \"at\", the anchor as [x, y] in mm").arg(id));
	}
	else if (params.contains(QLatin1String("at")))
	{
		fail(step, QString::fromLatin1("%1 takes its place from its outline or extent and has no \"at\"").arg(id));
	}

	if (clip)
	{
		auto const has_outline = params.contains(QLatin1String("outline"));
		if (has_outline)
		{
			auto const outline = params.value(QLatin1String("outline"));
			if (!outline.isArray() || outline.toArray().size() < 3)
				fail(step, QLatin1String("parameter \"outline\" must be a list of at least three points [x, y] in mm"));
			for (auto const& point : outline.toArray())
			{
				if (!isNumberPair(point))
					fail(step, QLatin1String("parameter \"outline\" must be a list of at least three points [x, y] in mm"));
			}
		}
		auto const clip_value = params.value(QLatin1String("clip")).toString();
		if (clip_value == QLatin1String("selection") && !has_outline)
			fail(step, QLatin1String("\"clip\": \"selection\" needs \"outline\": the runner has no selection"));
		if (clip_value == QLatin1String("extent") && has_outline)
			fail(step, QLatin1String("\"outline\" means \"clip\": \"selection\", not \"extent\""));
	}
}


StepResult runGeneratorStep(Map& map, const Step& step)
{
	StepResult result;
	auto const& p = step.params;
	QString error;

	if (step.name == regenerate_step)
	{
		QStringList wanted;
		for (auto const& item : p.value(QLatin1String("generators")).toArray())
			wanted.append(item.toString());
		std::vector<int> groups;
		for (auto group : generatedGroups(map, false))
		{
			auto const objects = groupObjects(map, group);
			if (!objects.empty() && (wanted.isEmpty() || wanted.contains(objects.front()->getTag(QStringLiteral("generator")))))
				groups.push_back(group);
		}
		auto const count = groups.empty() ? 0 : regenerateGroups(map, groups, &error);
		if (!error.isEmpty())
		{
			result.ok = false;
			result.message = error;
			return result;
		}
		result.details.insert(QStringLiteral("groups"), count);
		return result;
	}

	auto const id = p.value(QLatin1String("generator")).toString();
	auto params = defaultGeneratorParams(id, map);
	for (auto const& field : generatorFields(id))
	{
		if (!p.contains(field.key))
			continue;
		auto const value = p.value(field.key);
		switch (field.kind)
		{
		case GeneratorField::LineSymbol:
		case GeneratorField::AreaSymbol:
		case GeneratorField::TextSymbol:
			{
				auto const number = symbolNumber(value);
				if (!number.isEmpty() && !hasPublicSymbol(map, number, symbolTypes(field.kind)))
				{
					result.ok = false;
					result.message = QString::fromLatin1("the map has no %1 symbol %2 for %3").arg(symbolKindName(field.kind), number, quoted(field.key));
					return result;
				}
				params.insert(field.key, number);
			}
			break;
		default:
			params.insert(field.key, value);
			break;
		}
	}
	if (p.contains(QLatin1String("outline")))
	{
		params.insert(QStringLiteral("clip"), QStringLiteral("selection"));
		params.insert(QStringLiteral("outline"), p.value(QLatin1String("outline")));
	}

	MapCoordF anchor(0, 0);
	if (p.contains(QLatin1String("at")))
	{
		auto const at = p.value(QLatin1String("at")).toArray();
		anchor = MapCoordF(at.at(0).toDouble(), at.at(1).toDouble());
	}

	auto const group = runGenerator(map, id, params, anchor, &error);
	if (group == 0)
	{
		result.ok = false;
		result.message = error.isEmpty() ? QString::fromLatin1("%1 made nothing").arg(id) : error;
		return result;
	}
	auto part_index = -1;
	auto const objects = groupObjects(map, group, &part_index);
	auto const layout = part_index >= 0 && map.getPart(std::size_t(part_index))->role() == MapPart::Role::Layout;
	result.details.insert(QStringLiteral("generator"), id);
	result.details.insert(QStringLiteral("group"), group);
	result.details.insert(QStringLiteral("objects"), int(objects.size()));
	result.details.insert(QStringLiteral("part"), layout ? QStringLiteral("layout") : QStringLiteral("map"));
	return result;
}


}  // namespace Cli

}  // namespace OpenOrienteering
