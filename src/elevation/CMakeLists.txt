#
#    Copyright 2026 Matej Šebo
#
#    This file is part of FOOM.
#
#    FOOM is free software: you can redistribute it and/or modify
#    it under the terms of the GNU General Public License as published by
#    the Free Software Foundation, either version 3 of the License, or
#    (at your option) any later version.
#
#    FOOM is distributed in the hope that it will be useful,
#    but WITHOUT ANY WARRANTY; without even the implied warranty of
#    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#    GNU General Public License for more details.
#
#    You should have received a copy of the GNU General Public License
#    along with FOOM.  If not, see <http://www.gnu.org/licenses/>.


# The elevation module, see the design notes.
# Grid processing on GDAL; point clouds on LASzip when it is found.

find_package(GDAL MODULE REQUIRED)
find_package(Qt5Core REQUIRED)
find_package(Qt5Gui REQUIRED)
set(CMAKE_AUTOMOC ON)

set(MAPPER_ELEVATION_SOURCES
  cliffs.cpp
  contours.cpp
  elevation_commands.cpp
  elevation_georef.cpp
  elevation_model.cpp
  elevation_set.cpp
  elevation_wizard.cpp
  embedded_grid.cpp
  grid_frame.cpp
  grid_tiles.cpp
  landforms.cpp
  ocd_course_dem.cpp
  point_cloud.cpp
  point_cloud_products.cpp
  profile.cpp
  raster_products.cpp
  trees.cpp
)

mapper_translations_sources(${MAPPER_ELEVATION_SOURCES})

add_library(mapper-elevation STATIC ${MAPPER_ELEVATION_SOURCES})

target_compile_definitions(mapper-elevation PRIVATE
  QT_NO_CAST_FROM_ASCII
  QT_NO_CAST_TO_ASCII
  QT_USE_QSTRINGBUILDER
)

target_include_directories(mapper-elevation SYSTEM PRIVATE ${GDAL_INCLUDE_DIRS})
target_include_directories(mapper-elevation PRIVATE "${PROJECT_SOURCE_DIR}/src")

target_link_libraries(mapper-elevation ${GDAL_LIBRARIES} Qt5::Core Qt5::Gui mapper-gdal Mapper_Common)

# The static libraries depend on each other in a cycle; a third pass lets a user of the
# raster products (whose templates need the template classes of Mapper_Common) link.
set_target_properties(mapper-elevation PROPERTIES PREFIX "" LINK_INTERFACE_MULTIPLICITY 3)

# LASzip's own library: its laszip_api3 is a loader which looks for LASzip64.dll.
find_path(LASZIP_INCLUDE_DIR laszip/laszip_api.h)
find_library(LASZIP_LIBRARY NAMES laszip3 laszip)
if(LASZIP_INCLUDE_DIR AND LASZIP_LIBRARY)
  message(STATUS "Point clouds: ${LASZIP_LIBRARY}")
  target_compile_definitions(mapper-elevation PRIVATE FOOM_WITH_LASZIP LASZIP_DYN_LINK)
  target_include_directories(mapper-elevation SYSTEM PRIVATE "${LASZIP_INCLUDE_DIR}")
  target_link_libraries(mapper-elevation "${LASZIP_LIBRARY}")
else()
  message(STATUS "Point clouds: LASzip not found, not supported")
endif()
