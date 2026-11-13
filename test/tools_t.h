/*
 *    Copyright 2012, 2013 Thomas Schöps
 *    Copyright 2017, 2020, 2025 Kai Pastor
 *
 *    This file is part of OpenOrienteering.
 *
 *    OpenOrienteering is free software: you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation, either version 3 of the License, or
 *    (at your option) any later version.
 *
 *    OpenOrienteering is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with OpenOrienteering.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef OPENORIENTEERING_TOOLS_T_H
#define OPENORIENTEERING_TOOLS_T_H

#include <QObject>


/**
 * @test Tests the editing tools.
 */
class ToolsTest : public QObject
{
Q_OBJECT
private slots:
	void initTestCase();
	
	void editTool();
	
	void paintOnTemplateFeature();
	
	void testFindObjects();
	
	/** foom: Tests the to-polyline action. */
	void toPolyline();
	
	/** foom: Tests the mirror action. */
	void mirror();
	
	/** foom: Tests the move-by, rotate-by and stretch actions. */
	void numericTransforms();
	
	/** foom: Tests the align and distribute actions. */
	void alignAndDistribute();
	
	/** foom: Tests the reshape tool. */
	void reshape();
	
	/** foom: Tests the move-parallel tool and command. */
	void moveParallel();
	
	/** foom: Tests the vertex type commands. */
	void vertexType();
	
	/** foom: Tests the lasso selection tool. */
	void lasso();
	
	/** foom: Tests the group commands and the click rule. */
	void groups();
	
	/** foom: Tests the numeric entry bar. */
	void numericEntry();
	
	/** foom: Tests the view history. */
	void viewHistory();
	
	/** foom: Tests the bookmark commands. */
	void bookmarks();
	
	/** foom: Tests the shortcut store. */
	void shortcuts();
};

#endif
