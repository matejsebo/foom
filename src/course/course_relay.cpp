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


#include "course_relay.h"

#include <algorithm>
#include <cstddef>
#include <limits>

#include <QChar>
#include <QCoreApplication>
#include <QLatin1Char>

#include "course/course_edit.h"

namespace OpenOrienteering {

namespace {

QString tr(const char* text)
{
	return QCoreApplication::translate("OpenOrienteering::CourseRelay", text);
}

/// More combinations than this are not weighed against each other.
constexpr double combination_limit = 20000.0;

bool contains(const std::vector<int>& list, int value)
{
	return std::find(begin(list), end(list), value) != end(list);
}

/// The legs of a branch of a leg variation: numbers with dashes between them, a list and not a range.
std::vector<int> parseLegs(const QString& text)
{
	std::vector<int> legs;
	for (auto const& word : text.split(QLatin1Char('-'), QString::SkipEmptyParts))
	{
		auto ok = false;
		auto const leg = word.trimmed().toInt(&ok);
		if (!ok || leg < 1)
			return {};
		legs.push_back(leg);
	}
	return legs;
}

/// Reads the items of a relay into the tree of its variations.
class Parser
{
public:
	Parser(const Course& course, RelayStructure& structure)
	: course(course)
	, structure(structure)
	, size(int(course.items.size()))
	{}
	
	QString fault;
	
	/// Reads items up to one which ends a sequence, and returns its index.
	int sequence(int i, const std::vector<int>& legs, std::vector<RelayStructure::Node>& nodes)
	{
		auto after_variation = false;
		while (i < size && fault.isEmpty())
		{
			auto const& item = course.items[std::size_t(i)];
			switch (item.type)
			{
			case CourseItem::ForkBranch:
			case CourseItem::TeamForkEnd:
			case CourseItem::LegBranch:
			case CourseItem::LegForkEnd:
				return i;
			case CourseItem::TeamForkBegin:
			case CourseItem::LegForkBegin:
				if (after_variation)
				{
					fail(tr("There must be a control between two variations."));
					return i;
				}
				i = item.type == CourseItem::TeamForkBegin ? teamVariation(i, legs, nodes) : legVariation(i, legs, nodes);
				after_variation = true;
				break;
			default:
				{
					RelayStructure::Node node;
					node.item = i;
					nodes.push_back(node);
					if (item.type == CourseItem::Control)
						after_variation = false;
					++i;
				}
			}
		}
		return i;
	}
	
private:
	void fail(const QString& text)
	{
		if (fault.isEmpty())
			fault = text;
	}
	
	bool isType(int i, CourseItem::Type type) const
	{
		return i < size && course.items[std::size_t(i)].type == type;
	}
	
	int teamVariation(int i, const std::vector<int>& legs, std::vector<RelayStructure::Node>& nodes)
	{
		auto const said = course.items[std::size_t(i)].count;
		RelayStructure::Node node;
		node.kind = RelayStructure::Node::TeamVariation;
		node.item = i;
		node.fork = int(structure.team_forks.size());
		// Before its branches are read, so that the variations are in the order in which they begin.
		RelayFork fork;
		fork.begin = i;
		fork.legs = legs;
		structure.team_forks.push_back(fork);
		
		auto j = i + 1;
		while (isType(j, CourseItem::ForkBranch) && fault.isEmpty())
		{
			node.branches.emplace_back();
			j = sequence(j + 1, legs, node.branches.back());
		}
		if (!fault.isEmpty())
			return j;
		if (!isType(j, CourseItem::TeamForkEnd))
		{
			fail(tr("A variation has no end."));
			return j;
		}
		auto const count = int(node.branches.size());
		if (count != said)
			fail(tr("A team variation says %1 branches and has %2.").arg(said).arg(count));
		else if (count < 2)
			fail(tr("A variation needs at least two branches."));
		else if (int(legs.size()) % count != 0)
			fail(tr("A team variation of %1 branches cannot be run on %2 leg(s): the number of branches must be the number of legs, or divide it.").arg(count).arg(legs.size()));
		
		auto& stored = structure.team_forks[std::size_t(node.fork)];
		stored.end = j;
		stored.branches = count;
		nodes.push_back(node);
		return j + 1;
	}
	
	int legVariation(int i, const std::vector<int>& legs, std::vector<RelayStructure::Node>& nodes)
	{
		RelayStructure::Node node;
		node.kind = RelayStructure::Node::LegVariation;
		node.item = i;
		std::vector<int> seen;
		
		auto j = i + 1;
		while (isType(j, CourseItem::LegBranch) && fault.isEmpty())
		{
			auto branch_legs = parseLegs(course.items[std::size_t(j)].legs);
			if (branch_legs.empty())
			{
				fail(tr("A branch of a leg variation does not say its legs."));
				return j;
			}
			for (auto const leg : branch_legs)
			{
				if (!contains(legs, leg))
					fail(tr("Leg %1 is not one of the legs of its leg variation.").arg(leg));
				else if (contains(seen, leg))
					fail(tr("Leg %1 is in two branches of a leg variation.").arg(leg));
				seen.push_back(leg);
			}
			if (!fault.isEmpty())
				return j;
			std::sort(begin(branch_legs), end(branch_legs));
			node.branch_legs.push_back(branch_legs);
			node.branches.emplace_back();
			j = sequence(j + 1, branch_legs, node.branches.back());
		}
		if (!fault.isEmpty())
			return j;
		if (!isType(j, CourseItem::LegForkEnd))
		{
			fail(tr("A variation has no end."));
			return j;
		}
		if (node.branches.size() < 2)
		{
			fail(tr("A variation needs at least two branches."));
		}
		else
		{
			for (auto const leg : legs)
			{
				if (!contains(seen, leg))
				{
					fail(tr("Leg %1 is in no branch of a leg variation.").arg(leg));
					break;
				}
			}
		}
		nodes.push_back(node);
		return j + 1;
	}
	
	const Course& course;
	RelayStructure& structure;
	const int size;
};

/// Says whether a variant is one of the structure.
bool isVariantOf(const RelayStructure& structure, const RelayVariant& variant)
{
	if (!structure.isValid() || variant.leg < 1 || variant.leg > structure.legs
	    || variant.branches.size() != structure.team_forks.size())
		return false;
	for (std::size_t f = 0; f < structure.team_forks.size(); ++f)
	{
		auto const& fork = structure.team_forks[f];
		auto const branch = variant.branches[f];
		if (contains(fork.legs, variant.leg) ? (branch < 0 || branch >= fork.branches) : branch != -1)
			return false;
	}
	return true;
}

void collectItems(const Course& course, const std::vector<RelayStructure::Node>& nodes, const RelayVariant& variant, std::vector<CourseItem>& items)
{
	for (auto const& node : nodes)
	{
		switch (node.kind)
		{
		case RelayStructure::Node::Item:
			items.push_back(course.items[std::size_t(node.item)]);
			break;
		case RelayStructure::Node::TeamVariation:
			collectItems(course, node.branches[std::size_t(variant.branches[std::size_t(node.fork)])], variant, items);
			break;
		case RelayStructure::Node::LegVariation:
			for (std::size_t b = 0; b < node.branches.size(); ++b)
			{
				if (contains(node.branch_legs[b], variant.leg))
					collectItems(course, node.branches[b], variant, items);
			}
			break;
		}
	}
}

/// The orderings of a team variation: for each of its legs a branch, every branch as often as the others.
std::vector<std::vector<int>> orderingsOf(const RelayFork& fork)
{
	std::vector<std::vector<int>> orderings;
	auto const repeats = int(fork.legs.size()) / fork.branches;
	std::vector<int> ordering;
	for (int branch = 0; branch < fork.branches; ++branch)
		ordering.insert(ordering.end(), std::size_t(repeats), branch);
	do
	{
		orderings.push_back(ordering);
	}
	while (std::next_permutation(begin(ordering), end(ordering)));
	return orderings;
}

/// The number of these orderings, without making them.
double orderingCount(const RelayFork& fork)
{
	auto const legs = int(fork.legs.size());
	auto const repeats = legs / fork.branches;
	auto count = 1.0;
	for (int i = 2; i <= legs; ++i)
		count *= i;
	for (int branch = 0; branch < fork.branches; ++branch)
	{
		for (int i = 2; i <= repeats; ++i)
			count /= i;
	}
	return count;
}

int differences(const std::vector<int>& lhs, const std::vector<int>& rhs)
{
	auto count = 0;
	for (std::size_t i = 0; i < lhs.size(); ++i)
		count += lhs[i] != rhs[i] ? 1 : 0;
	return count;
}

/// The variants of a team: for every team variation the branch on each of its legs.
std::vector<RelayVariant> teamVariants(const RelayStructure& structure, const std::vector<std::vector<int>>& orderings)
{
	std::vector<RelayVariant> variants;
	for (int leg = 1; leg <= structure.legs; ++leg)
	{
		RelayVariant variant;
		variant.leg = leg;
		variant.branches.assign(structure.team_forks.size(), -1);
		for (std::size_t f = 0; f < structure.team_forks.size(); ++f)
		{
			auto const& legs = structure.team_forks[f].legs;
			auto const found = std::find(begin(legs), end(legs), leg);
			if (found != end(legs))
				variant.branches[f] = orderings[f][std::size_t(found - begin(legs))];
		}
		variants.push_back(variant);
	}
	return variants;
}

}  // namespace



RelayStructure relayStructure(const Course& course)
{
	RelayStructure structure;
	structure.has_variations = std::any_of(begin(course.items), end(course.items), [](const CourseItem& item) {
		return isVariationItem(item.type);
	});
	if (course.type == Course::Normal)
	{
		if (structure.has_variations)
			structure.faults.push_back(tr("A course with variations must be a relay or a one-man relay."));
		return structure;
	}
	structure.legs = course.legs;
	if (course.legs < 2)
	{
		structure.faults.push_back(tr("A relay has at least two legs."));
		return structure;
	}
	
	std::vector<int> legs;
	for (int leg = 1; leg <= course.legs; ++leg)
		legs.push_back(leg);
	Parser parser(course, structure);
	auto const stopped = parser.sequence(0, legs, structure.nodes);
	if (parser.fault.isEmpty() && stopped < int(course.items.size()))
		parser.fault = tr("A branch or an end of a variation stands without its beginning.");
	if (!parser.fault.isEmpty())
	{
		structure.faults.push_back(parser.fault);
		structure.team_forks.clear();
		structure.nodes.clear();
	}
	return structure;
}



bool operator==(const RelayVariant& lhs, const RelayVariant& rhs)
{
	return lhs.leg == rhs.leg && lhs.branches == rhs.branches;
}


QString relayVariantCode(const RelayVariant& variant)
{
	auto code = QString::number(variant.leg);
	for (auto const branch : variant.branches)
		code += branch < 0 ? QChar(QLatin1Char(' ')) : QChar(ushort('A' + branch));
	return code;
}


std::vector<RelayVariant> relayVariants(const RelayStructure& structure)
{
	std::vector<RelayVariant> variants;
	if (!structure.isValid())
		return variants;
	auto const fork_count = structure.team_forks.size();
	for (int leg = 1; leg <= structure.legs; ++leg)
	{
		RelayVariant variant;
		variant.leg = leg;
		variant.branches.assign(fork_count, -1);
		std::vector<std::size_t> on_leg;
		for (std::size_t f = 0; f < fork_count; ++f)
		{
			if (contains(structure.team_forks[f].legs, leg))
			{
				variant.branches[f] = 0;
				on_leg.push_back(f);
			}
		}
		// Count through the branches, the last variation fastest.
		for (;;)
		{
			variants.push_back(variant);
			auto position = on_leg.size();
			while (position > 0)
			{
				auto const f = on_leg[position - 1];
				if (++variant.branches[f] < structure.team_forks[f].branches)
					break;
				variant.branches[f] = 0;
				--position;
			}
			if (position == 0)
				break;
		}
	}
	return variants;
}


std::vector<CourseItem> relayVariantItems(const Course& course, const RelayStructure& structure, const RelayVariant& variant)
{
	std::vector<CourseItem> items;
	if (isVariantOf(structure, variant))
		collectItems(course, structure.nodes, variant, items);
	return items;
}


Course relayVariantCourse(const Course& course, const RelayStructure& structure, const RelayVariant& variant)
{
	auto result = course;
	result.name = course.name + QLatin1Char(' ') + relayVariantCode(variant).trimmed();
	result.type = Course::Normal;
	result.legs = 0;
	result.items = relayVariantItems(course, structure, variant);
	return result;
}


CourseData withRelayVariant(const CourseData& data, const QString& course, const RelayVariant& variant)
{
	auto const* found = data.findCourse(course);
	if (!found)
		return data;
	auto const structure = relayStructure(*found);
	if (!isVariantOf(structure, variant))
		return data;
	auto result = data;
	for (auto& entry : result.courses)
	{
		if (entry.name != course)
			continue;
		entry.items = relayVariantItems(*found, structure, variant);
		entry.type = Course::Normal;
		entry.legs = 0;
	}
	return result;
}



RelayAllocation allocateRelay(const RelayStructure& structure, int teams)
{
	RelayAllocation allocation;
	if (!structure.isValid() || structure.legs < 2)
		return allocation;
	allocation.ok = true;
	if (teams <= 0)
		return allocation;
	
	auto const& forks = structure.team_forks;
	auto const fork_count = forks.size();
	auto total = 1.0;
	for (auto const& fork : forks)
		total = std::min(total * orderingCount(fork), 1.0e18);
	
	if (total > combination_limit)
	{
		// The branches rotate: the first variation with every team, the next when the first has gone round, and so on.
		allocation.by_rotation = true;
		for (int team = 0; team < teams; ++team)
		{
			std::vector<std::vector<int>> orderings;
			auto period = 1;
			for (auto const& fork : forks)
			{
				auto const shift = (team / period) % fork.branches;
				std::vector<int> ordering;
				for (std::size_t p = 0; p < fork.legs.size(); ++p)
					ordering.push_back((int(p) + shift) % fork.branches);
				orderings.push_back(ordering);
				period = period > std::numeric_limits<int>::max() / fork.branches ? std::numeric_limits<int>::max() : period * fork.branches;
			}
			allocation.teams.push_back(teamVariants(structure, orderings));
		}
		return allocation;
	}
	
	// Every combination of orderings, the last variation counted fastest.
	std::vector<std::vector<std::vector<int>>> orderings;
	for (auto const& fork : forks)
		orderings.push_back(orderingsOf(fork));
	auto const combinations = int(total + 0.5);
	auto const indexes = [&orderings, fork_count](int combination) {
		std::vector<int> result(fork_count, 0);
		for (std::size_t f = fork_count; f > 0; --f)
		{
			auto const count = int(orderings[f - 1].size());
			result[f - 1] = combination % count;
			combination /= count;
		}
		return result;
	};
	auto const difference = [&orderings, fork_count](const std::vector<int>& lhs, const std::vector<int>& rhs) {
		auto sum = 0;
		for (std::size_t f = 0; f < fork_count; ++f)
			sum += differences(orderings[f][std::size_t(lhs[f])], orderings[f][std::size_t(rhs[f])]);
		return sum;
	};
	
	std::vector<int> used(std::size_t(combinations), 0);
	std::vector<std::vector<int>> before;  // the combinations of the two teams before, the last one first
	for (int team = 0; team < teams; ++team)
	{
		auto const least = *std::min_element(begin(used), end(used));
		auto best = -1;
		auto best_difference = -1;
		std::vector<int> best_indexes;
		for (int combination = 0; combination < combinations; ++combination)
		{
			if (used[std::size_t(combination)] != least)
				continue;
			auto const candidate = indexes(combination);
			auto sum = 0;
			for (auto const& other : before)
				sum += difference(candidate, other);
			if (sum > best_difference)
			{
				best = combination;
				best_difference = sum;
				best_indexes = candidate;
			}
		}
		++used[std::size_t(best)];
		before.insert(before.begin(), best_indexes);
		if (before.size() > 2)
			before.pop_back();
		
		std::vector<std::vector<int>> chosen;
		for (std::size_t f = 0; f < fork_count; ++f)
			chosen.push_back(orderings[f][std::size_t(best_indexes[f])]);
		allocation.teams.push_back(teamVariants(structure, chosen));
	}
	return allocation;
}



RelayTeams relayTeams(const CourseData& data, const QString& course)
{
	RelayTeams teams;
	for (auto const& course_class : data.classes)
	{
		if (course_class.course != course)
			continue;
		teams.class_name = course_class.name;
		if (course_class.first_no >= 0)
			teams.first_number = course_class.first_no;
		if (course_class.runners >= 0)
			teams.count = course_class.runners;
		else if (course_class.first_no >= 0 && course_class.last_no >= course_class.first_no)
			teams.count = course_class.last_no - course_class.first_no + 1;
		break;
	}
	return teams;
}


QString relayAllocationText(const CourseData& data)
{
	QStringList blocks;
	for (auto const& course : data.courses)
	{
		if (course.type == Course::Normal)
			continue;
		auto const structure = relayStructure(course);
		auto const teams = relayTeams(data, course.name);
		auto const allocation = allocateRelay(structure, teams.count);
		if (!allocation.ok)
			continue;
		
		QStringList lines;
		lines << course.name + QLatin1Char(';') + teams.class_name;
		for (std::size_t t = 0; t < allocation.teams.size(); ++t)
		{
			auto const number = QString::number(teams.first_number + int(t));
			if (course.type == Course::OneManRelay)
			{
				// The loops of the one runner, without their numbers.
				auto line = QString(number + QLatin1String(": "));
				for (auto const& variant : allocation.teams[t])
					line += QLatin1Char('\t') + relayVariantCode(variant).mid(QString::number(variant.leg).size());
				lines << line;
			}
			else
			{
				for (auto const& variant : allocation.teams[t])
					lines << number + QLatin1Char('.') + QString::number(variant.leg) + QLatin1String(": ") + relayVariantCode(variant);
			}
		}
		blocks << lines.join(QLatin1Char('\n')) + QLatin1Char('\n');
	}
	return blocks.join(QLatin1Char('\n'));
}


}  // namespace OpenOrienteering
