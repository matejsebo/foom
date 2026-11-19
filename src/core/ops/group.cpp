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


#include "group.h"

#include <memory>
#include <utility>
#include <vector>

#include <QString>

#include "core/objects/object.h"


namespace OpenOrienteering {

namespace ops {

OpResult setGroup(const Map& /*map*/, const std::vector<const Object*>& in, const GroupParams& params)
{
	OpResult result;
	if (in.empty())
	{
		result.error = QStringLiteral("No objects selected.");
		return result;
	}
	for (auto const* object : in)
	{
		if (!object || object->group() == params.group)
			continue;
		auto output = std::unique_ptr<Object>(object->duplicate());
		output->setGroup(params.group);
		result.out.push_back(std::move(output));
		result.consumed.push_back(object);
	}
	return result;
}


}  // namespace ops

}  // namespace OpenOrienteering
