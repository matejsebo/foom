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


#ifndef FOOM_COURSE_RELAY_H
#define FOOM_COURSE_RELAY_H

#include <vector>

#include <QString>
#include <QStringList>

#include "course/course_data.h"

namespace OpenOrienteering {


/**
 * Relays: the structure of the variations of a course, the variants
 * which the runners run, and the allocation of the variants to the teams.
 *
 * See the design notes, "Relays: variations, variants and
 * their allocation".
 */

/** A team variation: every team runs each of its branches, on different legs. */
struct RelayFork
{
	/** The indexes of the items which begin and end it. */
	int begin = -1;
	int end = -1;
	int branches = 0;
	/** The legs on which it is run, in their order: those of the course, or of the branch of a leg variation in which it stands. */
	std::vector<int> legs;
};

/** What relayStructure() reads from the items of a course. */
struct RelayStructure
{
	/** A part of the tree of the items. */
	struct Node
	{
		enum Kind { Item, TeamVariation, LegVariation };
		
		Kind kind = Item;
		/** The index of the item, or of the item which begins the variation. */
		int item = -1;
		/** For a team variation its index in team_forks. */
		int fork = -1;
		std::vector<std::vector<Node>> branches;
		/** For a leg variation the legs of each branch. */
		std::vector<std::vector<int>> branch_legs;
	};
	
	/** What is wrong, in words: the first fault which was found. Nothing else is to be used when there is one. */
	QStringList faults;
	/** The course has items of variations. */
	bool has_variations = false;
	/** The number of legs; 0 for a course which is no relay. */
	int legs = 0;
	/** The team variations in the order in which they begin. */
	std::vector<RelayFork> team_forks;
	std::vector<Node> nodes;
	
	bool isValid() const { return faults.isEmpty(); }
};

RelayStructure relayStructure(const Course& course);


/** One runner's course: a leg, and the branch of every team variation on it. */
struct RelayVariant
{
	/** From 1; 0 is no variant. */
	int leg = 0;
	/** For every team variation of the course the branch which is run, from 0; -1 for one which is not on the leg. */
	std::vector<int> branches;
};

bool operator==(const RelayVariant& lhs, const RelayVariant& rhs);
inline bool operator!=(const RelayVariant& lhs, const RelayVariant& rhs) { return !(lhs == rhs); }

/**
 * The code of a variant as the format has it: the leg, and a letter for every
 * team variation, a blank for one which is not on the leg ("1CB", "3B ").
 */
QString relayVariantCode(const RelayVariant& variant);

/**
 * The variants of a relay, by leg and then by branches. None for a
 * structure with a fault and for a course which is no relay.
 */
std::vector<RelayVariant> relayVariants(const RelayStructure& structure);

/** The items of a variant: those of the course without the variations and the branches which are not run. */
std::vector<CourseItem> relayVariantItems(const Course& course, const RelayStructure& structure, const RelayVariant& variant);

/**
 * A variant as a normal course, named by the course and the code of the
 * variant, with the climb and the extra length of the relay.
 */
Course relayVariantCourse(const Course& course, const RelayStructure& structure, const RelayVariant& variant);

/**
 * Course data in which the relay has the items of a variant, under its own
 * name and as a normal course, so that it is measured and drawn with its
 * overrides. The data as they are when the course has no such variant.
 */
CourseData withRelayVariant(const CourseData& data, const QString& course, const RelayVariant& variant);


/** Which team runs what. */
struct RelayAllocation
{
	bool ok = false;
	/** The combinations were too many to weigh; the branches rotate. */
	bool by_rotation = false;
	/** For every team its variants, one for each leg. */
	std::vector<std::vector<RelayVariant>> teams;
};

/**
 * Gives every team a combination of orderings of the branches, so that it
 * runs each branch of every team variation as often as the others: the
 * combination which was used least, among those the one which differs
 * most from the two teams before, among those the first of the list.
 * The same for the same input; more teams begin as fewer do.
 */
RelayAllocation allocateRelay(const RelayStructure& structure, int teams);


/** The teams of a relay, as its class says. */
struct RelayTeams
{
	int count = 0;
	int first_number = 1;
	QString class_name;
};

RelayTeams relayTeams(const CourseData& data, const QString& course);

/**
 * The allocation of all relays of the course data, in the form of the
 * export of the relay variations: "<course>;<class>", then "101.1: 1CB"
 * for every runner of a relay, or "1: <tab>ABAB<tab>BABA" for every runner
 * of a one-man relay. Lines end with a line feed.
 */
QString relayAllocationText(const CourseData& data);


}  // namespace OpenOrienteering

#endif
