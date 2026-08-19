# The notification server speaks the freedesktop protocol, so it needs QtDBus.
# dbus is not systemd — it is present on the de-systemd'd base (Bible §6.4).
find_package(Qt5 5.15 REQUIRED COMPONENTS DBus)
add_executable(castalia-notificaciones src/main.cpp)
target_link_libraries(castalia-notificaciones PRIVATE castalia-ui Qt5::DBus)
# The version the server reports over GetServerInformation comes from the
# canonical VERSION file, so it can't drift.
target_compile_definitions(castalia-notificaciones PRIVATE
    CASTALIA_VERSION="${PROJECT_VERSION}")
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_link_options(castalia-notificaciones PRIVATE -s)
endif()
