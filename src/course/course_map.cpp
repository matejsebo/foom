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


#include "course_map.h"

#include <algorithm>
#include <cstddef>
#include <functional>
#include <iterator>
#include <map>
#include <utility>
#include <vector>

#include <QtMath>
#include <Qt>
#include <QChar>
#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QLatin1Char>
#include <QLatin1String>
#include <QCoreApplication>
#include <QPainter>
#include <QPen>
#include <QPolygonF>
#include <QRectF>
#include <QTransform>
#include <QUuid>
#ifdef QT_PRINTSUPPORT_LIB
#  include <QPrinter>
#  include <QPrinterInfo>
#endif

#include "core/extent_printing.h"
#include "core/extents.h"
#include "core/map.h"
#include "core/map_coord.h"
#include "core/map_view.h"
#include "core/map_printer.h"
#include "mapper_config.h"
#include "core/objects/object.h"
#include "core/objects/text_object.h"
#include "core/renderables/renderable.h"
#include "core/map_part.h"
#include "core/symbols/line_symbol.h"
#include "core/symbols/point_symbol.h"
#include "core/symbols/symbol.h"
#include "course/course_canvas.h"
#include "course/course_description.h"
#include "course/course_parts.h"
#include "course/course_render.h"
#include "course/route_course.h"
#include "templates/template.h"

namespace OpenOrienteering {

namespace {

QString tr(const char* text)
{
	return QCoreApplication::translate("OpenOrienteering::CourseMap", text);
}

bool isHiddenRole(Symbol::CourseRole role)
{
	switch (role)
	{
	case Symbol::CourseStart:
	case Symbol::CourseControl:
	case Symbol::CourseFinish:
	case Symbol::CourseMarkedRoute:
	case Symbol::CourseDescription:
	case Symbol::CourseTitle:
	case Symbol::CourseStartNumber:
	case Symbol::CourseVariantCode:
		return true;
	default:
		return false;
	}
}

/// The letters of a variant, without its leg and without blanks.
QString variantLetters(const RelayVariant& variant)
{
	return relayVariantCode(variant).mid(QString::number(variant.leg).size()).remove(QLatin1Char(' '));
}

/// The sides of a course without variations: its parts, on sheets.
std::vector<CourseMapSheet> sheetsOfParts(const Course& course)
{
	std::vector<CourseMapSheet> sheets;
	auto const parts = courseParts(course);
	sheets.emplace_back();
	for (std::size_t p = 0; p < parts.size(); ++p)
	{
		CourseMapSide side;
		side.course = course.name;
		side.part = parts.size() > 1 ? int(p) : -1;
		sheets.back().sides.push_back(side);
		// A map exchange begins a new sheet, a map flip a new side.
		if (parts[p].ends_with == CoursePart::MapExchange)
			sheets.emplace_back();
	}
	for (std::size_t s = 0; s < sheets.size(); ++s)
		sheets[s].name = sheets.size() > 1 ? QString(course.name + QLatin1Char('_') + QString::number(s + 1)) : course.name;
	return sheets;
}

/// Copies of the symbols of a map which are shown whatever the map hides.
class ShownSymbols
{
public:
	ShownSymbols(std::vector<std::unique_ptr<Symbol>>& store, std::vector<const Symbol*>& sources) : store(store), sources(sources) {}
	
	const Symbol* get(const Symbol* symbol)
	{
		auto& copy = copies[symbol];
		if (!copy)
		{
			store.push_back(duplicate(*symbol));
			sources.push_back(symbol);
			copy = store.back().get();
			copy->setHidden(false);
		}
		return copy;
	}
	
private:
	std::vector<std::unique_ptr<Symbol>>& store;
	std::vector<const Symbol*>& sources;
	std::map<const Symbol*, Symbol*> copies;
};

}  // namespace



std::vector<CourseMapSheet> courseMapSheets(const CourseData& data, const QString& course_name, RelayMaps relay_maps)
{
	std::vector<CourseMapSheet> sheets;
	auto const* course = data.findCourse(course_name);
	if (!course)
		return sheets;
	auto const structure = relayStructure(*course);
	if (!structure.isValid())
		return sheets;
	if (course->type == Course::Normal)
		return sheetsOfParts(*course);
	
	auto const add = [&sheets, course](const QString& name, const RelayVariant& variant, const QString& start_number) {
		CourseMapSheet sheet;
		sheet.name = course->name + QLatin1Char('.') + name;
		CourseMapSide side;
		side.course = course->name;
		side.variant = variant;
		side.start_number = start_number;
		sheet.sides.push_back(side);
		sheets.push_back(sheet);
	};
	if (relay_maps == RelayMaps::Runners)
	{
		auto const teams = relayTeams(data, course->name);
		auto const allocation = allocateRelay(structure, teams.count);
		if (!allocation.ok)
			return sheets;
		for (std::size_t t = 0; t < allocation.teams.size(); ++t)
		{
			for (auto const& variant : allocation.teams[t])
			{
				auto const number = QString(QString::number(teams.first_number + int(t)) + QLatin1Char('.') + QString::number(variant.leg));
				add(number, variant, number);
			}
		}
		return sheets;
	}
	
	QStringList loops;
	for (auto const& variant : relayVariants(structure))
	{
		if (course->type == Course::OneManRelay)
		{
			// Every loop once, whichever leg runs it.
			auto const letters = variantLetters(variant);
			if (loops.contains(letters))
				continue;
			loops.push_back(letters);
			add(letters, variant, {});
		}
		else
		{
			add(relayVariantCode(variant).remove(QLatin1Char(' ')), variant, {});
		}
	}
	return sheets;
}


QString courseTitleText(const CourseData& data, const CourseMapSide& side)
{
	auto const* course = data.findCourse(side.course);
	if (!course)
		return {};
	auto name = course->name;
	if (side.part >= 0 && courseParts(*course).size() > 1)
		name += QLatin1String(" (") + QString::number(side.part + 1) + QLatin1Char(')');
	QStringList classes;
	for (auto const& entry : data.classes)
	{
		if (entry.course == course->name)
			classes.push_back(entry.name);
	}
	QStringList lines;
	auto const mode = data.options.titleMode();
	if (mode != 0 || classes.isEmpty())
		lines.push_back(name);
	if (mode != 2)
		lines += classes;
	return lines.join(QLatin1Char('\n'));
}


QString courseVariantText(const CourseData& data, const CourseMapSide& side)
{
	auto const* course = data.findCourse(side.course);
	if (!course || course->type == Course::Normal || side.variant.leg < 1)
		return {};
	return course->type == Course::OneManRelay ? variantLetters(side.variant)
	                                           : relayVariantCode(side.variant).remove(QLatin1Char(' '));
}



CourseMapDrawing::CourseMapDrawing() = default;
CourseMapDrawing::CourseMapDrawing(CourseMapDrawing&&) noexcept = default;
CourseMapDrawing& CourseMapDrawing::operator=(CourseMapDrawing&&) noexcept = default;
CourseMapDrawing::~CourseMapDrawing() = default;


CourseMapDrawing drawCourseMap(const Map& map, const CourseData& original, const CourseMapSide& side, const QStringList& hidden_numbers)
{
	CourseMapDrawing result;
	auto const isLeftOut = [&hidden_numbers](const Symbol* symbol) {
		return !hidden_numbers.isEmpty()
		       && hidden_numbers.contains(canvasSymbolNumber(symbol->getNumberComponent(0), symbol->getNumberComponent(1)));
	};
	if (!original.findCourse(side.course))
		return result;
	// A relay is drawn as the course of its variant.
	auto const data = side.variant.leg > 0 ? withRelayVariant(original, side.course, side.variant) : original;
	auto const* course = data.findCourse(side.course);
	if (!course)
		return result;
	auto course_drawing = drawCourse(map, data, side.course, side.part);
	if (!course_drawing.drawn)
		return result;
	result.drawn = true;
	ShownSymbols shown(result.symbols, result.symbol_sources);
	
	// The objects of the items.
	for (auto const& stop : courseStops(map, data, *course))
	{
		if (side.part >= 0 && coursePartOfItem(*course, stop.item) != side.part)
			continue;
		auto const& item = course->items[std::size_t(stop.item)];
		if (result.items.contains(item.code))
			continue;
		auto const* course_object = data.findObject(item.code);
		auto const* object = course_object ? map.findObject(course_object->object) : nullptr;
		if (!object || !object->getSymbol() || isLeftOut(object->getSymbol()))
			continue;
		std::unique_ptr<Object> copy(object->duplicate());
		copy->setSymbol(shown.get(object->getSymbol()), true);
		if (item.type == CourseItem::Start && copy->getType() == Object::Point && copy->getSymbol()->isRotatable())
		{
			auto const turned = std::find_if(begin(course_drawing.starts), end(course_drawing.starts), [&item](const CourseStartDrawing& start) {
				return start.code == item.code;
			});
			if (turned != end(course_drawing.starts))
				copy->setRotation(qDegreesToRadians(turned->angle));
		}
		result.objects.push_back(std::move(copy));
		result.items.push_back(item.code);
	}
	
	// The lines and the numbers. Their symbols are those of the map: it has their objects as overrides only.
	for (auto const& leg : course_drawing.legs)
		result.legs += leg.has_line || leg.overridden ? 1 : 0;
	result.numbers = int(course_drawing.numbers.size());
	for (auto& object : course_drawing.objects)
	{
		if (isLeftOut(object->getSymbol()))
			continue;
		object->setSymbol(shown.get(object->getSymbol()), true);
		result.objects.push_back(std::move(object));
	}
	
	// The description, which is painted as the editor paints it.
	result.description = drawCourseDescriptions(map, data, side.course, side.part);
	result.has_description = result.description.drawn && !result.description.layouts.empty();
	for (auto& symbol : result.description.symbols)
		symbol->setHidden(false);
	
	// The title, the variant code and the start number.
	auto const title = courseTitleText(original, side);
	auto const variant = courseVariantText(original, side);
	for (auto kind : { CourseObject::CourseTitle, CourseObject::VariantCode, CourseObject::StartNumber })
	{
		auto const text = kind == CourseObject::CourseTitle ? title : kind == CourseObject::VariantCode ? variant : side.start_number;
		if (text.isEmpty())
			continue;
		auto made = false;
		for (auto const& course_object : original.objects)
		{
			if (course_object.kind != kind || course_object.object.isNull())
				continue;
			auto const* object = map.findObject(course_object.object);
			if (!object || object->getType() != Object::Text || !object->getSymbol() || isLeftOut(object->getSymbol()))
				continue;
			std::unique_ptr<Object> copy(object->duplicate());
			copy->setSymbol(shown.get(object->getSymbol()), true);
			copy->asText()->setText(text);
			result.objects.push_back(std::move(copy));
			made = true;
		}
		if (made)
			result.texts.push_back(text);
	}
	return result;
}


void paintCourseMap(QPainter* painter, Map* map, const CourseMapDrawing& drawing, const QRectF& bounding_box, qreal scaling, qreal opacity)
{
	if (!drawing.drawn)
		return;
	// The description first: its white ground lies below the course.
	paintCourseDescriptions(painter, map, drawing.description, bounding_box, scaling, opacity);
	if (drawing.objects.empty())
		return;
	MapRenderables renderables(map);
	for (auto const& object : drawing.objects)
	{
		object->update();
		renderables.insertRenderablesOfObject(object.get());
	}
	RenderConfig config = { *map, bounding_box, scaling, RenderConfig::NoOptions, opacity };
	renderables.draw(painter, config);
	for (auto const& object : drawing.objects)
		renderables.removeRenderablesOfObject(object.get(), false);
}


// the design notes, "Course only, intensity, line width and the control descriptions"
namespace {

void scaleLineWidths(Symbol* symbol, double factor)
{
	if (!symbol)
		return;
	auto const scaled = [factor](int width) { return qRound(width * factor); };
	if (symbol->getType() == Symbol::Line)
	{
		auto* line = symbol->asLine();
		line->setLineWidth(0.001 * scaled(line->getLineWidth()));
		line->getBorder().width = scaled(line->getBorder().width);
		line->getRightBorder().width = scaled(line->getRightBorder().width);
	}
	else if (symbol->getType() == Symbol::Point)
	{
		auto* point = symbol->asPoint();
		point->setOuterWidth(scaled(point->getOuterWidth()));
		for (int i = 0; i < point->getNumElements(); ++i)
			scaleLineWidths(point->getElementSymbol(i), factor);
	}
}

}  // namespace


void scaleCourseLineWidths(CourseMapDrawing& drawing, double factor)
{
	for (auto& symbol : drawing.symbols)
		scaleLineWidths(symbol.get(), factor);
	for (auto& object : drawing.objects)
		object->setOutputDirty();
}



bool isDrawnByCourseMap(const Symbol& symbol)
{
	return isHiddenRole(symbol.courseRole());
}


CourseSymbolsHider::CourseSymbolsHider(Map& map, const QStringList& numbers)
{
	for (int i = 0; i < map.getNumSymbols(); ++i)
	{
		auto* symbol = map.getSymbol(i);
		auto const named = !numbers.isEmpty()
		                   && numbers.contains(canvasSymbolNumber(symbol->getNumberComponent(0), symbol->getNumberComponent(1)));
		if (!symbol->isHidden() && (named || isHiddenRole(symbol->courseRole())))
		{
			symbol->setHidden(true);
			hidden.push_back(symbol);
		}
	}
}

CourseSymbolsHider::~CourseSymbolsHider()
{
	for (auto* symbol : hidden)
		symbol->setHidden(false);
}



// the design notes, "Canvases: the background maps"
std::unique_ptr<MapView> canvasPrintView(Map& map, const MapView* view, const CourseCanvas& canvas)
{
	auto hides = false;
	for (int i = 0; i < map.getNumTemplates() && !hides; ++i)
		hides = canvas.hidden_templates.contains(map.getTemplate(i)->getTemplateFilename());
	if (!hides)
		return {};
	auto print_view = std::make_unique<MapView>(&map);
	if (view)
	{
		print_view->setMapVisibility(view->getMapVisibility());
		print_view->setAllTemplatesHidden(view->areAllTemplatesHidden());
	}
	for (int i = 0; i < map.getNumTemplates(); ++i)
	{
		auto* temp = map.getTemplate(i);
		auto visibility = view ? view->getTemplateVisibility(temp) : TemplateVisibility{ 1.0, true };
		if (canvas.hidden_templates.contains(temp->getTemplateFilename()))
			visibility.visible = false;
		print_view->setTemplateVisibility(temp, visibility);
	}
	return print_view;
}


QString courseMapOptionsError(const CourseMapExportOptions& options)
{
	if (options.intensity < 10 || options.intensity > 100)
		return tr("The intensity must be from 10 to 100 percent.");
	if (options.line_width < 50 || options.line_width > 200)
		return tr("The line width must be from 50 to 200 percent.");
	auto const stretched = [](double value) { return value >= 90.0 && value <= 110.0; };
	if (!stretched(options.stretch_x) || !stretched(options.stretch_y))
		return tr("The stretch must be from 90 to 110 percent.");
	if (qAbs(options.offset_x) > 25.0 || qAbs(options.offset_y) > 25.0)
		return tr("Adjustments must be between -25 mm and 25 mm.");
	if (!options.map_format.isEmpty()
	    && (options.content != CourseMapContent::Map || options.intensity != 100 || options.line_width != 100))
		return tr("Course only, the control descriptions, the intensity and the line width are for PDF files and printing.");
	if (options.content == CourseMapContent::Descriptions && options.placing == CourseMapPlacing::Original)
		return tr("The control descriptions cannot be placed over each other.");
	return {};
}


QString courseMapsOneFileName()
{
	return QStringLiteral("Courses");
}


QString courseMapFileName(const QString& base_name, const QString& sheet)
{
	return courseMapFileName(base_name, sheet, QStringLiteral("pdf"));
}

QString courseMapFileName(const QString& base_name, const QString& sheet, const QString& extension)
{
	auto name = QString(base_name + QLatin1Char('.') + sheet + QLatin1Char('.') + extension);
	for (auto& character : name)
	{
		if (character < QLatin1Char(' ') || QStringLiteral("\\/:*?\"<>|").contains(character))
			character = QLatin1Char('_');
	}
	return name;
}


void paintCourseRoutes(QPainter* painter, const CourseRoutes& routes)
{
	auto const paint = [painter](const std::vector<MapCoordF>& points) {
		if (points.size() < 2)
			return;
		QPolygonF line;
		line.reserve(int(points.size()));
		for (auto const& point : points)
			line.push_back(point);
		painter->drawPolyline(line);
	};
	painter->save();
	painter->setRenderHint(QPainter::Antialiasing, true);
	painter->setBrush(Qt::NoBrush);
	QPen pen(QColor(225, 0, 50), 0.3);
	pen.setCapStyle(Qt::RoundCap);
	pen.setJoinStyle(Qt::RoundJoin);
	painter->setPen(pen);
	for (auto const& leg : routes.legs)
		paint(leg.route.points);
	QPen via_pen(QColor(0, 90, 225), 0.3, Qt::DashLine);
	via_pen.setJoinStyle(Qt::RoundJoin);
	painter->setPen(via_pen);
	for (auto const& leg : routes.legs)
	{
		for (auto const& via_route : leg.via_routes)
			paint(via_route.points);
	}
	painter->restore();
}



void paintRoutePerimeter(QPainter* painter, const std::vector<MapCoordF>& perimeter)
{
	if (perimeter.size() < 3)
		return;
	QPolygonF polygon;
	for (auto const& point : perimeter)
		polygon.push_back(QPointF(point));
	painter->save();
	painter->setRenderHint(QPainter::Antialiasing, true);
	painter->setBrush(Qt::NoBrush);
	QPen pen(QColor(110, 110, 110), 0.25, Qt::DashLine);
	pen.setJoinStyle(Qt::MiterJoin);
	painter->setPen(pen);
	painter->drawPolygon(polygon);
	painter->restore();
}



#ifdef QT_PRINTSUPPORT_LIB

namespace {

/** Hides, as long as it lives, every symbol of the map but the kept ones. */
class AllSymbolsHider
{
public:
	AllSymbolsHider(Map& map, bool active, const std::vector<const Symbol*>& kept)
	{
		if (!active)
			return;
		for (int i = 0; i < map.getNumSymbols(); ++i)
		{
			auto* symbol = map.getSymbol(i);
			if (!symbol->isHidden() && std::find(kept.begin(), kept.end(), symbol) == kept.end())
			{
				symbol->setHidden(true);
				hidden.push_back(symbol);
			}
		}
	}
	~AllSymbolsHider()
	{
		for (auto* symbol : hidden)
			symbol->setHidden(false);
	}
	AllSymbolsHider(const AllSymbolsHider&) = delete;
	AllSymbolsHider& operator=(const AllSymbolsHider&) = delete;

private:
	std::vector<Symbol*> hidden;
};


/**
 * Sets up a printer for every sheet of the options and gives it to the
 * output while the symbols which the sheet draws itself are hidden.
 * False, with the error in the report, when the sheets cannot be printed
 * or when the output says so.
 */
bool printSheets(Map& map, const CourseMapExportOptions& options, const QPrinterInfo* target, CourseMapExportReport& report,
                 const std::function<bool(const QString&, MapPrinter&)>& output)
{
	auto const& data = map.courseData();
	const Extent* extent = nullptr;
	if (!options.extent.isEmpty())
	{
		auto const& extents = map.extents();
		auto const named = std::find_if(begin(extents), end(extents), [&options](const Extent& entry) { return entry.name == options.extent; });
		if (named == end(extents))
		{
			report.error = tr("The map has no extent \"%1\".").arg(options.extent);
			return false;
		}
		extent = &*named;
	}

	auto courses = options.courses;
	if (courses.isEmpty())
	{
		for (auto const& course : data.courses)
			courses.push_back(course.name);
	}

	// the design notes, "Course only, intensity, line width and the control descriptions"
	auto const content = options.content;
	auto const print_error = courseMapOptionsError(options);
	if (!print_error.isEmpty())
	{
		report.error = print_error;
		return false;
	}
	auto const use_canvases = options.use_canvases && content != CourseMapContent::Descriptions;
	if (content == CourseMapContent::Descriptions)
		extent = nullptr;

	// the design notes, "Several courses in one file"
	auto const placing = options.placing;
	if (placing == CourseMapPlacing::Beside || placing == CourseMapPlacing::Below)
	{
		report.error = tr("Maps beside or below each other can be written as map files only.");
		return false;
	}
	if (placing != CourseMapPlacing::Separate && use_canvases)
	{
		report.error = tr("By the canvases every map is a file of its own.");
		return false;
	}

	std::vector<CourseMapSheet> all_sheets;
	for (auto const& course : qAsConst(courses))
	{
		auto sheets = courseMapSheets(data, course, options.relay_maps);
		if (sheets.empty())
			report.skipped.push_back(course);
		for (auto& sheet : sheets)
			all_sheets.push_back(std::move(sheet));
	}
	if (placing != CourseMapPlacing::Separate)
	{
		// All maps are the sides of one sheet.
		CourseMapSheet one;
		one.name = courseMapsOneFileName();
		for (auto const& sheet : all_sheets)
			one.sides.insert(one.sides.end(), sheet.sides.begin(), sheet.sides.end());
		all_sheets.clear();
		if (!one.sides.empty())
			all_sheets.push_back(std::move(one));
	}

	for (auto const& sheet : all_sheets)
	{
		// With canvases the sheet is that of the canvas of its first side.
		auto const* canvas = use_canvases ? canvasFor(data.canvases, sheet.sides.front().course, sheet.sides.front().part) : nullptr;
		if (use_canvases && !canvas)
		{
			report.skipped.push_back(sheet.name);
			continue;
		}
		auto sheet_data = data;
		auto const* sheet_extent = extent;
		auto sheet_scale = content == CourseMapContent::Descriptions ? 0u : options.scale;
		auto sheet_name = sheet.name;
		QStringList hidden_numbers;
		if (canvas)
		{
			auto const& extents = map.extents();
			auto const named = std::find_if(begin(extents), end(extents), [canvas](const Extent& entry) { return entry.name == canvas->extent; });
			if (named == end(extents))
			{
				report.error = tr("No extent found for the canvas \"%1\". Define its extent first.").arg(canvas->name);
				return false;
			}
			sheet_extent = &*named;
			sheet_scale = canvas->scale;
			sheet_name = canvas->name + QLatin1Char('.') + sheet.name;
			hidden_numbers = canvas->hidden_symbols;
			if (canvas->box_size > 0.0)
				sheet_data.options.setValue(QLatin1Char('s'), QString::number(canvas->box_size, 'f', 2));
			if (canvas->max_rows > 0)
				sheet_data.options.setValue(QLatin1Char('i'), QString::number(canvas->max_rows));
		}

		// Drawn before the symbols are hidden: the copies are of symbols as the map shows them.
		std::vector<CourseMapDrawing> sides;
		for (auto const& side : sheet.sides)
			sides.push_back(drawCourseMap(map, sheet_data, side, hidden_numbers));
		if (options.line_width != 100)
		{
			for (auto& drawing : sides)
				scaleCourseLineWidths(drawing, options.line_width / 100.0);
		}
		// The control descriptions alone: the page is the box of their blocks.
		QRectF description_box;
		if (content == CourseMapContent::Descriptions)
		{
			for (auto const& drawing : sides)
			{
				for (auto const& layout : drawing.description.layouts)
				{
					for (auto const& block : layout.blocks)
						description_box = description_box.isValid() ? description_box.united(block) : block;
				}
			}
			if (!description_box.isValid())
			{
				report.skipped.push_back(sheet.name);
				continue;
			}
			description_box.adjust(-5.0, -5.0, 5.0, 5.0);
			sheet_name += QLatin1String(".Descriptions");
		}
		// Course only: the objects of the other course symbols, by the numbers 701 to 799, are a part of
		// the course; not those of the lines and the numbers (705, 704), which the map has as overrides only.
		auto const isOtherCourseSymbol = [](const Symbol& symbol) {
			auto const number = symbol.getNumberComponent(0);
			return number >= 701 && number <= 799 && number != 704 && number != 705 && !isDrawnByCourseMap(symbol);
		};
		CourseMapDrawing others;
		if (content == CourseMapContent::CourseOnly)
		{
			others.drawn = true;
			for (int p = 0; p < map.getNumParts(); ++p)
			{
				auto const* part = map.getPart(std::size_t(p));
				for (int i = 0; i < part->getNumObjects(); ++i)
				{
					auto const* object = part->getObject(i);
					auto const* symbol = object->getSymbol();
					if (!symbol || symbol->isHidden() || !isOtherCourseSymbol(*symbol)
					    || hidden_numbers.contains(canvasSymbolNumber(symbol->getNumberComponent(0), symbol->getNumberComponent(1))))
						continue;
					// With a copy of its symbol, as the map's symbols are hidden while the course is printed alone.
					auto source = std::find(others.symbol_sources.begin(), others.symbol_sources.end(), symbol);
					if (source == others.symbol_sources.end())
					{
						auto shown = duplicate(*symbol);
						shown->setHidden(false);
						others.symbols.push_back(std::move(shown));
						others.symbol_sources.push_back(symbol);
						source = others.symbol_sources.end() - 1;
					}
					std::unique_ptr<Object> copy(object->duplicate());
					copy->setSymbol(others.symbols[std::size_t(source - others.symbol_sources.begin())].get(), true);
					others.objects.push_back(std::move(copy));
				}
			}
		}
		// The routes which the map keeps for the sides.
		std::vector<CourseRoutes> side_routes;
		if (options.with_routes)
		{
			for (auto const& side : sheet.sides)
			{
				if (side.variant.leg > 0)
					side_routes.push_back(storedCourseRoutes(map, withRelayVariant(sheet_data, side.course, side.variant), side.course, side.part));
				else
					side_routes.push_back(storedCourseRoutes(map, sheet_data, side.course, side.part));
			}
		}

		// The background maps which the canvas hides are not printed; nor the map, for the course or the descriptions alone.
		auto sheet_view = canvas ? canvasPrintView(map, options.view, *canvas) : std::unique_ptr<MapView>{};
		if (content != CourseMapContent::Map)
		{
			sheet_view = std::make_unique<MapView>(&map);
			// The map is drawn with all its symbols hidden (below); the templates are hidden each by itself.
			for (int i = 0; i < map.getNumTemplates(); ++i)
				sheet_view->setTemplateVisibility(map.getTemplate(i), TemplateVisibility{ 1.0, false });
		}
		MapPrinter printer(map, sheet_view ? sheet_view.get() : options.view);
		printer.setTarget(target);
		printer.setMode(MapPrinterOptions::Vector);
		printer.setPrintTemplates(content == CourseMapContent::Map);
		if (content == CourseMapContent::Descriptions)
		{
			printer.setPrintArea(description_box);
			printer.setScale(map.getScaleDenominator());
		}
		else if (sheet_extent)
		{
			applyExtentToPrinter(printer, map, *sheet_extent);
			// The sides of the sheet are those of the course, not the backs of the extent.
			printer.setBackSides({});
		}
		else if (!map.hasPrinterConfig())
		{
			printer.setPrintArea(map.calculateExtent(false, true, options.view));
		}
		if (sheet_scale > 0)
			printer.setScale(sheet_scale);
		// One page for a side: without an extent which brings its paper, the page is as large as the area.
		if (!sheet_extent || sheet_scale > 0)
			printer.setCustomPageSize(printer.getPrintAreaPaperSize());

		// Course only: stretched about the bottom left corner of the print area, mirrored, and moved.
		QTransform adjustment;
		if (content == CourseMapContent::CourseOnly)
		{
			auto const area = printer.getPrintArea();
			auto const sx = options.stretch_x / 100.0;
			auto const sy = options.stretch_y / 100.0;
			adjustment = QTransform(sx, 0.0, 0.0, sy, area.left() * (1.0 - sx), area.bottom() * (1.0 - sy));
			if (options.mirror)
				adjustment *= QTransform(-1.0, 0.0, 0.0, 1.0, 2.0 * area.center().x(), 0.0);
			// From mm on the paper to mm of the map.
			auto const paper_to_map = printer.getOptions().scale > 0 ? double(printer.getOptions().scale) / double(map.getScaleDenominator()) : 1.0;
			adjustment *= QTransform::fromTranslate(options.offset_x * paper_to_map, -options.offset_y * paper_to_map);
		}
		auto const opacity = options.intensity / 100.0;
		// At their place all sides are painted on one page.
		auto const original = placing == CourseMapPlacing::Original;
		auto const descriptions_only = content == CourseMapContent::Descriptions;
		printer.setOverlay([&map, &sides, &side_routes, &others, original, descriptions_only, adjustment, opacity](QPainter* painter, const RenderConfig& config, int side) {
			painter->save();
			painter->setTransform(adjustment, true);
			auto const box = adjustment.inverted().mapRect(config.bounding_box);
			painter->setOpacity(opacity);
			if (descriptions_only)
			{
				if (side >= 0 && std::size_t(side) < sides.size())
					paintCourseDescriptions(painter, &map, sides[std::size_t(side)].description, box, config.scaling, opacity);
				painter->restore();
				return;
			}
			paintCourseMap(painter, &map, others, box, config.scaling, opacity);
			for (std::size_t i = 0; i < sides.size(); ++i)
			{
				if (original || int(i) == side)
					paintCourseMap(painter, &map, sides[i], box, config.scaling, opacity);
			}
			for (std::size_t i = 0; i < side_routes.size(); ++i)
			{
				if (original || int(i) == side)
				{
					painter->setOpacity(opacity);
					paintCourseRoutes(painter, side_routes[i]);
				}
			}
			painter->restore();
		}, original ? 1 : int(sides.size()));

		const CourseSymbolsHider hider(map, hidden_numbers);
		// The course or the descriptions alone: no symbol of the map is drawn but those of the descriptions.
		std::vector<const Symbol*> kept;
		if (content != CourseMapContent::Map)
		{
			for (auto const& drawing : sides)
			{
				for (auto const& object : drawing.description.objects)
					kept.push_back(object->getSymbol());
			}
		}
		const AllSymbolsHider all_hidden(map, content != CourseMapContent::Map, kept);
		if (!output(sheet_name, printer))
			return false;
	}
	return true;
}

}  // namespace

#endif


CourseMapExportReport exportCourseMaps(Map& map, const CourseMapExportOptions& options)
{
	if (!options.map_format.isEmpty())
		return exportCourseMapFiles(map, options);
	CourseMapExportReport report;
#ifndef QT_PRINTSUPPORT_LIB
	Q_UNUSED(map)
	Q_UNUSED(options)
	report.error = tr("This build cannot write PDF files.");
	return report;
#else
	QDir folder(options.folder);
	if (options.folder.isEmpty() || !folder.mkpath(QStringLiteral(".")))
	{
		report.error = tr("Cannot write into the folder \"%1\".").arg(options.folder);
		return report;
	}
	report.ok = printSheets(map, options, MapPrinter::pdfTarget(), report, [&](const QString& sheet_name, MapPrinter& printer) {
		auto const path = folder.absoluteFilePath(courseMapFileName(options.base_name, sheet_name));
		auto qprinter = printer.makePrinter();
		auto written = false;
		if (qprinter)
		{
			qprinter->setOutputFormat(QPrinter::PdfFormat);
			qprinter->setOutputFileName(path);
			qprinter->setCreator(QString::fromLatin1("FOOM " APP_VERSION));
			qprinter->setDocName(QFileInfo(path).completeBaseName());
			written = printer.printMap(qprinter.get());
		}
		if (!written)
		{
			QFile::remove(path);
			report.error = tr("Cannot write the file \"%1\".").arg(path);
			return false;
		}
		report.files.push_back(path);
		return true;
	});
	return report;
#endif
}


std::vector<QImage> courseMapImages(Map& map, const CourseMapExportOptions& options, double pixel_per_mm, CourseMapExportReport* report)
{
	std::vector<QImage> images;
	CourseMapExportReport own;
	auto& result = report ? *report : own;
	result = {};
#ifndef QT_PRINTSUPPORT_LIB
	Q_UNUSED(map)
	Q_UNUSED(options)
	Q_UNUSED(pixel_per_mm)
	result.error = tr("This build cannot print.");
#else
	result.ok = printSheets(map, options, MapPrinter::imageTarget(), result, [&](const QString& sheet_name, MapPrinter& printer) {
		printer.setMode(MapPrinterOptions::Raster);
		printer.setResolution(qRound(pixel_per_mm * 25.4));
		auto const size = printer.getPrintAreaPaperSize() * pixel_per_mm;
		QImage image(qRound(size.width()), qRound(size.height()), QImage::Format_ARGB32_Premultiplied);
		if (image.isNull())
		{
			result.error = tr("Cannot print the map of \"%1\".").arg(sheet_name);
			return false;
		}
		image.fill(Qt::white);
		{
			QPainter painter(&image);
			printer.drawPage(&painter, printer.getPrintArea(), &image);
		}
		images.push_back(image);
		result.files.push_back(sheet_name);
		return true;
	});
	if (!result.ok)
		images.clear();
#endif
	return images;
}


// the design notes, "Printing the course maps"
CourseMapExportReport printCourseMaps(Map& map, const CourseMapExportOptions& options, QPrinter* printer)
{
	CourseMapExportReport report;
#ifndef QT_PRINTSUPPORT_LIB
	Q_UNUSED(map)
	Q_UNUSED(options)
	Q_UNUSED(printer)
	report.error = tr("This build cannot print.");
	return report;
#else
	if (!printer)
	{
		report.error = tr("No printer.");
		return report;
	}
	// A printer which writes a file prints as the PDF export does; the paper is that of the printer either way.
	const QPrinterInfo info(*printer);
	auto const* target = printer->outputFormat() == QPrinter::PdfFormat ? MapPrinter::pdfTarget() : &info;
	report.ok = printSheets(map, options, target, report, [&](const QString& sheet_name, MapPrinter& map_printer) {
		printer->setDocName(sheet_name);
		if (!map_printer.printMap(printer))
		{
			report.error = tr("Cannot print the map of \"%1\".").arg(sheet_name);
			return false;
		}
		report.files.push_back(sheet_name);
		return true;
	});
	return report;
#endif
}


}  // namespace OpenOrienteering
