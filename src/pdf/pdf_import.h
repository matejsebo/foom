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

#ifndef FOOM_PDF_IMPORT_H
#define FOOM_PDF_IMPORT_H

#include <memory>
#include <vector>

#include <QPointF>
#include <QSizeF>
#include <QString>
#include <QStringList>

#include "fileformats/file_format.h"
#include "fileformats/file_import_export.h"

class QIODevice;

namespace OpenOrienteering {

class Map;
class MapView;


/**
 * The choices of a PDF import.
 *
 * See the design notes, "PDF import".
 */
struct PdfImportOptions
{
	int page = 1;              ///< The page, counted from 1.
	int pdf_scale = 0;         ///< The scale denominator at which the page was drawn; 0 means the map's.
	QPointF centre;            ///< Where the centre of the page goes, in map mm.
	bool text = true;          ///< Whether text objects are made.
};


/**
 * What a PDF import did.
 */
struct PdfImportResult
{
	bool ok = false;
	QString error;
	QStringList warnings;
	int objects = 0;           ///< Objects added (importPdf) or read (readPdfPage).
	int areas = 0;
	int lines = 0;
	int texts = 0;
	int images = 0;            ///< Embedded images and image masks, skipped.
	int shadings = 0;          ///< Shadings, skipped.
	int patterns = 0;          ///< Tiling pattern fills, skipped.
	int invisible_texts = 0;   ///< Text runs in render mode 3, skipped.
	QSizeF page_size_mm;       ///< The size of the page read.
};


/**
 * The sizes of the pages of a PDF file in mm (crop box), or an empty list and an error.
 */
std::vector<QSizeF> pdfPageSizes(const QString& path, QString* error = nullptr);

/**
 * Reads one page of a PDF file into a map: colours, symbols and objects.
 *
 * The map's scale is the target scale. The objects go into the map's current part.
 * Nothing is undoable.
 */
PdfImportResult readPdfPage(const QString& path, Map& map, const PdfImportOptions& options);

/**
 * Reads one page of a PDF file and adds its objects to the map as one undo step,
 * with the colours and symbols they use.
 */
PdfImportResult importPdf(Map& map, const QString& path, const PdfImportOptions& options);


/**
 * The PDF format, for import only.
 */
class PdfFileFormat : public FileFormat
{
public:
	PdfFileFormat();
	std::unique_ptr<Importer> makeImporter(const QString& path, Map* map, MapView* view) const override;
};


/**
 * Imports the first page of a PDF at the map's scale, centred on the origin.
 */
class PdfFileImport : public Importer
{
public:
	PdfFileImport(const QString& path, Map* map, MapView* view);
	~PdfFileImport() override;
	bool supportsQIODevice() const noexcept override;

protected:
	bool importImplementation() override;
};


}  // namespace OpenOrienteering

#endif
