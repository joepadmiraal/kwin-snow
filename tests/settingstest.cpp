/*
    SPDX-FileCopyrightText: 2026 Joep Admiraal <joep@groovytunes.nl>
    SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
    That the schema and the struct say the same thing.

    Snow::Settings carries the spec's defaults because it is what the simulation
    reads and what every other test here works from; src/snowconfig.kcfg carries
    them because it is what KConfigXT reads, and so what a KCM's "Defaults"
    button restores and what an unconfigured session gets. Nothing in the
    toolchain ties the two together, so a key added to one and forgotten in the
    other, or a default changed in one place, is a bug that only shows up as
    "the effect looks different the first time you open its settings".

    So this reads the schema as a file -- not through KConfigXT, which would
    only tell us what the schema says about itself -- and checks it against the
    struct, key by key. It is also where the spec's two Configuration tables are
    written down as a list, which is the other thing nothing else checks.
*/

#include "settings.h"

#include <QFile>
#include <QHash>
#include <QStringList>
#include <QTest>
#include <QXmlStreamReader>

using namespace Snow;

namespace
{

/** One <entry> of the schema, as written rather than as interpreted. */
struct Entry {
    QString type;
    QString defaultValue;
    QString minimum;
    QString maximum;
    QStringList choices;
};

struct Schema {
    QString group;
    QStringList order;
    QHash<QString, Entry> entries;
};

Schema readSchema()
{
    Schema schema;

    QFile file(QStringLiteral(SNOW_KCFG_PATH));
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning("could not open %s", SNOW_KCFG_PATH);
        return schema;
    }

    QXmlStreamReader xml(&file);
    QString name;
    while (!xml.atEnd()) {
        xml.readNext();
        if (!xml.isStartElement()) {
            continue;
        }

        const QStringView element = xml.name();
        if (element == QLatin1String("group")) {
            schema.group = xml.attributes().value(QLatin1String("name")).toString();
        } else if (element == QLatin1String("entry")) {
            name = xml.attributes().value(QLatin1String("name")).toString();
            schema.order.append(name);
            schema.entries[name].type = xml.attributes().value(QLatin1String("type")).toString();
        } else if (element == QLatin1String("choice")) {
            schema.entries[name].choices.append(xml.attributes().value(QLatin1String("name")).toString());
        } else if (element == QLatin1String("default")) {
            schema.entries[name].defaultValue = xml.readElementText();
        } else if (element == QLatin1String("min")) {
            schema.entries[name].minimum = xml.readElementText();
        } else if (element == QLatin1String("max")) {
            schema.entries[name].maximum = xml.readElementText();
        }
    }

    if (xml.hasError()) {
        qWarning("%s", qPrintable(xml.errorString()));
    }

    return schema;
}

QString boolText(bool value)
{
    return value ? QStringLiteral("true") : QStringLiteral("false");
}

} // namespace

class SettingsTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();

    void theSchemaIsTheSpecsTwoTables();
    void theGroupIsTheEffectsOwn();
    void everyDefaultIsTheStructsDefault();
    void theRangesAreTheSpecsAndTheClampsTheCodeMakes();
    void theEnumChoicesAreSpeltTheWayTheSpecSpellsThem();

private:
    Schema m_schema;
};

void SettingsTest::initTestCase()
{
    m_schema = readSchema();
    QVERIFY2(!m_schema.entries.isEmpty(), "the schema did not parse");
}

void SettingsTest::theSchemaIsTheSpecsTwoTables()
{
    // Basic -- what it looks like -- and then Advanced -- how it behaves -- in
    // the order the spec tables them, which is also the order the KCM's two
    // tabs will want them in.
    const QStringList expected{
        QStringLiteral("snowOnWindows"),
        QStringLiteral("snowOnPanels"),
        QStringLiteral("snowOnDesktop"),
        QStringLiteral("flakeStyle"),
        QStringLiteral("capStyle"),
        QStringLiteral("density"),
        QStringLiteral("fallSpeed"),
        QStringLiteral("windStrength"),
        QStringLiteral("flakesInFrontOfWindows"),
        QStringLiteral("maxDepth"),
        QStringLiteral("meltRate"),
        QStringLiteral("frameRateCap"),
    };

    QCOMPARE(m_schema.order, expected);
}

void SettingsTest::theGroupIsTheEffectsOwn()
{
    // [Effect-snow], because the effect is named `snow` -- the basename of its
    // plugin file (ADR-0004). Renaming the group orphans every key in every
    // kwinrc there is.
    QCOMPARE(m_schema.group, QStringLiteral("Effect-snow"));
}

void SettingsTest::everyDefaultIsTheStructsDefault()
{
    const Settings settings;
    const auto defaultOf = [this](const char *key) {
        return m_schema.entries.value(QLatin1String(key)).defaultValue;
    };

    QCOMPARE(defaultOf("snowOnWindows"), boolText(settings.snowOnWindows));
    QCOMPARE(defaultOf("snowOnPanels"), boolText(settings.snowOnPanels));
    QCOMPARE(defaultOf("snowOnDesktop"), boolText(settings.snowOnDesktop));

    QCOMPARE(defaultOf("flakeStyle"), QStringLiteral("depth"));
    QVERIFY(settings.flakeStyle == FlakeStyle::Depth);
    QCOMPARE(defaultOf("capStyle"), QStringLiteral("contour"));
    QVERIFY(settings.capStyle == CapStyle::Contour);

    QCOMPARE(defaultOf("density").toInt(), settings.density);
    QCOMPARE(defaultOf("fallSpeed").toInt(), settings.fallSpeed);
    QCOMPARE(defaultOf("windStrength").toInt(), settings.windStrength);
    QCOMPARE(defaultOf("flakesInFrontOfWindows"), boolText(settings.flakesInFrontOfWindows));
    QCOMPARE(defaultOf("maxDepth").toInt(), settings.maxDepth);
    QCOMPARE(defaultOf("meltRate").toDouble(), settings.meltRate);
    QCOMPARE(defaultOf("frameRateCap").toInt(), settings.frameRateCap);

    // A default spelled in a way toInt() reads as zero would pass every line
    // above that compares against a zero field, so check the text as well.
    for (const QString &key : std::as_const(m_schema.order)) {
        QVERIFY2(!m_schema.entries.value(key).defaultValue.isEmpty(),
                 qPrintable(key + QStringLiteral(" has no default")));
    }
}

void SettingsTest::theRangesAreTheSpecsAndTheClampsTheCodeMakes()
{
    const auto range = [this](const char *key) {
        const Entry entry = m_schema.entries.value(QLatin1String(key));
        return QStringLiteral("%1..%2").arg(entry.minimum, entry.maximum);
    };

    // The three the spec states outright.
    QCOMPARE(range("density"), QStringLiteral("1..10"));
    QCOMPARE(range("fallSpeed"), QStringLiteral("1..10"));
    QCOMPARE(range("windStrength"), QStringLiteral("0..10"));

    // KConfigXT clamps on read, so these are where a hand-edited kwinrc is
    // stopped. The frame rate's upper bound is the one frameInterval() clamps
    // to, and a lower bound of 1 is what keeps that clamp from ever having to
    // save anyone from a division by zero.
    QCOMPARE(range("frameRateCap"), QStringLiteral("1..240"));
    QCOMPARE(frameInterval(m_schema.entries.value(QStringLiteral("frameRateCap")).maximum.toInt()),
             frameInterval(1000));
    QCOMPARE(range("maxDepth"), QStringLiteral("1..200"));

    // 0 is permanent accumulation, so it has to be in range (spec:
    // Configuration).
    QCOMPARE(m_schema.entries.value(QStringLiteral("meltRate")).minimum, QStringLiteral("0"));
}

void SettingsTest::theEnumChoicesAreSpeltTheWayTheSpecSpellsThem()
{
    // These strings are written into kwinrc, so they are the spec's spelling
    // and not C++'s. Which C++ value each one maps to is settings.cpp's switch
    // over the generated enumerators, so the order here is free -- but the KCM
    // will bind a combobox to it, and a choice renamed or reordered is a
    // silently changed setting in every kwinrc that already names it.
    QCOMPARE(m_schema.entries.value(QStringLiteral("flakeStyle")).choices,
             (QStringList{QStringLiteral("blob"), QStringLiteral("crystal"), QStringLiteral("depth")}));
    QCOMPARE(m_schema.entries.value(QStringLiteral("capStyle")).choices,
             (QStringList{QStringLiteral("shaded"), QStringLiteral("contour")}));
}

QTEST_GUILESS_MAIN(SettingsTest)

#include "settingstest.moc"
