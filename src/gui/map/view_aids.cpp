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


#include "view_aids.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include <Qt>
#include <QAction>
#include <QActionGroup>
#include <QColor>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFormLayout>
#include <QGridLayout>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QSettings>
#include <QVariant>

#include "settings.h"
#include "core/georeferencing.h"
#include "core/latlon.h"
#include "core/map.h"
#include "core/map_part.h"
#include "core/map_view.h"
#include "core/objects/object.h"
#include "gui/map/map_editor.h"
#include "gui/map/map_widget.h"
#include "tools/tool.h"
#include "undo/map_data_undo.h"


namespace OpenOrienteering {

namespace {

const QString rulers_visible_key = QStringLiteral("ViewAids/RulersVisible");
const QString ruler_unit_key = QStringLiteral("ViewAids/RulerUnit");

/** The colour of guides: cyan, as in the spec. */
const QColor guide_color(0, 200, 230);

/** The corner square of the rulers: dragging it sets the origin, a double-click resets it. */
class RulerCorner : public QWidget
{
public:
	RulerCorner(ViewAids* aids, MapWidget* map_widget, QWidget* parent)
	: QWidget(parent), aids(aids), map_widget(map_widget)
	{
		setFixedSize(RulerWidget::thickness, RulerWidget::thickness);
		setCursor(Qt::CrossCursor);
		setToolTip(ViewAids::tr("Drag to set the origin of the rulers, double-click to reset it"));
	}

protected:
	void paintEvent(QPaintEvent* /*event*/) override
	{
		QPainter painter(this);
		painter.fillRect(rect(), palette().window());
		painter.setPen(palette().color(QPalette::WindowText));
		auto const c = rect().center();
		painter.drawLine(c.x() - 4, c.y(), c.x() + 4, c.y());
		painter.drawLine(c.x(), c.y() - 4, c.x(), c.y() + 4);
	}

	void mouseReleaseEvent(QMouseEvent* event) override
	{
		auto const pos = map_widget->mapFrom(window(), mapTo(window(), event->pos()));
		if (map_widget->rect().contains(pos))
			aids->setRulerOrigin(map_widget->viewportToMapF(pos));
	}

	void mouseDoubleClickEvent(QMouseEvent* /*event*/) override
	{
		aids->resetRulerOrigin();
	}

private:
	ViewAids* aids;
	MapWidget* map_widget;
};

/** A transparent layer over the map widget on which the guides are drawn. */
class GuideLayer : public QWidget
{
public:
	GuideLayer(const ViewAids* aids, MapWidget* map_widget)
	: QWidget(map_widget), aids(aids), map_widget(map_widget)
	{
		setAttribute(Qt::WA_TransparentForMouseEvents);
		setAttribute(Qt::WA_NoSystemBackground);
		setFocusPolicy(Qt::NoFocus);
		setGeometry(map_widget->rect());
		raise();
		show();
	}
	
protected:
	void paintEvent(QPaintEvent* /*event*/) override
	{
		QPainter painter(this);
		aids->drawGuides(&painter, map_widget);
	}
	
private:
	const ViewAids* aids;
	MapWidget* map_widget;
};

}  // namespace


// ### Ruler ticks ###

double rulerStep(double pixels_per_unit, double min_label_pixels)
{
	pixels_per_unit = std::abs(pixels_per_unit);
	if (!(pixels_per_unit > 0) || !std::isfinite(pixels_per_unit))
		return 1;
	auto const minimum = min_label_pixels / pixels_per_unit;
	auto power = std::pow(10.0, std::floor(std::log10(minimum)));
	for (;;)
	{
		for (auto const mantissa : { 1.0, 2.0, 5.0 })
		{
			// A little tolerance, so that exactly 60 pixels counts as 60.
			if (mantissa * power * pixels_per_unit >= min_label_pixels * (1 - 1e-9))
				return mantissa * power;
		}
		power *= 10;
	}
}

std::vector<RulerTick> rulerTicks(double value_at_zero, double units_per_pixel, int length, double min_label_pixels)
{
	std::vector<RulerTick> ticks;
	if (length <= 0 || units_per_pixel == 0 || !std::isfinite(units_per_pixel))
		return ticks;

	auto const pixels_per_unit = 1 / std::abs(units_per_pixel);
	auto const step = rulerStep(pixels_per_unit, min_label_pixels);
	auto divisions = 1;
	for (auto const n : { 10, 5, 2 })
	{
		if (step / n * pixels_per_unit >= 5)
		{
			divisions = n;
			break;
		}
	}
	auto const minor = step / divisions;

	auto const value_at_end = value_at_zero + units_per_pixel * length;
	auto const low = std::min(value_at_zero, value_at_end);
	auto const high = std::max(value_at_zero, value_at_end);
	auto const first = qint64(std::ceil(low / minor - 1e-9));
	auto const last = qint64(std::floor(high / minor + 1e-9));
	for (auto i = first; i <= last; ++i)
	{
		auto const value = double(i) * minor;
		RulerTick tick;
		tick.value = value;
		tick.pixel = (value - value_at_zero) / units_per_pixel;
		tick.labelled = i % divisions == 0;
		ticks.push_back(tick);
	}
	if (units_per_pixel < 0)
		std::reverse(ticks.begin(), ticks.end());
	return ticks;
}


// ### ViewAids ###

ViewAids::ViewAids(MapEditorController* editor, Map* map, MapWidget* map_widget)
: QObject(editor)
, editor(editor)
, map(map)
, map_widget(map_widget)
{
	QSettings settings;
	rulers_visible = settings.value(rulers_visible_key, false).toBool();
	ruler_unit = settings.value(ruler_unit_key, int(PaperMillimetres)).toInt() == GroundMetres ? GroundMetres : PaperMillimetres;

	guide_layer = new GuideLayer(this, map_widget);
	map_widget->installEventFilter(this);
	connect(map, &Map::guidesChanged, guide_layer.data(), qOverload<>(&QWidget::update));
	connect(map, &Map::guidesChanged, this, &ViewAids::syncActions);
	connect(map, &Map::objectSelectionChanged, this, &ViewAids::selectionChanged);
	if (auto* view = map_widget->getMapView())
	{
		connect(view, &MapView::viewChanged, this, &ViewAids::updateRulers);
		connect(view, &MapView::panOffsetChanged, this, &ViewAids::updateRulers);
	}
}

ViewAids::~ViewAids() = default;

QWidget* ViewAids::wrapMapWidget()
{
	frame = new QWidget();
	auto* layout = new QGridLayout(frame);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(0);
	corner = new RulerCorner(this, map_widget, frame);
	top_ruler = new RulerWidget(Qt::Horizontal, this, map_widget, frame);
	left_ruler = new RulerWidget(Qt::Vertical, this, map_widget, frame);
	layout->addWidget(corner, 0, 0);
	layout->addWidget(top_ruler, 0, 1);
	layout->addWidget(left_ruler, 1, 0);
	layout->addWidget(map_widget, 1, 1);
	layout->setRowStretch(1, 1);
	layout->setColumnStretch(1, 1);
	corner->setVisible(rulers_visible);
	top_ruler->setVisible(rulers_visible);
	left_ruler->setVisible(rulers_visible);
	return frame;
}

void ViewAids::addToViewMenu(QMenu* menu)
{
	menu->addSeparator();
	for (auto const* id : { "view-move-to", "find-selected", "view-rulers" })
	{
		if (auto* action = editor->getAction(id))
			menu->addAction(action);
	}
	auto* guides_menu = menu->addMenu(tr("Guides"));
	for (auto const* id : { "guides-show", "guides-lock", "guides-clear" })
	{
		if (auto* action = editor->getAction(id))
			guides_menu->addAction(action);
	}
	syncActions();
}

void ViewAids::syncActions()
{
	auto const& guides = map->guides();
	if (auto* action = editor->getAction("view-rulers"))
		action->setChecked(rulers_visible);
	if (auto* action = editor->getAction("guides-show"))
		action->setChecked(guides.visible);
	if (auto* action = editor->getAction("guides-lock"))
		action->setChecked(guides.locked);
	if (auto* action = editor->getAction("guides-clear"))
		action->setEnabled(!guides.list.empty() && !guides.locked);
}

RulerWidget* ViewAids::ruler(Qt::Orientation orientation) const
{
	return orientation == Qt::Horizontal ? top_ruler.data() : left_ruler.data();
}

void ViewAids::setRulersVisible(bool visible)
{
	rulers_visible = visible;
	QSettings().setValue(rulers_visible_key, visible);
	for (QWidget* widget : { static_cast<QWidget*>(corner.data()), static_cast<QWidget*>(top_ruler.data()), static_cast<QWidget*>(left_ruler.data()) })
	{
		if (widget)
			widget->setVisible(visible);
	}
	syncActions();
}

void ViewAids::setRulerUnit(int unit)
{
	ruler_unit = unit == GroundMetres ? GroundMetres : PaperMillimetres;
	QSettings().setValue(ruler_unit_key, int(ruler_unit));
	updateRulers();
}

void ViewAids::setRulerOrigin(const MapCoordF& origin)
{
	ruler_origin = origin;
	updateRulers();
}

void ViewAids::resetRulerOrigin()
{
	setRulerOrigin(MapCoordF(0, 0));
}

double ViewAids::rulerValue(Qt::Orientation orientation, const MapCoordF& coord) const
{
	auto const factor = ruler_unit == GroundMetres ? map->getScaleDenominator() / 1000.0 : 1.0;
	if (orientation == Qt::Horizontal)
		return (coord.x() - ruler_origin.x()) * factor;
	return -(coord.y() - ruler_origin.y()) * factor;
}

void ViewAids::updateRulers()
{
	if (guide_layer)
		guide_layer->update();
	if (top_ruler)
		top_ruler->update();
	if (left_ruler)
		left_ruler->update();
}


void ViewAids::setGuides(Guides guides)
{
	auto* step = new MapDataUndoStep(map, MapDataUndoStep::Guides, MapDataUndoStep::currentXml(*map, MapDataUndoStep::Guides));
	map->setGuides(std::move(guides));
	map->push(step);
	map->setHasUnsavedChanges(true);
}

void ViewAids::addGuide(const Guide& guide)
{
	if (map->guides().locked)
		return;
	auto guides = map->guides();
	guides.list.push_back(guide);
	guides.visible = true;
	setGuides(std::move(guides));
}

void ViewAids::moveGuide(int index, qint32 position)
{
	auto guides = map->guides();
	if (guides.locked || index < 0 || std::size_t(index) >= guides.list.size() || guides.list[std::size_t(index)].position == position)
		return;
	guides.list[std::size_t(index)].position = position;
	setGuides(std::move(guides));
}

void ViewAids::removeGuide(int index)
{
	auto guides = map->guides();
	if (guides.locked || index < 0 || std::size_t(index) >= guides.list.size())
		return;
	guides.list.erase(guides.list.begin() + index);
	setGuides(std::move(guides));
}

void ViewAids::clearGuides()
{
	if (map->guides().list.empty() || map->guides().locked)
		return;
	auto guides = map->guides();
	guides.list.clear();
	setGuides(std::move(guides));
}

void ViewAids::setGuidesVisible(bool visible)
{
	// Showing and locking are view state: saved with the map, but not undone.
	auto guides = map->guides();
	if (guides.visible == visible)
		return;
	guides.visible = visible;
	map->setGuides(std::move(guides));
	map->setHasUnsavedChanges(true);
}

void ViewAids::setGuidesLocked(bool locked)
{
	auto guides = map->guides();
	if (guides.locked == locked)
		return;
	guides.locked = locked;
	map->setGuides(std::move(guides));
	map->setHasUnsavedChanges(true);
}


void ViewAids::beginGuideDrag(Qt::Orientation orientation, int index)
{
	auto const& guides = map->guides();
	if (guides.locked)
		return;
	if (index >= 0 && std::size_t(index) >= guides.list.size())
		return;
	drag_active = true;
	drag_orientation = index >= 0 ? guides.list[std::size_t(index)].orientation : orientation;
	drag_index = index;
	drag_position = index >= 0 ? guides.list[std::size_t(index)].position : 0;
	drag_inside = index >= 0;
}

void ViewAids::updateGuideDrag(const QPoint& widget_pos)
{
	if (!drag_active)
		return;
	drag_inside = map_widget->rect().contains(widget_pos);
	drag_position = guideThrough(drag_orientation, map_widget->viewportToMapF(widget_pos)).position;
	if (guide_layer)
		guide_layer->update();
}

void ViewAids::finishGuideDrag(const QPoint& widget_pos)
{
	if (!drag_active)
		return;
	updateGuideDrag(widget_pos);
	drag_active = false;
	if (drag_inside)
	{
		if (drag_index < 0)
			addGuide(Guide{ drag_orientation, drag_position });
		else
			moveGuide(drag_index, drag_position);
	}
	else if (drag_index >= 0)
	{
		removeGuide(drag_index);
	}
	if (guide_layer)
		guide_layer->update();
}

void ViewAids::drawGuides(QPainter* painter, MapWidget* widget) const
{
	auto const& guides = map->guides();
	if (!guides.visible && !drag_active)
		return;

	auto const rect = QRectF(widget->rect());
	auto const draw = [painter, widget, &rect](const Guide& guide) {
		// Through two map points, so that a rotated view is drawn right.
		auto const c1 = widget->viewportToMapF(rect.topLeft());
		auto const c2 = widget->viewportToMapF(rect.bottomRight());
		auto const span = std::abs(c1.x() - c2.x()) + std::abs(c1.y() - c2.y()) + 1;
		auto const centre = widget->viewportToMapF(rect.center());
		auto const p = guide.positionMM();
		auto const a = guide.orientation == Qt::Horizontal ? QPointF(centre.x() - span, p) : QPointF(p, centre.y() - span);
		auto const b = guide.orientation == Qt::Horizontal ? QPointF(centre.x() + span, p) : QPointF(p, centre.y() + span);
		painter->drawLine(widget->mapToViewport(a), widget->mapToViewport(b));
	};

	painter->save();
	painter->setRenderHint(QPainter::Antialiasing, false);
	QPen pen(guide_color);
	pen.setWidth(0);
	painter->setPen(pen);
	if (guides.visible)
	{
		for (std::size_t i = 0; i < guides.list.size(); ++i)
		{
			if (!(drag_active && int(i) == drag_index))
				draw(guides.list[i]);
		}
	}
	if (drag_active && drag_inside)
	{
		pen.setStyle(Qt::DashLine);
		painter->setPen(pen);
		draw(Guide{ drag_orientation, drag_position });
	}
	painter->restore();
}

bool ViewAids::eventFilter(QObject* watched, QEvent* event)
{
	if (watched != map_widget)
		return false;

	switch (event->type())
	{
	case QEvent::Resize:
		if (guide_layer)
			guide_layer->setGeometry(map_widget->rect());
		return false;
	case QEvent::MouseButtonPress:
		{
			auto* mouse = static_cast<QMouseEvent*>(event);
			auto const& guides = map->guides();
			auto* tool = editor->getTool();
			if (mouse->button() != Qt::LeftButton || !guides.visible || guides.locked
			    || !tool || tool->toolType() != MapEditorTool::EditPoint)
				return false;
			auto const tolerance = map_widget->getMapView()->pixelToLength(Settings::getInstance().getMapEditorClickTolerancePx()) / 1000;
			auto const position = map_widget->viewportToMapF(mouse->pos());
			// Objects come first: a guide is taken only where the edit tool would not take an object.
			SelectionInfoVector objects;
			map->findAllObjectsAt(position, tolerance, false, false, false, false, objects);
			if (!objects.empty())
				return false;
			auto const index = guideAt(guides.list, position, tolerance);
			if (index < 0)
				return false;
			beginGuideDrag(guides.list[std::size_t(index)].orientation, index);
			return true;
		}
	case QEvent::MouseMove:
		if (drag_active)
		{
			updateGuideDrag(static_cast<QMouseEvent*>(event)->pos());
			return true;
		}
		return false;
	case QEvent::MouseButtonRelease:
		if (drag_active)
		{
			finishGuideDrag(static_cast<QMouseEvent*>(event)->pos());
			return true;
		}
		return false;
	default:
		return false;
	}
}


// ### Find selected ###

void ViewAids::selectionChanged()
{
	find_index = 0;
}

void ViewAids::findSelected()
{
	// The selected objects in the order of the map, so that the steps are stable.
	std::vector<const Object*> selected;
	if (map->getNumSelectedObjects() == 0)
		return;
	for (int p = 0; p < map->getNumParts(); ++p)
	{
		auto const* part = map->getPart(std::size_t(p));
		for (int i = 0; i < part->getNumObjects(); ++i)
		{
			auto const* object = part->getObject(i);
			if (map->isObjectSelected(object))
				selected.push_back(object);
		}
	}
	if (selected.empty())
		return;
	if (find_index < 0 || std::size_t(find_index) >= selected.size())
		find_index = 0;

	auto const* object = selected[std::size_t(find_index)];
	object->update();
	auto const extent = object->getExtent();
	auto* view = map_widget->getMapView();
	view->setCenter(MapCoord(extent.center()));

	// Zoom out, never in, so that the object fits with a margin.
	auto const viewport = map_widget->rect();
	if (extent.width() > 0 || extent.height() > 0)
	{
		auto const pixels_per_mm = view->lengthToPixel(1000);
		auto needed = pixels_per_mm;
		if (extent.width() > 0)
			needed = std::min(needed, 0.9 * viewport.width() / extent.width());
		if (extent.height() > 0)
			needed = std::min(needed, 0.9 * viewport.height() / extent.height());
		if (needed < pixels_per_mm && needed > 0)
			view->setZoom(view->getZoom() * needed / pixels_per_mm);
	}

	find_index = int((std::size_t(find_index) + 1) % selected.size());
}


// ### Move to coordinates ###

MapCoordF ViewAids::toMapCoordinates(CoordinateKind kind, double first, double second, bool* ok) const
{
	auto converted = true;
	MapCoordF result;
	auto const& georef = map->getGeoreferencing();
	switch (kind)
	{
	case MapCoordinates:
		result = MapCoordF(first, -second);
		break;
	case ProjectedCoordinates:
		result = georef.toMapCoordF(QPointF(first, second));
		break;
	case GeographicCoordinates:
		if (georef.getState() != Georeferencing::Geospatial)
			converted = false;
		else
			result = georef.toMapCoordF(LatLon(first, second), &converted);
		break;
	}
	if (!std::isfinite(result.x()) || !std::isfinite(result.y()))
		converted = false;
	if (ok)
		*ok = converted;
	return result;
}

void ViewAids::moveTo(const MapCoordF& position)
{
	map_widget->getMapView()->setCenter(MapCoord(position));
}

void ViewAids::moveToClicked()
{
	QDialog dialog(map_widget->window());
	dialog.setWindowTitle(tr("Move to coordinates"));
	auto* layout = new QFormLayout(&dialog);
	auto* kind_box = new QComboBox();
	kind_box->addItem(tr("Map coordinates"), int(MapCoordinates));
	kind_box->addItem(tr("Projected coordinates"), int(ProjectedCoordinates));
	if (map->getGeoreferencing().getState() == Georeferencing::Geospatial)
		kind_box->addItem(tr("Geographic coordinates"), int(GeographicCoordinates));
	layout->addRow(tr("Coordinates:"), kind_box);

	auto* first_label = new QLabel();
	auto* second_label = new QLabel();
	auto* first_box = new QDoubleSpinBox();
	auto* second_box = new QDoubleSpinBox();
	for (auto* box : { first_box, second_box })
	{
		box->setRange(-1e9, 1e9);
		box->setAccelerated(true);
	}
	layout->addRow(first_label, first_box);
	layout->addRow(second_label, second_box);

	auto const centre = MapCoordF(map_widget->getMapView()->center());
	auto const update = [this, kind_box, first_label, second_label, first_box, second_box, centre]() {
		auto const kind = CoordinateKind(kind_box->currentData().toInt());
		auto const set_format = [first_box, second_box](int decimals, const QString& suffix) {
			for (auto* box : { first_box, second_box })
			{
				box->setDecimals(decimals);
				box->setSuffix(suffix);
			}
		};
		switch (kind)
		{
		case MapCoordinates:
			first_label->setText(tr("X:"));
			second_label->setText(tr("Y:"));
			set_format(2, tr(" mm"));
			first_box->setValue(centre.x());
			second_box->setValue(-centre.y());
			break;
		case ProjectedCoordinates:
			{
				first_label->setText(tr("Easting:"));
				second_label->setText(tr("Northing:"));
				set_format(2, tr(" m"));
				auto const projected = map->getGeoreferencing().toProjectedCoords(centre);
				first_box->setValue(projected.x());
				second_box->setValue(projected.y());
				break;
			}
		case GeographicCoordinates:
			{
				first_label->setText(tr("Latitude:"));
				second_label->setText(tr("Longitude:"));
				set_format(7, QString(QChar(0x00b0)));
				auto const latlon = map->getGeoreferencing().toGeographicCoords(centre);
				first_box->setValue(latlon.latitude());
				second_box->setValue(latlon.longitude());
				break;
			}
		}
	};
	update();
	connect(kind_box, qOverload<int>(&QComboBox::currentIndexChanged), &dialog, update);

	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	layout->addRow(buttons);
	connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	if (dialog.exec() != QDialog::Accepted)
		return;

	bool ok = false;
	auto const position = toMapCoordinates(CoordinateKind(kind_box->currentData().toInt()), first_box->value(), second_box->value(), &ok);
	if (ok)
		moveTo(position);
}


// ### RulerWidget ###

RulerWidget::RulerWidget(Qt::Orientation orientation, ViewAids* aids, MapWidget* map_widget, QWidget* parent)
: QWidget(parent)
, ruler_orientation(orientation)
, aids(aids)
, map_widget(map_widget)
{
	if (orientation == Qt::Horizontal)
	{
		setFixedHeight(thickness);
		setCursor(Qt::SplitVCursor);
	}
	else
	{
		setFixedWidth(thickness);
		setCursor(Qt::SplitHCursor);
	}
	setToolTip(ViewAids::tr("Drag into the map to add a guide"));
}

RulerWidget::~RulerWidget() = default;

QPoint RulerWidget::toMapWidget(const QPoint& pos) const
{
	return map_widget->mapFrom(window(), mapTo(window(), pos));
}

std::vector<RulerTick> RulerWidget::ticks() const
{
	auto const origin = toMapWidget(QPoint(0, 0));
	if (ruler_orientation == Qt::Horizontal)
	{
		auto const v0 = aids->rulerValue(Qt::Horizontal, map_widget->viewportToMapF(QPointF(origin.x(), 0)));
		auto const v1 = aids->rulerValue(Qt::Horizontal, map_widget->viewportToMapF(QPointF(origin.x() + 1000, 0)));
		return rulerTicks(v0, (v1 - v0) / 1000, width());
	}
	auto const v0 = aids->rulerValue(Qt::Vertical, map_widget->viewportToMapF(QPointF(0, origin.y())));
	auto const v1 = aids->rulerValue(Qt::Vertical, map_widget->viewportToMapF(QPointF(0, origin.y() + 1000)));
	return rulerTicks(v0, (v1 - v0) / 1000, height());
}

void RulerWidget::paintEvent(QPaintEvent* /*event*/)
{
	QPainter painter(this);
	painter.fillRect(rect(), palette().window());
	painter.setPen(palette().color(QPalette::WindowText));
	auto font = painter.font();
	font.setPixelSize(9);
	painter.setFont(font);

	auto const horizontal = ruler_orientation == Qt::Horizontal;
	auto const length = horizontal ? height() : width();
	if (horizontal)
		painter.drawLine(0, height() - 1, width(), height() - 1);
	else
		painter.drawLine(width() - 1, 0, width() - 1, height());

	for (auto const& tick : ticks())
	{
		auto const p = int(std::lround(tick.pixel));
		auto const tick_length = tick.labelled ? length : length / 4;
		if (horizontal)
			painter.drawLine(p, height() - tick_length, p, height());
		else
			painter.drawLine(width() - tick_length, p, width(), p);
		if (!tick.labelled)
			continue;
		auto const label = QString::number(tick.value, 'g', 6);
		if (horizontal)
		{
			painter.drawText(QPoint(p + 2, 9), label);
		}
		else
		{
			painter.save();
			painter.translate(9, p - 2);
			painter.rotate(-90);
			painter.drawText(QPoint(0, 0), label);
			painter.restore();
		}
	}
}

void RulerWidget::mousePressEvent(QMouseEvent* event)
{
	if (event->button() == Qt::LeftButton)
		aids->beginGuideDrag(ruler_orientation);
}

void RulerWidget::mouseMoveEvent(QMouseEvent* event)
{
	aids->updateGuideDrag(toMapWidget(event->pos()));
}

void RulerWidget::mouseReleaseEvent(QMouseEvent* event)
{
	if (event->button() == Qt::LeftButton)
		aids->finishGuideDrag(toMapWidget(event->pos()));
}

void RulerWidget::contextMenuEvent(QContextMenuEvent* event)
{
	QMenu menu;
	auto* group = new QActionGroup(&menu);
	auto* mm = menu.addAction(ViewAids::tr("Millimetres on paper"));
	auto* m = menu.addAction(ViewAids::tr("Metres on the ground"));
	for (auto* action : { mm, m })
	{
		action->setCheckable(true);
		group->addAction(action);
	}
	(aids->rulerUnit() == ViewAids::GroundMetres ? m : mm)->setChecked(true);
	menu.addSeparator();
	auto* reset = menu.addAction(ViewAids::tr("Reset origin"));
	auto* chosen = menu.exec(event->globalPos());
	if (chosen == mm)
		aids->setRulerUnit(ViewAids::PaperMillimetres);
	else if (chosen == m)
		aids->setRulerUnit(ViewAids::GroundMetres);
	else if (chosen == reset)
		aids->resetRulerOrigin();
}


}  // namespace OpenOrienteering
