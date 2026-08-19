# castalia-panel — Phase 0 proof of concept (Bible §7.2, §12).
#
# The taskbar reads real windows over EWMH via libxcb; xcb is a small, stable
# C library already present on any X system (Xorg/Qt depend on it), linked
# directly so the panel stays self-contained.
#
# The system tray is StatusNotifierItem, which is D-Bus — hence Qt5::DBus here
# (Bible §7.4). The panel is one of the three processes that genuinely needs
# it; the apps do not.
find_package(Qt5 5.15 REQUIRED COMPONENTS DBus)
add_executable(castalia-panel
    src/main.cpp
    src/CastaliaPanel.cpp
    src/CastaliaMenu.cpp
    src/WindowList.cpp
    src/AppRoster.cpp
    src/Ewmh.cpp
    src/Switcher.cpp
    src/TrayHost.cpp
    src/XEmbedTray.cpp
)
target_link_libraries(castalia-panel PRIVATE castalia-ui xcb Qt5::DBus)
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_link_options(castalia-panel PRIVATE -s)
endif()
