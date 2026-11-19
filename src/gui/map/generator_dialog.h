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


#ifndef FOOM_GENERATOR_DIALOG_H
#define FOOM_GENERATOR_DIALOG_H

#include <vector>

#include <QDialog>
#include <QJsonObject>
#include <QObject>
#include <QString>

#include "layout/generator.h"

class QWidget;

namespace OpenOrienteering {

class Map;


/**
 * The dialog of a generator: a form built from the generator's parameters.
 *
 * See the design notes, "Generators".
 */
class GeneratorDialog : public QDialog
{
Q_OBJECT
public:
	GeneratorDialog(const Map& map, const QString& id, const QJsonObject& params, QWidget* parent = nullptr);
	
	~GeneratorDialog() override;
	
	/** The parameters as chosen. */
	QJsonObject params() const;
	
private:
	const Map& map;
	QJsonObject initial_params;
	std::vector<GeneratorField> fields;
	std::vector<QWidget*> editors;
};


}  // namespace OpenOrienteering

#endif
