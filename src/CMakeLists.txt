# The effect's identity is its file name: KF6 derives the plugin id from the
# basename of the .so and ignores KPlugin.Id when the two disagree. So "snow.so"
# is what makes the effect known to KWin as "snow" -- the name that appears in
# kwinrc as [Plugins] snowEnabled and, later, in [Effect-snow]. This matches how
# KWin's own effects are named (blur.so, cube.so, glide.so).
add_library(snow MODULE)

target_sources(snow PRIVATE
    cap.cpp
    cappainter.cpp
    catcher.cpp
    catcherregistry.cpp
    flakepainter.cpp
    flakesprite.cpp
    frameclock.cpp
    framepacer.cpp
    main.cpp
    settings.cpp
    snoweffect.cpp
    snowfall.cpp
    snowfallregistry.cpp
    snowline.cpp
    snowlogging.cpp
    suspension.cpp
)

set_target_properties(snow PROPERTIES PREFIX "")

# The knobs, generated into SnowConfig from the schema that also spells out the
# defaults and the ranges. The generated files land in the build directory, so
# the effect's own sources include them by plain name.
kconfig_add_kcfg_files(snow snowconfig.kcfgc)

# ...which is where settings.cpp includes snowconfig.h from.
target_include_directories(snow PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")

# moc embeds metadata.json into the plugin, but it is referenced from inside the
# KWIN_EFFECT_FACTORY macro where AutoMoc's scanner cannot see it -- so say so,
# or editing the metadata silently does not rebuild.
set_property(TARGET snow APPEND PROPERTY
    AUTOGEN_TARGET_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/metadata.json")

target_link_libraries(snow PRIVATE
    KWin::kwin
    Qt6::Core
    Qt6::Gui
    KF6::ConfigCore
    # The generated SnowConfig inherits KConfigSkeleton, which lives here.
    KF6::ConfigGui
    KF6::CoreAddons
)

install(TARGETS snow DESTINATION "${SNOW_PLUGIN_INSTALL_DIR}")

# The gear beside Snow in System Settings, which reads the same schema as
# the effect but is loaded by a different process and links a widget toolkit
# the effect has no business having.
add_subdirectory(kcm)
