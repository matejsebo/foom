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


#ifndef FOOM_COURSE_COMPOSE_H
#define FOOM_COURSE_COMPOSE_H

#include <vector>

#include <QString>

#include "core/map_coord.h"
#include "course/course_data.h"

namespace OpenOrienteering {

class Map;
class Symbol;


/**
 * Composes a course by clicks in the map: what a click, a double click
 * and a click with Ctrl do to the course and to the map. It knows nothing
 * of the screen; ComposeCourseTool gives it the clicks.
 *
 * Every change is one undo step of the map.
 *
 * See the design notes, "Composing a course and the bulk
 * commands".
 */
class CourseComposer
{
public:
	enum Outcome
	{
		Nothing,
		Added,            ///< an object was put at the end of the course
		Inserted,         ///< an object was put in at the place of the insert mode
		Removed,          ///< an item was taken out of the course
		InsertModeBegun,  ///< the click was on a leg; nothing was changed
		Refused
	};
	
	enum Reason
	{
		NoReason,
		NoCourse,      ///< there is no course of the name
		HasVariation,  ///< the course has a variation
		NoSymbol,      ///< the map has no symbol with the role which is needed
		Complete,      ///< the course ends with a finish
		BesideItself   ///< the object would stand beside itself
	};
	
	struct Result
	{
		Outcome outcome = Nothing;
		Reason reason = NoReason;
		/** The code of the object which was put in or taken out. */
		QString code;
		/** The kind for which the map has no symbol. */
		CourseObject::Kind kind = CourseObject::Control;
	};
	
	/** What is under a click. */
	struct Target
	{
		enum What { Nothing, Object, Leg };
		
		What what = Nothing;
		/** The code of the course object. */
		QString code;
		/** The index of the item at which the leg begins. */
		int item = -1;
	};
	
	explicit CourseComposer(Map& map);
	
	/** The name of the course which is composed. */
	const QString& course() const { return course_name; }
	/** Another course ends the insert mode. */
	void setCourse(const QString& name);
	
	/** Why the course cannot be composed: NoCourse, HasVariation, or NoReason. */
	Reason refusal() const;
	/** The course has no items. */
	bool isEmpty() const;
	/** The course ends with a finish and takes nothing more at its end. */
	bool isComplete() const;
	
	/**
	 * What is at a place: a start, a control or a finish whose centre is no
	 * farther than the extent of its symbol or than the reach, or a marked
	 * route within the reach; failing that, a leg of the course within the
	 * reach. Lengths are in mm.
	 */
	Target targetAt(const MapCoordF& pos, double reach) const;
	
	/** A click: puts an object at the end or at the place of the insert mode, or begins the insert mode. */
	Result click(const MapCoordF& pos, double reach);
	/**
	 * The second click of a double click, after click() was called for the
	 * first: the control which that click made becomes a finish.
	 */
	Result doubleClick(const MapCoordF& pos, double reach);
	/** A click with Ctrl: takes the last item of the object at the place out of the course. */
	Result removeAt(const MapCoordF& pos, double reach);
	
	/** False as soon as the items of the course are not those which the composer left. */
	bool inInsertMode() const;
	void leaveInsertMode();
	
	/**
	 * The places from which a line to the mouse shows what a click would
	 * do: the end of the course, or in the insert mode both ends of the
	 * place. Empty when a click would put nothing in.
	 */
	std::vector<MapCoordF> rubberEnds() const;
	
private:
	const Course* findCourse() const;
	const Symbol* symbolFor(CourseObject::Kind kind) const;
	/** Makes an object and puts it into the course as the item with the index. */
	Result makeObject(CourseObject::Kind kind, const MapCoordF& pos, int index, Outcome outcome);
	/** Puts a course object which is there into the course. */
	Result takeObject(const QString& code, int index, Outcome outcome);
	bool besideItself(const Course& course, int index, const QString& code) const;
	void remember();
	
	Map& map;
	QString course_name;
	/** The index of the item behind which the next object is put in; -1 outside of the insert mode. */
	int insert_after = -1;
	/** The last click made a control at the end of the course. */
	bool made_control = false;
	/** The items of the course as the composer left them. */
	std::vector<CourseItem> items_left;
};


}  // namespace OpenOrienteering

#endif
