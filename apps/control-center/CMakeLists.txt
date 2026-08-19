find_package(Qt5 5.15 REQUIRED COMPONENTS Svg)
add_executable(castalia-control-center
    src/main.cpp
    src/ControlCenter.cpp
)
target_link_libraries(castalia-control-center PRIVATE castalia-ui Qt5::Svg)
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_link_options(castalia-control-center PRIVATE -s)
endif()
