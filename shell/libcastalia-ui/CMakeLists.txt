# libcastalia-ui — the shared foundation every first-party app links
# (Bible §12.3): theme-token access and native brand artwork today;
# the common widget/style/help contract grows here.
add_library(castalia-ui STATIC
    ThemeTokens.cpp
    Mark.cpp
    Theme.cpp
    Sound.cpp
    Aurora.cpp
    Recent.cpp
    Blocks.cpp
    Locale.cpp
)
target_include_directories(castalia-ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(castalia-ui PUBLIC Qt5::Widgets)

# Sending a notification needs QtDBus, and QtDBus in all 43 apps to serve the
# handful that announce anything is a cost the FLOOR tier should not pay. It
# is its own tiny library: link it only where you notify (Bible §7.4, §16).
find_package(Qt5 5.15 REQUIRED COMPONENTS DBus)
add_library(castalia-notify STATIC Notify.cpp)
target_include_directories(castalia-notify PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(castalia-notify PUBLIC Qt5::Core Qt5::DBus)

# Head-less self-test for the shared foundation (Bible §17.4). Run in CI
# alongside the offscreen render smoke.
add_executable(castalia-ui-selftest tests/selftest.cpp)
target_link_libraries(castalia-ui-selftest PRIVATE castalia-ui)
