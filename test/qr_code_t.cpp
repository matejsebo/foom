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


#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <vector>

#include <QtGlobal>
#include <QtTest>
#include <QByteArray>
#include <QFile>
#include <QIODevice>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLatin1Char>
#include <QObject>
#include <QString>

#include "layout/qr_code.h"

using namespace OpenOrienteering;


namespace {

// ISO/IEC 18004, annex E: the centres of the alignment patterns, by version.
const std::vector<std::vector<int>> alignment_table = {
	{},
	{ 6, 18 }, { 6, 22 }, { 6, 26 }, { 6, 30 }, { 6, 34 },
	{ 6, 22, 38 }, { 6, 24, 42 }, { 6, 26, 46 }, { 6, 28, 50 }, { 6, 30, 54 }, { 6, 32, 58 }, { 6, 34, 62 },
	{ 6, 26, 46, 66 }, { 6, 26, 48, 70 }, { 6, 26, 50, 74 }, { 6, 30, 54, 78 }, { 6, 30, 56, 82 }, { 6, 30, 58, 86 }, { 6, 34, 62, 90 },
	{ 6, 28, 50, 72, 94 }, { 6, 26, 50, 74, 98 }, { 6, 30, 54, 78, 102 }, { 6, 28, 54, 80, 106 }, { 6, 32, 58, 84, 110 }, { 6, 30, 58, 86, 114 }, { 6, 34, 62, 90, 118 },
	{ 6, 26, 50, 74, 98, 122 }, { 6, 30, 54, 78, 102, 126 }, { 6, 26, 52, 78, 104, 130 }, { 6, 30, 56, 82, 108, 134 }, { 6, 34, 60, 86, 112, 138 }, { 6, 30, 58, 86, 114, 142 }, { 6, 34, 62, 90, 118, 146 },
	{ 6, 30, 54, 78, 102, 126, 150 }, { 6, 24, 50, 76, 102, 128, 154 }, { 6, 28, 54, 80, 106, 132, 158 }, { 6, 32, 58, 84, 110, 136, 162 }, { 6, 26, 54, 82, 110, 138, 166 }, { 6, 30, 58, 86, 114, 142, 170 },
};

const QrEcLevel levels[4] = { QrEcLevel::L, QrEcLevel::M, QrEcLevel::Q, QrEcLevel::H };

/// The format information beside the top left finder pattern: level bits and mask, when its BCH check passes.
bool readFormat(const QrCode& code, int& level_bits, int& mask)
{
	auto bits = 0;
	for (int i = 0; i <= 5; ++i)
		bits |= int(code.dark(8, i)) << i;
	bits |= int(code.dark(8, 7)) << 6;
	bits |= int(code.dark(8, 8)) << 7;
	bits |= int(code.dark(7, 8)) << 8;
	for (int i = 9; i < 15; ++i)
		bits |= int(code.dark(14 - i, 8)) << i;
	bits ^= 0x5412;
	auto remainder = bits;
	for (int i = 14; i >= 10; --i)
	{
		if ((remainder >> i) & 1)
			remainder ^= 0x537 << (i - 10);
	}
	level_bits = bits >> 13;
	mask = (bits >> 10) & 7;
	return remainder == 0;
}

/// Multiplication in the field of 256 elements, by shifting and adding.
int multiply(int a, int b)
{
	auto product = 0;
	for (; b != 0; b >>= 1)
	{
		if (b & 1)
			product ^= a;
		a <<= 1;
		if (a & 0x100)
			a ^= 0x11D;
	}
	return product;
}

/// True when the block, data and error correction, has no syndrome.
bool blockIsClean(const std::vector<int>& block, int ec_count)
{
	auto root = 1;
	for (int i = 0; i < ec_count; ++i)
	{
		auto value = 0;
		for (auto codeword : block)
			value = multiply(value, root) ^ codeword;
		if (value != 0)
			return false;
		root = multiply(root, 2);
	}
	return true;
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

/// Reads a symbol back. Returns false with a reason when something is wrong.
bool decode(const QrCode& code, QByteArray& data, QString& reason)
{
	auto const size = code.size;
	auto const version = (size - 17) / 4;
	if (version != code.version || size != 17 + 4 * version)
	{
		reason = QStringLiteral("size");
		return false;
	}
	
	// The modules which are not data.
	std::vector<char> function(std::size_t(size) * std::size_t(size), 0);
	auto const mark = [&function, size](int x, int y) {
		function[std::size_t(y) * std::size_t(size) + std::size_t(x)] = 1;
	};
	for (int y = 0; y < size; ++y)
	{
		for (int x = 0; x < size; ++x)
		{
			auto const finder = (x < 9 && y < 9) || (x >= size - 8 && y < 9) || (x < 9 && y >= size - 8);
			auto const version_info = version >= 7 && ((x >= size - 11 && x < size - 8 && y < 6) || (y >= size - 11 && y < size - 8 && x < 6));
			if (finder || version_info || x == 6 || y == 6)
				mark(x, y);
		}
	}
	auto const& positions = alignment_table[std::size_t(version - 1)];
	auto const count = positions.size();
	for (std::size_t i = 0; i < count; ++i)
	{
		for (std::size_t j = 0; j < count; ++j)
		{
			if ((i == 0 && j == 0) || (i == 0 && j == count - 1) || (i == count - 1 && j == 0))
				continue;
			for (int dy = -2; dy <= 2; ++dy)
			{
				for (int dx = -2; dx <= 2; ++dx)
					mark(positions[i] + dx, positions[j] + dy);
			}
		}
	}
	
	int level_bits, mask;
	if (!readFormat(code, level_bits, mask))
	{
		reason = QStringLiteral("format");
		return false;
	}
	
	// The codewords, along the path of the standard.
	std::vector<int> codewords;
	auto bits = 0;
	auto bit_count = 0;
	for (int right = size - 1; right >= 1; right -= 2)
	{
		if (right == 6)
			right = 5;
		auto const upward = ((right + 1) & 2) == 0;
		for (int step = 0; step < size; ++step)
		{
			auto const y = upward ? size - 1 - step : step;
			for (int j = 0; j < 2; ++j)
			{
				auto const x = right - j;
				if (function[std::size_t(y) * std::size_t(size) + std::size_t(x)])
					continue;
				bits = (bits << 1) | int(code.dark(x, y) != maskBit(mask, x, y));
				if (++bit_count == 8)
				{
					codewords.push_back(bits);
					bits = 0;
					bit_count = 0;
				}
			}
		}
	}
	
	// The blocks. Their number is whichever leaves every block without a syndrome.
	auto const total = int(codewords.size());
	auto const data_count = qrDataCodewords(version, code.level);
	auto const ec_total = total - data_count;
	std::vector<int> ordered;
	for (int block_count = 1; block_count <= 81 && ordered.empty(); ++block_count)
	{
		if (ec_total % block_count != 0 || ec_total / block_count < 7 || ec_total / block_count > 30)
			continue;
		auto const ec_count = ec_total / block_count;
		auto const short_blocks = block_count - total % block_count;
		auto const short_data = total / block_count - ec_count;
		std::vector<std::vector<int>> blocks(static_cast<std::size_t>(block_count));
		std::size_t next = 0;
		for (int i = 0; i <= short_data; ++i)
		{
			for (int b = 0; b < block_count; ++b)
			{
				if (i < short_data || b >= short_blocks)
					blocks[std::size_t(b)].push_back(codewords[next++]);
			}
		}
		for (int i = 0; i < ec_count; ++i)
		{
			for (int b = 0; b < block_count; ++b)
				blocks[std::size_t(b)].push_back(codewords[next++]);
		}
		auto clean = true;
		for (auto const& block : blocks)
			clean = clean && blockIsClean(block, ec_count);
		if (!clean)
			continue;
		for (auto const& block : blocks)
			ordered.insert(ordered.end(), block.begin(), block.end() - ec_count);
	}
	if (ordered.empty())
	{
		reason = QStringLiteral("syndromes");
		return false;
	}
	
	// Byte mode: four bits of mode, the count, the bytes.
	std::size_t position = 0;
	auto const take = [&ordered, &position](int n) {
		auto value = 0;
		for (int i = 0; i < n; ++i, ++position)
			value = (value << 1) | ((ordered[position >> 3] >> (7 - (position & 7))) & 1);
		return value;
	};
	if (take(4) != 4)
	{
		reason = QStringLiteral("mode");
		return false;
	}
	auto const length = take(version >= 10 ? 16 : 8);
	data.clear();
	for (int i = 0; i < length; ++i)
		data.append(char(take(8)));
	return true;
}

QByteArray sampleText(int length)
{
	QByteArray text;
	for (int i = 0; i < length; ++i)
		text.append(char(32 + (i * 7 + i / 5) % 95));
	return text;
}

}  // namespace


/**
 * @test Tests the QR code encoder.
 *
 * the design notes, "Generators", second change, Accept 14 to 17.
 */
class QrCodeTest : public QObject
{
Q_OBJECT
private slots:
	/// Accept (14)
	void reedSolomonTest()
	{
		const std::vector<std::uint8_t> data { 0x10, 0x20, 0x0C, 0x56, 0x61, 0x80, 0xEC, 0x11, 0xEC, 0x11, 0xEC, 0x11, 0xEC, 0x11, 0xEC, 0x11 };
		const std::vector<std::uint8_t> expected { 0xA5, 0x24, 0xD4, 0xC1, 0xED, 0x36, 0xC7, 0x87, 0x2C, 0x55 };
		QVERIFY(qrReedSolomon(data, 10) == expected);
	}
	
	void alignmentTest()
	{
		for (int version = 1; version <= 40; ++version)
			QVERIFY2(qrAlignmentPositions(version) == alignment_table[std::size_t(version - 1)], qPrintable(QString::number(version)));
	}
	
	/// Accept (15)
	void structureTest()
	{
		QString error;
		auto const code = encodeQr(QByteArray("FOOM"), QrEcLevel::M, &error);
		QVERIFY2(code.isValid(), qPrintable(error));
		QCOMPARE(code.version, 1);
		QCOMPARE(code.size, 21);
		QCOMPARE(code.modules.size(), std::size_t(21 * 21));
		
		// The finder patterns and the light separators around them.
		const int centres[3][2] = { { 3, 3 }, { 17, 3 }, { 3, 17 } };
		for (auto const& centre : centres)
		{
			for (int dy = -4; dy <= 4; ++dy)
			{
				for (int dx = -4; dx <= 4; ++dx)
				{
					auto const x = centre[0] + dx;
					auto const y = centre[1] + dy;
					if (x < 0 || y < 0 || x >= 21 || y >= 21)
						continue;
					auto const ring = std::max(std::abs(dx), std::abs(dy));
					QCOMPARE(code.dark(x, y), ring != 2 && ring != 4);
				}
			}
		}
		// The timing patterns between them, and the dark module.
		for (int i = 8; i <= 12; ++i)
		{
			QCOMPARE(code.dark(i, 6), i % 2 == 0);
			QCOMPARE(code.dark(6, i), i % 2 == 0);
		}
		QVERIFY(code.dark(8, 13));
		
		int level_bits, mask;
		QVERIFY(readFormat(code, level_bits, mask));
		QCOMPARE(level_bits, 0);  // M
		QCOMPARE(mask, code.mask);
		QVERIFY(code.mask >= 0 && code.mask < 8);
		
		QByteArray data;
		QString reason;
		QVERIFY2(decode(code, data, reason), qPrintable(reason));
		QCOMPARE(data, QByteArray("FOOM"));
	}
	
	/// Accept (16)
	void capacityTest()
	{
		QCOMPARE(encodeQr(sampleText(17), QrEcLevel::L).version, 1);
		QCOMPARE(encodeQr(sampleText(18), QrEcLevel::L).version, 2);
		QCOMPARE(encodeQr(sampleText(2953), QrEcLevel::L).version, 40);
		QString error;
		QVERIFY(!encodeQr(sampleText(2954), QrEcLevel::L, &error).isValid());
		QVERIFY(!error.isEmpty());
		error.clear();
		QVERIFY(!encodeQr(QByteArray(), QrEcLevel::L, &error).isValid());
		QVERIFY(!error.isEmpty());
		
		// Data codewords as the standard's table 7 gives them.
		const int known[7][5] = {
			{ 1, 19, 16, 13, 9 },
			{ 2, 34, 28, 22, 16 },
			{ 5, 108, 86, 62, 46 },
			{ 10, 274, 216, 154, 122 },
			{ 20, 861, 669, 485, 385 },
			{ 30, 1735, 1373, 985, 745 },
			{ 40, 2956, 2334, 1666, 1276 },
		};
		for (auto const& row : known)
		{
			for (int l = 0; l < 4; ++l)
				QCOMPARE(qrDataCodewords(row[0], levels[l]), row[l + 1]);
		}
		
		for (int version = 1; version <= 40; ++version)
		{
			for (auto level : levels)
			{
				auto const bytes = (qrDataCodewords(version, level) * 8 - 4 - (version >= 10 ? 16 : 8)) / 8;
				auto const code = encodeQr(sampleText(bytes), level);
				QCOMPARE(code.version, version);
				QCOMPARE(code.size, 17 + 4 * version);
			}
			auto const bytes = (qrDataCodewords(version, QrEcLevel::L) * 8 - 4 - (version >= 10 ? 16 : 8)) / 8;
			QCOMPARE(encodeQr(sampleText(bytes + 1), QrEcLevel::L).version, version < 40 ? version + 1 : 0);
		}
	}
	
	/// Accept (17)
	void roundTripTest_data()
	{
		QTest::addColumn<int>("length");
		for (auto length : { 1, 30, 200, 1000, 1200 })
			QTest::newRow(qPrintable(QString::number(length))) << length;
	}
	
	void roundTripTest()
	{
		QFETCH(int, length);
		auto const text = sampleText(length);
		for (auto level : levels)
		{
			auto const code = encodeQr(text, level);
			QVERIFY(code.isValid());
			QByteArray data;
			QString reason;
			QVERIFY2(decode(code, data, reason), qPrintable(QString(reason + QLatin1Char(' ') + QString::number(code.version))));
			QCOMPARE(data, text);
		}
	}
	
	/**
	 * Writes a symbol of every version and level, each filled to its
	 * capacity, as JSON for a check with a reader which is not ours.
	 * Runs only when the environment variable FOOM_QR_DUMP names the file.
	 */
	void dumpForReadersTest()
	{
		auto const path = qEnvironmentVariable("FOOM_QR_DUMP");
		if (path.isEmpty())
			QSKIP("FOOM_QR_DUMP is not set.");
		QJsonArray symbols;
		for (int version = 1; version <= 40; ++version)
		{
			for (int l = 0; l < 4; ++l)
			{
				auto const bytes = (qrDataCodewords(version, levels[l]) * 8 - 4 - (version >= 10 ? 16 : 8)) / 8;
				auto const text = sampleText(bytes);
				auto const code = encodeQr(text, levels[l]);
				QCOMPARE(code.version, version);
				QString modules;
				for (auto module : code.modules)
					modules += QLatin1Char(module ? '1' : '0');
				QJsonObject symbol;
				symbol.insert(QStringLiteral("version"), version);
				symbol.insert(QStringLiteral("level"), QString(QLatin1Char("LMQH"[l])));
				symbol.insert(QStringLiteral("mask"), code.mask);
				symbol.insert(QStringLiteral("text"), QString::fromLatin1(text));
				symbol.insert(QStringLiteral("modules"), modules);
				symbols.append(symbol);
			}
		}
		QFile file(path);
		QVERIFY(file.open(QIODevice::WriteOnly));
		file.write(QJsonDocument(symbols).toJson(QJsonDocument::Compact));
	}
	
	void utf8Test()
	{
		auto const text = QString::fromUtf8("Mapa: \xC5\xA0\xC3\xA1rka 2026").toUtf8();
		auto const code = encodeQr(text, QrEcLevel::Q);
		QByteArray data;
		QString reason;
		QVERIFY2(decode(code, data, reason), qPrintable(reason));
		QCOMPARE(data, text);
	}
};


QTEST_APPLESS_MAIN(QrCodeTest)
#include "qr_code_t.moc"  // IWYU pragma: keep
