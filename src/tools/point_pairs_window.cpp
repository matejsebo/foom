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

#include "point_pairs_window.h"

#include <cmath>

#include <Qt>
#include <QAbstractItemView>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLatin1String>
#include <QPointF>
#include <QPushButton>
#include <QStringList>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include "core/georeferencing.h"
#include "core/map.h"


namespace OpenOrienteering {

namespace {

QTableWidgetItem* numberItem(double value)
{
	auto* item = new QTableWidgetItem(QString::number(value, 'f', 2));
	item->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
	item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
	return item;
}

}  // namespace



PointPairTable::PointPairTable(const Map& map, bool with_residuals, QWidget* parent)
: QTableWidget(parent)
, map(map)
, with_residuals(with_residuals)
{
	QStringList headers = { tr("Used"), tr("No."), tr("x-Old"), tr("y-Old"), tr("x-New"), tr("y-New"), tr("Distance [m]") };
	if (with_residuals)
		headers << tr("Residual [m]");
	setColumnCount(headers.size());
	setHorizontalHeaderLabels(headers);
	verticalHeader()->setVisible(false);
	setSelectionBehavior(QAbstractItemView::SelectRows);
	setSelectionMode(QAbstractItemView::SingleSelection);
	horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
	connect(this, &QTableWidget::itemChanged, this, &PointPairTable::cellChanged);
}

PointPairTable::~PointPairTable() = default;


void PointPairTable::setPairs(const std::vector<PointPair>& pairs, const std::vector<double>& residuals)
{
	updating = true;
	auto const selected = selectedPair();
	auto const& georef = map.getGeoreferencing();
	// Paper millimetres to metres.
	auto const metres = georef.getScaleDenominator() / 1000.0;
	setRowCount(int(pairs.size()));
	for (int row = 0; row < int(pairs.size()); ++row)
	{
		auto const& pair = pairs[std::size_t(row)];
		auto* used = new QTableWidgetItem();
		used->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
		used->setCheckState(pair.used ? Qt::Checked : Qt::Unchecked);
		setItem(row, 0, used);
		auto* number = new QTableWidgetItem(QString::number(row + 1));
		number->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
		setItem(row, 1, number);
		auto const from = georef.toProjectedCoords(pair.map);
		auto const to = georef.toProjectedCoords(pair.reference);
		setItem(row, 2, numberItem(from.x()));
		setItem(row, 3, numberItem(from.y()));
		setItem(row, 4, numberItem(to.x()));
		setItem(row, 5, numberItem(to.y()));
		setItem(row, 6, numberItem(std::hypot(to.x() - from.x(), to.y() - from.y())));
		if (with_residuals)
		{
			if (std::size_t(row) < residuals.size())
				setItem(row, 7, numberItem(residuals[std::size_t(row)] * metres));
			else
				setItem(row, 7, new QTableWidgetItem());
		}
	}
	if (selected >= 0 && selected < rowCount())
		selectRow(selected);
	updating = false;
}

int PointPairTable::selectedPair() const
{
	auto const rows = selectionModel() ? selectionModel()->selectedRows() : QModelIndexList{};
	return rows.isEmpty() ? -1 : rows.front().row();
}

void PointPairTable::cellChanged(QTableWidgetItem* item)
{
	if (updating || !item || item->column() != 0)
		return;
	emit usedChanged(item->row(), item->checkState() == Qt::Checked);
}



PointPairsWindow::PointPairsWindow(const Map& map, const QString& title, const QString& points_title, bool with_residuals, QWidget* parent)
: QDialog(parent)
{
	setWindowTitle(title);
	setModal(false);
	// Beside the map: the map keeps the keyboard.
	setAttribute(Qt::WA_ShowWithoutActivating);
	pair_table = new PointPairTable(map, with_residuals);
	remove_button = new QPushButton(tr("Remove"));
	remove_button->setObjectName(QStringLiteral("remove"));
	remove_all_button = new QPushButton(tr("Remove all"));
	remove_all_button->setObjectName(QStringLiteral("remove_all"));
	load_button = new QPushButton(tr("Load..."));
	load_button->setObjectName(QStringLiteral("load"));
	save_button = new QPushButton(tr("Save..."));
	save_button->setObjectName(QStringLiteral("save"));
	transform_button = new QPushButton(tr("Transform"));
	transform_button->setObjectName(QStringLiteral("transform"));
	transform_button->setAutoDefault(false);

	connect(remove_button, &QPushButton::clicked, this, [this]() {
		auto const index = pair_table->selectedPair();
		if (index >= 0)
			emit removeRequested(index);
	});
	connect(remove_all_button, &QPushButton::clicked, this, &PointPairsWindow::removeAllRequested);
	connect(load_button, &QPushButton::clicked, this, &PointPairsWindow::loadRequested);
	connect(save_button, &QPushButton::clicked, this, &PointPairsWindow::saveRequested);
	connect(transform_button, &QPushButton::clicked, this, &PointPairsWindow::transformRequested);

	auto* buttons = new QVBoxLayout();
	for (auto* button : { remove_button, remove_all_button, load_button, save_button })
	{
		button->setAutoDefault(false);
		buttons->addWidget(button);
	}
	buttons->addStretch(1);
	buttons->addWidget(transform_button);
	auto* points_layout = new QHBoxLayout();
	points_layout->addWidget(pair_table, 1);
	points_layout->addLayout(buttons);
	auto* points = new QGroupBox(points_title);
	points->setLayout(points_layout);

	sections = new QVBoxLayout();
	auto* layout = new QVBoxLayout(this);
	layout->addLayout(sections);
	layout->addWidget(points, 1);
	resize(640, 320);
}

PointPairsWindow::~PointPairsWindow() = default;


void PointPairsWindow::addSection(QWidget* section)
{
	sections->addWidget(section);
}

QString PointPairsWindow::fileFilter()
{
	return tr("Text files") + QLatin1String(" (*.txt);;") + tr("All files") + QLatin1String(" (*.*)");
}

void PointPairsWindow::reject()
{
	// Escape and the close button of the window (QDialog::closeEvent() rejects) end the tool.
	QDialog::reject();
	emit closed();
}


}  // namespace OpenOrienteering
