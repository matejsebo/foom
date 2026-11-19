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


#include "qr_code.h"

#include <algorithm>
#include <cstdlib>
#include <limits>

#include <QCoreApplication>


namespace OpenOrienteering {

namespace {

// ISO/IEC 18004, table 9: error correction codewords per block, by level and version.
const std::uint8_t ec_codewords_per_block[4][40] = {
	{  7, 10, 15, 20, 26, 18, 20, 24, 30, 18, 20, 24, 26, 30, 22, 24, 28, 30, 28, 28, 28, 28, 30, 30, 26, 28, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30 },
	{ 10, 16, 26, 18, 24, 16, 18, 22, 22, 26, 30, 22, 22, 24, 24, 28, 28, 26, 26, 26, 26, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28 },
	{ 13, 22, 18, 26, 18, 24, 18, 22, 20, 24, 28, 26, 24, 20, 30, 24, 28, 28, 26, 30, 28, 30, 30, 30, 30, 28, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30 },
	{ 17, 28, 22, 16, 22, 28, 26, 26, 24, 28, 24, 28, 22, 24, 24, 30, 28, 28, 26, 28, 30, 24, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30 },
};

// The same table: number of error correction blocks.
const std::uint8_t ec_blocks[4][40] = {
	{ 1, 1, 1, 1, 1, 2, 2, 2, 2,  4,  4,  4,  4,  4,  6,  6,  6,  6,  7,  8,  8,  9,  9, 10, 12, 12, 12, 13, 14, 15, 16, 17, 18, 19, 19, 20, 21, 22, 24, 25 },
	{ 1, 1, 1, 2, 2, 4, 4, 4, 5,  5,  5,  8,  9,  9, 10, 10, 11, 13, 14, 16, 17, 17, 18, 20, 21, 23, 25, 26, 28, 29, 31, 33, 35, 37, 38, 40, 43, 45, 47, 49 },
	{ 1, 1, 2, 2, 4, 4, 6, 6, 8,  8,  8, 10, 12, 16, 12, 17, 16, 18, 21, 20, 23, 23, 25, 27, 29, 34, 34, 35, 38, 40, 43, 45, 48, 51, 53, 56, 59, 62, 65, 68 },
	{ 1, 1, 2, 4, 4, 4, 5, 6, 8,  8, 11, 11, 16, 16, 18, 16, 19, 21, 25, 25, 25, 34, 30, 32, 35, 37, 40, 42, 45, 48, 51, 54, 57, 60, 63, 66, 70, 74, 77, 81 },
};

/** The two bits which name the level in the format information. */
int levelBits(QrEcLevel level)
{
	switch (level)
	{
	case QrEcLevel::L:  return 1;
	case QrEcLevel::M:  return 0;
	case QrEcLevel::Q:  return 3;
	case QrEcLevel::H:  return 2;
	}
	return 0;
}

/** The number of codewords, data and error correction, which a version holds. */
int totalCodewords(int version)
{
	// All modules, less the finder patterns with separators and format
	// information, the timing patterns, the alignment patterns and the
	// version information.
	auto const size = 17 + 4 * version;
	auto modules = size * size - 3 * 64 - 2 * 15 - 1 - 2 * (size - 16);
	if (version >= 2)
	{
		auto const count = version / 7 + 2;
		// 25 modules each, 5 of them on a timing pattern for those in row or column 6.
		modules -= 25 * (count * count - 3) - 2 * 5 * (count - 2);
	}
	if (version >= 7)
		modules -= 36;
	return modules / 8;
}


// The field of 256 elements with the polynomial x^8 + x^4 + x^3 + x^2 + 1.
struct Field
{
	std::uint8_t exp[510];
	std::uint8_t log[256];
	
	Field()
	{
		auto x = 1;
		for (int i = 0; i < 255; ++i)
		{
			exp[i] = std::uint8_t(x);
			exp[i + 255] = std::uint8_t(x);
			log[x] = std::uint8_t(i);
			x <<= 1;
			if (x & 0x100)
				x ^= 0x11D;
		}
		log[0] = 0;
	}
	
	std::uint8_t multiply(std::uint8_t a, std::uint8_t b) const
	{
		if (a == 0 || b == 0)
			return 0;
		return exp[log[a] + log[b]];
	}
};

const Field& field()
{
	static const Field instance;
	return instance;
}


/** A square of modules, with a record of which belong to function patterns. */
struct Grid
{
	int size;
	std::vector<std::uint8_t> modules;
	std::vector<std::uint8_t> function;
	
	explicit Grid(int size)
	: size(size)
	, modules(std::size_t(size) * std::size_t(size), 0)
	, function(std::size_t(size) * std::size_t(size), 0)
	{}
	
	std::size_t index(int x, int y) const { return std::size_t(y) * std::size_t(size) + std::size_t(x); }
	bool dark(int x, int y) const { return modules[index(x, y)] != 0; }
	
	void setFunction(int x, int y, bool is_dark)
	{
		if (x < 0 || y < 0 || x >= size || y >= size)
			return;
		modules[index(x, y)] = is_dark ? 1 : 0;
		function[index(x, y)] = 1;
	}
};

void drawFinder(Grid& grid, int centre_x, int centre_y)
{
	// The pattern of 7 modules a side and the separator around it.
	for (int dy = -4; dy <= 4; ++dy)
	{
		for (int dx = -4; dx <= 4; ++dx)
		{
			auto const ring = std::max(std::abs(dx), std::abs(dy));
			grid.setFunction(centre_x + dx, centre_y + dy, ring != 2 && ring != 4);
		}
	}
}

void drawAlignment(Grid& grid, int centre_x, int centre_y)
{
	for (int dy = -2; dy <= 2; ++dy)
	{
		for (int dx = -2; dx <= 2; ++dx)
			grid.setFunction(centre_x + dx, centre_y + dy, std::max(std::abs(dx), std::abs(dy)) != 1);
	}
}

void drawFormat(Grid& grid, QrEcLevel level, int mask)
{
	// Five bits of data, ten bits of a BCH code with the generator 10100110111.
	auto const data = (levelBits(level) << 3) | mask;
	auto remainder = data;
	for (int i = 0; i < 10; ++i)
		remainder = (remainder << 1) ^ ((remainder >> 9) * 0x537);
	auto const bits = ((data << 10) | remainder) ^ 0x5412;
	auto const bit = [bits](int i) { return ((bits >> i) & 1) != 0; };
	
	auto const size = grid.size;
	// Around the top left finder pattern.
	for (int i = 0; i <= 5; ++i)
		grid.setFunction(8, i, bit(i));
	grid.setFunction(8, 7, bit(6));
	grid.setFunction(8, 8, bit(7));
	grid.setFunction(7, 8, bit(8));
	for (int i = 9; i < 15; ++i)
		grid.setFunction(14 - i, 8, bit(i));
	// Below the top right and beside the bottom left finder pattern.
	for (int i = 0; i < 8; ++i)
		grid.setFunction(size - 1 - i, 8, bit(i));
	for (int i = 8; i < 15; ++i)
		grid.setFunction(8, size - 15 + i, bit(i));
	grid.setFunction(8, size - 8, true);
}

void drawVersion(Grid& grid, int version)
{
	if (version < 7)
		return;
	// Six bits of data, twelve bits of a BCH code with the generator 1111100100101.
	auto remainder = version;
	for (int i = 0; i < 12; ++i)
		remainder = (remainder << 1) ^ ((remainder >> 11) * 0x1F25);
	auto const bits = (version << 12) | remainder;
	for (int i = 0; i < 18; ++i)
	{
		auto const is_dark = ((bits >> i) & 1) != 0;
		auto const a = grid.size - 11 + i % 3;
		auto const b = i / 3;
		grid.setFunction(a, b, is_dark);
		grid.setFunction(b, a, is_dark);
	}
}

void drawFunctionPatterns(Grid& grid, int version, QrEcLevel level)
{
	auto const size = grid.size;
	for (int i = 0; i < size; ++i)
	{
		grid.setFunction(6, i, i % 2 == 0);
		grid.setFunction(i, 6, i % 2 == 0);
	}
	drawFinder(grid, 3, 3);
	drawFinder(grid, size - 4, 3);
	drawFinder(grid, 3, size - 4);
	
	auto const positions = qrAlignmentPositions(version);
	auto const count = int(positions.size());
	for (int i = 0; i < count; ++i)
	{
		for (int j = 0; j < count; ++j)
		{
			// Not where the finder patterns are.
			if ((i == 0 && j == 0) || (i == 0 && j == count - 1) || (i == count - 1 && j == 0))
				continue;
			drawAlignment(grid, positions[std::size_t(i)], positions[std::size_t(j)]);
		}
	}
	
	// The mask is not known yet; this reserves the modules.
	drawFormat(grid, level, 0);
	drawVersion(grid, version);
}

/** Writes the codewords into the modules which no function pattern uses. */
void drawCodewords(Grid& grid, const std::vector<std::uint8_t>& codewords)
{
	auto const size = grid.size;
	auto const bit_count = codewords.size() * 8;
	std::size_t i = 0;
	// Columns in pairs from the right, going up and down in turn.
	for (int right = size - 1; right >= 1; right -= 2)
	{
		if (right == 6)
			right = 5;  // The vertical timing pattern has a column to itself.
		auto const upward = ((right + 1) & 2) == 0;
		for (int step = 0; step < size; ++step)
		{
			auto const y = upward ? size - 1 - step : step;
			for (int j = 0; j < 2; ++j)
			{
				auto const x = right - j;
				if (grid.function[grid.index(x, y)] || i >= bit_count)
					continue;
				grid.modules[grid.index(x, y)] = (codewords[i >> 3] >> (7 - (i & 7))) & 1;
				++i;
			}
		}
	}
}

bool maskBit(int mask, int x, int y)
{
	switch (mask)
	{
	case 0:  return (x + y) % 2 == 0;
	case 1:  return y % 2 == 0;
	case 2:  return x % 3 == 0;
	case 3:  return (x + y) % 3 == 0;
	case 4:  return (x / 3 + y / 2) % 2 == 0;
	case 5:  return x * y % 2 + x * y % 3 == 0;
	case 6:  return (x * y % 2 + x * y % 3) % 2 == 0;
	default: return ((x + y) % 2 + x * y % 3) % 2 == 0;
	}
}

/** Applies or removes a mask. */
void applyMask(Grid& grid, int mask)
{
	for (int y = 0; y < grid.size; ++y)
	{
		for (int x = 0; x < grid.size; ++x)
		{
			if (!grid.function[grid.index(x, y)] && maskBit(mask, x, y))
				grid.modules[grid.index(x, y)] ^= 1;
		}
	}
}

/** The penalty of a symbol, by the four rules of the standard. */
long penalty(const Grid& grid)
{
	auto const size = grid.size;
	long result = 0;
	
	// Rule 1: runs of five or more modules of one colour in a row or column.
	// Rule 3: the finder pattern's 1:1:3:1:1 with four light modules on a side.
	static const bool finder_like[2][11] = {
		{ true, false, true, true, true, false, true, false, false, false, false },
		{ false, false, false, false, true, false, true, true, true, false, true },
	};
	for (int direction = 0; direction < 2; ++direction)
	{
		auto const at = [&grid, direction](int line, int i) {
			return direction == 0 ? grid.dark(i, line) : grid.dark(line, i);
		};
		for (int line = 0; line < size; ++line)
		{
			auto run = 1;
			for (int i = 1; i <= size; ++i)
			{
				if (i < size && at(line, i) == at(line, i - 1))
				{
					++run;
					continue;
				}
				if (run >= 5)
					result += 3 + (run - 5);
				run = 1;
			}
			for (int i = 0; i + 11 <= size; ++i)
			{
				for (auto const& pattern : finder_like)
				{
					auto match = true;
					for (int k = 0; k < 11 && match; ++k)
						match = at(line, i + k) == pattern[k];
					if (match)
						result += 40;
				}
			}
		}
	}
	
	// Rule 2: blocks of two by two modules of one colour.
	for (int y = 0; y + 1 < size; ++y)
	{
		for (int x = 0; x + 1 < size; ++x)
		{
			auto const colour = grid.dark(x, y);
			if (grid.dark(x + 1, y) == colour && grid.dark(x, y + 1) == colour && grid.dark(x + 1, y + 1) == colour)
				result += 3;
		}
	}
	
	// Rule 4: the share of dark modules, in steps of 5 % away from 50 %.
	long dark = 0;
	for (auto module : grid.modules)
		dark += module;
	auto const total = long(size) * size;
	result += 10 * (std::labs(dark * 100 - total * 50) / (total * 5));
	return result;
}

/** The data codewords: mode, count, bytes, terminator and padding. */
std::vector<std::uint8_t> dataCodewords(const QByteArray& data, int version, QrEcLevel level)
{
	auto const capacity = std::size_t(qrDataCodewords(version, level));
	std::vector<std::uint8_t> codewords;
	codewords.reserve(capacity);
	auto pending = 0u;      // bits not yet written, in the low end
	auto pending_bits = 0;
	auto const append = [&](unsigned int value, int bits) {
		pending = (pending << bits) | value;
		pending_bits += bits;
		while (pending_bits >= 8)
		{
			codewords.push_back(std::uint8_t(pending >> (pending_bits - 8)));
			pending_bits -= 8;
			pending &= (1u << pending_bits) - 1;
		}
	};
	
	append(4, 4);  // byte mode
	auto const count = static_cast<unsigned int>(data.size());
	if (version >= 10)
		append(count >> 8, 8);
	append(count & 0xFF, 8);
	for (auto byte : data)
		append(static_cast<unsigned char>(byte), 8);
	// The terminator, as far as there is room, and zeros up to a whole codeword.
	if (codewords.size() < capacity)
		append(0, 4);
	if (pending_bits > 0)
		append(0, 8 - pending_bits);
	for (auto pad = std::uint8_t(0xEC); codewords.size() < capacity; pad ^= 0xEC ^ 0x11)
		codewords.push_back(pad);
	codewords.resize(capacity);
	return codewords;
}

/** Splits the data into blocks, adds error correction and interleaves. */
std::vector<std::uint8_t> finalCodewords(const std::vector<std::uint8_t>& data, int version, QrEcLevel level)
{
	auto const block_count = int(ec_blocks[int(level)][version - 1]);
	auto const ec_count = int(ec_codewords_per_block[int(level)][version - 1]);
	auto const total = totalCodewords(version);
	auto const short_blocks = block_count - total % block_count;
	auto const short_data = total / block_count - ec_count;
	
	std::vector<std::vector<std::uint8_t>> data_blocks;
	std::vector<std::vector<std::uint8_t>> ec_blocks_out;
	std::size_t offset = 0;
	for (int i = 0; i < block_count; ++i)
	{
		auto const length = std::size_t(short_data + (i < short_blocks ? 0 : 1));
		data_blocks.emplace_back(data.begin() + std::ptrdiff_t(offset), data.begin() + std::ptrdiff_t(offset + length));
		offset += length;
		ec_blocks_out.push_back(qrReedSolomon(data_blocks.back(), ec_count));
	}
	
	std::vector<std::uint8_t> result;
	result.reserve(std::size_t(total));
	for (std::size_t i = 0; i <= std::size_t(short_data); ++i)
	{
		for (auto const& block : data_blocks)
		{
			if (i < block.size())
				result.push_back(block[i]);
		}
	}
	for (std::size_t i = 0; i < std::size_t(ec_count); ++i)
	{
		for (auto const& block : ec_blocks_out)
			result.push_back(block[i]);
	}
	return result;
}

}  // namespace


std::vector<std::uint8_t> qrReedSolomon(const std::vector<std::uint8_t>& data, int ec_count)
{
	auto const& gf = field();
	auto const n = std::size_t(std::max(1, ec_count));
	
	// The generator polynomial, the product of (x - 2^i) for i from 0 to n-1,
	// highest power first and without its leading coefficient 1.
	std::vector<std::uint8_t> generator(n, 0);
	generator[n - 1] = 1;
	std::uint8_t root = 1;
	for (std::size_t i = 0; i < n; ++i)
	{
		for (std::size_t j = 0; j < n; ++j)
		{
			generator[j] = gf.multiply(generator[j], root);
			if (j + 1 < n)
				generator[j] ^= generator[j + 1];
		}
		root = gf.multiply(root, 2);
	}
	
	// The remainder of the data, shifted by n codewords, divided by it.
	std::vector<std::uint8_t> remainder(n, 0);
	for (auto byte : data)
	{
		auto const factor = std::uint8_t(byte ^ remainder.front());
		remainder.erase(remainder.begin());
		remainder.push_back(0);
		for (std::size_t j = 0; j < n; ++j)
			remainder[j] ^= gf.multiply(generator[j], factor);
	}
	return remainder;
}

int qrDataCodewords(int version, QrEcLevel level)
{
	if (version < 1 || version > 40)
		return 0;
	return totalCodewords(version) - int(ec_codewords_per_block[int(level)][version - 1]) * int(ec_blocks[int(level)][version - 1]);
}

std::vector<int> qrAlignmentPositions(int version)
{
	std::vector<int> positions;
	if (version < 2 || version > 40)
		return positions;
	auto const count = version / 7 + 2;
	auto const last = 4 * version + 10;
	// The patterns stand on even steps from the last one down, the step being
	// the even spacing rounded up to an even number; the first stays at 6.
	// Version 32 is the one exception in the standard's table.
	auto const step = version == 32 ? 26 : (version * 4 + count * 2 + 1) / (count * 2 - 2) * 2;
	positions.resize(std::size_t(count));
	positions[0] = 6;
	for (int i = count - 1, position = last; i >= 1; --i, position -= step)
		positions[std::size_t(i)] = position;
	return positions;
}

QrCode encodeQr(const QByteArray& data, QrEcLevel level, QString* error)
{
	QrCode code;
	code.level = level;
	if (data.isEmpty())
	{
		if (error)
			*error = QCoreApplication::translate("OpenOrienteering::Generator", "There is no text to encode.");
		return code;
	}
	
	auto version = 0;
	for (int v = 1; v <= 40 && version == 0; ++v)
	{
		auto const needed = 4 + (v >= 10 ? 16 : 8) + 8 * qint64(data.size());
		if (needed <= 8 * qint64(qrDataCodewords(v, level)))
			version = v;
	}
	if (version == 0)
	{
		if (error)
			*error = QCoreApplication::translate("OpenOrienteering::Generator", "The text is too long for a QR code at this error correction level.");
		return code;
	}
	
	Grid grid(17 + 4 * version);
	drawFunctionPatterns(grid, version, level);
	drawCodewords(grid, finalCodewords(dataCodewords(data, version, level), version, level));
	
	auto best_mask = 0;
	auto best_penalty = std::numeric_limits<long>::max();
	for (int mask = 0; mask < 8; ++mask)
	{
		applyMask(grid, mask);
		drawFormat(grid, level, mask);
		auto const value = penalty(grid);
		if (value < best_penalty)
		{
			best_penalty = value;
			best_mask = mask;
		}
		applyMask(grid, mask);  // Applying it again removes it.
	}
	applyMask(grid, best_mask);
	drawFormat(grid, level, best_mask);
	
	code.version = version;
	code.size = grid.size;
	code.mask = best_mask;
	code.modules = std::move(grid.modules);
	return code;
}


}  // namespace OpenOrienteering
