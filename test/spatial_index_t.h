/*
 *    Copyright 2026 Matej Šebo
 *
 *    This file is part of FOOM, the Fantastic Open Orienteering Mapper,
 *    a fork of OpenOrienteering Mapper.
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


#ifndef OPENORIENTEERING_SPATIAL_INDEX_T_H
#define OPENORIENTEERING_SPATIAL_INDEX_T_H

#include <QObject>
#include <QStringList>


/**
 * @test Tests the spatial index of map parts.
 * 
 * This test implements the Accept paragraph of the "Spatial index" section
 * in the design notes.
 */
class SpatialIndexTest : public QObject
{
Q_OBJECT
private slots:
	void initTestCase();
	
	/** The candidates of query() contain every object the brute-force scan finds. */
	void candidateSupersetTest_data();
	void candidateSupersetTest();
	
	/** findObjectsAt() and findObjectsAtBox() return what the brute-force loops returned. */
	void findObjectsUnchangedTest_data();
	void findObjectsUnchangedTest();
	
	/** Adding, moving, replacing, removing and importing objects keeps the index right. */
	void maintenanceTest();
	
	/** Objects over 4,096 cells live in the large list and are found everywhere. */
	void largeObjectTest();
	
	/** The timing bounds of the Accept paragraph on a synthetic map of 200,000 objects. */
	void timingTest();
	
private:
	QStringList sample_maps;
};

#endif
