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


#include "course_canvases_dialog.h"

#include <algorithm>
#include <cstddef>
#include <iterator>

#include <Qt>
#include <QBrush>
#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QIODevice>
#include <QInputDialog>
#include <QLabel>
#include <QLatin1Char>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QPushButton>
#include <QSaveFile>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QVariant>

#include "core/background_extents.h"
#include "core/extents.h"
#include "core/map.h"
#include "core/symbols/symbol.h"
#include "course/course_data.h"
#include "course/course_parts.h"
#include "templates/template.h"

namespace OpenOrienteering {

namespace {

/// The canvas which has the entry, or -1.
int canvasOf(const CourseCanvases& canvases, const CanvasCourse& entry)
{
	for (std::size_t c = 0; c < canvases.canvases.size(); ++c)
	{
		auto const& courses = canvases.canvases[c].courses;
		auto const has = std::any_of(begin(courses), end(courses), [&entry](const CanvasCourse& other) {
			return other.course == entry.course && other.part == entry.part;
		});
		if (has)
			return int(c);
	}
	return -1;
}

}  // namespace



CourseCanvasesDialog::CourseCanvasesDialog(const Map* map, QWidget* parent)
: QDialog(parent)
, map(map)
, data(map->courseData().canvases)
{
	setWindowTitle(tr("Canvases"));
	
	// What can be given to a canvas: all controls, the courses, and the parts of a course with a map exchange or flip.
	entries.emplace_back();
	for (auto const& course : map->courseData().courses)
	{
		auto const parts = courseParts(course);
		for (std::size_t p = 0; p < parts.size(); ++p)
		{
			CanvasCourse entry;
			entry.course = course.name;
			if (parts.size() > 1)
			{
				entry.part = int(p);
				entry.flip = parts[p].ends_with == CoursePart::MapFlip || (p > 0 && parts[p - 1].ends_with == CoursePart::MapFlip);
			}
			entries.push_back(entry);
		}
	}
	
	active_check = new QCheckBox(tr("Use the canvases in the export of the course maps"));
	active_check->setObjectName(QStringLiteral("canvases-active"));
	active_check->setChecked(data.active);
	
	canvas_list = new QListWidget();
	canvas_list->setObjectName(QStringLiteral("canvases-list"));
	auto* new_button = new QPushButton(tr("New..."));
	rename_button = new QPushButton(tr("Rename..."));
	delete_button = new QPushButton(tr("Delete"));
	auto* list_layout = new QVBoxLayout();
	list_layout->addWidget(new QLabel(tr("Canvas:")));
	list_layout->addWidget(canvas_list, 1);
	list_layout->addWidget(new_button);
	list_layout->addWidget(rename_button);
	list_layout->addWidget(delete_button);
	// the design notes, "Canvases: the XML files"
	auto* load_xml_button = new QPushButton(tr("Load from XML..."));
	load_xml_button->setObjectName(QStringLiteral("canvases-load-xml"));
	auto* save_xml_button = new QPushButton(tr("Save as XML..."));
	save_xml_button->setObjectName(QStringLiteral("canvases-save-xml"));
	list_layout->addSpacing(8);
	list_layout->addWidget(load_xml_button);
	list_layout->addWidget(save_xml_button);
	
	scale_edit = new QSpinBox();
	scale_edit->setObjectName(QStringLiteral("canvas-scale"));
	scale_edit->setRange(0, 1000000);
	scale_edit->setSingleStep(500);
	scale_edit->setPrefix(QStringLiteral("1:"));
	scale_edit->setSpecialValueText(tr("Scale of the map"));
	extent_box = new QComboBox();
	extent_box->setObjectName(QStringLiteral("canvas-extent"));
	extent_box->addItem(tr("(none)"));
	for (auto const& extent : map->extents())
		extent_box->addItem(extent.name);
	extent_box->setToolTip(tr("The extents of the map are made with the command Define extent."));
	// the design notes, "Canvases: load extent from background map"
	background_extents = backgroundMapExtents(*map);
	load_extent_box = new QComboBox();
	load_extent_box->setObjectName(QStringLiteral("canvas-load-extent"));
	load_extent_box->addItem(QString{});
	for (auto const& entry : background_extents)
		load_extent_box->addItem(backgroundExtentLabel(entry));
	load_extent_box->setEnabled(!background_extents.empty());
	box_edit = new QDoubleSpinBox();
	box_edit->setObjectName(QStringLiteral("canvas-box"));
	box_edit->setRange(0.0, 20.0);
	box_edit->setDecimals(2);
	box_edit->setSingleStep(0.5);
	box_edit->setSuffix(QLatin1Char(' ') + tr("mm"));
	box_edit->setSpecialValueText(tr("As in the course options"));
	rows_edit = new QSpinBox();
	rows_edit->setObjectName(QStringLiteral("canvas-rows"));
	rows_edit->setRange(0, 200);
	rows_edit->setSpecialValueText(tr("As in the course options"));
	
	course_list = new QListWidget();
	course_list->setObjectName(QStringLiteral("canvas-courses"));
	course_list->setToolTip(tr("Yellow: no canvas has it. Green: this canvas has it. What is checked here is taken from the canvas which had it."));
	for (auto const& entry : entries)
	{
		auto* item = new QListWidgetItem(entryText(entry), course_list);
		item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
		item->setCheckState(Qt::Unchecked);
	}
	symbol_list = new QListWidget();
	symbol_list->setObjectName(QStringLiteral("canvas-symbols"));
	symbol_list->setToolTip(tr("The course symbols which are checked are not printed on the maps of this canvas."));
	for (int i = 0; i < map->getNumSymbols(); ++i)
	{
		auto const* symbol = map->getSymbol(i);
		auto const first = symbol->getNumberComponent(0);
		if (symbol->courseRole() == Symbol::NoCourseRole && (first < 700 || first > 799))
			continue;
		auto* item = new QListWidgetItem(symbol->getNumberAsString() + QLatin1Char(' ') + symbol->getPlainTextName(), symbol_list);
		item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
		item->setCheckState(Qt::Unchecked);
		item->setData(Qt::UserRole, canvasSymbolNumber(first, symbol->getNumberComponent(1)));
	}
	
	// the design notes, "Canvases: the background maps"
	template_list = new QListWidget();
	template_list->setObjectName(QStringLiteral("canvas-templates"));
	template_list->setToolTip(tr("The background maps which are checked are not printed on the maps of this canvas."));
	QStringList template_names;
	for (int i = 0; i < map->getNumTemplates(); ++i)
	{
		auto const& name = map->getTemplate(i)->getTemplateFilename();
		if (!template_names.contains(name))
			template_names.push_back(name);
	}
	// Those which the canvases of a file name, whether the map has them or not.
	for (auto const& name : data.used_templates)
	{
		if (!template_names.contains(name))
			template_names.push_back(name);
	}
	for (auto const& canvas : data.canvases)
	{
		for (auto const& name : canvas.hidden_templates)
		{
			if (!template_names.contains(name))
				template_names.push_back(name);
		}
	}
	for (auto const& name : template_names)
	{
		auto* item = new QListWidgetItem(name, template_list);
		item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
		item->setCheckState(Qt::Unchecked);
	}

	auto* form = new QFormLayout();
	form->addRow(tr("Export scale:"), scale_edit);
	form->addRow(tr("Extent:"), extent_box);
	form->addRow(tr("Load extent from background map:"), load_extent_box);
	form->addRow(tr("Description box:"), box_edit);
	form->addRow(tr("Rows of the description:"), rows_edit);
	auto* lists = new QHBoxLayout();
	auto* courses_layout = new QVBoxLayout();
	courses_layout->addWidget(new QLabel(tr("Used for:")));
	courses_layout->addWidget(course_list, 1);
	auto* symbols_layout = new QVBoxLayout();
	symbols_layout->addWidget(new QLabel(tr("Hidden course symbols:")));
	symbols_layout->addWidget(symbol_list, 1);
	symbols_layout->addWidget(new QLabel(tr("Hidden background maps:")));
	symbols_layout->addWidget(template_list, 1);
	lists->addLayout(courses_layout, 1);
	lists->addLayout(symbols_layout, 1);
	canvas_panel = new QWidget();
	auto* panel_layout = new QVBoxLayout(canvas_panel);
	panel_layout->setContentsMargins(0, 0, 0, 0);
	panel_layout->addLayout(form);
	panel_layout->addLayout(lists, 1);
	
	auto* middle = new QHBoxLayout();
	middle->addLayout(list_layout);
	middle->addWidget(canvas_panel, 1);
	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	auto* layout = new QVBoxLayout(this);
	layout->addLayout(middle, 1);
	layout->addWidget(active_check);
	layout->addWidget(buttons);
	
	connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	connect(active_check, &QCheckBox::toggled, this, [this](bool checked) { data.active = checked; });
	connect(canvas_list, &QListWidget::currentRowChanged, this, &CourseCanvasesDialog::refreshCanvas);
	connect(new_button, &QPushButton::clicked, this, &CourseCanvasesDialog::newClicked);
	connect(rename_button, &QPushButton::clicked, this, &CourseCanvasesDialog::renameClicked);
	connect(delete_button, &QPushButton::clicked, this, [this]() { removeCanvas(currentCanvas()); });
	connect(load_xml_button, &QPushButton::clicked, this, &CourseCanvasesDialog::loadXmlClicked);
	connect(save_xml_button, &QPushButton::clicked, this, &CourseCanvasesDialog::saveXmlClicked);
	connect(scale_edit, QOverload<int>::of(&QSpinBox::valueChanged), this, &CourseCanvasesDialog::valuesChanged);
	connect(extent_box, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &CourseCanvasesDialog::valuesChanged);
	connect(load_extent_box, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &CourseCanvasesDialog::backgroundExtentChosen);
	connect(box_edit, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &CourseCanvasesDialog::valuesChanged);
	connect(rows_edit, QOverload<int>::of(&QSpinBox::valueChanged), this, &CourseCanvasesDialog::valuesChanged);
	connect(course_list, &QListWidget::itemChanged, this, &CourseCanvasesDialog::courseChanged);
	connect(symbol_list, &QListWidget::itemChanged, this, &CourseCanvasesDialog::symbolChanged);
	connect(template_list, &QListWidget::itemChanged, this, &CourseCanvasesDialog::templateChanged);
	
	refreshList(data.canvases.empty() ? -1 : 0);
}

CourseCanvasesDialog::~CourseCanvasesDialog() = default;


QString CourseCanvasesDialog::entryText(const CanvasCourse& entry)
{
	if (entry.course.isEmpty())
		return tr("All controls");
	if (entry.part < 0)
		return entry.course;
	return entry.flip ? tr("%1 - Map flip #%2").arg(entry.course).arg(entry.part + 1)
	                  : tr("%1 - Map exchange #%2").arg(entry.course).arg(entry.part + 1);
}


int CourseCanvasesDialog::currentCanvas() const
{
	return canvas_list->currentRow();
}


void CourseCanvasesDialog::setCurrentCanvas(int index)
{
	canvas_list->setCurrentRow(index);
}


bool CourseCanvasesDialog::addCanvas(const QString& name)
{
	auto const trimmed = name.trimmed();
	auto const taken = std::any_of(begin(data.canvases), end(data.canvases), [&trimmed](const CourseCanvas& canvas) { return canvas.name == trimmed; });
	if (trimmed.isEmpty() || taken)
		return false;
	CourseCanvas canvas;
	canvas.name = trimmed;
	data.canvases.push_back(canvas);
	refreshList(int(data.canvases.size()) - 1);
	return true;
}


bool CourseCanvasesDialog::renameCanvas(int index, const QString& name)
{
	auto const trimmed = name.trimmed();
	if (index < 0 || std::size_t(index) >= data.canvases.size() || trimmed.isEmpty())
		return false;
	for (std::size_t c = 0; c < data.canvases.size(); ++c)
	{
		if (int(c) != index && data.canvases[c].name == trimmed)
			return false;
	}
	data.canvases[std::size_t(index)].name = trimmed;
	refreshList(index);
	return true;
}


void CourseCanvasesDialog::removeCanvas(int index)
{
	if (index < 0 || std::size_t(index) >= data.canvases.size())
		return;
	data.canvases.erase(begin(data.canvases) + index);
	refreshList(std::min(index, int(data.canvases.size()) - 1));
}


void CourseCanvasesDialog::refreshList(int current)
{
	{
		const QSignalBlocker block(canvas_list);
		canvas_list->clear();
		for (auto const& canvas : data.canvases)
			canvas_list->addItem(canvas.name);
		canvas_list->setCurrentRow(current);
	}
	refreshCanvas();
}


void CourseCanvasesDialog::refreshCanvas()
{
	refreshing = true;
	auto const index = currentCanvas();
	auto const valid = index >= 0 && std::size_t(index) < data.canvases.size();
	canvas_panel->setEnabled(valid);
	rename_button->setEnabled(valid);
	delete_button->setEnabled(valid);
	const CourseCanvas empty;
	auto const& canvas = valid ? data.canvases[std::size_t(index)] : empty;
	
	scale_edit->setValue(int(canvas.scale));
	extent_box->setCurrentIndex(std::max(0, extent_box->findText(canvas.extent)));
	box_edit->setValue(canvas.box_size);
	rows_edit->setValue(canvas.max_rows);
	
	for (std::size_t e = 0; e < entries.size(); ++e)
	{
		auto* item = course_list->item(int(e));
		auto const owner = canvasOf(data, entries[e]);
		item->setCheckState(valid && owner == index ? Qt::Checked : Qt::Unchecked);
		item->setBackground(owner < 0 ? QBrush(QColor(255, 255, 160))
		                              : owner == index ? QBrush(QColor(170, 235, 170)) : QBrush());
	}
	for (int i = 0; i < symbol_list->count(); ++i)
	{
		auto* item = symbol_list->item(i);
		item->setCheckState(canvas.hidden_symbols.contains(item->data(Qt::UserRole).toString()) ? Qt::Checked : Qt::Unchecked);
	}
	for (int i = 0; i < template_list->count(); ++i)
	{
		auto* item = template_list->item(i);
		item->setCheckState(canvas.hidden_templates.contains(item->text()) ? Qt::Checked : Qt::Unchecked);
	}
	refreshing = false;
}


void CourseCanvasesDialog::valuesChanged()
{
	auto const index = currentCanvas();
	if (refreshing || index < 0 || std::size_t(index) >= data.canvases.size())
		return;
	auto& canvas = data.canvases[std::size_t(index)];
	canvas.scale = unsigned(scale_edit->value());
	canvas.extent = extent_box->currentIndex() > 0 ? extent_box->currentText() : QString{};
	canvas.box_size = box_edit->value();
	canvas.max_rows = rows_edit->value();
}


void CourseCanvasesDialog::courseChanged(QListWidgetItem* item)
{
	auto const index = currentCanvas();
	auto const row = course_list->row(item);
	if (refreshing || index < 0 || std::size_t(index) >= data.canvases.size() || row < 0 || std::size_t(row) >= entries.size())
		return;
	auto const& entry = entries[std::size_t(row)];
	// It leaves the canvas which had it.
	for (auto& canvas : data.canvases)
	{
		canvas.courses.erase(std::remove_if(begin(canvas.courses), end(canvas.courses), [&entry](const CanvasCourse& other) {
			return other.course == entry.course && other.part == entry.part;
		}), end(canvas.courses));
	}
	if (item->checkState() == Qt::Checked)
		data.canvases[std::size_t(index)].courses.push_back(entry);
	refreshCanvas();
}


void CourseCanvasesDialog::symbolChanged(QListWidgetItem* item)
{
	auto const index = currentCanvas();
	if (refreshing || index < 0 || std::size_t(index) >= data.canvases.size())
		return;
	auto const number = item->data(Qt::UserRole).toString();
	auto& hidden = data.canvases[std::size_t(index)].hidden_symbols;
	hidden.removeAll(number);
	if (item->checkState() == Qt::Checked)
	{
		hidden.push_back(number);
		// What a canvas hides is a symbol which the canvases govern.
		if (!data.used_symbols.contains(number))
			data.used_symbols.push_back(number);
	}
}


// the design notes, "Canvases: the background maps"
void CourseCanvasesDialog::templateChanged(QListWidgetItem* item)
{
	auto const index = currentCanvas();
	if (refreshing || index < 0 || std::size_t(index) >= data.canvases.size())
		return;
	auto const name = item->text();
	auto& hidden = data.canvases[std::size_t(index)].hidden_templates;
	hidden.removeAll(name);
	if (item->checkState() == Qt::Checked)
	{
		hidden.push_back(name);
		// What a canvas hides is a background map which the canvases govern.
		if (!data.used_templates.contains(name))
			data.used_templates.push_back(name);
	}
}


// the design notes, "Canvases: load extent from background map"
void CourseCanvasesDialog::backgroundExtentChosen(int index)
{
	if (index < 1 || std::size_t(index) > background_extents.size())
		return;
	auto const& extent = background_extents[std::size_t(index - 1)].extent;
	keepLoadedExtent(extent);
	extent_box->setCurrentIndex(extent_box->findText(extent.name));
	QSignalBlocker blocker(load_extent_box);
	load_extent_box->setCurrentIndex(0);
}

void CourseCanvasesDialog::keepLoadedExtent(const Extent& extent)
{
	auto const same = std::find_if(loaded_extents.begin(), loaded_extents.end(), [&extent](const Extent& other) { return other.name == extent.name; });
	if (same != loaded_extents.end())
		*same = extent;
	else
		loaded_extents.push_back(extent);
	if (extent_box->findText(extent.name) < 0)
	{
		const QSignalBlocker block(extent_box);
		extent_box->addItem(extent.name);
	}
}


// the design notes, "Canvases: the XML files"
bool CourseCanvasesDialog::loadXml(const QString& path)
{
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly))
		return false;
	CourseCanvases read;
	std::vector<Extent> extents;
	if (!canvasesFromXmlScript(file.readAll(), read, extents))
		return false;
	for (auto const& extent : extents)
		keepLoadedExtent(extent);
	data = read;
	{
		const QSignalBlocker block(active_check);
		active_check->setChecked(data.active);
	}
	refreshList(data.canvases.empty() ? -1 : 0);
	return true;
}


bool CourseCanvasesDialog::saveXml(const QString& path) const
{
	// The extents of the map, and those which the dialog took in.
	auto extents = map->extents();
	for (auto const& extent : loaded_extents)
	{
		auto const same = std::find_if(extents.begin(), extents.end(), [&extent](const Extent& other) { return other.name == extent.name; });
		if (same != extents.end())
			*same = extent;
		else
			extents.push_back(extent);
	}
	QSaveFile file(path);
	if (!file.open(QIODevice::WriteOnly))
		return false;
	file.write(canvasesToXmlScript(data, extents));
	return file.commit();
}


void CourseCanvasesDialog::loadXmlClicked()
{
	auto const path = QFileDialog::getOpenFileName(this, tr("Load from XML"), QString{}, tr("XML files (*.xml);;All files (*.*)"));
	if (!path.isEmpty() && !loadXml(path))
		QMessageBox::warning(this, tr("Load from XML"), tr("The file \"%1\" has no canvases which can be read.").arg(QDir::toNativeSeparators(path)));
}


void CourseCanvasesDialog::saveXmlClicked()
{
	auto path = QFileDialog::getSaveFileName(this, tr("Save as XML"), QString{}, tr("XML files (*.xml)"));
	if (path.isEmpty())
		return;
	if (!path.endsWith(QLatin1String(".xml"), Qt::CaseInsensitive))
		path += QLatin1String(".xml");
	if (!saveXml(path))
		QMessageBox::warning(this, tr("Save as XML"), tr("Cannot write the file \"%1\".").arg(QDir::toNativeSeparators(path)));
}


void CourseCanvasesDialog::newClicked()
{
	auto ok = false;
	auto const name = QInputDialog::getText(this, tr("New canvas"), tr("Name:"), QLineEdit::Normal,
	                                        tr("Canvas %1").arg(data.canvases.size() + 1), &ok);
	if (ok && !addCanvas(name))
		QMessageBox::warning(this, tr("New canvas"), tr("A canvas needs a name which no other canvas has."));
}


void CourseCanvasesDialog::renameClicked()
{
	auto const index = currentCanvas();
	if (index < 0 || std::size_t(index) >= data.canvases.size())
		return;
	auto ok = false;
	auto const name = QInputDialog::getText(this, tr("Rename canvas"), tr("Name:"), QLineEdit::Normal, data.canvases[std::size_t(index)].name, &ok);
	if (ok && !renameCanvas(index, name))
		QMessageBox::warning(this, tr("Rename canvas"), tr("A canvas needs a name which no other canvas has."));
}


}  // namespace OpenOrienteering
