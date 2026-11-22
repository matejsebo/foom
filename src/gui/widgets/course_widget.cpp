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


#include "course_widget.h"

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <map>
#include <utility>
#include <vector>

#include <QAbstractItemView>
#include <QAction>
#include <QBrush>
#include <QChar>
#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QImage>
#include <QInputDialog>
#include <QLabel>
#include <QLatin1Char>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QMetaObject>
#include <QPalette>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QSet>
#include <QSignalBlocker>
#include <QSize>
#include <QSpinBox>
#include <QStyle>
#include <QStyleOptionComboBox>
#include <QStylePainter>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QUuid>
#include <QVBoxLayout>
#include <QVariant>
#include <QWheelEvent>

#include "core/map.h"
#include "core/map_part.h"
#include "core/objects/object.h"
#include "core/symbols/symbol.h"
#include "course/course_description.h"
#include "course/course_diagram.h"
#include "course/course_edit.h"
#include "course/course_parts.h"
#include "course/course_relay.h"
#include "course/course_relay_edit.h"
#include "course/course_render.h"
#include "course/course_storage.h"
#include "gui/widgets/course_diagram_widget.h"
#include "undo/map_data_undo.h"
#include "undo/object_undo.h"
#include "undo/undo.h"
#include "undo/undo_manager.h"

namespace OpenOrienteering {

namespace {

/// The value which says that the current item is kept at a refresh.
constexpr int keep_item = -2;

/// The number at the end of a code, for the order of the list; -1 without one.
int trailingNumber(const QString& code)
{
	auto first = code.size();
	while (first > 0 && code[first - 1].isDigit())
		--first;
	auto ok = false;
	auto const number = code.mid(first).toInt(&ok);
	return ok ? number : -1;
}

/// The order of the list of course objects: by kind, then by number, then by code.
bool listOrder(const CourseObject* lhs, const CourseObject* rhs)
{
	if (lhs->kind != rhs->kind)
		return lhs->kind < rhs->kind;
	auto const left = trailingNumber(lhs->code);
	auto const right = trailingNumber(rhs->code);
	if (left != right)
		return left < right;
	return lhs->code < rhs->code;
}

/// The index of the row at the top of a list, to scroll back to it.
int topRow(const QTreeWidget* list)
{
	return list->indexOfTopLevelItem(list->itemAt(1, 1));
}

void scrollToRow(QTreeWidget* list, int row)
{
	if (row >= 0 && list->topLevelItemCount() > 0)
		list->scrollToItem(list->topLevelItem(std::min(row, list->topLevelItemCount() - 1)), QAbstractItemView::PositionAtTop);
}

/// A number of a class as the text of its cell: nothing when it is not given.
QString numberText(int number)
{
	return number < 0 ? QString{} : QString::number(number);
}

/// The text of a cell as a number of a class; false when it is no number.
bool numberOfText(const QString& text, int& number)
{
	auto const trimmed = text.trimmed();
	if (trimmed.isEmpty())
	{
		number = -1;
		return true;
	}
	auto ok = false;
	auto const value = trimmed.toInt(&ok);
	if (!ok || value < 0)
		return false;
	number = value;
	return true;
}

QPushButton* makeButton(const QString& text, const char* name, QHBoxLayout* layout)
{
	auto* button = new QPushButton(text);
	button->setObjectName(QString::fromLatin1(name));
	layout->addWidget(button);
	return button;
}

/// The number of a cell as the OCD records have it: 1010 is "1.010".
QString cellNumberText(int number)
{
	return QStringLiteral("%1.%2").arg(number / 1000).arg(number % 1000, 3, 10, QLatin1Char('0'));
}

/**
 * A list of description symbols. While it is closed it shows the picture
 * of its entry alone, so that six of them stand side by side; its popup
 * is as wide as number and name need.
 */
class CellBox : public QComboBox
{
public:
	using QComboBox::QComboBox;
	
	QSize sizeHint() const override
	{
		return minimumSizeHint();
	}
	
	QSize minimumSizeHint() const override
	{
		QStyleOptionComboBox option;
		initStyleOption(&option);
		return style()->sizeFromContents(QStyle::CT_ComboBox, &option, iconSize() + QSize(2, 2), this);
	}
	
	void showPopup() override
	{
		auto const width = view()->sizeHintForColumn(0) + style()->pixelMetric(QStyle::PM_ScrollBarExtent, nullptr, this) + 8;
		view()->setMinimumWidth(width);
		QComboBox::showPopup();
	}
	
protected:
	void paintEvent(QPaintEvent* /*event*/) override
	{
		QStylePainter painter(this);
		QStyleOptionComboBox option;
		initStyleOption(&option);
		// Number and name are in the popup. Only a symbol which the map has
		// not, and so has no picture, is shown by its number.
		if (!currentData(CourseWidget::CellMissingRole).toBool())
			option.currentText.clear();
		painter.drawComplexControl(QStyle::CC_ComboBox, option);
		painter.drawControl(QStyle::CE_ComboBoxLabel, option);
	}
	
	void wheelEvent(QWheelEvent* event) override
	{
		// The wheel does not change a cell in passing.
		event->ignore();
	}
};

}  // namespace



CourseWidget::CourseWidget(Map* map, QWidget* parent)
: QWidget(parent)
, map(map)
{
	object_list = new QTreeWidget();
	object_list->setObjectName(QStringLiteral("course-objects"));
	object_list->setHeaderLabels({ tr("Code"), tr("Kind"), tr("Courses") });
	object_list->setRootIsDecorated(false);
	object_list->setUniformRowHeights(true);
	object_list->setSelectionMode(QAbstractItemView::ExtendedSelection);
	object_list->setEditTriggers(QAbstractItemView::NoEditTriggers);
	object_list->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
	object_list->setToolTip(tr("The course objects. Those which no course uses are red. Double-click an object to put it into the course."));
	
	auto* object_buttons = new QHBoxLayout();
	code_button = makeButton(tr("Change code..."), "course-object-code", object_buttons);
	delete_object_button = makeButton(tr("Delete"), "course-object-delete", object_buttons);
	text_block_button = makeButton(tr("Add text block..."), "course-text-block", object_buttons);
	text_block_button->setToolTip(tr("A text block is a line of text in the control descriptions of the courses into which it is put."));
	object_buttons->addStretch(1);
	
	// The description of the course object which is selected;
	// the design notes, "The editor of a description".
	auto* cell_layout = new QGridLayout();
	const QString meanings[CourseObject::ColumnCount] = {
	    tr("C: which of several similar features"),
	    tr("D: the feature"),
	    tr("E: its appearance, or a second feature"),
	    tr("F: how the two features meet; a size is entered in the field below"),
	    tr("G: where the flag is"),
	    tr("H: other information"),
	};
	for (int column = 0; column < CourseObject::ColumnCount; ++column)
	{
		const QChar letter = QLatin1Char("CDEFGH"[column]);
		auto* label = new QLabel(QString(letter));
		label->setAlignment(Qt::AlignCenter);
		auto* list = new CellBox();
		list->setObjectName(QStringLiteral("course-cell-%1").arg(letter.toLower()));
		list->setIconSize(QSize(22, 22));
		list->setMaxVisibleItems(20);
		list->setToolTip(meanings[column]);
		cell_layout->addWidget(label, 0, column);
		cell_layout->addWidget(list, 1, column);
		cell_list[column] = list;
		connect(list, QOverload<int>::of(&QComboBox::activated), this, [this, column](int index) { cellChosen(column, index); });
	}
	
	size_edit = new QLineEdit();
	size_edit->setObjectName(QStringLiteral("course-size"));
	size_edit->setMaximumWidth(80);
	size_edit->setToolTip(tr("The size of the feature in metres, for column F: 1.5, or 5/10, or 8x4."));
	funnel_check = new QCheckBox(tr("Funnel only"));
	funnel_check->setObjectName(QStringLiteral("course-funnel"));
	funnel_check->setToolTip(tr("The marked route is the funnel before the finish, and is not taped all the way."));
	auto* size_layout = new QHBoxLayout();
	size_layout->addWidget(new QLabel(tr("Size:")));
	size_layout->addWidget(size_edit);
	size_layout->addWidget(funnel_check);
	size_layout->addStretch(1);
	
	text_edit = new QLineEdit();
	text_edit->setObjectName(QStringLiteral("course-text"));
	text_edit->setToolTip(tr("The description in words, or the text of a text block. While it is empty, the description says the names of the symbols, which are shown in grey."));
	auto* text_layout = new QHBoxLayout();
	text_layout->addWidget(new QLabel(tr("Text:")));
	text_layout->addWidget(text_edit, 1);
	
	selector = new QComboBox();
	selector->setObjectName(QStringLiteral("course-selector"));
	auto* selector_layout = new QHBoxLayout();
	selector_layout->addWidget(new QLabel(tr("Course:")));
	selector_layout->addWidget(selector, 1);
	// The variants of a relay; the design notes, "Relays: variations, variants and their allocation".
	variant_box = new QComboBox();
	variant_box->setObjectName(QStringLiteral("course-variant"));
	variant_box->setToolTip(tr("The variant of the relay which is measured and drawn: the leg, and a letter for the branch of every team variation."));
	variant_box->hide();
	selector_layout->addWidget(variant_box);
	// The parts of a course with a map exchange or flip; the design notes, "The parts of a course".
	part_box = new QComboBox();
	part_box->setObjectName(QStringLiteral("course-part"));
	part_box->setToolTip(tr("The part of the course which is drawn: a map exchange or a map flip ends a part."));
	part_box->hide();
	selector_layout->addWidget(part_box);
	
	// The type of the course and its legs; the design notes,
	// "Editing relays: type, legs and variations in the dock".
	type_box = new QComboBox();
	type_box->setObjectName(QStringLiteral("course-type"));
	type_box->addItem(tr("Normal course"));
	type_box->addItem(tr("Relay"));
	type_box->addItem(tr("One-man relay"));
	legs_edit = new QSpinBox();
	legs_edit->setObjectName(QStringLiteral("course-legs"));
	legs_edit->setRange(2, max_relay_legs);
	legs_edit->setKeyboardTracking(false);
	legs_edit->setToolTip(tr("The number of legs of a relay, or of loops of a one-man relay."));
	auto* type_layout = new QHBoxLayout();
	type_layout->addWidget(new QLabel(tr("Type:")));
	type_layout->addWidget(type_box);
	type_layout->addWidget(new QLabel(tr("Legs:")));
	type_layout->addWidget(legs_edit);
	type_layout->addStretch(1);
	
	// Length and climb of the course; the design notes,
	// "Drawing a course, its length and climb".
	length_label = new QLabel();
	length_label->setObjectName(QStringLiteral("course-length"));
	climb_edit = new QSpinBox();
	climb_edit->setObjectName(QStringLiteral("course-climb"));
	climb_edit->setRange(0, 9999);
	climb_edit->setSuffix(QLatin1Char(' ') + tr("m"));
	climb_edit->setKeyboardTracking(false);
	extra_edit = new QDoubleSpinBox();
	extra_edit->setObjectName(QStringLiteral("course-extra"));
	extra_edit->setRange(-99.99, 99.99);
	extra_edit->setDecimals(2);
	extra_edit->setSingleStep(0.01);
	extra_edit->setSuffix(QLatin1Char(' ') + tr("km"));
	extra_edit->setKeyboardTracking(false);
	extra_edit->setToolTip(tr("A length which is added to the measured one."));
	auto* length_layout = new QHBoxLayout();
	length_layout->addWidget(new QLabel(tr("Length:")));
	length_layout->addWidget(length_label);
	length_layout->addStretch(1);
	auto* climb_layout = new QHBoxLayout();
	climb_layout->addWidget(new QLabel(tr("Climb:")));
	climb_layout->addWidget(climb_edit);
	climb_layout->addWidget(new QLabel(tr("Extra length:")));
	climb_layout->addWidget(extra_edit);
	climb_layout->addStretch(1);
	
	auto* course_buttons = new QHBoxLayout();
	new_course_button = makeButton(tr("New..."), "course-new", course_buttons);
	duplicate_course_button = makeButton(tr("Duplicate..."), "course-duplicate", course_buttons);
	rename_course_button = makeButton(tr("Rename..."), "course-rename", course_buttons);
	delete_course_button = makeButton(tr("Delete"), "course-delete", course_buttons);
	course_buttons->addStretch(1);
	
	item_list = new QTreeWidget();
	item_list->setObjectName(QStringLiteral("course-items"));
	item_list->setHeaderLabels({ tr("No."), tr("Item") });
	item_list->setRootIsDecorated(false);
	item_list->setUniformRowHeights(true);
	item_list->setEditTriggers(QAbstractItemView::NoEditTriggers);
	item_list->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
	
	// the design notes, "Relays: the branch diagram"
	diagram_view = new CourseDiagramWidget();
	diagram_area = new QScrollArea();
	diagram_area->setObjectName(QStringLiteral("course-diagram-area"));
	diagram_area->setWidget(diagram_view);
	diagram_area->setWidgetResizable(false);
	diagram_area->setBackgroundRole(QPalette::Base);
	diagram_area->hide();
	diagram_check = new QCheckBox(tr("Diagram"));
	diagram_check->setObjectName(QStringLiteral("course-diagram-check"));
	diagram_check->setToolTip(tr("Show a course with variations as a diagram, its branches side by side."));
	diagram_check->setChecked(true);
	diagram_check->setEnabled(false);
	
	auto* item_buttons = new QHBoxLayout();
	insert_button = makeButton(tr("Insert"), "course-item-insert", item_buttons);
	insert_button->setToolTip(tr("Put the selected course objects into the course, behind the current item."));
	remove_button = makeButton(tr("Remove"), "course-item-remove", item_buttons);
	up_button = makeButton(tr("Up"), "course-item-up", item_buttons);
	down_button = makeButton(tr("Down"), "course-item-down", item_buttons);
	item_buttons->addStretch(1);
	auto* variation_buttons = new QHBoxLayout();
	other_button = makeButton(tr("Insert other..."), "course-item-insert-other", variation_buttons);
	other_button->setToolTip(tr("Put an item which is no course object behind the current item."));
	auto* other_menu = new QMenu(other_button);
	other_menu->setToolTipsVisible(true);
	auto const addEntry = [other_menu](const QString& text, const char* name) {
		auto* action = other_menu->addAction(text);
		action->setObjectName(QString::fromLatin1(name));
		return action;
	};
	auto* crossing_act = addEntry(tr("Mandatory crossing point"), "course-insert-crossing");
	auto* passage_act = addEntry(tr("Mandatory passage"), "course-insert-passage");
	auto* exchange_act = addEntry(tr("Map exchange"), "course-insert-exchange");
	exchange_act->setToolTip(tr("The runner gets another map here: the items behind are the next part of the course."));
	auto* flip_act = addEntry(tr("Map flip"), "course-insert-flip");
	flip_act->setToolTip(tr("The runner turns the map over here: the items behind are the next part of the course."));
	other_menu->addSeparator();
	team_variation_act = addEntry(tr("Team variation..."), "course-team-variation");
	team_variation_act->setToolTip(tr("Put a variation behind the current item at which the runners of different teams go to different controls."));
	leg_variation_act = addEntry(tr("Leg variation..."), "course-leg-variation");
	leg_variation_act->setToolTip(tr("Put a variation behind the current item at which the runners of different legs go to different controls."));
	other_button->setMenu(other_menu);
	remove_variation_button = makeButton(tr("Remove variation"), "course-variation-remove", variation_buttons);
	remove_variation_button->setToolTip(tr("Take the variation of the current marker out of the course, with all that is in it."));
	variation_buttons->addStretch(1);
	variation_buttons->addWidget(diagram_check);
	
	class_table = new QTableWidget(0, 4);
	class_table->setObjectName(QStringLiteral("course-classes"));
	class_table->setHorizontalHeaderLabels({ tr("Class"), tr("Runners"), tr("First number"), tr("Last number") });
	class_table->verticalHeader()->hide();
	class_table->setSelectionBehavior(QAbstractItemView::SelectRows);
	class_table->setSelectionMode(QAbstractItemView::SingleSelection);
	class_table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
	class_table->horizontalHeader()->setStretchLastSection(true);
	
	auto* class_buttons = new QHBoxLayout();
	add_class_button = makeButton(tr("Add class..."), "course-class-add", class_buttons);
	delete_class_button = makeButton(tr("Delete class"), "course-class-delete", class_buttons);
	class_buttons->addStretch(1);
	
	auto* layout = new QVBoxLayout(this);
	layout->addWidget(object_list, 4);
	layout->addLayout(object_buttons);
	layout->addLayout(cell_layout);
	layout->addLayout(size_layout);
	layout->addLayout(text_layout);
	layout->addLayout(selector_layout);
	layout->addLayout(course_buttons);
	layout->addLayout(type_layout);
	layout->addLayout(length_layout);
	layout->addLayout(climb_layout);
	layout->addWidget(item_list, 4);
	layout->addWidget(diagram_area, 4);
	layout->addLayout(item_buttons);
	layout->addLayout(variation_buttons);
	layout->addWidget(class_table, 2);
	layout->addLayout(class_buttons);
	
	connect(object_list, &QTreeWidget::itemSelectionChanged, this, &CourseWidget::objectRowsSelected);
	connect(object_list, &QTreeWidget::itemDoubleClicked, this, &CourseWidget::objectDoubleClicked);
	connect(selector, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &CourseWidget::courseChosen);
	connect(variant_box, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &CourseWidget::variantChosen);
	connect(item_list, &QTreeWidget::currentItemChanged, this, &CourseWidget::updateButtons);
	connect(item_list, &QTreeWidget::currentItemChanged, this, [this]() {
		diagram_view->setCurrentItem(item_list->indexOfTopLevelItem(item_list->currentItem()));
	});
	connect(diagram_view, &CourseDiagramWidget::itemClicked, this, [this](int item) {
		if (item >= 0 && item < item_list->topLevelItemCount())
			item_list->setCurrentItem(item_list->topLevelItem(item));
	});
	connect(diagram_check, &QCheckBox::toggled, this, &CourseWidget::refreshDiagram);
	connect(class_table, &QTableWidget::cellChanged, this, &CourseWidget::classCellChanged);
	connect(class_table, &QTableWidget::currentCellChanged, this, &CourseWidget::updateButtons);
	
	connect(code_button, &QPushButton::clicked, this, &CourseWidget::changeCodeClicked);
	connect(delete_object_button, &QPushButton::clicked, this, &CourseWidget::deleteObjectClicked);
	connect(text_block_button, &QPushButton::clicked, this, &CourseWidget::addTextBlockClicked);
	connect(size_edit, &QLineEdit::editingFinished, this, &CourseWidget::sizeEntered);
	connect(text_edit, &QLineEdit::editingFinished, this, &CourseWidget::textEntered);
	connect(funnel_check, &QCheckBox::clicked, this, &CourseWidget::setFunnel);
	connect(new_course_button, &QPushButton::clicked, this, &CourseWidget::newCourseClicked);
	connect(duplicate_course_button, &QPushButton::clicked, this, &CourseWidget::duplicateCourseClicked);
	connect(rename_course_button, &QPushButton::clicked, this, &CourseWidget::renameCourseClicked);
	connect(delete_course_button, &QPushButton::clicked, this, &CourseWidget::deleteCourseClicked);
	connect(insert_button, &QPushButton::clicked, this, [this]() { insertSelectedObjects(); showRefusal(); });
	connect(remove_button, &QPushButton::clicked, this, [this]() { removeCurrentItem(); showRefusal(); });
	connect(up_button, &QPushButton::clicked, this, [this]() { moveCurrentItem(-1); showRefusal(); });
	connect(down_button, &QPushButton::clicked, this, [this]() { moveCurrentItem(1); showRefusal(); });
	connect(type_box, QOverload<int>::of(&QComboBox::activated), this, &CourseWidget::typeChosen);
	connect(legs_edit, QOverload<int>::of(&QSpinBox::valueChanged), this, &CourseWidget::legsEntered);
	connect(crossing_act, &QAction::triggered, this, [this]() { insertMarker(CourseItem::CrossingPoint); showRefusal(); });
	connect(passage_act, &QAction::triggered, this, [this]() { insertMarker(CourseItem::Passage); showRefusal(); });
	connect(exchange_act, &QAction::triggered, this, [this]() { insertMarker(CourseItem::MapExchange); showRefusal(); });
	connect(flip_act, &QAction::triggered, this, [this]() { insertMarker(CourseItem::MapFlip); showRefusal(); });
	connect(team_variation_act, &QAction::triggered, this, &CourseWidget::teamVariationClicked);
	connect(leg_variation_act, &QAction::triggered, this, &CourseWidget::legVariationClicked);
	connect(part_box, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() { emit currentCourseChanged(); });
	connect(remove_variation_button, &QPushButton::clicked, this, &CourseWidget::removeVariationClicked);
	connect(add_class_button, &QPushButton::clicked, this, &CourseWidget::addClassClicked);
	connect(delete_class_button, &QPushButton::clicked, this, &CourseWidget::removeCurrentClass);
	
	connect(climb_edit, QOverload<int>::of(&QSpinBox::valueChanged), this, &CourseWidget::setClimb);
	connect(extra_edit, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &CourseWidget::setExtraLength);
	
	connect(map, &Map::courseDataChanged, this, &CourseWidget::refresh);
	// The length follows the objects of the map.
	connect(map, &Map::selectedObjectEdited, this, &CourseWidget::updateLength);
	connect(&map->undoManager(), &UndoManager::canUndoChanged, this, &CourseWidget::updateLength);
	connect(&map->undoManager(), &UndoManager::canRedoChanged, this, &CourseWidget::updateLength);
	connect(map, &Map::objectSelectionChanged, this, &CourseWidget::mapSelectionChanged);
	// The lists of description symbols follow the symbols of the map, once the change of the symbols is done.
	auto const symbols_changed = [this]() {
		cell_lists_dirty = true;
		QMetaObject::invokeMethod(this, [this]() {
			if (cell_lists_dirty && !refreshing)
				refreshDescription();
		}, Qt::QueuedConnection);
	};
	connect(map, &Map::symbolAdded, this, symbols_changed);
	connect(map, &Map::symbolChanged, this, symbols_changed);
	connect(map, &Map::symbolDeleted, this, symbols_changed);
	
	refresh();
}


CourseWidget::~CourseWidget() = default;



QString CourseWidget::kindName(CourseObject::Kind kind)
{
	switch (kind)
	{
	case CourseObject::Start:        return tr("Start");
	case CourseObject::Control:      return tr("Control");
	case CourseObject::MarkedRoute:  return tr("Marked route");
	case CourseObject::Finish:       return tr("Finish");
	case CourseObject::Description:  return tr("Control description");
	case CourseObject::CourseTitle:  return tr("Course title");
	case CourseObject::VariantCode:  return tr("Variant code");
	case CourseObject::StartNumber:  return tr("Start number");
	case CourseObject::TextBlock:    return tr("Text block");
	}
	return {};
}


QString CourseWidget::itemText(const CourseItem& item)
{
	QString text;
	switch (item.type)
	{
	case CourseItem::Start:
	case CourseItem::Control:
	case CourseItem::MarkedRoute:
	case CourseItem::Finish:
	case CourseItem::TextBlock:
		return item.code;
	case CourseItem::CrossingPoint:  text = tr("Mandatory crossing point"); break;
	case CourseItem::Passage:        text = tr("Mandatory passage"); break;
	case CourseItem::MapExchange:    text = tr("Map exchange"); break;
	case CourseItem::MapFlip:        text = tr("Map flip"); break;
	case CourseItem::TeamForkBegin:  text = tr("Team variation, %1 branches").arg(item.count); break;
	case CourseItem::ForkBranch:     text = tr("Branch"); break;
	case CourseItem::TeamForkEnd:    text = tr("End of team variation"); break;
	case CourseItem::LegForkBegin:   text = tr("Leg variation"); break;
	case CourseItem::LegBranch:      text = tr("Branch for legs %1").arg(item.legs); break;
	case CourseItem::LegForkEnd:     text = tr("End of leg variation"); break;
	}
	if (!item.code.isEmpty())
	{
		text += QLatin1Char(' ');
		text += item.code;
	}
	return text;
}



QString CourseWidget::currentCourse() const
{
	return selector->currentIndex() > 0 ? selector->currentText() : QString{};
}


int CourseWidget::courseIndex(const QString& name) const
{
	// The first entry stands for all course objects, whatever a course is called.
	for (int i = 1; i < selector->count(); ++i)
	{
		if (selector->itemText(i) == name)
			return i;
	}
	return name.isEmpty() ? 0 : -1;
}


void CourseWidget::setCurrentCourse(const QString& name)
{
	auto const index = courseIndex(name);
	if (index >= 0)
		selector->setCurrentIndex(index);
}


QStringList CourseWidget::selectedCodes() const
{
	QStringList codes;
	for (int i = 0; i < object_list->topLevelItemCount(); ++i)
	{
		auto const* row = object_list->topLevelItem(i);
		if (row->isSelected())
			codes.push_back(row->data(0, CodeRole).toString());
	}
	return codes;
}



void CourseWidget::refresh()
{
	refreshing = true;
	auto const& data = map->courseData();
	
	auto const course = has_wanted_course ? wanted_course : currentCourse();
	auto const item = wanted_item != keep_item ? wanted_item : item_list->indexOfTopLevelItem(item_list->currentItem());
	auto const codes = selectedCodes();
	// The lists are built anew; they stay where they were scrolled to.
	auto const rows = object_list->topLevelItemCount();
	auto const top_object = topRow(object_list);
	auto const top_item = course == currentCourse() ? topRow(item_list) : -1;
	
	{
		const QSignalBlocker block(selector);
		selector->clear();
		selector->addItem(tr("All course objects"));
		for (auto const& entry : data.courses)
			selector->addItem(entry.name);
		selector->setCurrentIndex(std::max(courseIndex(course), 0));
	}
	
	{
		const QSignalBlocker block(object_list);
		object_list->clear();
		std::vector<const CourseObject*> objects;
		objects.reserve(data.objects.size());
		for (auto const& object : data.objects)
			objects.push_back(&object);
		std::stable_sort(begin(objects), end(objects), listOrder);
		for (auto const* object : objects)
		{
			auto* row = new QTreeWidgetItem(object_list);
			row->setText(0, object->code);
			row->setText(1, kindName(object->kind));
			row->setData(0, CodeRole, object->code);
			row->setData(0, UuidRole, object->object);
			auto unused = false;
			if (canBeCourseItem(object->kind))
			{
				auto const count = coursesUsing(data, object->code).size();
				row->setText(2, QString::number(count));
				unused = count == 0 && object->kind != CourseObject::TextBlock;
			}
			row->setData(0, UnusedRole, unused);
			if (unused)
			{
				for (int column = 0; column < 3; ++column)
					row->setForeground(column, QBrush(QColor(200, 0, 0)));
			}
			if (codes.contains(object->code))
				row->setSelected(true);
		}
		scrollToRow(object_list, top_object);
		// A course object which was just made is shown.
		if (map->getNumSelectedObjects() > 0)
			selectRowsOfMapSelection(object_list->topLevelItemCount() > rows);
	}
	
	refreshItems(item, top_item);
	if (!editing_class)
		refreshClasses();
	refreshCourseValues();
	refreshing = false;
	updateButtons();
	// See mapSelectionChanged().
	codes_at_refresh = selectedCodes();
	if (!just_refreshed)
	{
		just_refreshed = true;
		QMetaObject::invokeMethod(this, [this]() { just_refreshed = false; }, Qt::QueuedConnection);
	}
	emit currentCourseChanged();
}


void CourseWidget::refreshVariants()
{
	auto const name = currentCourse();
	auto const* course = map->courseData().findCourse(name);
	QStringList codes;
	if (course && course->type != Course::Normal)
	{
		for (auto const& variant : relayVariants(relayStructure(*course)))
			codes.push_back(relayVariantCode(variant).trimmed());
	}
	QStringList shown;
	for (int i = 0; i < variant_box->count(); ++i)
		shown.push_back(variant_box->itemText(i));
	
	const QSignalBlocker block(variant_box);
	if (codes != shown || name != variant_course)
	{
		// The same relay keeps its variant; another course begins with its first.
		auto const index = name == variant_course ? variant_box->currentIndex() : 0;
		variant_box->clear();
		variant_box->addItems(codes);
		variant_box->setCurrentIndex(codes.isEmpty() ? -1 : (index >= 0 && index < codes.size() ? index : 0));
		variant_course = name;
	}
	variant_box->setVisible(!codes.isEmpty());
}


void CourseWidget::variantChosen(int /*index*/)
{
	updateLength();
	emit currentCourseChanged();
}


CourseData CourseWidget::previewData() const
{
	auto const& data = map->courseData();
	auto const* course = data.findCourse(currentCourse());
	if (!course || variant_box->isHidden() || variant_box->currentIndex() < 0)
		return data;
	auto const variants = relayVariants(relayStructure(*course));
	auto const index = std::size_t(variant_box->currentIndex());
	return index < variants.size() ? withRelayVariant(data, course->name, variants[index]) : data;
}


void CourseWidget::refreshCourseValues()
{
	refreshVariants();
	refreshParts();
	auto const* course = map->courseData().findCourse(currentCourse());
	{
		const QSignalBlocker block_climb(climb_edit);
		const QSignalBlocker block_extra(extra_edit);
		climb_edit->setValue(course ? course->climb_m : 0);
		extra_edit->setValue(course ? course->extra_km : 0.0);
	}
	climb_edit->setEnabled(course != nullptr);
	extra_edit->setEnabled(course != nullptr);
	{
		const QSignalBlocker block_type(type_box);
		const QSignalBlocker block_legs(legs_edit);
		type_box->setCurrentIndex(course ? int(course->type) : 0);
		legs_edit->setValue(course && course->legs >= 2 ? course->legs : 2);
	}
	type_box->setEnabled(course != nullptr);
	legs_edit->setEnabled(course && course->type != Course::Normal);
	updateLength();
}


void CourseWidget::updateLength()
{
	// A relay is measured by the variant which is chosen.
	auto const data = previewData();
	auto const* course = data.findCourse(currentCourse());
	if (!course)
	{
		length_label->clear();
		return;
	}
	auto const length = measureCourse(*map, data, *course);
	// A dash for what cannot be measured: a course with variations, or an item without its object.
	length_label->setText(length.known ? formatCourseLength(length.total_m) : QStringLiteral("-"));
}


void CourseWidget::refreshItems(int current, int top)
{
	const QSignalBlocker block(item_list);
	item_list->clear();
	auto const& data = map->courseData();
	auto const* course = data.findCourse(currentCourse());
	item_list->setEnabled(course != nullptr);
	if (!course)
	{
		refreshDiagram();
		return;
	}
	
	auto const has_variation = std::any_of(begin(course->items), end(course->items), [](const CourseItem& item) {
		return isVariationItem(item.type);
	});
	auto number = 0;
	// What is in a branch is indented under its marker; the branches of a team variation have their letters.
	auto depth = 0;
	std::vector<int> branches;
	for (auto const& item : course->items)
	{
		auto* row = new QTreeWidgetItem(item_list);
		if (item.type == CourseItem::Control && !has_variation)
			row->setText(0, QString::number(++number));
		auto text = itemText(item);
		auto level = depth;
		switch (item.type)
		{
		case CourseItem::TeamForkBegin:
		case CourseItem::LegForkBegin:
			branches.push_back(0);
			depth += 2;
			break;
		case CourseItem::ForkBranch:
			if (!branches.empty() && branches.back() < 26)
				text = tr("Branch %1").arg(QChar(ushort('A' + branches.back()++)));
			level = std::max(depth - 1, 0);
			break;
		case CourseItem::LegBranch:
			level = std::max(depth - 1, 0);
			break;
		case CourseItem::TeamForkEnd:
		case CourseItem::LegForkEnd:
			depth = std::max(depth - 2, 0);
			level = depth;
			if (!branches.empty())
				branches.pop_back();
			break;
		default:
			break;
		}
		row->setText(1, QString(level * 4, QLatin1Char(' ')) + text);
		if (item.namesObject() && !data.findObject(item.code))
		{
			row->setForeground(1, QBrush(QColor(200, 0, 0)));
			row->setToolTip(1, tr("There is no course object with this code."));
		}
	}
	scrollToRow(item_list, top);
	if (current >= 0 && current < item_list->topLevelItemCount())
		item_list->setCurrentItem(item_list->topLevelItem(current));
	else
		item_list->setCurrentItem(nullptr);
	refreshDiagram();
}


void CourseWidget::refreshDiagram()
{
	auto const* course = map->courseData().findCourse(currentCourse());
	auto const has_variation = course && std::any_of(begin(course->items), end(course->items), [](const CourseItem& item) {
		return isVariationItem(item.type);
	});
	diagram_check->setEnabled(has_variation);
	auto const shown = has_variation && diagram_check->isChecked();
	diagram_view->setDiagram(shown ? courseDiagram(*course) : CourseDiagram{});
	diagram_view->setCurrentItem(item_list->indexOfTopLevelItem(item_list->currentItem()));
	diagram_area->setVisible(shown);
	item_list->setVisible(!shown);
}


void CourseWidget::refreshClasses()
{
	const QSignalBlocker block(class_table);
	auto const& data = map->courseData();
	auto const course = currentCourse();
	class_table->setRowCount(0);
	class_table->setEnabled(!course.isEmpty());
	if (course.isEmpty())
		return;
	
	for (std::size_t i = 0; i < data.classes.size(); ++i)
	{
		auto const& entry = data.classes[i];
		if (entry.course != course)
			continue;
		auto const row = class_table->rowCount();
		class_table->insertRow(row);
		auto const texts = { entry.name, numberText(entry.runners), numberText(entry.first_no), numberText(entry.last_no) };
		auto column = 0;
		for (auto const& text : texts)
		{
			auto* cell = new QTableWidgetItem(text);
			cell->setData(Qt::UserRole, int(i));
			class_table->setItem(row, column++, cell);
		}
	}
}


void CourseWidget::updateButtons()
{
	if (refreshing)
		return;
	auto const& data = map->courseData();
	auto const codes = selectedCodes();
	auto const* course = data.findCourse(currentCourse());
	auto const insertable = std::any_of(codes.begin(), codes.end(), [&data](const QString& code) {
		auto const* object = data.findObject(code);
		return object && canBeCourseItem(object->kind);
	});
	auto const item = item_list->indexOfTopLevelItem(item_list->currentItem());
	auto const movable = course && item >= 0 && std::size_t(item) < course->items.size()
	                     && !isVariationItem(course->items[std::size_t(item)].type);
	
	code_button->setEnabled(codes.size() == 1);
	delete_object_button->setEnabled(!codes.isEmpty());
	duplicate_course_button->setEnabled(course != nullptr);
	rename_course_button->setEnabled(course != nullptr);
	delete_course_button->setEnabled(course != nullptr);
	insert_button->setEnabled(course && insertable);
	remove_button->setEnabled(movable);
	up_button->setEnabled(movable && item > 0);
	down_button->setEnabled(movable && item + 1 < item_list->topLevelItemCount());
	auto const is_relay = course && course->type != Course::Normal;
	other_button->setEnabled(course != nullptr);
	team_variation_act->setEnabled(is_relay);
	leg_variation_act->setEnabled(is_relay);
	remove_variation_button->setEnabled(course && item >= 0 && std::size_t(item) < course->items.size()
	                                    && isVariationItem(course->items[std::size_t(item)].type));
	add_class_button->setEnabled(course != nullptr);
	delete_class_button->setEnabled(course && class_table->currentRow() >= 0);
	refreshDescription();
}



bool CourseWidget::selectRowsOfMapSelection(bool scroll)
{
	QSet<QUuid> selected;
	for (auto const* object : map->selectedObjects())
		selected.insert(object->uuid());
	
	const QSignalBlocker block(object_list);
	object_list->clearSelection();
	QTreeWidgetItem* first = nullptr;
	for (int i = 0; i < object_list->topLevelItemCount(); ++i)
	{
		auto* row = object_list->topLevelItem(i);
		auto const uuid = row->data(0, UuidRole).toUuid();
		if (uuid.isNull() || !selected.contains(uuid))
			continue;
		row->setSelected(true);
		if (!first)
			first = row;
	}
	if (first && scroll)
		object_list->scrollToItem(first);
	return first != nullptr;
}


void CourseWidget::objectRowsSelected()
{
	if (refreshing)
		return;
	syncing_selection = true;
	map->clearObjectSelection(false);
	auto const* part = map->getCurrentPart();
	for (auto const* row : object_list->selectedItems())
	{
		auto const uuid = row->data(0, UuidRole).toUuid();
		auto* object = uuid.isNull() ? nullptr : map->findObject(uuid);
		// Only the objects of the current part can be selected.
		if (object && part && part->findObjectIndex(object) >= 0)
			map->addObjectToSelection(object, false);
	}
	map->emitSelectionChanged();
	syncing_selection = false;
	updateButtons();
}


void CourseWidget::mapSelectionChanged()
{
	if (syncing_selection || refreshing)
		return;
	// An undo or a redo selects the objects of its step. A step of course
	// data has none, and would leave the dock without the row whose
	// description was just changed: the rows stay selected then.
	if (just_refreshed && map->getNumSelectedObjects() == 0 && !codes_at_refresh.isEmpty())
	{
		{
			const QSignalBlocker block(object_list);
			for (int i = 0; i < object_list->topLevelItemCount(); ++i)
			{
				auto* row = object_list->topLevelItem(i);
				row->setSelected(codes_at_refresh.contains(row->data(0, CodeRole).toString()));
			}
		}
		objectRowsSelected();
		return;
	}
	selectRowsOfMapSelection(true);
	updateButtons();
}


void CourseWidget::objectDoubleClicked(QTreeWidgetItem* item, int /*column*/)
{
	if (item)
		insertCodes({ item->data(0, CodeRole).toString() });
}


void CourseWidget::courseChosen(int /*index*/)
{
	if (refreshing)
		return;
	refreshItems(-1);
	refreshClasses();
	refreshCourseValues();
	updateButtons();
	emit currentCourseChanged();
}



bool CourseWidget::changeCode(const QString& old_code, const QString& new_code)
{
	auto data = map->courseData();
	if (!renameCourseObject(data, old_code, new_code))
		return false;
	changeCourseData(*map, std::move(data));
	// The row keeps its place in the selection under its new code.
	if (auto* object = map->courseData().findObject(new_code))
	{
		for (int i = 0; i < object_list->topLevelItemCount(); ++i)
		{
			auto* row = object_list->topLevelItem(i);
			if (row->data(0, CodeRole).toString() == object->code)
			{
				const QSignalBlocker block(object_list);
				row->setSelected(true);
			}
		}
		updateButtons();
	}
	return true;
}


bool CourseWidget::deleteObject(const QString& code)
{
	return deleteObjects({ code });
}


bool CourseWidget::deleteObjects(const QStringList& codes)
{
	auto data = map->courseData();
	std::map<int, std::vector<Object*>> objects_of_part;
	auto found = false;
	auto data_changed = false;
	for (auto const& code : codes)
	{
		auto const* object = data.findObject(code);
		if (!object)
			continue;
		found = true;
		auto* map_object = object->object.isNull() ? nullptr : map->findObject(object->object);
		auto part_index = -1;
		for (int p = 0; map_object && p < map->getNumParts() && part_index < 0; ++p)
		{
			if (map->getPart(std::size_t(p))->findObjectIndex(map_object) >= 0)
				part_index = p;
		}
		if (part_index >= 0)
		{
			// The course object goes with its map object, see syncCourseObjects().
			objects_of_part[part_index].push_back(map_object);
		}
		else
		{
			removeCourseObject(data, code);
			data_changed = true;
		}
	}
	if (!found)
		return false;
	
	std::vector<UndoStep*> steps;
	if (data_changed)
	{
		steps.push_back(new MapDataUndoStep(map, MapDataUndoStep::Courses, MapDataUndoStep::currentXml(*map, MapDataUndoStep::Courses)));
		map->setCourseData(std::move(data));
		map->setOtherDirty();
	}
	for (auto const& entry : objects_of_part)
	{
		auto* step = new AddObjectsUndoStep(map);
		step->setPartIndex(entry.first);
		auto const* part = map->getPart(std::size_t(entry.first));
		for (auto* map_object : entry.second)
			step->addObject(part->findObjectIndex(map_object), map_object);
		step->removeContainedObjects(true);
		steps.push_back(step);
	}
	if (steps.size() == 1)
	{
		map->push(steps.front());
	}
	else
	{
		auto* combined = new CombinedUndoStep(map);
		for (auto* step : steps)
			combined->push(step);
		map->push(combined);
	}
	return true;
}



void CourseWidget::insertCodes(const QStringList& codes)
{
	auto const name = currentCourse();
	auto data = map->courseData();
	auto const* course = data.findCourse(name);
	if (!course)
		return;
	last_refusal.clear();
	auto index = insertIndex(*course);
	auto inserted = false;
	for (auto const& code : codes)
	{
		if (insertCourseItem(data, name, index, code))
		{
			++index;
			inserted = true;
		}
	}
	if (!inserted)
		return;
	last_refusal = relayFaultOfChange(map->courseData(), data, name);
	if (!last_refusal.isEmpty())
		return;
	wanted_item = index - 1;
	changeCourseData(*map, std::move(data));
	wanted_item = keep_item;
}


void CourseWidget::insertSelectedObjects()
{
	insertCodes(selectedCodes());
}


void CourseWidget::removeCurrentItem()
{
	last_refusal.clear();
	auto const index = item_list->indexOfTopLevelItem(item_list->currentItem());
	auto data = map->courseData();
	if (!removeCourseItem(data, currentCourse(), index))
		return;
	// A relay keeps its control between two variations.
	last_refusal = relayFaultOfChange(map->courseData(), data, currentCourse());
	if (!last_refusal.isEmpty())
		return;
	wanted_item = std::min(index, item_list->topLevelItemCount() - 2);
	changeCourseData(*map, std::move(data));
	wanted_item = keep_item;
}


void CourseWidget::moveCurrentItem(int direction)
{
	last_refusal.clear();
	auto const index = item_list->indexOfTopLevelItem(item_list->currentItem());
	auto data = map->courseData();
	if (!moveCourseItem(data, currentCourse(), index, index + direction))
		return;
	last_refusal = relayFaultOfChange(map->courseData(), data, currentCourse());
	if (!last_refusal.isEmpty())
		return;
	wanted_item = index + direction;
	changeCourseData(*map, std::move(data));
	wanted_item = keep_item;
}



bool CourseWidget::addCourse(const QString& name)
{
	auto data = map->courseData();
	if (!OpenOrienteering::addCourse(data, name))
		return false;
	has_wanted_course = true;
	wanted_course = name;
	wanted_item = -1;
	changeCourseData(*map, std::move(data));
	has_wanted_course = false;
	wanted_item = keep_item;
	return true;
}


bool CourseWidget::duplicateCurrentCourse(const QString& name)
{
	auto data = map->courseData();
	if (!duplicateCourse(data, currentCourse(), name))
		return false;
	has_wanted_course = true;
	wanted_course = name;
	wanted_item = -1;
	changeCourseData(*map, std::move(data));
	has_wanted_course = false;
	wanted_item = keep_item;
	return true;
}


bool CourseWidget::renameCurrentCourse(const QString& name)
{
	auto data = map->courseData();
	if (currentCourse().isEmpty() || !renameCourse(data, currentCourse(), name))
		return false;
	has_wanted_course = true;
	wanted_course = name;
	changeCourseData(*map, std::move(data));
	has_wanted_course = false;
	return true;
}


void CourseWidget::removeCurrentCourse()
{
	auto data = map->courseData();
	if (!removeCourse(data, currentCourse()))
		return;
	has_wanted_course = true;
	wanted_course.clear();
	wanted_item = -1;
	changeCourseData(*map, std::move(data));
	has_wanted_course = false;
	wanted_item = keep_item;
}



bool CourseWidget::setCourseType(Course::Type type, int legs)
{
	last_refusal.clear();
	auto data = map->courseData();
	if (!OpenOrienteering::setCourseType(data, currentCourse(), type, legs, &last_refusal))
		return false;
	if (data != map->courseData())
		changeCourseData(*map, std::move(data));
	return true;
}


int CourseWidget::insertIndex(const Course& course) const
{
	auto index = item_list->indexOfTopLevelItem(item_list->currentItem());
	if (index < 0)
		return int(course.items.size());
	++index;
	// Behind the begin of a variation is its first branch.
	if (std::size_t(index) < course.items.size())
	{
		auto const before = course.items[std::size_t(index - 1)].type;
		auto const here = course.items[std::size_t(index)].type;
		if ((before == CourseItem::TeamForkBegin && here == CourseItem::ForkBranch)
		    || (before == CourseItem::LegForkBegin && here == CourseItem::LegBranch))
			++index;
	}
	return index;
}


bool CourseWidget::insertMarker(CourseItem::Type type)
{
	last_refusal.clear();
	auto const name = currentCourse();
	auto data = map->courseData();
	auto const* course = data.findCourse(name);
	if (!course)
		return false;
	auto const index = insertIndex(*course);
	if (!insertCourseMarker(data, name, index, type))
		return false;
	last_refusal = relayFaultOfChange(map->courseData(), data, name);
	if (!last_refusal.isEmpty())
		return false;
	wanted_item = index;
	changeCourseData(*map, std::move(data));
	wanted_item = keep_item;
	return true;
}


void CourseWidget::refreshParts()
{
	auto const name = currentCourse();
	auto const* course = map->courseData().findCourse(name);
	auto const count = course ? int(courseParts(*course).size()) : 1;
	auto const shown = part_box->count() > 0 ? part_box->count() - 1 : 1;
	const QSignalBlocker block(part_box);
	if (count != shown || name != part_course)
	{
		// The same course keeps its part; another course begins with all of them.
		auto const index = name == part_course ? part_box->currentIndex() : 0;
		part_box->clear();
		if (count > 1)
		{
			part_box->addItem(tr("All parts"));
			for (int part = 1; part <= count; ++part)
				part_box->addItem(tr("Part %1").arg(part));
			part_box->setCurrentIndex(index >= 0 && index <= count ? index : 0);
		}
		part_course = name;
	}
	part_box->setVisible(count > 1);
}


int CourseWidget::previewPart() const
{
	return part_box->isHidden() ? -1 : part_box->currentIndex() - 1;
}


QString CourseWidget::previewVariant() const
{
	return variant_box->isHidden() ? QString{} : variant_box->currentText().trimmed();
}


bool CourseWidget::addVariation(bool of_team, int branches, const QStringList& branch_legs)
{
	last_refusal.clear();
	auto const name = currentCourse();
	auto data = map->courseData();
	auto const* course = data.findCourse(name);
	if (!course)
		return false;
	auto const index = insertIndex(*course);
	auto const done = of_team ? insertTeamVariation(data, name, index, branches, &last_refusal)
	                          : insertLegVariation(data, name, index, branch_legs, &last_refusal);
	if (!done)
		return false;
	// The first branch is current, so that Insert fills it.
	wanted_item = index + 1;
	changeCourseData(*map, std::move(data));
	wanted_item = keep_item;
	return true;
}


bool CourseWidget::addTeamVariation(int branches)
{
	return addVariation(true, branches, {});
}


bool CourseWidget::addLegVariation(const QStringList& branch_legs)
{
	return addVariation(false, 0, branch_legs);
}


bool CourseWidget::removeCurrentVariation()
{
	last_refusal.clear();
	auto const name = currentCourse();
	auto const index = item_list->indexOfTopLevelItem(item_list->currentItem());
	auto data = map->courseData();
	auto const* course = data.findCourse(name);
	auto first = -1;
	auto last = -1;
	if (!course || !variationRange(*course, index, first, last) || !removeVariation(data, name, index))
		return false;
	wanted_item = first - 1;
	changeCourseData(*map, std::move(data));
	wanted_item = keep_item;
	return true;
}


void CourseWidget::showRefusal()
{
	if (!last_refusal.isEmpty())
		QMessageBox::warning(this, tr("Course"), last_refusal);
}


void CourseWidget::typeChosen(int index)
{
	auto const* course = map->courseData().findCourse(currentCourse());
	if (!course || index < 0)
		return;
	auto const type = Course::Type(index);
	auto legs = course->legs;
	if (type != Course::Normal && legs < 2)
		legs = type == Course::Relay ? 3 : 2;
	if (!setCourseType(type, legs))
	{
		showRefusal();
		refreshCourseValues();
	}
}


void CourseWidget::legsEntered(int legs)
{
	if (refreshing)
		return;
	// Once the box is done with the click: a message under its arrow would keep it stepping.
	QMetaObject::invokeMethod(this, [this, legs]() {
		auto const* course = map->courseData().findCourse(currentCourse());
		if (!course || course->type == Course::Normal || setCourseType(course->type, legs))
			return;
		showRefusal();
		refreshCourseValues();
	}, Qt::QueuedConnection);
}


void CourseWidget::teamVariationClicked()
{
	auto const* course = map->courseData().findCourse(currentCourse());
	if (!course)
		return;
	auto const legs = int(relayLegsAt(*course, insertIndex(*course)).size());
	if (legs < 2)
	{
		QMessageBox::warning(this, tr("Team variation"), tr("One leg alone is run at this place. A team variation needs two legs at least."));
		return;
	}
	auto ok = false;
	auto const branches = QInputDialog::getInt(this, tr("Team variation"),
	                                           tr("The runners of different teams go to different controls.\nThe number of branches, which is the number of legs at this place (%1) or divides it:").arg(legs),
	                                           legs, 2, legs, 1, &ok);
	if (ok && !addTeamVariation(branches))
		showRefusal();
}


void CourseWidget::legVariationClicked()
{
	auto const* course = map->courseData().findCourse(currentCourse());
	if (!course)
		return;
	auto const legs = relayLegsAt(*course, insertIndex(*course));
	if (legs.size() < 2)
	{
		QMessageBox::warning(this, tr("Leg variation"), tr("One leg alone is run at this place. A leg variation needs two legs at least."));
		return;
	}
	// All legs but the last against the last.
	QStringList first;
	for (std::size_t i = 0; i + 1 < legs.size(); ++i)
		first.push_back(QString::number(legs[i]));
	auto const proposal = QString(first.join(QLatin1Char('-')) + QLatin1String(", ") + QString::number(legs.back()));
	auto ok = false;
	auto text = QInputDialog::getText(this, tr("Leg variation"),
	                                  tr("The runners of different legs go to different controls.\nThe legs of each branch, with dashes between the legs and commas between the branches:"),
	                                  QLineEdit::Normal, proposal, &ok);
	if (!ok)
		return;
	text.replace(QLatin1Char(';'), QLatin1Char(','));
	if (!addLegVariation(text.split(QLatin1Char(','), QString::SkipEmptyParts)))
		showRefusal();
}


void CourseWidget::removeVariationClicked()
{
	auto const* course = map->courseData().findCourse(currentCourse());
	auto const index = item_list->indexOfTopLevelItem(item_list->currentItem());
	auto first = -1;
	auto last = -1;
	if (!course || !variationRange(*course, index, first, last))
		return;
	auto inside = 0;
	for (int i = first; i <= last; ++i)
		inside += isVariationItem(course->items[std::size_t(i)].type) ? 0 : 1;
	auto question = first == last ? tr("The variation of this marker is not whole. Remove the marker?") : tr("Remove this variation?");
	if (inside > 0)
		question += QLatin1Char('\n') + tr("%n item(s) in it leave the course. The course objects stay in the map.", nullptr, inside);
	if (QMessageBox::question(this, tr("Remove variation"), question) == QMessageBox::Yes)
		removeCurrentVariation();
}



void CourseWidget::setClimb(int metres)
{
	auto const name = currentCourse();
	auto data = map->courseData();
	for (auto& course : data.courses)
	{
		if (course.name != name)
			continue;
		course.climb_m = metres;
		course.climb_manual = true;
	}
	if (!refreshing)
		changeCourseData(*map, std::move(data));
}


void CourseWidget::setExtraLength(double km)
{
	auto const name = currentCourse();
	auto data = map->courseData();
	for (auto& course : data.courses)
	{
		if (course.name != name)
			continue;
		course.extra_km = km;
		course.extra_set = true;
	}
	if (!refreshing)
		changeCourseData(*map, std::move(data));
}



QString CourseWidget::descriptionObject() const
{
	auto const codes = selectedCodes();
	if (codes.size() != 1 || !map->courseData().findObject(codes.front()))
		return {};
	return codes.front();
}


void CourseWidget::fillCellLists()
{
	for (int column = 0; column < CourseObject::ColumnCount; ++column)
	{
		auto* list = cell_list[column];
		const QSignalBlocker block(list);
		list->clear();
		list->addItem(tr("(empty)"), 0);
		for (auto const* symbol : descriptionSymbols(*map, CourseObject::Column(column)))
		{
			auto const number = descriptionCellNumber(*symbol);
			const QString text = cellNumberText(number) + QLatin1Char(' ') + symbol->getPlainTextName();
			list->addItem(QIcon(QPixmap::fromImage(symbol->getIcon(map))), text, number);
		}
	}
}


void CourseWidget::refreshDescription()
{
	if (cell_lists_dirty)
	{
		cell_lists_dirty = false;
		fillCellLists();
	}
	
	auto const code = descriptionObject();
	auto const* object = code.isEmpty() ? nullptr : map->courseData().findObject(code);
	auto const has_cells = object && hasDescriptionCells(object->kind);
	auto const has_text = object && hasDescriptionText(object->kind);
	auto const is_route = object && object->kind == CourseObject::MarkedRoute;
	
	for (int column = 0; column < CourseObject::ColumnCount; ++column)
	{
		auto* list = cell_list[column];
		const QSignalBlocker block(list);
		// The entry of a symbol which the map has not is there only while it is the cell.
		auto const last = list->count() - 1;
		if (last > 0 && list->itemData(last, CellMissingRole).toBool())
			list->removeItem(last);
		auto const number = has_cells ? object->cell[std::size_t(column)] : 0;
		auto index = list->findData(number, CellNumberRole);
		if (index < 0)
		{
			list->addItem(cellNumberText(number), number);
			index = list->count() - 1;
			list->setItemData(index, true, CellMissingRole);
		}
		list->setCurrentIndex(index);
		list->setEnabled(has_cells);
	}
	
	auto const set_text = [](QLineEdit* field, const QString& text) {
		// Setting a text moves the cursor, also when it is the text which is there.
		if (field->text() != text)
		{
			const QSignalBlocker block(field);
			field->setText(text);
		}
	};
	set_text(size_edit, has_cells ? courseObjectField(*object, QLatin1Char('s')) : QString{});
	size_edit->setEnabled(has_cells);
	set_text(text_edit, has_text ? object->text : QString{});
	text_edit->setPlaceholderText(has_cells ? automaticCourseObjectText(*map, *object) : QString{});
	text_edit->setEnabled(has_text);
	{
		const QSignalBlocker block(funnel_check);
		funnel_check->setChecked(is_route && courseObjectField(*object, QLatin1Char('m')) == QLatin1String("f"));
	}
	funnel_check->setEnabled(is_route);
}


bool CourseWidget::setCell(CourseObject::Column column, int number)
{
	auto const code = descriptionObject();
	auto data = map->courseData();
	if (code.isEmpty() || !setCourseObjectCell(*map, data, code, column, number))
		return false;
	changeCourseData(*map, std::move(data));
	return true;
}


bool CourseWidget::setSize(const QString& size)
{
	auto const code = descriptionObject();
	auto data = map->courseData();
	if (code.isEmpty() || !setCourseObjectSize(*map, data, code, size))
		return false;
	changeCourseData(*map, std::move(data));
	return true;
}


bool CourseWidget::setDescriptionText(const QString& text)
{
	auto const code = descriptionObject();
	auto data = map->courseData();
	if (code.isEmpty() || !setCourseObjectText(data, code, text))
		return false;
	changeCourseData(*map, std::move(data));
	return true;
}


bool CourseWidget::setFunnel(bool funnel)
{
	auto const code = descriptionObject();
	auto data = map->courseData();
	if (code.isEmpty() || !setMarkedRouteFunnel(data, code, funnel))
		return false;
	changeCourseData(*map, std::move(data));
	return true;
}


QString CourseWidget::addTextBlock(const QString& text)
{
	auto data = map->courseData();
	auto const code = OpenOrienteering::addTextBlock(data, text);
	changeCourseData(*map, std::move(data));
	// The new text block is the one which is selected, so that its text can be changed.
	for (int i = 0; i < object_list->topLevelItemCount(); ++i)
	{
		auto* row = object_list->topLevelItem(i);
		if (row->data(0, CodeRole).toString() != code)
			continue;
		object_list->clearSelection();
		row->setSelected(true);
		object_list->scrollToItem(row);
		break;
	}
	return code;
}


void CourseWidget::cellChosen(int column, int index)
{
	if (refreshing || column < 0 || column >= CourseObject::ColumnCount)
		return;
	setCell(CourseObject::Column(column), cell_list[column]->itemData(index, CellNumberRole).toInt());
	// A choice which is not taken leaves the list as it was.
	refreshDescription();
}


void CourseWidget::sizeEntered()
{
	if (refreshing || !size_edit->isEnabled())
		return;
	setSize(size_edit->text());
	// The field shows the size as it is kept, and the old one when the new one is not taken.
	refreshDescription();
}


void CourseWidget::textEntered()
{
	if (refreshing || !text_edit->isEnabled())
		return;
	setDescriptionText(text_edit->text());
	refreshDescription();
}


void CourseWidget::addTextBlockClicked()
{
	auto ok = false;
	auto const text = QInputDialog::getText(this, tr("Add text block"), tr("Text:"), QLineEdit::Normal, {}, &ok);
	if (ok)
		addTextBlock(text);
}



bool CourseWidget::addClass(const QString& name)
{
	last_refusal.clear();
	auto const course = currentCourse();
	auto data = map->courseData();
	if (course.isEmpty() || !isValidClassName(data, name))
	{
		last_refusal = tr("The name \"%1\" cannot be given. A name is not empty and is not the name of another class.").arg(name);
		return false;
	}
	auto const* found = data.findCourse(course);
	auto const has_class = std::any_of(begin(data.classes), end(data.classes), [&course](const CourseClass& entry) {
		return entry.course == course;
	});
	if (found && found->type != Course::Normal && has_class)
	{
		last_refusal = tr("A relay or a one-man relay can have one class only.");
		return false;
	}
	CourseClass entry;
	entry.name = name;
	entry.course = course;
	data.classes.push_back(entry);
	changeCourseData(*map, std::move(data));
	class_table->setCurrentCell(class_table->rowCount() - 1, 0);
	return true;
}


void CourseWidget::removeCurrentClass()
{
	auto const* cell = class_table->item(class_table->currentRow(), 0);
	if (!cell)
		return;
	auto const index = cell->data(Qt::UserRole).toInt();
	auto data = map->courseData();
	if (index < 0 || std::size_t(index) >= data.classes.size())
		return;
	data.classes.erase(begin(data.classes) + index);
	changeCourseData(*map, std::move(data));
}


void CourseWidget::classCellChanged(int row, int column)
{
	if (refreshing || editing_class)
		return;
	auto* cell = class_table->item(row, column);
	if (!cell)
		return;
	auto const index = cell->data(Qt::UserRole).toInt();
	auto data = map->courseData();
	if (index < 0 || std::size_t(index) >= data.classes.size())
		return;
	
	auto& entry = data.classes[std::size_t(index)];
	auto const text = cell->text();
	switch (column)
	{
	case 0:
		if (text.trimmed() != entry.name && isValidClassName(data, text.trimmed()))
			entry.name = text.trimmed();
		break;
	case 1:
		numberOfText(text, entry.runners);
		break;
	case 2:
		numberOfText(text, entry.first_no);
		break;
	case 3:
		numberOfText(text, entry.last_no);
		break;
	default:
		break;
	}
	
	// The table is not built anew while one of its cells reports a change;
	// the cell gets the text of what the class has now.
	auto const shown = column == 0 ? entry.name
	                   : numberText(column == 1 ? entry.runners : (column == 2 ? entry.first_no : entry.last_no));
	editing_class = true;
	changeCourseData(*map, std::move(data));
	{
		const QSignalBlocker block(class_table);
		cell->setText(shown);
	}
	editing_class = false;
}



void CourseWidget::changeCodeClicked()
{
	auto const codes = selectedCodes();
	if (codes.size() != 1)
		return;
	auto ok = false;
	auto const code = QInputDialog::getText(this, tr("Change code"), tr("Code:"), QLineEdit::Normal, codes.front(), &ok).trimmed();
	if (ok && !changeCode(codes.front(), code))
		QMessageBox::warning(this, tr("Change code"), tr("The code \"%1\" cannot be given. A code is not empty, has no blank and is not the code of another course object.").arg(code));
}


void CourseWidget::deleteObjectClicked()
{
	auto const codes = selectedCodes();
	if (codes.isEmpty())
		return;
	auto const& data = map->courseData();
	QStringList used;
	for (auto const& code : codes)
	{
		auto const courses = coursesUsing(data, code);
		if (!courses.isEmpty())
			used.push_back(tr("%1 is used in: %2").arg(code, courses.join(QLatin1String(", "))));
	}
	if (!used.isEmpty())
	{
		auto const answer = QMessageBox::question(this, tr("Delete course objects"),
		                                          used.join(QLatin1Char('\n')) + QLatin1String("\n\n")
		                                          + tr("Delete the objects and remove them from these courses?"));
		if (answer != QMessageBox::Yes)
			return;
	}
	deleteObjects(codes);
}


void CourseWidget::newCourseClicked()
{
	auto ok = false;
	auto const name = QInputDialog::getText(this, tr("New course"), tr("Name:"), QLineEdit::Normal, {}, &ok).trimmed();
	if (ok && !addCourse(name))
		QMessageBox::warning(this, tr("New course"), tr("The name \"%1\" cannot be given. A name is not empty and is not the name of another course.").arg(name));
}


void CourseWidget::duplicateCourseClicked()
{
	auto const original = currentCourse();
	if (original.isEmpty())
		return;
	QString proposal = original + QLatin1String(" 2");
	for (int number = 3; map->courseData().findCourse(proposal); ++number)
		proposal = original + QLatin1Char(' ') + QString::number(number);
	auto ok = false;
	auto const name = QInputDialog::getText(this, tr("Duplicate course"), tr("Name:"), QLineEdit::Normal, proposal, &ok).trimmed();
	if (ok && !duplicateCurrentCourse(name))
		QMessageBox::warning(this, tr("Duplicate course"), tr("The name \"%1\" cannot be given. A name is not empty and is not the name of another course.").arg(name));
}


void CourseWidget::renameCourseClicked()
{
	auto const original = currentCourse();
	if (original.isEmpty())
		return;
	auto ok = false;
	auto const name = QInputDialog::getText(this, tr("Rename course"), tr("Name:"), QLineEdit::Normal, original, &ok).trimmed();
	if (ok && !renameCurrentCourse(name))
		QMessageBox::warning(this, tr("Rename course"), tr("The name \"%1\" cannot be given. A name is not empty and is not the name of another course.").arg(name));
}


void CourseWidget::deleteCourseClicked()
{
	auto const name = currentCourse();
	if (name.isEmpty())
		return;
	auto const answer = QMessageBox::question(this, tr("Delete course"), tr("Delete the course \"%1\" with its classes?").arg(name));
	if (answer == QMessageBox::Yes)
		removeCurrentCourse();
}


void CourseWidget::addClassClicked()
{
	if (currentCourse().isEmpty())
		return;
	auto ok = false;
	auto const name = QInputDialog::getText(this, tr("Add class"), tr("Name:"), QLineEdit::Normal, {}, &ok).trimmed();
	if (ok && !addClass(name))
		QMessageBox::warning(this, tr("Add class"), last_refusal);
}


}  // namespace OpenOrienteering
