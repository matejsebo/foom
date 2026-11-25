#
#    Copyright 2012-2022, 2025, 2026 Kai Pastor
#    
#    This file is part of OpenOrienteering.
# 
#    OpenOrienteering is free software: you can redistribute it and/or modify
#    it under the terms of the GNU General Public License as published by
#    the Free Software Foundation, either version 3 of the License, or
#    (at your option) any later version.
# 
#    OpenOrienteering is distributed in the hope that it will be useful,
#    but WITHOUT ANY WARRANTY; without even the implied warranty of
#    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#    GNU General Public License for more details.
# 
#    You should have received a copy of the GNU General Public License
#    along with OpenOrienteering.  If not, see <http://www.gnu.org/licenses/>.


find_package(Qt5Network REQUIRED)
find_package(Qt5PrintSupport REQUIRED)
find_package(Qt5Test REQUIRED)
find_package(Qt5Widgets REQUIRED)
find_package(Qt5Positioning)
find_package(Qt5Sensors)

set(CMAKE_AUTOMOC ON)

if(Mapper_DEVELOPMENT_BUILD)
	include(EnableSanitize)
	enable_sanitize(address undefined NO_RECOVER)
endif()

add_definitions(
  -DQT_NO_CAST_FROM_ASCII
  -DQT_NO_CAST_TO_ASCII
  -DQT_USE_QSTRINGBUILDER
)

configure_file(test_config.h.in ${CMAKE_CURRENT_BINARY_DIR}/test_config.h @ONLY)


# This function defines a test from a list of source file names
# (without ending). For each filename, a .cpp file and .h must exist.
# The .h file is processed with Qt's moc.
# The first filename is also used as the name of the executable file and
# as the name of the test.
#
# The test executable is linked only to external libraries by default.
# It only needs to be rebuild and run when one of its components was modified.
# Additional link libraries may be added to the executable target as usual.
#
# The autorun parameter controls whether the test will be run automatically
# when executing Mapper-with-test.
#
function(add_test_helper testname autorun)
	unset(TEST_${testname}_SRCS)
	set(manual_test 0)
	foreach(arg ${ARGN})
		if("${arg}" STREQUAL "MANUAL")
			set(manual_test 1)
		else()
			list(APPEND TEST_${testname}_SRCS ${arg}.cpp)
		endif()
	endforeach()
	add_executable(${testname} ${testname}.cpp ${TEST_${testname}_SRCS})
	target_link_libraries(${testname} PRIVATE
	  ${PROJ_LIBRARIES}
	  Qt5::Test
	  Qt5::Gui
	  Polyclipping::Polyclipping
	)
	if("${ARGN}" MATCHES "crs_template_implementation")
		target_link_libraries(${testname} PRIVATE Qt5::Widgets)
	endif()
	if(NOT manual_test)
		configure_file(TESTNAME-RUN.cmake.in ${CMAKE_CURRENT_BINARY_DIR}/${testname}-RUN.cmake @ONLY)
		add_test(${testname} ${CMAKE_COMMAND} -P ${testname}-RUN.cmake)
		add_custom_command(OUTPUT ${testname}-RUN.stamp
		  COMMAND ${CMAKE_COMMAND} -P ${testname}-RUN.cmake
		  DEPENDS ${testname}
		)
		if(autorun)
			set_property(DIRECTORY APPEND PROPERTY Mapper_AUTORUN_TESTS ${testname})
		endif()
	endif()
endfunction()


# This function defines a unit test from a list of source file names
# (without ending). For each filename, a .cpp file and .h must exist.
# The .h file is processed with Qt's moc.
# The first filename is also used as the name of the executable file and
# as the name of the test.
#
# When Mapper_DEVELOPMENT_BUILD is enabled, a unit test executable is linked
# only to external libraries by default. It only needs to be rebuild and run
# when its explicit sources are modified.
# 
# When Mapper_DEVELOPMENT_BUILD is disabled, the additional explicit sources
# are ignored, and the unit test executable is linked directly to the
# Mapper_Common library. This is a faster choice for one-time builds like CI,
# and it also avoids problems with collecting test coverage information.
#
function(add_unit_test testname)
	if(Mapper_DEVELOPMENT_BUILD)
		add_test_helper("${testname}" 1 ${ARGN})
	elseif("${ARGN}" MATCHES "MANUAL")
		add_system_test("${testname}" MANUAL)
	else()
		add_system_test("${testname}")
	endif()
endfunction()


# This function defines a system test from a list of source file names
# (without ending). For each filename, a .cpp file and .h must exist.
# The .h file is processed with Qt's moc.
# The first filename is also used as the name of the executable file and
# as the name of the test.
#
# A system test executable is linked to the full Mapper runtime.
# That is why it will be rebuild and run very often.
#
function(add_system_test testname)
	add_test_helper("${testname}" ${Mapper_AUTORUN_SYSTEM_TESTS} ${ARGN})
	target_link_libraries(${testname} PRIVATE
	  Mapper_Common
	  Qt5::Widgets
	)
endfunction()


# Include generated files (moc output, build configuration)
include_directories("${PROJECT_BINARY_DIR}/src")
include_directories("${PROJECT_SOURCE_DIR}/src")
include_directories("${CMAKE_CURRENT_BINARY_DIR}")


add_library(MapperTestUtils STATIC
    image-difference.cpp
)
target_link_libraries(MapperTestUtils PRIVATE
  Qt5::Widgets
  Qt5::Test
)


# Unit tests
add_unit_test(tst_qglobal)
add_unit_test(autosave_t MANUAL ../src/core/autosave
	../src/settings
)
add_unit_test(encoding_t ../src/util/encoding)
add_unit_test(georef_ocd_mapping_t
	../src/settings
	../src/gui/util_gui
	../src/gui/widgets/crs_param_widgets
	../src/core/georeferencing
	../src/core/crs_template
	../src/core/crs_template_implementation
	../src/fileformats/ocd_georef_fields
	../src/fileformats/file_format
	../src/util/key_value_container
	../src/util/xml_stream_util
	../src/core/map_coord
)
add_unit_test(georeferencing_t ../src/core/georeferencing
	../src/settings
	../src/core/crs_template
	../src/core/crs_template_implementation
	../src/core/latlon
	../src/gui/util_gui
	../src/gui/widgets/crs_param_widgets
	../src/mapper_resource
	../src/fileformats/file_format
	../src/util/key_value_container
	../src/util/xml_stream_util
	../src/core/map_coord
)
if (Mapper_USE_GDAL)
	# cf. src/gdal/CMakeLists.txt
	target_compile_definitions(georeferencing_t PRIVATE MAPPER_TEST_GDAL)
	target_include_directories(georeferencing_t SYSTEM PRIVATE ${GDAL_INCLUDE_DIRS})
	target_link_libraries(georeferencing_t PRIVATE ${GDAL_LIBRARIES})
endif()
add_unit_test(grid_t ../src/util/util)
add_unit_test(key_value_container_t ../src/util/key_value_container)
add_unit_test(locale_t ../src/util/translation_util)
add_unit_test(map_color_t ../src/core/map_color)
add_unit_test(ocd_t ../src/fileformats/ocd_types)
add_unit_test(ocd_parameter_stream_reader_t ../src/fileformats/ocd_parameter_stream_reader)
add_unit_test(qpainter_t)
add_unit_test(util_t ../src/util/util
	../src/settings
)

# Benchmarks
add_system_test(coord_xml_t MANUAL)

# System tests
add_system_test(file_format_t)
add_system_test(cli_t)  # Runs the foom-cli executable
target_link_libraries(cli_t PRIVATE MapperTestUtils)
target_compile_definitions(cli_t PRIVATE "FOOM_CLI_EXECUTABLE=\"$<TARGET_FILE:foom-cli>\"")
add_dependencies(cli_t foom-cli)
add_system_test(duplicate_equals_t)
add_system_test(map_t)
add_system_test(map_printer_t)
add_system_test(map_transform_t)  # foom: the design notes, Move, stretch and mirror
target_link_libraries(map_printer_t PRIVATE MapperTestUtils)
add_system_test(object_t)
add_system_test(object_query_t)
add_system_test(check_t)
add_system_test(colour_tools_t)  # foom: the design notes, Colours
add_system_test(ops_t)
add_system_test(object_commands_t)  # foom: the design notes, Object commands
add_system_test(map_export_t)  # foom: the design notes, Export
add_system_test(path_object_t)
add_system_test(spatial_index_t)
add_system_test(style_t)
target_link_libraries(style_t  PRIVATE scaling-icon-engine)
add_system_test(symbol_set_t)
add_system_test(symbol_t)
target_link_libraries(symbol_t PRIVATE MapperTestUtils)
add_system_test(path_text_t)
target_link_libraries(path_text_t PRIVATE MapperTestUtils)
add_system_test(line_text_t)  # foom: the design notes, Text on a path: editor, tool, lengthen
if(TARGET mapper-pdf)
	add_system_test(pdf_import_t)  # foom: the design notes, PDF import
	target_compile_definitions(pdf_import_t PRIVATE "FOOM_CLI_EXECUTABLE=\"$<TARGET_FILE:foom-cli>\"")
	add_dependencies(pdf_import_t foom-cli)
endif()
if (Mapper_USE_GDAL)
	add_system_test(tile_template_t)  # foom: tile service templates, needs the GDAL WMS driver
	target_include_directories(tile_template_t SYSTEM PRIVATE ${GDAL_INCLUDE_DIRS})
	target_link_libraries(tile_template_t PRIVATE ${GDAL_LIBRARIES} Qt5::Network)
	add_system_test(tile_map_widget_t)  # foom: the map of the surroundings, the design notes
	target_link_libraries(tile_map_widget_t PRIVATE Qt5::Network)
	add_system_test(online_map_t)  # foom: online map and geoviewer, the design notes
	target_include_directories(online_map_t SYSTEM PRIVATE ${GDAL_INCLUDE_DIRS})
	target_link_libraries(online_map_t PRIVATE ${GDAL_LIBRARIES} Qt5::Network)
	add_system_test(geodata_import_t)  # foom: import options, the design notes
	target_include_directories(geodata_import_t SYSTEM PRIVATE ${GDAL_INCLUDE_DIRS})
	target_link_libraries(geodata_import_t PRIVATE ${GDAL_LIBRARIES})
	target_compile_definitions(geodata_import_t PRIVATE "FOOM_CLI_EXECUTABLE=\"$<TARGET_FILE:foom-cli>\"")
	add_system_test(layout_part_t)  # foom: layout part commands, the design notes
	target_include_directories(layout_part_t SYSTEM PRIVATE ${GDAL_INCLUDE_DIRS})
	target_link_libraries(layout_part_t PRIVATE ${GDAL_LIBRARIES})
	add_system_test(new_map_t)  # foom: new-map flow, the design notes
	add_system_test(course_map_t)  # foom: the design notes, The maps of the courses as PDF files
	target_compile_definitions(course_map_t PRIVATE "FOOM_CLI_EXECUTABLE=\"$<TARGET_FILE:foom-cli>\"")
	add_dependencies(course_map_t foom-cli)
	add_system_test(extent_print_t)  # foom: the design notes, Extents in print and export
	target_compile_definitions(extent_print_t PRIVATE "FOOM_CLI_EXECUTABLE=\"$<TARGET_FILE:foom-cli>\"")
	add_system_test(print_output_t)  # foom: the design notes, Print and raster output
	target_include_directories(print_output_t SYSTEM PRIVATE ${GDAL_INCLUDE_DIRS})
	target_link_libraries(print_output_t PRIVATE ${GDAL_LIBRARIES})
	target_compile_definitions(print_output_t PRIVATE "FOOM_CLI_EXECUTABLE=\"$<TARGET_FILE:foom-cli>\"")
	add_dependencies(print_output_t foom-cli)
	add_system_test(raster_export_t)  # foom: the design notes, raster colours and tiles; spot colour view
	target_include_directories(raster_export_t SYSTEM PRIVATE ${GDAL_INCLUDE_DIRS})
	target_link_libraries(raster_export_t PRIVATE ${GDAL_LIBRARIES})
	target_compile_definitions(raster_export_t PRIVATE "FOOM_CLI_EXECUTABLE=\"$<TARGET_FILE:foom-cli>\"")
	add_dependencies(raster_export_t foom-cli)
	add_system_test(dxf_crt_t)  # foom: the design notes, DXF export through a cross-reference table
	target_include_directories(dxf_crt_t SYSTEM PRIVATE ${GDAL_INCLUDE_DIRS})
	target_link_libraries(dxf_crt_t PRIVATE ${GDAL_LIBRARIES})
	target_compile_definitions(dxf_crt_t PRIVATE "FOOM_CLI_EXECUTABLE=\"$<TARGET_FILE:foom-cli>\"")
	add_dependencies(dxf_crt_t foom-cli)
	add_system_test(image_object_t)  # foom: the design notes, Image object
	target_compile_definitions(image_object_t PRIVATE "FOOM_CLI_EXECUTABLE=\"$<TARGET_FILE:foom-cli>\"")
	target_link_libraries(new_map_t PRIVATE Qt5::Network mapper-elevation)
	target_compile_definitions(new_map_t PRIVATE "FOOM_CLI_EXECUTABLE=\"$<TARGET_FILE:foom-cli>\"")
endif()
if(TARGET mapper-elevation)
	add_system_test(elevation_t)  # foom: the design notes
	target_link_libraries(elevation_t PRIVATE mapper-elevation)
	# foom: larger fixtures are kept outside the repository; the tests which need them skip without them
	set(FOOM_FIXTURES_DIR "" CACHE PATH "A folder with further test fixtures")
	target_compile_definitions(elevation_t PRIVATE "FOOM_FIXTURES_DIR=\"${FOOM_FIXTURES_DIR}\"")
	add_system_test(point_cloud_t)  # foom: the design notes, Reading point clouds and gridding them
	target_link_libraries(point_cloud_t PRIVATE mapper-elevation)
	target_include_directories(point_cloud_t SYSTEM PRIVATE ${GDAL_INCLUDE_DIRS})
	target_compile_definitions(point_cloud_t PRIVATE "FOOM_FIXTURES_DIR=\"${FOOM_FIXTURES_DIR}\"")
	target_include_directories(elevation_t SYSTEM PRIVATE ${GDAL_INCLUDE_DIRS})
	add_system_test(runner_elevation_t)  # foom: the design notes, Bicubic resampling for hill shading: the runner step
	target_link_libraries(runner_elevation_t PRIVATE mapper-elevation)
	target_compile_definitions(runner_elevation_t PRIVATE "FOOM_CLI_EXECUTABLE=\"$<TARGET_FILE:foom-cli>\"")
	add_dependencies(runner_elevation_t foom-cli)
	add_system_test(elevation_wizard_t)  # foom: the design notes, Wizard
	target_link_libraries(elevation_wizard_t PRIVATE mapper-elevation)
	target_compile_definitions(elevation_wizard_t PRIVATE "FOOM_FIXTURES_DIR=\"${FOOM_FIXTURES_DIR}\"" "FOOM_CLI_EXECUTABLE=\"$<TARGET_FILE:foom-cli>\"")
	add_dependencies(elevation_wizard_t foom-cli)
endif()
add_system_test(map_part_role_t)  # foom: the design notes, Map part roles
add_system_test(private_symbol_t)  # foom: the design notes, Private symbols
add_system_test(generator_t)  # foom: the design notes, Generators
add_system_test(course_model_t)  # foom: the design notes, Data model and OCD records
add_system_test(course_storage_t)  # foom: the design notes, Storage
add_system_test(course_ocd_t)  # foom: the design notes, OCD files from the course data
add_system_test(course_symbol_t)  # foom: the design notes, Course role of symbols
add_system_test(course_dock_t)  # foom: the design notes, Course dock
add_system_test(course_render_t)  # foom: the design notes, Drawing a course, its length and climb
add_system_test(course_description_t)  # foom: the design notes, The description of a course
add_system_test(course_cells_t)  # foom: the design notes, The editor of a description
add_system_test(course_symbols_t)  # foom: the design notes, FOOM's course symbols
add_system_test(course_compose_t)  # foom: the design notes, Composing a course and the bulk commands
add_system_test(course_report_t)  # foom: the design notes, Statistics and the consistency check
add_system_test(course_punch_t)  # foom: the design notes, Punching unit ids
add_system_test(course_interchange_t)  # foom: the design notes, Interchange: IOF XML, GPX and KML
add_system_test(background_extents_t)  # foom: the design notes, Canvases: load extent from background map
add_system_test(course_relay_t)  # foom: the design notes, Relays: variations, variants and their allocation
add_system_test(course_parts_t)  # foom: the design notes, The parts of a course: map exchange and flip
add_system_test(course_canvas_t)  # foom: the design notes, Canvases: the model, the files and the export
add_system_test(route_terrain_t)  # foom: the design notes, Route analysis: runnability and the terrain rasters
add_system_test(route_search_t)  # foom: the design notes, Route analysis: the search
add_system_test(route_course_t)  # foom: the design notes, Route analysis: the routes of a course
add_system_test(route_settings_t)  # foom: the design notes, Route analysis: the settings and the elevation
add_system_test(route_store_t)  # foom: the design notes, Route analysis: all courses, and the routes in the file
target_compile_definitions(route_store_t PRIVATE "FOOM_CLI_EXECUTABLE=\"$<TARGET_FILE:foom-cli>\"")
add_dependencies(route_store_t foom-cli)
add_system_test(route_ocd_t)  # foom: the design notes, Route analysis: the routes in OCD files
add_system_test(route_via_t)  # foom: the design notes, Route analysis: via points and the leg by hand
add_system_test(route_map_t)  # foom: the design notes, Route analysis: the routes on the maps of the courses
target_compile_definitions(route_map_t PRIVATE "FOOM_CLI_EXECUTABLE=\"$<TARGET_FILE:foom-cli>\"")
add_dependencies(route_map_t foom-cli)
add_system_test(route_alternatives_t)  # foom: the design notes, Route analysis: alternative routes
target_compile_definitions(route_alternatives_t PRIVATE "FOOM_CLI_EXECUTABLE=\"$<TARGET_FILE:foom-cli>\"")
add_dependencies(route_alternatives_t foom-cli)
add_system_test(route_perimeter_t)  # foom: the design notes, Route analysis: the perimeter
add_system_test(route_cache_t)  # foom: the design notes, Route analysis: the cache of the rasters
target_compile_definitions(route_cache_t PRIVATE "FOOM_CLI_EXECUTABLE=\"$<TARGET_FILE:foom-cli>\"")
add_dependencies(route_cache_t foom-cli)
add_system_test(route_print_t)  # foom: the design notes, Route analysis: a print of the routes
target_compile_definitions(route_print_t PRIVATE "FOOM_CLI_EXECUTABLE=\"$<TARGET_FILE:foom-cli>\"")
add_dependencies(route_print_t foom-cli)
add_system_test(runner_template_t)  # foom: the design notes, Headless runner: the files of the templates
target_compile_definitions(runner_template_t PRIVATE "FOOM_CLI_EXECUTABLE=\"$<TARGET_FILE:foom-cli>\"")
add_dependencies(runner_template_t foom-cli)
add_system_test(runner_background_t)  # foom: the design notes, Background maps and point clouds in the runner; XML scripts
target_compile_definitions(runner_background_t PRIVATE "FOOM_CLI_EXECUTABLE=\"$<TARGET_FILE:foom-cli>\"")
add_dependencies(runner_background_t foom-cli)
target_sources(runner_background_t PRIVATE "${PROJECT_SOURCE_DIR}/src/cli/xml_script.cpp")  # foom: the reader of XML scripts, tested step by step
if(TARGET mapper-elevation)
	target_link_libraries(runner_background_t PRIVATE mapper-elevation)
	target_compile_definitions(runner_background_t PRIVATE FOOM_TEST_ELEVATION)
endif()
add_system_test(runner_check_t)  # foom: the design notes, Headless runner: check and course-statistics
target_compile_definitions(runner_check_t PRIVATE "FOOM_CLI_EXECUTABLE=\"$<TARGET_FILE:foom-cli>\"")
add_dependencies(runner_check_t foom-cli)
add_system_test(prj_file_t)  # foom: the design notes, Projection file
add_system_test(template_separation_t)  # foom: the design notes, Templates on a spot colour plate
target_compile_definitions(template_separation_t PRIVATE "FOOM_CLI_EXECUTABLE=\"$<TARGET_FILE:foom-cli>\"")
add_dependencies(template_separation_t foom-cli)
add_system_test(template_embed_t)  # foom: the design notes, Embedded raster templates
add_system_test(template_favourite_t)  # foom: the design notes, Favourite templates
add_system_test(template_relink_t)  # foom: the design notes, Relink by folder
add_system_test(template_effects_t)  # foom: the design notes, Block out, dim and colour infrared
if (Mapper_USE_GDAL)
	target_compile_definitions(template_effects_t PRIVATE MAPPER_TEST_GDAL)
	target_include_directories(template_effects_t SYSTEM PRIVATE ${GDAL_INCLUDE_DIRS})
	target_link_libraries(template_effects_t PRIVATE ${GDAL_LIBRARIES})
endif()
add_system_test(runner_courses_t)  # foom: the design notes, Headless runner: export-courses and import-courses
target_compile_definitions(runner_courses_t PRIVATE "FOOM_CLI_EXECUTABLE=\"$<TARGET_FILE:foom-cli>\"")
add_dependencies(runner_courses_t foom-cli)
add_system_test(runner_generate_t)  # foom: the design notes, Generators, third change: generate and regenerate
target_compile_definitions(runner_generate_t PRIVATE "FOOM_CLI_EXECUTABLE=\"$<TARGET_FILE:foom-cli>\"")
add_dependencies(runner_generate_t foom-cli)
add_system_test(sketch_t)  # foom: the design notes, Sketch layer
add_system_test(merge_t)  # foom: the design notes, Merge changes from file
target_compile_definitions(merge_t PRIVATE "FOOM_CLI_EXECUTABLE=\"$<TARGET_FILE:foom-cli>\"")
add_dependencies(merge_t foom-cli)
add_system_test(generator_more_t)  # foom: the design notes, Generators, second change
add_system_test(qr_code_t)  # foom: the design notes, Generators, gen-qr-code
add_system_test(import_points_t)  # foom: the design notes, Delimited text
add_system_test(map_import_policy_t)  # foom: the design notes, Map import policies
add_system_test(text_symbol_t)
add_system_test(line_taper_t)
add_system_test(editing_view_t)  # foom: corner and virtual gap vertices, selection rule, glyphs, curve entry, draft view, highlights
add_system_test(area_pattern_t)
add_system_test(foreign_data_t)
add_system_test(ocd_layout_image_t)  # foom: the design notes, OCD embedded images
add_system_test(ocd_graphic_object_t)  # foom: the design notes, Graphic and layout objects in OCD files
add_system_test(ocd_sketch_t)  # foom: the design notes, Sketch strokes in OCD files
add_system_test(ocd_guide_t)  # foom: the design notes, OCD string type 26
add_system_test(ocd2018_t)
add_system_test(template_t)
add_system_test(tools_t)
if (Mapper_USE_GDAL)  # foom: the design notes, Block out, dim and colour infrared
	target_compile_definitions(tools_t PRIVATE MAPPER_TEST_GDAL)
	target_include_directories(tools_t SYSTEM PRIVATE ${GDAL_INCLUDE_DIRS})
	target_link_libraries(tools_t PRIVATE ${GDAL_LIBRARIES})
endif()
add_system_test(view_aids_t)  # foom: the design notes, view aids
add_system_test(settings_backup_t)  # foom: the design notes, backup on open and settings transfer
add_system_test(track_t)  # Could be unit test, but needs Georeferencing
add_system_test(transform_t)
add_system_test(undo_manager_t)

if(TARGET mapper-sensors)
	add_system_test(gnss_gate_t)  # foom: the design notes, GNSS quality gate
	target_link_libraries(gnss_gate_t  PRIVATE mapper-sensors fake_position_source)
	if(TARGET Qt5::Positioning)
		target_link_libraries(gnss_gate_t  PRIVATE Qt5::Positioning)
	endif()
	add_system_test(nmea_source_t)  # foom: the design notes, NMEA source over TCP
	target_link_libraries(nmea_source_t  PRIVATE mapper-sensors)
	foreach(lib Qt5::Positioning Qt5::Network)
		if(TARGET ${lib})
			target_link_libraries(nmea_source_t  PRIVATE ${lib})
		endif()
	endforeach()
	add_system_test(gps_record_t)  # foom: the design notes, Recording into a symbol
	target_link_libraries(gps_record_t  PRIVATE mapper-sensors)
	foreach(lib Qt5::Positioning Qt5::Network)
		if(TARGET ${lib})
			target_link_libraries(gps_record_t  PRIVATE ${lib})
		endif()
	endforeach()
	add_system_test(gnss_extras_t)  # foom: the design notes, Antenna height, averaged vertex and tail
	target_link_libraries(gnss_extras_t  PRIVATE mapper-sensors)
	foreach(lib Qt5::Positioning Qt5::Network)
		if(TARGET ${lib})
			target_link_libraries(gnss_extras_t  PRIVATE ${lib})
		endif()
	endforeach()
	add_system_test(gnss_file_t)  # foom: the design notes, NMEA and FRWD files
	target_link_libraries(gnss_file_t  PRIVATE mapper-sensors)
	add_system_test(gnss_offset_log_t)  # foom: the design notes, Position offset and sentence log
	target_link_libraries(gnss_offset_log_t  PRIVATE mapper-sensors)
	foreach(lib Qt5::Positioning Qt5::Network)
		if(TARGET ${lib})
			target_link_libraries(gnss_offset_log_t  PRIVATE ${lib})
		endif()
	endforeach()
	add_system_test(rangefinder_t)  # foom: the design notes, Rangefinder dock
	target_link_libraries(rangefinder_t  PRIVATE mapper-sensors)
	if(TARGET Qt5::Network)
		target_link_libraries(rangefinder_t  PRIVATE Qt5::Network)
	endif()
	add_system_test(sensors_t)
	target_link_libraries(sensors_t  PRIVATE mapper-sensors)
	foreach(lib Qt5::Positioning Qt5::Sensors)
		if(TARGET ${lib})
			target_link_libraries(sensors_t  PRIVATE ${lib})
		endif()
	endforeach()
endif()

# Collect the AUTORUN_TESTS
get_property(Mapper_AUTORUN_TESTS DIRECTORY PROPERTY Mapper_AUTORUN_TESTS)
if(Mapper_AUTORUN_TESTS)
	configure_file(AUTORUN_TESTS.cmake.in ${CMAKE_CURRENT_BINARY_DIR}/AUTORUN_TESTS.cmake @ONLY)
	
	# Unlike the default "test" target, this custom target is displayed in Qt
	# Creator's Projects view, and when build, it prints the output of failed
	# tests. It maybe used as an additional build step, or it can be build from
	# the Projects view. It also provides access to the the script template.
	add_custom_target(AUTORUN_TESTS
	  COMMAND "${CMAKE_COMMAND}" -P AUTORUN_TESTS.cmake
	  SOURCES AUTORUN_TESTS.cmake.in
	  COMMENT "Running tests for modified sources"
	)
	add_dependencies(AUTORUN_TESTS ${Mapper_AUTORUN_TESTS})
endif()


# foom: purity of src/core/ops/, see the design notes, "Geometry operations".
add_test(NAME ops_purity
  COMMAND "${CMAKE_COMMAND}" "-DOPS_DIR=${PROJECT_SOURCE_DIR}/src/core/ops" -P "${CMAKE_CURRENT_SOURCE_DIR}/ops_purity.cmake"
)
add_test(NAME ops_purity_negative
  COMMAND "${CMAKE_COMMAND}" "-DOPS_DIR=${CMAKE_CURRENT_SOURCE_DIR}/data/ops_purity/bad" -P "${CMAKE_CURRENT_SOURCE_DIR}/ops_purity.cmake"
)
set_tests_properties(ops_purity_negative PROPERTIES PASS_REGULAR_EXPRESSION "Editor code included")


set(IOF_3_XSD_FILE "" CACHE FILEPATH "Path of the IOF Data Standard 3 XSD file")
if(IOF_3_XSD_FILE)
	find_program(XMLLINT NAMES xmllint)
	add_test(NAME validate_iof_course
		COMMAND "${XMLLINT}" --schema "${IOF_3_XSD_FILE}" "${CMAKE_CURRENT_SOURCE_DIR}/data/export/iof-3.0-course.xml"
	)
endif()
