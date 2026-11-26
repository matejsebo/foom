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


# A package for Windows which is built in an MSYS2 environment brings the
# libraries of MSYS2's packages along. Which ones, is known only when the
# package is put together: the libraries which the program names bring
# their own. So the list is written then. msys2-bundled.cmake looks up the
# MSYS2 package of every file which was bundled, copies the licence files of
# that package into the documentation and writes bundled-libraries.html,
# to which the licensing page links.

find_program(MSYS2_PACMAN pacman)
if(NOT MSYS2_PACMAN)
	message(FATAL_ERROR "LICENSING_PROVIDER=msys2 needs pacman, which was not found")
endif()
get_filename_component(msys2_prefix "${CMAKE_CXX_COMPILER}" DIRECTORY)
get_filename_component(msys2_prefix "${msys2_prefix}" DIRECTORY)
get_filename_component(msys2_root "${msys2_prefix}" DIRECTORY)
if(Mapper_PACKAGE_ASSISTANT)
	get_filename_component(msys2_assistant_name "${Qt5Help_ASSISTANT_EXECUTABLE}" NAME)
endif()

# What is compiled into the program keeps the notice of its sources.
# The other components come with the bundle and are listed with it.
set(msys2_components ${third_party_components})
foreach(component ${msys2_components})
	if(NOT DEFINED explicit_copyright_${component})
		list(REMOVE_ITEM third_party_components ${component})
	endif()
endforeach()

set(third_party_copyright
  "  <li><a href=\"bundled-libraries.html\">The libraries which this package brings along</a>, each with its version and its licence</li>"
)

configure_file("msys2-bundled.cmake.in" "msys2-bundled.cmake" @ONLY)
install(SCRIPT "${CMAKE_CURRENT_BINARY_DIR}/msys2-bundled.cmake")
