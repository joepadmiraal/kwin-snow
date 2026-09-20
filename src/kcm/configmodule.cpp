/*
    SPDX-FileCopyrightText: 2026 Joep Admiraal <joep@groovytunes.nl>
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "configmodule.h"

#include "snowconfig.h"

#include <KLocalizedString>
#include <KPluginFactory>

#include <QCheckBox>
#include <QComboBox>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDoubleSpinBox>
#include <QFontDatabase>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QSlider>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QWidget>

namespace Snow
{

namespace
{

/** The object name that binds a widget to @a key of `[Effect-snow]`. */
QString boundName(const QString &key)
{
    return QStringLiteral("kcfg_") + key;
}

/**
 * Give @a widget the bounds @a key is declared with in snowconfig.kcfg.
 *
 * Read back off the item rather than written out again here. The schema is
 * already the one place that clamps a hand-edited kwinrc, and a KCM carrying
 * its own copy of the numbers would be a second place for them to be wrong --
 * silently, since the two are only ever compared by someone reading both files.
 */
template<typename Widget>
void applySchemaRange(Widget *widget, const QString &key)
{
    const KConfigSkeletonItem *item = SnowConfig::self()->findItem(key);
    Q_ASSERT(item);
    if (!item) {
        // A key that does not match the schema, in a build where the assert
        // above does not stop us: leave the widget at its default bounds
        // rather than dereferencing a null item.
        return;
    }

    using Bound = decltype(widget->minimum());
    widget->setMinimum(item->minValue().value<Bound>());
    widget->setMaximum(item->maxValue().value<Bound>());
}

/**
 * A slider for one of the 1-to-10 knobs, with its two ends named.
 *
 * The number itself tells nobody anything -- "wind 7" is not a quantity you can
 * picture -- so the captions carry the meaning and no spin box is offered
 * beside it. The slider is what carries the value: it is the widget that is
 * named after the key, and the returned row is only its container.
 */
QWidget *namedEndsSlider(QWidget *parent, const QString &key, const QString &low, const QString &high)
{
    auto *row = new QWidget(parent);
    // The only field on either page that wants the width; see the growth policy
    // the two forms are given.
    row->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    auto *rows = new QVBoxLayout(row);
    rows->setContentsMargins(0, 0, 0, 0);
    rows->setSpacing(0);

    auto *slider = new QSlider(Qt::Horizontal, row);
    slider->setObjectName(boundName(key));
    applySchemaRange(slider, key);
    slider->setSingleStep(1);
    slider->setPageStep(1);
    slider->setTickPosition(QSlider::TicksBelow);
    slider->setTickInterval(1);
    rows->addWidget(slider);

    const QFont caption = QFontDatabase::systemFont(QFontDatabase::SmallestReadableFont);
    auto *ends = new QHBoxLayout;
    auto *lowLabel = new QLabel(low, row);
    lowLabel->setFont(caption);
    auto *highLabel = new QLabel(high, row);
    highLabel->setFont(caption);
    ends->addWidget(lowLabel);
    ends->addStretch();
    ends->addWidget(highLabel);
    rows->addLayout(ends);

    return row;
}

/**
 * A form whose fields keep their natural width unless they ask for more.
 *
 * A spin box stretched across the page looks like somewhere to type a sentence
 * rather than somewhere to put a number, while a slider is nothing but its
 * width. Only the sliders here ask to grow.
 */
QFormLayout *pageForm(QWidget *page)
{
    auto *form = new QFormLayout(page);
    form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    return form;
}

/** A combo box bound to one of the two enum keys. */
QComboBox *styleCombo(QWidget *parent, const QString &key)
{
    auto *combo = new QComboBox(parent);
    combo->setObjectName(boundName(key));
    // Wide, so that the two of them line up with each other and with the slider
    // below rather than each stopping at its longest choice.
    combo->setSizePolicy(QSizePolicy::Expanding, combo->sizePolicy().verticalPolicy());
    return combo;
}

/**
 * One choice of a @ref styleCombo: the name snowconfig.kcfg knows it by, the
 * name somebody reads, and the sentence that explains it.
 *
 * KConfigDialogManager binds a non-editable combo box by its index, so what is
 * stored is the position of this row and not @a schemaChoice -- which is here
 * to say which choice a row is, beside the row rather than in a comment, and to
 * be what tests/kcmtest.cpp holds the order against.
 */
void addChoice(QComboBox *combo, const QString &schemaChoice, const QString &name, const QString &explanation)
{
    combo->addItem(name);
    combo->setItemData(combo->count() - 1, explanation, Qt::ToolTipRole);
    combo->setItemData(combo->count() - 1, schemaChoice, Qt::UserRole);
}

} // namespace

ConfigModule::ConfigModule(QObject *parent, const KPluginMetaData &data)
    : KCModule(parent, data)
{
    // The effect's own schema, opened on the file the effect's group lives in.
    // Inside KWin the effect hands the schema KWin's own KSharedConfig object;
    // out here there is no compositor, so it is kwinrc by name.
    SnowConfig::instance(QStringLiteral("kwinrc"));

    // Two tabs, splitting what the snow looks like from how it behaves. What
    // somebody comes here to change -- whether it snows on their panels, how
    // much of it there is -- is in the first one, and the knobs that need a
    // pixel or a rate to mean anything are behind the second.
    auto *tabs = new QTabWidget(widget());
    tabs->addTab(buildBasicTab(tabs), i18n("Basic"));
    tabs->addTab(buildAdvancedTab(tabs), i18n("Advanced"));

    auto *layout = new QVBoxLayout(widget());
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(tabs);

    // Binds every kcfg_-named widget above to its key in one call. Everything
    // this module does for Apply, Defaults, the modified marker and the
    // per-row default indicators happens in here.
    addConfig(SnowConfig::self(), widget());
}

QWidget *ConfigModule::buildBasicTab(QWidget *parent)
{
    auto *page = new QWidget(parent);
    auto *form = pageForm(page);

    // The three Snow Catcher classes. Switching one off does not clear the snow
    // standing on it -- that melts, fast, and is watchable -- so these say where
    // snow settles rather than promising anything will vanish.
    auto *onWindows = new QCheckBox(i18n("Windows"), page);
    onWindows->setObjectName(boundName(QStringLiteral("snowOnWindows")));
    auto *onPanels = new QCheckBox(i18n("Panels"), page);
    onPanels->setObjectName(boundName(QStringLiteral("snowOnPanels")));
    auto *onDesktop = new QCheckBox(i18n("The desktop"), page);
    onDesktop->setObjectName(boundName(QStringLiteral("snowOnDesktop")));

    onWindows->setToolTip(i18n("Snow piles on the top edge of every window."));
    onPanels->setToolTip(i18n("Snow piles on the top edge of panels. A panel along the top of the "
                              "screen never catches any: snow tucked under the screen edge reads as "
                              "a rendering fault rather than as snow."));
    onDesktop->setToolTip(i18n("Snow piles along the bottom of the screen, under every window."));

    form->addRow(i18n("Snow settles on:"), onWindows);
    form->addRow(QString(), onPanels);
    form->addRow(QString(), onDesktop);

    // These rows are in the order snowconfig.kcfg lists the choices in, and a
    // row inserted in the wrong place quietly rewrites the setting rather than
    // failing: what is stored is the index.
    auto *flakeStyle = styleCombo(page, QStringLiteral("flakeStyle"));
    addChoice(flakeStyle, QStringLiteral("blob"), i18n("Soft blobs"),
              i18n("A round flake with a soft edge, all one size."));
    addChoice(flakeStyle, QStringLiteral("crystal"), i18n("Turning crystals"),
              i18n("A six-point crystal that turns as it falls, all one size."));
    addChoice(flakeStyle, QStringLiteral("depth"), i18n("Near and far"),
              i18n("Flakes at varied distances: the near ones larger, brighter and faster."));
    form->addRow(i18n("Flakes:"), flakeStyle);

    auto *capStyle = styleCombo(page, QStringLiteral("capStyle"));
    addChoice(capStyle, QStringLiteral("shaded"), i18n("Smooth"),
              i18n("An even top edge, shaded beneath its leading edge."));
    addChoice(capStyle, QStringLiteral("contour"), i18n("Settled"),
              i18n("The same, with the top edge roughened so a pile looks settled rather than extruded."));
    form->addRow(i18n("Pile edge:"), capStyle);

    // Not a flake count: snow per unit of screen area, so this holds its
    // meaning between a laptop panel and a 4K display.
    auto *density = namedEndsSlider(page, QStringLiteral("density"), i18n("Sparse"), i18n("Heavy"));
    density->setToolTip(i18n("How much snow is in the air. Turning it up or down is approached over "
                             "the few seconds it takes snow to fall, rather than flakes appearing or "
                             "disappearing in mid-air."));
    form->addRow(i18n("Snowfall:"), density);

    // Deliberately absent: the ramp that takes a pile down to nothing across a
    // window's outermost columns. It approximates the rounded corners of a
    // decoration, so it is a correctness fix and there is no version of it
    // anybody would want to choose.

    return page;
}

QWidget *ConfigModule::buildAdvancedTab(QWidget *parent)
{
    auto *page = new QWidget(parent);
    auto *form = pageForm(page);

    auto *fallSpeed = namedEndsSlider(page, QStringLiteral("fallSpeed"), i18n("Drifting"), i18n("Brisk"));
    form->addRow(i18n("Fall speed:"), fallSpeed);

    auto *windStrength = namedEndsSlider(page, QStringLiteral("windStrength"), i18n("Still"), i18n("Gusty"));
    windStrength->setToolTip(i18n("Gusts that every flake answers in its own way. At the lowest "
                                  "setting each flake still sways on its own."));
    form->addRow(i18n("Wind:"), windStrength);

    // What the wind does to a Flake that misses the top edge, which is the only
    // place a Flake is ever seen over a window: it went in past a side edge,
    // under the line snow settles on, so nothing caught it. Next to the wind
    // because there is nothing to choose without one.
    auto *inFront = new QCheckBox(i18n("Falls in front of windows"), page);
    inFront->setObjectName(boundName(QStringLiteral("flakesInFrontOfWindows")));
    inFront->setToolTip(i18n("A gust blows flakes in past the left and right edges of a window or a "
                             "panel, below the edge that snow settles on. They keep falling in front "
                             "of it. Switch this off and they pass behind it instead, coming back "
                             "out below. Either way the snow that settles is the same."));
    form->addRow(i18n("Blown snow:"), inFront);

    auto *maxDepth = new QSpinBox(page);
    maxDepth->setObjectName(boundName(QStringLiteral("maxDepth")));
    applySchemaRange(maxDepth, QStringLiteral("maxDepth"));
    maxDepth->setSuffix(i18nc("Suffix of a spin box holding a length in pixels", " px"));
    maxDepth->setToolTip(i18n("How deep snow is allowed to pile up. Snow that would go over it is "
                              "discarded rather than sliding onto whatever is below. Lowering it "
                              "leaves deeper piles to melt down to the new depth rather than cutting "
                              "them off."));
    form->addRow(i18n("Deepest pile:"), maxDepth);

    auto *meltRate = new QDoubleSpinBox(page);
    meltRate->setObjectName(boundName(QStringLiteral("meltRate")));
    applySchemaRange(meltRate, QStringLiteral("meltRate"));
    meltRate->setDecimals(1);
    meltRate->setSingleStep(0.1);
    meltRate->setSuffix(i18nc("Suffix of a spin box holding a speed in pixels per second", " px/s"));
    // The bottom of the range is not "melts very slowly", it is a different
    // rule: nothing decays at all and every pile keeps whatever lands on it
    // until the effect is switched off. The number 0 with a px/s after it says
    // none of that, so at the minimum the spin box stops showing a number.
    meltRate->setSpecialValueText(i18n("Never: snow keeps piling up"));
    meltRate->setToolTip(i18n("How fast standing snow melts away. This is the other half of how deep "
                              "the snow gets: it settles where melting balances snowfall."));
    form->addRow(i18n("Melts at:"), meltRate);

    auto *frameRateCap = new QSpinBox(page);
    frameRateCap->setObjectName(boundName(QStringLiteral("frameRateCap")));
    applySchemaRange(frameRateCap, QStringLiteral("frameRateCap"));
    frameRateCap->setSuffix(i18nc("Suffix of a spin box holding a frame rate", " fps"));
    frameRateCap->setToolTip(i18n("Falling snow repaints the screen, and nothing else on a still "
                                  "desktop does, so this is very nearly the whole of what the effect "
                                  "costs in power."));
    form->addRow(i18n("Animate at most:"), frameRateCap);

    return page;
}

void ConfigModule::save()
{
    KCModule::save();

    // The keys are in kwinrc now, and the compositor is still running on what
    // it read last. This is the same call docs/development.md drives the nested
    // session with, and what the effect answers by melting toward the new state
    // rather than snapping to it -- which is why it is worth watching from here
    // rather than only trusting.
    //
    // "snow" is the effect's plugin id, which under KF6 is the basename of
    // snow.so; see docs/adr/0004-effect-identity-is-the-plugin-filename.md.
    QDBusMessage message = QDBusMessage::createMethodCall(QStringLiteral("org.kde.KWin"),
                                                          QStringLiteral("/Effects"),
                                                          QStringLiteral("org.kde.kwin.Effects"),
                                                          QStringLiteral("reconfigureEffect"));
    message.setArguments({QStringLiteral("snow")});
    QDBusConnection::sessionBus().send(message);
}

} // namespace Snow

// The factory is named out in full rather than through K_PLUGIN_CLASS_WITH_JSON,
// which derives the name by pasting "Factory" onto the class name -- and a
// namespace-qualified one does not survive the paste. (KWin gets away with it
// because kcoreaddons_add_plugin() defines the name for it; this project
// installs to one hand-picked directory and so does not use that macro.)
K_PLUGIN_FACTORY_WITH_JSON(SnowConfigFactory, "metadata.json", registerPlugin<Snow::ConfigModule>();)

#include "configmodule.moc"
