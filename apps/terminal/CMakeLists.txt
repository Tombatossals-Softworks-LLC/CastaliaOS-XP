# castalia-terminal — original PTY/VT100 terminal (Bible §9.3).
# forkpty lives in libutil.
add_executable(castalia-terminal src/main.cpp)
target_link_libraries(castalia-terminal PRIVATE castalia-ui util)
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_link_options(castalia-terminal PRIVATE -s)
endif()
