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
	
	/** foom: Tests the sketch pen, eraser and note tools and the sketch list. */
	void sketchTools();
	
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
	
	/** foom: Tests the find and replace text commands. */
	void findReplaceText();
	
	/** foom: Tests the named extent commands. */
	void extents();
	void demFeatures();
	
	/** foom: Tests the cliffs of the Extract features command. */
	void demCliffs();
	
	/** foom: Tests the trees of the Extract features command. */
	void demTrees();
	
	/** foom: Tests the grid commands of the Elevation menu. */
	void demGridCommands();
	
	/** foom: Tests the hypsometric map command. */
	void demHypsometric();
	
	/** foom: Tests the vegetation height command. */
	void demVegetationHeight();
	
	/** foom: Tests the profile command. */
	void demProfile();
	
	/** foom: Tests the merging of contour lines. */
	void demMergeContours();
	
	/** foom: Tests the selection commands of the Select menu. */
	void selectionCommands();
	
	/** foom: Tests saved selections. */
	void savedSelections();
	
	/** foom: Tests the interpolate command. */
	void interpolateCommand();
	
	/** foom: Tests the object information command. */
	void objectInformationCommand();
	
	/** foom: Tests selecting symbols and the symbol replacement commands. */
	void symbolTools();
	
	/** foom: Tests the status set commands. */
	void statusSetCommands();
	
	/** foom: Tests the compare with file command. */
	void symbolCompareCommand();
	
	/** foom: Tests the convert to point objects command. */
	void convertToPointCommand();
	
	/** foom: Tests the symbol descriptions file commands. */
	void symbolDescriptionCommands();
	
	/** foom: Tests the favourites filter of the symbol pane. */
	void favouritesFilter();
	
	/** foom: Tests the symbol groups dock and the group filter. */
	void symbolGroupsDock();
	
	/** foom: Tests the update from file command. */
	void symbolUpdateCommand();
	
	/** foom: Tests the point cloud import command. */
	void demPointCloud();
	
	/** foom: Tests the map transform commands. */
	void mapTransforms();
	
	/** foom: Tests the affine transformation tool. */
	void mapAffineTool();
	
	/** foom: Tests the rubbersheeting tool. */
	void mapRubbersheetTool();
	
	/** foom: Tests the local transformation tool. */
	void mapLocalWarpTool();
	
	/** foom: Tests the change of coordinate system command. */
	void mapReproject();
	
	/** foom: Tests the rotation to magnetic north and the centring commands. */
	void mapRotateMagneticAndCenter();
	
	/** foom: Tests the map check command and the Issues window. */
	void mapCheck();
	
	/** foom: Tests the legibility check command. */
	void legibility();
	
	/** foom: Tests the drawing-time legibility feedback of the path tool. */
	void drawingFeedback();
	
	/** foom: Tests the topology commands of the editor. */
	void topology();
	
	/** foom: Tests make rectangular and generalize buildings in the editor. */
	void buildings();
	
	/** foom: Tests the smooth command in the editor. */
	void smooth();
	void rectangleBox();
	void preferredTool();
};

#endif
