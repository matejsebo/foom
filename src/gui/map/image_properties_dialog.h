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


#ifndef FOOM_IMAGE_PROPERTIES_DIALOG_H
#define FOOM_IMAGE_PROPERTIES_DIALOG_H

#include <QtGlobal>
#include <QDialog>
#include <QObject>

#include "core/objects/image_object.h"

class QComboBox;
class QSpinBox;
class QWidget;

namespace OpenOrienteering {


/**
 * The dialog of `image-properties`: opacity and layer of image objects.
 *
 * See the design notes, "Image object".
 */
class ImagePropertiesDialog : public QDialog
{
Q_OBJECT
public:
	/** Shows the properties of the given image. */
	explicit ImagePropertiesDialog(const ImageObject& image, QWidget* parent = nullptr);
	
	~ImagePropertiesDialog() override;
	
	/** The opacity as chosen, 0 to 1. */
	qreal opacity() const;
	
	/** The layer as chosen. */
	ImageObject::Layer layer() const;
	
private:
	QSpinBox* opacity_edit;
	QComboBox* layer_combo;
};


}  // namespace OpenOrienteering

#endif
