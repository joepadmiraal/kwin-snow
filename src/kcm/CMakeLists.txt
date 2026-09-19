# The configuration module keeps the name `kwin_snow_config` although the effect
# it configures is named `snow`: an effect finds its module through the
# X-KDE-ConfigModule key in its own metadata rather than by any name convention,
# so the two are free to differ, and every other effect's module is named this
# way. See docs/adr/0004-effect-identity-is-the-plugin-filename.md.
add_library(kwin_snow_config MODULE)

target_sources(kwin_snow_config PRIVATE
    configmodule.cpp
)

set_target_properties(kwin_snow_config PROPERTIES PREFIX "")

# The effect's schema, generated a second time into this plugin's own binary
# directory. The two .so files are loaded by two different processes -- KWin and
# System Settings -- so what they share is the schema file, not an object.
kconfig_add_kcfg_files(kwin_snow_config ../snowconfig.kcfgc)

# ...which is where configmodule.cpp includes snowconfig.h from.
target_include_directories(kwin_snow_config PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")

# i18n() resolves against this catalogue instead of the host application's.
# Nothing translates the effect yet, so every string comes back as it is
# written -- but naming a domain here is what keeps these strings out of System
# Settings' own catalogue, which is the process this plugin is loaded into.
target_compile_definitions(kwin_snow_config PRIVATE TRANSLATION_DOMAIN="kwin_effect_snow")

# K_PLUGIN_CLASS_WITH_JSON hides the reference to metadata.json from AutoMoc's
# scanner in the same way KWIN_EFFECT_FACTORY does in the effect, so say so, or
# editing the metadata silently does not rebuild.
set_property(TARGET kwin_snow_config APPEND PROPERTY
    AUTOGEN_TARGET_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/metadata.json")

target_link_libraries(kwin_snow_config PRIVATE
    Qt6::Core
    # reconfigureEffect on the running compositor, from save().
    Qt6::DBus
    Qt6::Gui
    Qt6::Widgets
    KF6::ConfigCore
    # The generated SnowConfig inherits KConfigSkeleton, which lives here.
    KF6::ConfigGui
    # KConfigDialogManager, which is what KCModule::addConfig() sets up.
    KF6::ConfigWidgets
    KF6::CoreAddons
    KF6::I18n
    KF6::KCMUtils
)

install(TARGETS kwin_snow_config DESTINATION "${SNOW_CONFIG_INSTALL_DIR}")
