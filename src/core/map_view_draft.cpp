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

// foom: the draft view of MapView, the design notes, "Draft view"

#include "map_view.h"

#include <QtGlobal>


namespace OpenOrienteering {

void MapView::setDraftMode(bool value)
{
	if (draft_mode != value)
	{
		draft_mode = value;
		emit visibilityChanged(VisibilityFeature::DraftMode, value);
	}
}

void MapView::setDraftMapDim(qreal value)
{
	value = qBound(qreal(0), value, qreal(1));
	if (!qFuzzyCompare(1.0 + draft_map_dim, 1.0 + value))
	{
		draft_map_dim = value;
		emit visibilityChanged(VisibilityFeature::DraftMode, draft_mode);
	}
}

void MapView::setDraftTemplateDim(qreal value)
{
	value = qBound(qreal(0), value, qreal(1));
	if (!qFuzzyCompare(1.0 + draft_template_dim, 1.0 + value))
	{
		draft_template_dim = value;
		emit visibilityChanged(VisibilityFeature::DraftMode, draft_mode);
	}
}

TemplateVisibility MapView::screenMapVisibility() const
{
	auto visibility = effectiveMapVisibility();
	if (draft_mode)
	{
		visibility.opacity *= 1 - draft_map_dim;
		visibility.visible &= visibility.opacity >= 0.005;
	}
	return visibility;
}

TemplateVisibility MapView::screenTemplateVisibility(const Template* temp) const
{
	auto visibility = getTemplateVisibility(temp);
	if (draft_mode)
		visibility.opacity *= 1 - draft_template_dim;
	return visibility;
}


}  // namespace OpenOrienteering
