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

#include "draft_view_bar.h"

#include <QAction>
#include <QLabel>
#include <QSignalBlocker>
#include <QSlider>
#include <QString>

#include "core/map_view.h"
#include "gui/map/map_editor.h"
#include "gui/map/map_widget.h"


namespace OpenOrienteering {

DraftViewBar::DraftViewBar(MapEditorController* editor, QAction* draft_view_action, QWidget* parent)
: QToolBar(tr("Draft view"), parent)
, editor(editor)
{
	setObjectName(QStringLiteral("Draft view toolbar"));
	addAction(draft_view_action);

	auto const make_slider = [this](const QString& label, const QString& tip) {
		auto* text = new QLabel(label);
		text->setToolTip(tip);
		addWidget(text);
		auto* slider = new QSlider(Qt::Horizontal);
		slider->setRange(0, 100);
		slider->setValue(50);
		slider->setMaximumWidth(120);
		slider->setToolTip(tip);
		addWidget(slider);
		return slider;
	};
	map_slider = make_slider(tr("Map"), tr("How much the draft view dims the map, in percent"));
	template_slider = make_slider(tr("Background maps"), tr("How much the draft view dims the background maps, in percent"));

	connect(map_slider, &QSlider::valueChanged, this, [this](int value) {
		if (auto* v = view())
			v->setDraftMapDim(value / 100.0);
	});
	connect(template_slider, &QSlider::valueChanged, this, [this](int value) {
		if (auto* v = view())
			v->setDraftTemplateDim(value / 100.0);
	});
}

DraftViewBar::~DraftViewBar() = default;


MapView* DraftViewBar::view() const
{
	auto* widget = editor->getMainWidget();
	return widget ? widget->getMapView() : nullptr;
}


void DraftViewBar::updateFromView()
{
	if (auto* v = view())
	{
		QSignalBlocker block_map(map_slider);
		QSignalBlocker block_template(template_slider);
		map_slider->setValue(qRound(v->draftMapDim() * 100));
		template_slider->setValue(qRound(v->draftTemplateDim() * 100));
	}
}


}  // namespace OpenOrienteering
