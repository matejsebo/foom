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

# PDF import on Poppler's core library, see the design notes, "PDF import".
# Only added when Poppler is found (top-level CMakeLists.txt).

find_package(Qt5Core REQUIRED)
find_package(Qt5Gui REQUIRED)
find_package(Qt5Widgets REQUIRED)
set(CMAKE_AUTOMOC ON)

set(MAPPER_PDF_SOURCES
  pdf_import.cpp
  pdf_import_dialog.cpp
)

mapper_translations_sources(${MAPPER_PDF_SOURCES})

add_library(mapper-pdf STATIC ${MAPPER_PDF_SOURCES})

target_compile_definitions(mapper-pdf PRIVATE
  QT_NO_CAST_FROM_ASCII
  QT_NO_CAST_TO_ASCII
  QT_USE_QSTRINGBUILDER
)

target_compile_definitions(mapper-pdf INTERFACE FOOM_WITH_POPPLER)

target_include_directories(mapper-pdf PRIVATE "${PROJECT_SOURCE_DIR}/src")

target_link_libraries(mapper-pdf PkgConfig::POPPLER Qt5::Core Qt5::Gui Qt5::Widgets Mapper_Common)

# Poppler's headers need C++20; the rest of the program keeps its standard.
set_target_properties(mapper-pdf PROPERTIES
  PREFIX ""
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED ON
)
