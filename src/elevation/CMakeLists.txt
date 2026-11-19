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
# Grid processing on GDAL; the point-cloud layer is not built yet.

find_package(GDAL MODULE REQUIRED)
find_package(Qt5Core REQUIRED)
find_package(Qt5Gui REQUIRED)
set(CMAKE_AUTOMOC ON)

set(MAPPER_ELEVATION_SOURCES
  contours.cpp
  elevation_model.cpp
  elevation_set.cpp
  raster_products.cpp
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

set_target_properties(mapper-elevation PROPERTIES PREFIX "")
