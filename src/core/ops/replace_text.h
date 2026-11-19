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


#ifndef OPENORIENTEERING_OPS_REPLACE_TEXT_H
#define OPENORIENTEERING_OPS_REPLACE_TEXT_H

#include <vector>

#include <QString>

#include "core/ops/op.h"

namespace OpenOrienteering {

class Map;
class Object;

namespace ops {

/**
 * Parameters of replaceText() and deleteMatchingText().
 */
struct ReplaceTextParams : public OpParams
{
	/** The text to find; must not be empty. */
	QString find;
	
	/** The text which replaces every match. */
	QString replacement;
	
	bool case_sensitive = false;
	
	/** Match only where the find text is bounded by word boundaries. */
	bool whole_word = false;
};


/**
 * Returns true when the text holds a match.
 */
bool textMatches(const QString& text, const ReplaceTextParams& params);

/**
 * Returns the text with every match replaced.
 */
QString replacedText(const QString& text, const ReplaceTextParams& params);

/**
 * Replaces every match in the text objects among the inputs
 * ("find-replace-text").
 *
 * Text objects without a match and other objects are left alone. The outputs
 * carry the inputs' symbol, tags and identity. An empty find text is an error.
 *
 * See the design notes, "Find and replace text".
 */
OpResult replaceText(const Map& map, const std::vector<const Object*>& in, const ReplaceTextParams& params);

/**
 * Consumes every text object among the inputs which holds a match, and
 * outputs nothing, so that applyOp() deletes them.
 */
OpResult deleteMatchingText(const Map& map, const std::vector<const Object*>& in, const ReplaceTextParams& params);


}  // namespace ops

}  // namespace OpenOrienteering

#endif
