/*
 *    Copyright 2026 Matej Šebo
 *
 *    This file is part of FOOM, the Fantastic Open Orienteering Mapper,
 *    a fork of OpenOrienteering Mapper.
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


#include "map_check.h"

#include <algorithm>
#include <cstddef>
#include <memory>
#include <set>
#include <utility>
#include <vector>

#include <QtGlobal>
#include <QCoreApplication>
#include <QRectF>
#include <QString>
#include <QUuid>

#include "core/map.h"
#include "core/map_color.h"
#include "core/map_coord.h"
#include "core/map_part.h"
#include "core/objects/object.h"
#include "core/objects/text_object.h"
#include "core/ops/apply_op.h"
#include "core/ops/op.h"
#include "core/ops/topology.h"
#include "core/symbols/line_symbol.h"
#include "core/symbols/point_symbol.h"
#include "core/symbols/symbol.h"
#include "core/symbols/text_symbol.h"
#include "core/virtual_path.h"
#include "templates/template.h"


namespace OpenOrienteering {

namespace {

QString tr(const char* text)
{
	return QCoreApplication::translate("OpenOrienteering::MapCheck", text);
}

MapCoordF centreOf(const Object& object)
{
	object.update();
	return MapCoordF(object.getExtent().center());
}

Issue objectIssue(const char* rule, Issue::Severity severity, const Object& object, const QString& message)
{
	Issue issue;
	issue.rule = rule;
	issue.severity = severity;
	issue.objects.append(object.uuid());
	issue.where = centreOf(object);
	issue.message = message;
	return issue;
}

bool isAreaOnly(const Symbol* symbol)
{
	auto const types = symbol->getContainedTypes();
	return (types & Symbol::Area) && !(types & Symbol::Line);
}

/** Counts the vertices of the coordinate range, leaving out curve handles. */
std::size_t countVertices(const MapCoordVector& coords, std::size_t first, std::size_t last)
{
	std::size_t count = 0;
	for (auto i = first; i <= last && i < coords.size(); ++i)
	{
		++count;
		if (coords[i].isCurveStart())
			i += 2;
	}
	return count;
}

bool hasRepeatedVertices(const MapCoordVector& coords)
{
	auto changed = false;
	ops::withoutRepeatedVertices(coords, changed);
	return changed;
}

/** Returns a copy of the path with new coordinates and the identity of the original. */
std::unique_ptr<PathObject> replacement(const PathObject& path, MapCoordVector coords)
{
	auto output = std::unique_ptr<PathObject>(new PathObject(path.getSymbol(), std::move(coords)));
	output->setTags(path.tags());
	output->setPatternRotation(path.getPatternRotation());
	output->setPatternOrigin(path.getPatternOrigin());
	output->setIdentity(path.uuid(), path.createdTime(), path.modifiedTime());
	output->setDerivedFrom(path.derivedFrom());
	output->setGroup(path.group());
	return output;
}

void checkObjects(const Map& map, const MapPart& part, std::vector<Issue>& issues)
{
	std::set<const Object*> in_duplicate_group;
	for (int i = 0; i < part.getNumObjects(); ++i)
	{
		auto const* object = part.getObject(i);
		auto const* symbol = object->getSymbol();
		if (!symbol || symbol == static_cast<const Symbol*>(Map::getUndefinedLine()) || symbol == static_cast<const Symbol*>(Map::getUndefinedPoint()) || symbol == static_cast<const Symbol*>(Map::getUndefinedText()))
		{
			issues.push_back(objectIssue(rule::symbol_undefined, Issue::Error, *object, tr("The object has no defined symbol.")));
			continue;
		}
		if (!symbol->isTypeCompatibleTo(object))
		{
			issues.push_back(objectIssue(rule::symbol_wrong_type, Issue::Error, *object, tr("The symbol %1 does not fit the object's type.").arg(symbol->getNumberAsString())));
			continue;
		}
		switch (object->getType())
		{
		case Object::Text:
			if (object->asText()->getText().trimmed().isEmpty())
				issues.push_back(objectIssue(rule::text_empty, Issue::Warning, *object, tr("The text object is empty.")));
			break;
		case Object::Path:
			{
				auto const* path = object->asPath();
				auto const& coords = path->getRawCoordinateVector();
				auto const area_only = isAreaOnly(symbol);
				auto const first_part_vertices = path->parts().empty() ? std::size_t(0) : countVertices(coords, path->parts().front().first_index, path->parts().front().last_index);
				if (area_only ? first_part_vertices < 3 : countVertices(coords, 0, coords.size()) < 2)
				{
					issues.push_back(objectIssue(rule::path_degenerate, Issue::Error, *object, area_only ? tr("The area has fewer than three vertices.") : tr("The line has fewer than two vertices.")));
					break;
				}
				if (hasRepeatedVertices(coords))
					issues.push_back(objectIssue(rule::vertex_duplicate, Issue::Warning, *object, tr("The path repeats a vertex.")));
				if (area_only && !path->parts().front().isClosed())
					issues.push_back(objectIssue(rule::area_open, Issue::Warning, *object, tr("The area outline is not closed.")));
			}
			break;
		default:
			break;
		}
	
		// Duplicates: the same symbol and equal geometry.
		if (in_duplicate_group.count(object))
			continue;
		object->update();
		std::vector<Object*> candidates;
		part.query(object->getExtent(), candidates);
		Issue group;
		for (auto const* candidate : candidates)
		{
			if (candidate == object || candidate->getSymbol() != symbol || in_duplicate_group.count(candidate) || !object->equals(candidate, false))
				continue;
			if (group.objects.isEmpty())
			{
				group = objectIssue(rule::object_duplicate, Issue::Warning, *object, QString{});
				in_duplicate_group.insert(object);
			}
			group.objects.append(candidate->uuid());
			in_duplicate_group.insert(candidate);
		}
		if (!group.objects.isEmpty())
		{
			group.message = tr("%1 equal objects of symbol %2.").arg(group.objects.size()).arg(symbol->getNumberAsString());
			group.measured = group.objects.size();
			issues.push_back(group);
		}
	}
	Q_UNUSED(map);
}

}  // namespace


std::vector<Issue> checkMap(const Map& map)
{
	// the design notes, "Map check".
	std::vector<Issue> issues;
	for (int p = 0; p < map.getNumParts(); ++p)
		checkObjects(map, *map.getPart(p), issues);
	
	std::vector<bool> symbols_in_use;
	map.determineSymbolsInUse(symbols_in_use);
	for (int i = 0; i < map.getNumSymbols(); ++i)
	{
		if (std::size_t(i) < symbols_in_use.size() && symbols_in_use[std::size_t(i)])
			continue;
		auto const* symbol = map.getSymbol(i);
		Issue issue;
		issue.rule = rule::symbol_unused;
		issue.severity = Issue::Info;
		issue.subject = symbol->getNumberAsString();
		issue.measured = i;
		issue.message = tr("The symbol %1 %2 is not used.").arg(symbol->getNumberAsString(), symbol->getName());
		issues.push_back(issue);
	}
	for (int i = 0; i < map.getNumColors(); ++i)
	{
		auto const* color = map.getColor(i);
		if (map.isColorUsedByASymbol(color))
			continue;
		Issue issue;
		issue.rule = rule::color_unused;
		issue.severity = Issue::Info;
		issue.subject = color->getName();
		issue.measured = i;
		issue.message = tr("The color %1 is not used by any symbol.").arg(color->getName());
		issues.push_back(issue);
	}
	for (int i = 0; i < map.getNumTemplates(); ++i)
	{
		auto const* temp = map.getTemplate(i);
		if (temp->getTemplateState() != Template::Invalid)
			continue;
		Issue issue;
		issue.rule = rule::template_missing;
		issue.severity = Issue::Error;
		issue.subject = temp->getTemplatePath();
		issue.message = tr("The template file %1 cannot be loaded.").arg(temp->getTemplatePath());
		issues.push_back(issue);
	}
	return issues;
}

bool fixIssue(Map& map, const Issue& issue)
{
	if (issue.rule == rule::symbol_unused)
	{
		// Symbol numbers need not be unique: take the first unused symbol of the number.
		std::vector<bool> in_use;
		map.determineSymbolsInUse(in_use);
		for (int i = 0; i < map.getNumSymbols(); ++i)
		{
			if (map.getSymbol(i)->getNumberAsString() != issue.subject || (std::size_t(i) < in_use.size() && in_use[std::size_t(i)]))
				continue;
			map.deleteSymbol(i);
			return true;
		}
		return false;
	}
	if (issue.rule == rule::color_unused)
	{
		auto const index = int(issue.measured);
		if (index < 0 || index >= map.getNumColors() || map.getColor(index)->getName() != issue.subject || map.isColorUsedByASymbol(map.getColor(index)))
			return false;
		map.deleteColor(index);
		return true;
	}
	if (issue.objects.isEmpty())
		return false;  // template-missing, and anything without a fix
	
	// Object rules: find the objects and the part they live in.
	std::vector<const Object*> objects;
	for (auto const& uuid : issue.objects)
	{
		if (auto const* object = map.findObject(uuid))
			objects.push_back(object);
	}
	if (objects.empty())
		return false;
	auto part_index = -1;
	for (int p = 0; p < map.getNumParts() && part_index < 0; ++p)
	{
		if (map.getPart(p)->contains(const_cast<Object*>(objects.front())))
			part_index = p;
	}
	if (part_index < 0)
		return false;
	
	ops::OpResult result;
	if (issue.rule == rule::text_empty || issue.rule == rule::path_degenerate || issue.rule == rule::symbol_undefined || issue.rule == rule::symbol_wrong_type)
	{
		result.consumed = objects;
	}
	else if (issue.rule == rule::object_duplicate)
	{
		result.consumed.assign(begin(objects) + 1, end(objects));
	}
	else if (issue.rule == rule::vertex_duplicate || issue.rule == rule::area_open)
	{
		for (auto const* object : objects)
		{
			if (object->getType() != Object::Path)
				continue;
			auto const* path = object->asPath();
			auto changed = false;
			auto coords = issue.rule == rule::vertex_duplicate ? ops::withoutRepeatedVertices(path->getRawCoordinateVector(), changed) : path->getRawCoordinateVector();
			auto output = replacement(*path, std::move(coords));
			if (issue.rule == rule::area_open && !output->parts().empty() && !output->parts().front().isClosed())
			{
				output->parts().front().setClosed(true, true);
				changed = true;
			}
			if (!changed)
				continue;
			result.out.push_back(std::move(output));
			result.consumed.push_back(object);
		}
	}
	else
	{
		return false;
	}
	if (result.out.empty() && result.consumed.empty())
		return false;
	
	auto const previous_part = map.getCurrentPartIndex();
	map.setCurrentPartIndex(std::size_t(part_index));
	auto const applied = ops::applyOp(map, std::move(result));
	map.setCurrentPartIndex(previous_part);
	return applied;
}


}  // namespace OpenOrienteering
