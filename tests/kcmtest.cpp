/*
    SPDX-FileCopyrightText: 2026 Joep Admiraal <joep@groovytunes.nl>
    SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
    That the config module offers every knob the schema declares, and no others.

    The module binds its widgets to keys by object name -- a check box called
    `kcfg_snowOnPanels` is what makes that check box the snowOnPanels key -- and
    nothing anywhere checks the spelling. A key added to src/snowconfig.kcfg and
    forgotten here is a knob that simply is not in the dialog; a misspelt object
    name is a widget that moves and does nothing, which is worse, because it
    looks like it worked. Neither shows up as an error.

    The enum combo boxes carry the same risk twice over. KConfigDialogManager
    stores a non-editable combo box by its index, so a row added in the wrong
    place does not fail: it silently makes "Turning crystals" mean `depth`.

    So this loads the plugin that is actually built, walks its widgets, and holds
    them against the schema read as a file -- the same reading tests/settingstest
    does, and for the same reason: it is the file that is the source of truth,
    not what KConfigXT can be persuaded to say about itself.
*/

#include <KCModule>
#include <KPluginFactory>
#include <KPluginMetaData>

#include <QComboBox>
#include <QFile>
#include <QStandardPaths>
#include <QTest>
#include <QWidget>
#include <QXmlStreamReader>

namespace
{

/** The keys of `[Effect-snow]`, in the order snowconfig.kcfg declares them. */
struct Schema {
    QStringList keys;
    /** The `<choice>` names of each enum key, in order. */
    QHash<QString, QStringList> choices;
    QHash<QString, QString> minimum;
    QHash<QString, QString> maximum;
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
    QString key;
    while (!xml.atEnd()) {
        xml.readNext();
        if (!xml.isStartElement()) {
            continue;
        }

        const QStringView element = xml.name();
        if (element == QLatin1String("entry")) {
            key = xml.attributes().value(QLatin1String("name")).toString();
            schema.keys.append(key);
        } else if (element == QLatin1String("choice")) {
            schema.choices[key].append(xml.attributes().value(QLatin1String("name")).toString());
        } else if (element == QLatin1String("min")) {
            schema.minimum[key] = xml.readElementText();
        } else if (element == QLatin1String("max")) {
            schema.maximum[key] = xml.readElementText();
        }
    }

    return schema;
}

/** The key a `kcfg_`-named widget is bound to, or a null string if it is not. */
QString boundKey(const QWidget *widget)
{
    const QString name = widget->objectName();
    const QString prefix = QStringLiteral("kcfg_");
    return name.startsWith(prefix) ? name.mid(prefix.length()) : QString();
}

} // namespace

class KcmTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void offersEveryKeyAndNoOthers();
    void choicesAreTheSchemasInOrder();
    void choicesAreTheSchemasInOrder_data();
    void numericRangesComeFromTheSchema();
    void numericRangesComeFromTheSchema_data();

private:
    Schema m_schema;
    KCModule *m_module = nullptr;
};

void KcmTest::initTestCase()
{
    // The module opens kwinrc by name, which outside a compositor is the one in
    // the config home. Test mode moves that somewhere throwaway, so running the
    // tests reads the session's settings no more than it writes them -- and a
    // developer who has actually configured the effect does not get a different
    // answer from a developer who has not.
    QStandardPaths::setTestModeEnabled(true);

    m_schema = readSchema();
    QVERIFY(!m_schema.keys.isEmpty());

    // The plugin as built, rather than the class linked in: the object names and
    // the combo rows are only worth checking in the artefact that System
    // Settings will load.
    const KPluginMetaData metaData{QStringLiteral(SNOW_KCM_PATH)};
    QVERIFY2(metaData.isValid(), qPrintable(QStringLiteral(SNOW_KCM_PATH)));

    auto loaded = KPluginFactory::instantiatePlugin<KCModule>(metaData, nullptr, {});
    QVERIFY2(loaded, qPrintable(loaded.errorString));
    m_module = loaded.plugin;
}

void KcmTest::offersEveryKeyAndNoOthers()
{
    QStringList bound;
    const auto widgets = m_module->widget()->findChildren<QWidget *>();
    for (const QWidget *widget : widgets) {
        const QString key = boundKey(widget);
        if (!key.isEmpty()) {
            bound.append(key);
        }
    }

    // Sorted, because where a knob sits in the dialog is a matter of what goes
    // on which tab and in what order it reads; that it is there at all is not.
    bound.sort();
    QStringList expected = m_schema.keys;
    expected.sort();
    QCOMPARE(bound, expected);
}

void KcmTest::choicesAreTheSchemasInOrder_data()
{
    QTest::addColumn<QString>("key");
    QTest::newRow("flakeStyle") << QStringLiteral("flakeStyle");
    QTest::newRow("capStyle") << QStringLiteral("capStyle");
}

void KcmTest::choicesAreTheSchemasInOrder()
{
    QFETCH(QString, key);

    auto *combo = m_module->widget()->findChild<QComboBox *>(QStringLiteral("kcfg_") + key);
    QVERIFY(combo);
    // Binding by index only holds while the combo box cannot be typed into.
    QVERIFY(!combo->isEditable());

    QStringList rows;
    for (int row = 0; row < combo->count(); ++row) {
        rows.append(combo->itemData(row, Qt::UserRole).toString());
    }
    QCOMPARE(rows, m_schema.choices.value(key));
}

void KcmTest::numericRangesComeFromTheSchema_data()
{
    QTest::addColumn<QString>("key");
    for (const QString &key : std::as_const(m_schema.keys)) {
        if (m_schema.minimum.contains(key)) {
            QTest::newRow(qPrintable(key)) << key;
        }
    }
}

void KcmTest::numericRangesComeFromTheSchema()
{
    QFETCH(QString, key);

    QWidget *widget = m_module->widget()->findChild<QWidget *>(QStringLiteral("kcfg_") + key);
    QVERIFY(widget);

    // Whatever the widget is -- a slider, an int spin box, a double one -- the
    // bounds are properties, so this asks for them the same way for all three
    // and stays true if one of them is swapped for another.
    QCOMPARE(widget->property("minimum").toString(), m_schema.minimum.value(key));
    QCOMPARE(widget->property("maximum").toString(), m_schema.maximum.value(key));
}

QTEST_MAIN(KcmTest)

#include "kcmtest.moc"
