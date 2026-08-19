# castalia-explorer — Phase 0 proof of concept (Bible §9.1, §12).
add_executable(castalia-explorer
    src/main.cpp
    src/ExplorerWindow.cpp
)
target_link_libraries(castalia-explorer PRIVATE castalia-ui)
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_link_options(castalia-explorer PRIVATE -s)
endif()
