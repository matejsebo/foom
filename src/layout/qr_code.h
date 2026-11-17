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


#ifndef FOOM_QR_CODE_H
#define FOOM_QR_CODE_H

#include <cstddef>
#include <cstdint>
#include <vector>

#include <QByteArray>
#include <QString>

namespace OpenOrienteering {

/**
 * A QR code encoder, written for FOOM from ISO/IEC 18004.
 *
 * Byte mode only, versions 1 to 40, no structured append, no ECI header.
 *
 * See the design notes, "Generators", gen-qr-code.
 */

/** The error correction level. */
enum class QrEcLevel { L, M, Q, H };

/** A QR code symbol. */
struct QrCode
{
	int version = 0;                    ///< 1 to 40, or 0 for no symbol.
	int size = 0;                       ///< Modules a side, without the quiet zone.
	int mask = -1;                      ///< The mask pattern, 0 to 7.
	QrEcLevel level = QrEcLevel::M;
	std::vector<std::uint8_t> modules;  ///< Row by row from the top, 1 for dark.
	
	bool isValid() const { return version > 0; }
	
	/** The module in column x of row y. */
	bool dark(int x, int y) const { return modules[std::size_t(y) * std::size_t(size) + std::size_t(x)] != 0; }
};

/**
 * Encodes the bytes in the smallest version which holds them at the level,
 * with the mask of the lowest penalty.
 *
 * Returns an invalid symbol with a message in error when there are no bytes
 * or more than version 40 holds.
 */
QrCode encodeQr(const QByteArray& data, QrEcLevel level, QString* error = nullptr);

/** The error correction codewords for the data codewords of one block. */
std::vector<std::uint8_t> qrReedSolomon(const std::vector<std::uint8_t>& data, int ec_count);

/** The number of data codewords of a version at a level. */
int qrDataCodewords(int version, QrEcLevel level);

/** The row and column coordinates of the alignment pattern centres. */
std::vector<int> qrAlignmentPositions(int version);


}  // namespace OpenOrienteering

#endif
