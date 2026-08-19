# castalia-desktop — Phase 0 proof of concept (Bible §7.5, §12).
# Qt5Svg renders the wallpaper source in the PoC; production pre-bakes
# rasters (§8.4) and this dependency drops away.
find_package(Qt5 5.15 REQUIRED COMPONENTS Svg)
add_executable(castalia-desktop
    src/main.cpp
    src/DesktopWindow.cpp
)
target_link_libraries(castalia-desktop PRIVATE castalia-ui Qt5::Svg)
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_link_options(castalia-desktop PRIVATE -s)
endif()
