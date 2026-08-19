# Castalia shell — panel, explorer, and the shared libcastalia-ui.
cmake_minimum_required(VERSION 3.16)

# Single source of truth for the product version: the repo-root VERSION file.
# Read it here so PROJECT_VERSION — and the About box that compiles it in
# (apps/acerca) — can never drift from the released/packaged version.
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/../VERSION" _castalia_version LIMIT_COUNT 1)
string(STRIP "${_castalia_version}" _castalia_version)
string(REGEX MATCH "^[0-9]+\\.[0-9]+(\\.[0-9]+)?" _castalia_semver "${_castalia_version}")
if(NOT _castalia_semver)
    message(FATAL_ERROR
        "no numeric version in ${CMAKE_CURRENT_SOURCE_DIR}/../VERSION")
endif()
project(castalia-shell VERSION ${_castalia_semver} LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt5 5.15 REQUIRED COMPONENTS Widgets)

add_subdirectory(libcastalia-ui)
add_subdirectory(panel)
add_subdirectory(explorer)
add_subdirectory(desktop)

# First-party apps live at the repo-level /apps (Bible §17.1); build them
# through the same tree so they share libcastalia-ui.
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../apps ${CMAKE_BINARY_DIR}/apps)

# The graphical installer front-end (Bible §14) — a Qt wizard that drives the
# shared Python backend in installer/castalia_installer.
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../installer/gui
                 ${CMAKE_BINARY_DIR}/installer-gui)

# The Recovery Center (Bible §9/§1063) — a Qt front-end over the shared Python
# Restore Points backend in recovery/castalia_recovery.
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../recovery/gui
                 ${CMAKE_BINARY_DIR}/recovery-gui)
