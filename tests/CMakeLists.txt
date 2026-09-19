# Catcher and Snowline are plain C++ with no compositor in them, so the
# geometry rules of ADR-0001 can be checked without one. KWin::kwin is here
# only to resolve the EffectWindow and LogicalOutput calls in catcher.cpp; no
# test touches either, and both are passed as nullptr.
add_executable(snowlinetest
    snowlinetest.cpp
    ../src/cap.cpp
    ../src/catcher.cpp
    ../src/snowline.cpp
)

target_include_directories(snowlinetest PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../src")

target_link_libraries(snowlinetest PRIVATE
    KWin::kwin
    Qt6::Core
    Qt6::Gui
    Qt6::Test
)

add_test(NAME snowline COMMAND snowlinetest)

# Snowfall is a rectangle, a list of Catchers and some arithmetic, with no
# compositor in any of it -- which is what lets the Flake motion and the landing
# be checked here rather than by watching a screen. Like the test above it links
# KWin::kwin only to resolve the EffectWindow and LogicalOutput calls in
# catcher.cpp, and passes nullptr for both.
add_executable(snowfalltest
    snowfalltest.cpp
    ../src/cap.cpp
    ../src/catcher.cpp
    ../src/snowfall.cpp
    ../src/snowline.cpp
)

target_include_directories(snowfalltest PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../src")

target_link_libraries(snowfalltest PRIVATE
    KWin::kwin
    Qt6::Core
    Qt6::Gui
    Qt6::Test
)

add_test(NAME snowfall COMMAND snowfalltest)

# The sprites and their placement are Qt and arithmetic -- no compositor, and
# unlike the two tests above not even a KWin symbol to resolve. What a Flake
# looks like once it is a texture is the one thing here that still needs the
# nested session and the published prototype; see docs/development.md.
add_executable(flakespritetest
    flakespritetest.cpp
    ../src/flakesprite.cpp
)

target_include_directories(flakespritetest PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../src")

target_link_libraries(flakespritetest PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Test
)

add_test(NAME flakesprite COMMAND flakespritetest)

# A Cap's contour and its shading are arithmetic and a QImage -- no compositor,
# and like the sprite test above not even a KWin symbol to resolve. What turns
# any of it into pixels is CapPainter, which is all KWin and none of this.
add_executable(captest
    captest.cpp
    ../src/cap.cpp
    ../src/snowline.cpp
)

target_include_directories(captest PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../src")

target_link_libraries(captest PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Test
)

add_test(NAME cap COMMAND captest)

# When the next frame is asked for is arithmetic over a clock, with neither a
# compositor nor a Qt event loop in it -- so the pacing the frame rate cap rests
# on is checkable here, where the frame rate itself is not checkable anywhere:
# a nested compositor does not reach the cap to begin with.
add_executable(framepacertest
    framepacertest.cpp
    ../src/framepacer.cpp
)

target_include_directories(framepacertest PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../src")

target_link_libraries(framepacertest PRIVATE
    Qt6::Core
    Qt6::Test
)

add_test(NAME framepacer COMMAND framepacertest)

# The schema and the struct have to carry the same defaults, and nothing in the
# toolchain ties them together -- so the schema is read here as a file rather
# than through KConfigXT, which is also why this test needs neither KConfig nor
# a compositor. It is handed the path because the file it checks is an input to
# the build rather than something that is installed anywhere.
add_executable(settingstest settingstest.cpp)

target_include_directories(settingstest PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../src")

target_compile_definitions(settingstest PRIVATE
    SNOW_KCFG_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../src/snowconfig.kcfg")

target_link_libraries(settingstest PRIVATE
    Qt6::Core
    Qt6::Test
)

add_test(NAME settings COMMAND settingstest)

# The config module binds widgets to keys by object name and stores its combo
# boxes by row, and nothing checks either spelling -- so the plugin as built is
# loaded here and held against the schema read as a file, the same way the test
# above reads it. This is the one test that needs a widget toolkit, and it runs
# offscreen; it never saves, and QStandardPaths test mode keeps it away from the
# session's kwinrc either way.
add_executable(kcmtest kcmtest.cpp)

target_compile_definitions(kcmtest PRIVATE
    SNOW_KCFG_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../src/snowconfig.kcfg"
    SNOW_KCM_PATH="$<TARGET_FILE:kwin_snow_config>")

target_link_libraries(kcmtest PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Test
    Qt6::Widgets
    KF6::CoreAddons
    KF6::KCMUtils
)

add_test(NAME kcm COMMAND kcmtest)
set_tests_properties(kcm PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
