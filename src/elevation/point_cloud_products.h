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

#ifndef OPENORIENTEERING_POINT_CLOUD_PRODUCTS_H
#define OPENORIENTEERING_POINT_CLOUD_PRODUCTS_H

#include <vector>

#include <QtGlobal>
#include <QRgb>
#include <QString>
#include <QStringList>

#include "elevation/point_cloud.h"
#include "elevation/raster_products.h"

namespace OpenOrienteering {

class ElevationModel;


/**
 * The settings of the feature map.
 *
 * See the design notes, "Feature map, intensity and classification maps".
 */
struct FeatureMapParams
{
	double cell = 1.0;
	double band_from = 0.0;   ///< Height above the terrain model, m.
	double band_to = 2.0;
};


/**
 * The highest return inside the band per cell, slope-shaded; Grayscale8, cells without returns black.
 */
RasterProduct featureMap(const QStringList& paths, const ElevationModel& dtm, const FeatureMapParams& params);

/**
 * The mean first-return intensity per cell, stretched from the 2nd to the 98th percentile; Grayscale8.
 */
RasterProduct intensityMap(const QStringList& paths, const ElevationModel& dtm, double cell = 1.0);

/**
 * The most frequent class per cell, coloured by classColor(); RGB32, cells without returns black.
 */
RasterProduct classificationMap(const QStringList& paths, const ElevationModel& dtm, double cell = 1.0);

/// The colour of a LAS class in the classification map.
QRgb classColor(int classification);



}  // namespace OpenOrienteering

#endif
