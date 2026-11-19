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


#include "apply_op.h"

#include <memory>
#include <vector>

#include <QtGlobal>

#include "core/map.h"
#include "core/map_part.h"
#include "core/objects/object.h"
#include "undo/object_undo.h"
#include "undo/undo.h"


namespace OpenOrienteering {

namespace ops {

bool applyOp(Map& map, OpResult&& result, const Object* principal)
{
	if (!result.error.isEmpty())
		return false;
	if (result.out.empty() && result.consumed.empty())
		return true;  // nothing to do, and nothing to record
	
	auto* part = map.getCurrentPart();
	for (auto const* object : result.consumed)
	{
		if (!object || part->findObjectIndex(object) < 0)
			return false;
	}
	
	if (principal)
	{
		for (auto& object : result.out)
		{
			object->setSymbol(principal->getSymbol(), true);
			object->setTags(principal->tags());
			object->setIdentity(principal->uuid(), principal->createdTime(), principal->modifiedTime());
			object->setDerivedFrom(principal->derivedFrom());
		}
	}
	auto const now = Object::currentTime();
	for (auto& object : result.out)
		object->touch(now);
	
	// Remove the consumed objects, cf. BooleanTool::executeForObjects.
	// The inputs are const for the operation; the map owns them and may edit them.
	auto add_step = std::unique_ptr<AddObjectsUndoStep>(new AddObjectsUndoStep(&map));
	for (auto const* object : result.consumed)
	{
		auto* consumed = const_cast<Object*>(object);
		add_step->addObject(consumed, consumed);
	}
	// Keep as separate loop to get the correct index in the previous loop.
	for (auto const* object : result.consumed)
	{
		auto* consumed = const_cast<Object*>(object);
		map.removeObjectFromSelection(consumed, false);
		part->releaseObject(consumed);
		consumed->setMap(&map);  // necessary so objects are saved correctly
	}
	
	// Add the outputs, and select them.
	map.clearObjectSelection(false);
	auto delete_step = std::unique_ptr<DeleteObjectsUndoStep>(new DeleteObjectsUndoStep(&map));
	std::vector<Object*> added;
	added.reserve(result.out.size());
	for (auto& object : result.out)
	{
		auto* output = object.release();
		map.addObject(output);
		map.addObjectToSelection(output, false);
		added.push_back(output);
	}
	for (auto* output : added)
		delete_step->addObject(part->findObjectIndex(output));
	
	auto undo_step = std::unique_ptr<CombinedUndoStep>(new CombinedUndoStep(&map));
	if (!add_step->isEmpty())
		undo_step->push(add_step.release());
	if (!delete_step->isEmpty())
		undo_step->push(delete_step.release());
	if (undo_step->getNumSubSteps() > 0)
		map.push(undo_step.release());
	
	map.setObjectsDirty();
	map.emitSelectionChanged();
	map.emitSelectionEdited();
	return true;
}


}  // namespace ops

}  // namespace OpenOrienteering
