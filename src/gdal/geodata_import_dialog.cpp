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


#include "geodata_import_dialog.h"

#include <algorithm>
#include <map>

#include <Qt>
#include <QAbstractButton>
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLatin1String>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPointF>
#include <QPolygonF>
#include <QPushButton>
#include <QRadioButton>
#include <QVariant>

#include "core/georeferencing.h"
#include "core/map.h"
#include "core/map_coord.h"
#include "gui/util_gui.h"


namespace OpenOrienteering {

namespace {

enum ClipChoice { ClipNone, ClipMapExtent, ClipView, ClipBox };
enum SymbolsChoice { DefaultSymbols, CrtSymbols };

QPolygonF rectPolygon(const QRectF& rect)
{
	return QPolygonF{ QVector<QPointF>{ rect.topLeft(), rect.topRight(), rect.bottomRight(), rect.bottomLeft() } };
}

}  // namespace



GeodataImportDialog::GeodataImportDialog(const QStringList& paths, const Map& map, const QRectF& view_rect, QWidget* parent)
: QDialog(parent, Qt::WindowSystemMenuHint | Qt::WindowTitleHint)
, map(map)
, paths(paths)
, view_rect(view_rect)
{
	setWindowTitle(tr("Import geodata"));
	auto* layout = new QFormLayout(this);
	
	QStringList names;
	for (auto const& path : paths)
		names.push_back(QFileInfo(path).fileName());
	auto* files_label = new QLabel(names.join(QLatin1String(", ")));
	files_label->setWordWrap(true);
	layout->addRow(tr("Files:"), files_label);
	
	layout->addRow(Util::Headline::create(tr("Layers")));
	layer_list = new QListWidget();
	layout->addRow(layer_list);
	
	layout->addRow(Util::Headline::create(tr("Coordinate system")));
	auto const crs_name = paths.isEmpty() ? QString{} : geodataCrsName(paths.front());
	auto* detected_label = new QLabel(crs_name.isEmpty() ? tr("None found: the coordinates are read as the map's projected coordinates.") : crs_name);
	detected_label->setWordWrap(true);
	detected_radio = new QRadioButton(tr("As detected"));
	layout->addRow(detected_radio, detected_label);
	override_radio = new QRadioButton(tr("Override:"));
	crs_edit = new QLineEdit();
	crs_edit->setPlaceholderText(tr("EPSG code or PROJ specification"));
	layout->addRow(override_radio, crs_edit);
	unreferenced_radio = new QRadioButton(tr("Unreferenced:"));
	units_combo = new QComboBox();
	units_combo->addItem(tr("1 m per unit, on the ground"), int(GeodataImportOptions::MetresOnGround));
	units_combo->addItem(tr("1 mm per unit, on paper"), int(GeodataImportOptions::MillimetresOnPaper));
	units_combo->addItem(tr("Custom"), int(GeodataImportOptions::CustomMillimetres));
	layout->addRow(unreferenced_radio, units_combo);
	custom_spin = Util::SpinBox::create(4, 0.0001, 100000.0, tr("mm per unit"));
	custom_spin->setValue(1.0);
	layout->addRow(tr("Custom scale:"), custom_spin);
	offset_x_spin = Util::SpinBox::create(2, -100000.0, 100000.0, tr("mm"));
	offset_y_spin = Util::SpinBox::create(2, -100000.0, 100000.0, tr("mm"));
	auto* offset_layout = new QHBoxLayout();
	offset_layout->addWidget(offset_x_spin);
	offset_layout->addWidget(offset_y_spin);
	layout->addRow(tr("Offset X, Y:"), offset_layout);
	rotation_spin = Util::SpinBox::create(2, -360.0, 360.0, trUtf8("\u00b0"));
	layout->addRow(tr("Rotation:"), rotation_spin);
	auto* crs_group = new QButtonGroup(this);
	crs_group->addButton(detected_radio);
	crs_group->addButton(override_radio);
	crs_group->addButton(unreferenced_radio);
	
	auto const georeferenced = map.getGeoreferencing().getState() == Georeferencing::Geospatial;
	if (georeferenced)
	{
		detected_radio->setChecked(true);
	}
	else
	{
		detected_radio->setEnabled(false);
		override_radio->setEnabled(false);
		unreferenced_radio->setChecked(true);
		layout->addRow(new QLabel(tr("The map has no coordinate reference system, so only unreferenced coordinates can be imported.")));
	}
	
	layout->addRow(Util::Headline::create(tr("Options")));
	clip_combo = new QComboBox();
	clip_combo->addItem(tr("None"), int(ClipNone));
	clip_combo->addItem(tr("Map extent"), int(ClipMapExtent));
	clip_combo->addItem(tr("Current view"), int(ClipView));
	clip_combo->addItem(tr("Box"), int(ClipBox));
	layout->addRow(tr("Clip:"), clip_combo);
	auto* box_layout = new QHBoxLayout();
	QPointF min_corner, max_corner;
	{
		auto const& georef = map.getGeoreferencing();
		auto first = true;
		for (auto const& corner : { view_rect.topLeft(), view_rect.topRight(), view_rect.bottomRight(), view_rect.bottomLeft() })
		{
			auto const p = georef.toProjectedCoords(MapCoordF(corner));
			min_corner = first ? p : QPointF(std::min(min_corner.x(), p.x()), std::min(min_corner.y(), p.y()));
			max_corner = first ? p : QPointF(std::max(max_corner.x(), p.x()), std::max(max_corner.y(), p.y()));
			first = false;
		}
	}
	double const box_values[4] = { min_corner.x(), min_corner.y(), max_corner.x(), max_corner.y() };
	for (int i = 0; i < 4; ++i)
	{
		box_spins[i] = Util::SpinBox::create(2, -100000000.0, 100000000.0, tr("m"));
		box_spins[i]->setValue(box_values[i]);
		box_layout->addWidget(box_spins[i]);
	}
	layout->addRow(tr("Box (E, N, E, N):"), box_layout);
	
	auto const attributes = paths.isEmpty() ? QStringList{} : geodataAttributes(paths.front());
	layer_tag_combo = new QComboBox();
	layer_tag_combo->addItem(tr("None"));
	layer_tag_combo->addItem(tr("Layer name"));
	text_combo = new QComboBox();
	text_combo->addItem(tr("None"));
	for (auto const& attribute : attributes)
	{
		layer_tag_combo->addItem(attribute, attribute);
		text_combo->addItem(attribute, attribute);
	}
	layout->addRow(tr("Layer name tag:"), layer_tag_combo);
	layout->addRow(tr("Text from attribute:"), text_combo);
	
	blocks_check = new QCheckBox(tr("Import block references as point objects"));
	auto const has_dxf = std::any_of(paths.begin(), paths.end(), [](const QString& path) {
		return path.endsWith(QLatin1String(".dxf"), Qt::CaseInsensitive);
	});
	blocks_check->setEnabled(has_dxf);
	layout->addRow(blocks_check);
	
	simplify_spin = Util::SpinBox::create(2, 0.0, 10.0, tr("mm"), 0.05);
	simplify_spin->setSpecialValueText(tr("Off"));
	layout->addRow(tr("Simplify:"), simplify_spin);
	
	symbols_combo = new QComboBox();
	symbols_combo->addItem(tr("Default symbols"), int(DefaultSymbols));
	symbols_combo->addItem(tr("CRT file"), int(CrtSymbols));
	layout->addRow(tr("Symbols:"), symbols_combo);
	crt_edit = new QLineEdit();
	crt_button = new QPushButton(tr("Browse..."));
	auto* crt_layout = new QHBoxLayout();
	crt_layout->addWidget(crt_edit, 1);
	crt_layout->addWidget(crt_button);
	layout->addRow(tr("CRT file:"), crt_layout);
	
	buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	layout->addRow(buttons);
	
	fillLayers();
	
	connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	connect(crt_button, &QPushButton::clicked, this, &GeodataImportDialog::browseCrtFile);
	connect(layer_list, &QListWidget::itemChanged, this, &GeodataImportDialog::updateWidgets);
	for (auto* radio : { detected_radio, override_radio, unreferenced_radio })
		connect(radio, &QAbstractButton::toggled, this, &GeodataImportDialog::updateWidgets);
	for (auto* combo : { units_combo, clip_combo, symbols_combo })
		connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &GeodataImportDialog::updateWidgets);
	connect(crs_edit, &QLineEdit::textChanged, this, &GeodataImportDialog::updateWidgets);
	connect(crt_edit, &QLineEdit::textChanged, this, &GeodataImportDialog::updateWidgets);
	updateWidgets();
}


GeodataImportDialog::~GeodataImportDialog() = default;


void GeodataImportDialog::fillLayers()
{
	struct Entry { GeodataLayer::Geometry geometry; qint64 features; };
	std::map<QString, Entry> layers;
	QStringList order;
	for (auto const& path : paths)
	{
		for (auto const& layer : geodataLayers(path))
		{
			auto found = layers.find(layer.name);
			if (found == layers.end())
			{
				layers.emplace(layer.name, Entry{ layer.geometry, layer.features });
				order.push_back(layer.name);
			}
			else
			{
				found->second.features += layer.features;
				if (found->second.geometry != layer.geometry)
					found->second.geometry = GeodataLayer::Mixed;
			}
		}
	}
	for (auto const& name : order)
	{
		auto const& entry = layers.at(name);
		QString geometry;
		switch (entry.geometry)
		{
		case GeodataLayer::Point:
			geometry = tr("points");
			break;
		case GeodataLayer::Line:
			geometry = tr("lines");
			break;
		case GeodataLayer::Polygon:
			geometry = tr("areas");
			break;
		case GeodataLayer::Mixed:
			geometry = tr("mixed");
			break;
		}
		auto* item = new QListWidgetItem(tr("%1 (%2, %n feature(s))", nullptr, int(entry.features)).arg(name, geometry), layer_list);
		item->setData(Qt::UserRole, name);
		item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
		item->setCheckState(Qt::Checked);
	}
}


void GeodataImportDialog::updateWidgets()
{
	auto const unreferenced = unreferenced_radio->isChecked();
	crs_edit->setEnabled(override_radio->isChecked());
	units_combo->setEnabled(unreferenced);
	custom_spin->setEnabled(unreferenced && units_combo->currentData().toInt() == GeodataImportOptions::CustomMillimetres);
	offset_x_spin->setEnabled(unreferenced);
	offset_y_spin->setEnabled(unreferenced);
	rotation_spin->setEnabled(unreferenced);
	for (auto* spin : box_spins)
		spin->setEnabled(clip_combo->currentData().toInt() == ClipBox);
	auto const crt = symbols_combo->currentData().toInt() == CrtSymbols;
	crt_edit->setEnabled(crt);
	crt_button->setEnabled(crt);
	
	auto checked = 0;
	for (int i = 0; i < layer_list->count(); ++i)
		checked += layer_list->item(i)->checkState() == Qt::Checked;
	auto const ok = (checked > 0 || layer_list->count() == 0)
	                && (!override_radio->isChecked() || !crs_edit->text().trimmed().isEmpty())
	                && (!crt || !crt_edit->text().trimmed().isEmpty());
	buttons->button(QDialogButtonBox::Ok)->setEnabled(ok);
}


void GeodataImportDialog::browseCrtFile()
{
	auto const start = crt_edit->text().isEmpty() ? QFileInfo(paths.value(0)).path() : crt_edit->text();
	auto const path = QFileDialog::getOpenFileName(this, tr("Open CRT file"), start, tr("CRT files (*.crt)"));
	if (!path.isEmpty())
		crt_edit->setText(QDir::toNativeSeparators(path));
}


GeodataImportOptions GeodataImportDialog::options() const
{
	GeodataImportOptions options;
	
	QStringList checked;
	for (int i = 0; i < layer_list->count(); ++i)
	{
		if (layer_list->item(i)->checkState() == Qt::Checked)
			checked.push_back(layer_list->item(i)->data(Qt::UserRole).toString());
	}
	if (checked.size() < layer_list->count())
		options.layers = checked;
	
	if (override_radio->isChecked())
	{
		options.crs_mode = GeodataImportOptions::CrsOverride;
		options.crs_spec = crs_edit->text().trimmed();
	}
	else if (unreferenced_radio->isChecked())
	{
		options.crs_mode = GeodataImportOptions::CrsUnreferenced;
		options.units = GeodataImportOptions::Units(units_combo->currentData().toInt());
		options.custom_mm_per_unit = custom_spin->value();
		options.offset_mm = QPointF(offset_x_spin->value(), offset_y_spin->value());
		options.rotation_deg = rotation_spin->value();
	}
	
	switch (clip_combo->currentData().toInt())
	{
	case ClipMapExtent:
		{
			auto const extent = map.calculateExtent();
			if (extent.isValid())
				options.clip = rectPolygon(extent);
		}
		break;
	case ClipView:
		options.clip = rectPolygon(view_rect);
		break;
	case ClipBox:
		options.clip = geodataClipBox(map, QRectF(QPointF(box_spins[0]->value(), box_spins[1]->value()),
		                                          QPointF(box_spins[2]->value(), box_spins[3]->value())));
		break;
	default:
		break;
	}
	
	if (layer_tag_combo->currentIndex() == 1)
	{
		options.layer_tag = GeodataImportOptions::LayerNameTag;
	}
	else if (layer_tag_combo->currentIndex() > 1)
	{
		options.layer_tag = GeodataImportOptions::AttributeTag;
		options.layer_tag_attribute = layer_tag_combo->currentData().toString();
	}
	options.text_attribute = text_combo->currentData().toString();
	options.blocks_as_points = blocks_check->isEnabled() && blocks_check->isChecked();
	options.simplify_mm = simplify_spin->value();
	if (symbols_combo->currentData().toInt() == CrtSymbols)
		options.crt_file = QDir::fromNativeSeparators(crt_edit->text().trimmed());
	return options;
}


}  // namespace OpenOrienteering
