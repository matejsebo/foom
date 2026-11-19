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


#ifndef FOOM_COURSE_REPORT_H
#define FOOM_COURSE_REPORT_H

#include <vector>

#include <QString>
#include <QStringList>

#include "check/issue.h"
#include "course/course_data.h"

namespace OpenOrienteering {

class Map;

namespace rule {

constexpr char course_start_missing[] = "course-start-missing";
constexpr char course_finish_missing[] = "course-finish-missing";
constexpr char course_climb_zero[] = "course-climb-zero";
constexpr char course_identical[] = "course-identical";
constexpr char course_not_checked[] = "course-not-checked";
constexpr char relay_structure[] = "relay-structure";
constexpr char control_unused[] = "control-unused";
constexpr char control_too_close[] = "control-too-close";
constexpr char leg_opposite[] = "leg-opposite";
constexpr char description_d_empty[] = "description-d-empty";
constexpr char description_de_missing[] = "description-de-missing";
constexpr char description_fg_missing[] = "description-fg-missing";
constexpr char punch_consecutive[] = "punch-consecutive";
constexpr char punch_repeated[] = "punch-repeated";

}  // namespace rule


/**
 * The statistics of the courses of a map and of the event.
 *
 * See the design notes, "Statistics and the consistency check".
 */
struct CourseStatistics
{
	struct CourseRow
	{
		QString name;
		Course::Type type = Course::Normal;
		bool has_variation = false;
		QStringList classes;
		/** The sum of the runners of the classes; a number which is not given counts 0. */
		int runners = 0;
		/** False for a course with variations and for one with an item whose object is gone. */
		bool measured = false;
		/** With the extra length, in m. */
		double length_m = 0.0;
		int climb_m = 0;
		/** The number of the control items. */
		int controls = 0;
	};
	
	struct ObjectRow
	{
		QString code;
		CourseObject::Kind kind = CourseObject::Control;
		/** The courses which have an item of the object. */
		QStringList courses;
		/** The runners of these courses. */
		int runners = 0;
	};
	
	struct LegRow
	{
		QString from;
		QString to;
		/** The straight from where the one is left to where the other is entered, in m; negative when it is not known. */
		double length_m = -1.0;
		QStringList courses;
		int runners = 0;
	};
	
	struct ClosePair
	{
		QString first;
		QString second;
		double distance_m = 0.0;
	};
	
	struct OppositeLegs
	{
		QString first;
		QString second;
		/** The courses which run from the first to the second, and those which run back. */
		QStringList forward;
		QStringList backward;
	};
	
	std::vector<CourseRow> courses;
	/** Start, control, marked route and finish, in this order and by their numbers. */
	std::vector<ObjectRow> objects;
	/** The legs of the courses without variations, the most used first. */
	std::vector<LegRow> legs;
	
	/** The controls, which are the rows, and the courses, which are the columns. */
	QStringList matrix_controls;
	QStringList matrix_courses;
	/** The position of the control in the course: "2", "2/5", "x" in a course with variations, or nothing. */
	std::vector<QStringList> matrix;
	
	int control_count = 0;
	int course_count = 0;
	int class_count = 0;
	/** The pairs of controls which are closer than the limit, the nearest first. */
	double close_limit_m = 0.0;
	std::vector<ClosePair> close_pairs;
	std::vector<OppositeLegs> opposite;
	/** The groups of courses with the same items. */
	std::vector<QStringList> identical;
	QStringList without_start_or_finish;
	/** The courses with variations: not looked at for legs and directions. */
	QStringList not_verified;
};

/** The distance below which the statistics name a pair of controls: 60 m, and 30 m in a map of 1:5,000 or larger. */
double statisticsControlDistance(const Map& map);

/** The distance below which the check reports a pair of controls: 30 m, and 25 m in a map of 1:5,000 or larger. */
double checkControlDistance(const Map& map);

/** The statistics; a limit of 0 is that of statisticsControlDistance(). */
CourseStatistics courseStatistics(const Map& map, const CourseData& data, double close_limit_m = 0.0);

/** The statistics as a text: tables with tabulators, lines ended by a line feed. */
QString courseStatisticsText(const CourseStatistics& statistics);

/** The last part of that text: the numbers and the lists of the event. */
QString courseEventText(const CourseStatistics& statistics);

/**
 * The consistency check of the courses, for the issue list. A limit of 0
 * is that of checkControlDistance().
 */
std::vector<Issue> checkCourses(const Map& map, const CourseData& data, double close_limit_m = 0.0);


}  // namespace OpenOrienteering

#endif
