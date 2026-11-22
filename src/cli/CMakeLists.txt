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


# foom-cli: the headless runner, see "Headless runner" in the design notes.
# A console program (no WIN32 flag), so that its output reaches the terminal on Windows.

set(foom_cli_SRCS
  background_steps.cpp  # foom
  generator_steps.cpp
  job.cpp
  main.cpp
  xml_script.cpp  # foom
  steps.cpp
)

add_executable(foom-cli ${foom_cli_SRCS})
target_link_libraries(foom-cli
  Mapper_Common
)
target_compile_definitions(foom-cli PRIVATE
  QT_NO_CAST_FROM_ASCII
  QT_NO_CAST_TO_ASCII
  QT_USE_QSTRINGBUILDER
)

install(TARGETS foom-cli
  RUNTIME DESTINATION "${MAPPER_RUNTIME_DESTINATION}"
)
