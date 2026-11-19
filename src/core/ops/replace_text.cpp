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


#include "replace_text.h"

#include <memory>
#include <utility>
#include <vector>

#include <QRegularExpression>
#include <QString>

#include "core/objects/object.h"
#include "core/objects/text_object.h"


namespace OpenOrienteering {

namespace ops {

namespace {

QRegularExpression pattern(const ReplaceTextParams& params)
{
	auto expression = QRegularExpression::escape(params.find);
	if (params.whole_word)
		expression = QLatin1String("\\b") + expression + QLatin1String("\\b");
	QRegularExpression::PatternOptions options = QRegularExpression::UseUnicodePropertiesOption;
	if (!params.case_sensitive)
		options |= QRegularExpression::CaseInsensitiveOption;
	return QRegularExpression(expression, options);
}

}  // namespace


bool textMatches(const QString& text, const ReplaceTextParams& params)
{
	return !params.find.isEmpty() && pattern(params).match(text).hasMatch();
}

QString replacedText(const QString& text, const ReplaceTextParams& params)
{
	if (params.find.isEmpty())
		return text;
	auto result = text;
	return result.replace(pattern(params), params.replacement);
}

OpResult replaceText(const Map& /*map*/, const std::vector<const Object*>& in, const ReplaceTextParams& params)
{
	OpResult result;
	if (params.find.isEmpty())
	{
		result.error = QStringLiteral("Enter the text to find.");
		return result;
	}
	for (auto const* object : in)
	{
		if (!object || object->getType() != Object::Text)
			continue;
		auto const* text = object->asText();
		auto const replaced = replacedText(text->getText(), params);
		if (replaced == text->getText())
			continue;
		auto output = std::unique_ptr<TextObject>(static_cast<TextObject*>(text->duplicate()));
		output->setText(replaced);
		result.out.push_back(std::move(output));
		result.consumed.push_back(object);
	}
	return result;
}

OpResult deleteMatchingText(const Map& /*map*/, const std::vector<const Object*>& in, const ReplaceTextParams& params)
{
	OpResult result;
	if (params.find.isEmpty())
	{
		result.error = QStringLiteral("Enter the text to find.");
		return result;
	}
	for (auto const* object : in)
	{
		if (object && object->getType() == Object::Text && textMatches(object->asText()->getText(), params))
			result.consumed.push_back(object);
	}
	return result;
}


}  // namespace ops

}  // namespace OpenOrienteering
