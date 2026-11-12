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


#ifndef OPENORIENTEERING_OPS_OP_H
#define OPENORIENTEERING_OPS_OP_H

#include <memory>
#include <vector>

#include <QString>

namespace OpenOrienteering {

class Map;
class Object;

/**
 * Geometry operations: pure functions on map objects.
 *
 * Every non-interactive edit is a function with the signature of Op,
 * taking its own parameter type derived from OpParams. It reads the map
 * and the input objects and returns an OpResult; it changes nothing.
 * applyOp() turns the result into one undo step on the map.
 *
 * Nothing in this directory may include editor code (src/gui/, src/undo/,
 * src/tools/, widgets); the ctest ops_purity checks this. apply_op.cpp is
 * the one exception, for the undo steps.
 *
 * See the design notes, "Geometry operations".
 */
namespace ops {

/**
 * Base of the parameter types of operations.
 *
 * Lengths are millimetres on paper unless named _m.
 */
struct OpParams
{
	virtual ~OpParams() = default;
};


/**
 * The result of an operation.
 *
 * When error is not empty, the operation failed, out and consumed are empty,
 * and applyOp() leaves the map unchanged.
 */
struct OpResult
{
	/** The new objects, to be added to the map. */
	std::vector<std::unique_ptr<Object>> out;

	/** The inputs which the outputs replace, to be removed from the map. */
	std::vector<const Object*> consumed;

	/** The failure message, empty on success. */
	QString error;
};


/**
 * The signature of an operation.
 *
 * Operations take their own OpParams subclass by reference to it.
 */
using Op = OpResult (*)(const Map& map, const std::vector<const Object*>& in, const OpParams& params);


}  // namespace ops

}  // namespace OpenOrienteering

#endif
