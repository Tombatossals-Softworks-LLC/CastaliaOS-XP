# No Qt5::Svg here any more: the mark comes from libcastalia-ui, which paints
# the artwork PNG (or its vector re-drawing) without an SVG renderer.
add_executable(castalia-acerca src/main.cpp)
target_link_libraries(castalia-acerca PRIVATE castalia-ui)
# Compile in the canonical project version so the About box can't drift.
target_compile_definitions(castalia-acerca PRIVATE
    CASTALIA_VERSION="${PROJECT_VERSION}")
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_link_options(castalia-acerca PRIVATE -s)
endif()
