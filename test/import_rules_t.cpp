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
#include <utility>
#include <vector>

#include <QtGlobal>
#include <QtTest>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QLatin1String>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTextStream>

#include "test_config.h"

#include "global.h"
#include "core/map.h"
#include "core/objects/object.h"
#include "core/objects/object_query.h"
#include "core/objects/symbol_rule_set.h"
#include "core/symbols/symbol.h"
#include "gdal/geodata_import.h"

using namespace OpenOrienteering;


namespace {

QString sourcePath(const QString& relative)
{
	return QDir(QString::fromUtf8(MAPPER_TEST_SOURCE_DIR)).absoluteFilePath(QStringLiteral("../") + relative);
}

QString fixturePath(const QString& name)
{
	return QDir(QString::fromUtf8(MAPPER_TEST_SOURCE_DIR)).absoluteFilePath(QStringLiteral("data/import-rules/") + name);
}

/// The queries of the "# not mapped:" lines of a rule file's header.
std::vector<ObjectQuery> notMapped(const QString& crt_path, const Map& map, QStringList& texts)
{
	std::vector<ObjectQuery> result;
	QFile file(crt_path);
	if (!file.open(QIODevice::ReadOnly))
		return result;
	QTextStream stream(&file);
	stream.setCodec("UTF-8");
	ObjectQueryParser parser(&map);
	QLatin1String const prefix("# not mapped:");
	while (!stream.atEnd())
	{
		auto const line = stream.readLine().trimmed();
		if (!line.startsWith(prefix))
			continue;
		auto const text = line.mid(prefix.size()).trimmed();
		auto query = parser.parse(text);
		texts.push_back(text);
		result.push_back(std::move(query));
	}
	return result;
}

bool compatible(const Object* object, const Symbol* symbol)
{
	switch (object->getType())
	{
	case Object::Point:
		return symbol->getType() == Symbol::Point;
	case Object::Path:
		return symbol->getType() == Symbol::Line || symbol->getType() == Symbol::Area || symbol->getType() == Symbol::Combined;
	case Object::Text:
		return symbol->getType() == Symbol::Text;
	}
	return false;
}

QString describe(const Object* object)
{
	QStringList tags;
	for (auto const& tag : object->tags())
		tags.push_back(tag.key + QLatin1Char('=') + tag.value);
	return tags.join(QLatin1String(", "));
}

}  // namespace


/**
 * foom: The import rule tables of national datasets.
 *
 * See the design notes, "Import rule tables".
 */
class ImportRulesTest : public QObject
{
Q_OBJECT
private slots:
	void initTestCase()
	{
		QCoreApplication::setOrganizationName(QStringLiteral("OpenOrienteering.org"));
		QCoreApplication::setApplicationName(QString::fromLatin1(metaObject()->className()));
		doStaticInitializations();
	}

	void tableTest_data()
	{
		QTest::addColumn<QString>("dataset");
		QTest::addColumn<QString>("symbol_set");
		auto const isom = QStringLiteral("10000/ISOM 2017-2_10000.omap");
		auto const issprom = QStringLiteral("4000/ISSprOM 2019_4000.omap");
		for (auto const* dataset : { "TNM" })
		{
			QTest::newRow(QByteArray(QByteArray(dataset) + " ISOM").constData()) << QString::fromLatin1(dataset) << isom;
			QTest::newRow(QByteArray(QByteArray(dataset) + " ISSprOM").constData()) << QString::fromLatin1(dataset) << issprom;
		}
	}

	/**
	 * Every feature of the dataset's catalogue fixture lands on a symbol of
	 * the symbol set of its kind, or its class is listed as not mapped.
	 */
	void tableTest()
	{
		QFETCH(QString, dataset);
		QFETCH(QString, symbol_set);

		Map map;
		QVERIFY(map.loadFrom(sourcePath(QStringLiteral("symbol sets/") + symbol_set)));
		auto const set_symbols = map.getNumSymbols();
		std::vector<const Symbol*> symbols;
		for (int i = 0; i < set_symbols; ++i)
			symbols.push_back(map.getSymbol(i));

		auto const crt = sourcePath(QStringLiteral("symbol sets/") + dataset + QLatin1Char('-') + map.symbolSetId() + QLatin1String(".crt"));
		QVERIFY2(QFileInfo::exists(crt), qPrintable(crt));

		// Every rule names a visible symbol of the set and has a query.
		{
			QFile file(crt);
			QVERIFY(file.open(QIODevice::ReadOnly));
			QTextStream stream(&file);
			stream.setCodec("UTF-8");
			auto const rules = SymbolRuleSet::loadCrt(stream, map);
			QCOMPARE(stream.status(), QTextStream::Ok);
			QVERIFY(!rules.empty());
			for (auto const& rule : rules)
			{
				auto const ok = rule.type != SymbolRule::NoAssignment && rule.symbol && !rule.symbol->isHidden() && rule.query.getOperator();
				QVERIFY2(ok, qPrintable(QStringLiteral("Invalid rule: %1  %2").arg(rule.symbol ? rule.symbol->getNumberAsString() : QStringLiteral("???"), rule.query.toString())));
			}
		}

		QStringList not_mapped_texts;
		auto const not_mapped = notMapped(crt, map, not_mapped_texts);

		GeodataImportOptions options;
		options.crs_mode = GeodataImportOptions::CrsUnreferenced;
		options.units = GeodataImportOptions::MillimetresOnPaper;
		options.layer_tag = GeodataImportOptions::AttributeTag;
		options.layer_tag_attribute = QStringLiteral("foom_layer");
		options.crt_file = crt;
		auto const fixture = fixturePath(dataset + QLatin1String(".geojson"));
		QVERIFY2(QFileInfo::exists(fixture), qPrintable(fixture));
		auto const result = importGeodata(map, { fixture }, options);
		QVERIFY2(result.ok, qPrintable(result.error));
		QVERIFY(result.objects > 0);

		QStringList failures;
		int mapped = 0;
		std::vector<int> used(not_mapped.size(), 0);
		map.applyOnAllObjects([&](const Object* object) {
			auto const* symbol = object->getSymbol();
			if (std::find(symbols.begin(), symbols.end(), symbol) != symbols.end())
			{
				++mapped;
				if (!compatible(object, symbol))
					failures.push_back(QStringLiteral("%1 on %2 of another kind").arg(describe(object), symbol->getNumberAsString()));
				return;
			}
			for (std::size_t i = 0; i < not_mapped.size(); ++i)
			{
				if (not_mapped[i](object))
				{
					++used[i];
					return;
				}
			}
			failures.push_back(QStringLiteral("%1 on a default symbol").arg(describe(object)));
		});
		for (std::size_t i = 0; i < used.size(); ++i)
		{
			if (!used[i])
				failures.push_back(QStringLiteral("no feature of the not mapped class %1").arg(not_mapped_texts[int(i)]));
		}
		QVERIFY2(failures.isEmpty(), qPrintable(failures.join(QLatin1Char('\n'))));
		QVERIFY(mapped > 0);
	}
};


/*
 * We don't need a real GUI window.
 */
namespace  {
	auto Q_DECL_UNUSED qpa_selected = qputenv("QT_QPA_PLATFORM", "offscreen");  // clazy:exclude=non-pod-global-static
}


QTEST_MAIN(ImportRulesTest)
#include "import_rules_t.moc"  // IWYU pragma: keep
