/*
    SPDX-FileCopyrightText: 2026 Joep Admiraal <joep@groovytunes.nl>
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "catcherregistry.h"

#include "snowlogging.h"

#include <QStringList>

#include <core/output.h>
#include <effect/effecthandler.h>
#include <effect/effectwindow.h>

namespace Snow
{

// How far a Panel's top edge may sit from its output's top edge and still count
// as being against it. A floating Panel keeps a visibly larger gap, so this only
// has to absorb rounding.
static constexpr qreal s_topEdgeTolerance = 1.0;

CatcherRegistry::CatcherRegistry(QObject *parent)
    : QObject(parent)
{
    const QList<KWin::LogicalOutput *> screens = KWin::effects->screens();
    for (KWin::LogicalOutput *output : screens) {
        addOutput(output);
    }

    // The effect can be switched on at any time, so the world already exists.
    const QList<KWin::EffectWindow *> windows = KWin::effects->stackingOrder();
    for (KWin::EffectWindow *window : windows) {
        trackWindow(window);
    }

    connect(KWin::effects, &KWin::EffectsHandler::screenAdded, this, &CatcherRegistry::addOutput);
    connect(KWin::effects, &KWin::EffectsHandler::screenRemoved, this, &CatcherRegistry::removeOutput);
    connect(KWin::effects, &KWin::EffectsHandler::windowAdded, this, &CatcherRegistry::trackWindow);
    // windowClosed is where a Snowline is discarded (spec: Behaviour); windowDeleted
    // is the backstop for a window that was never announced as closed.
    connect(KWin::effects, &KWin::EffectsHandler::windowClosed, this, &CatcherRegistry::forgetWindow);
    connect(KWin::effects, &KWin::EffectsHandler::windowDeleted, this, &CatcherRegistry::forgetWindow);

    logCatchers(QStringLiteral("registry opened"));
}

CatcherRegistry::~CatcherRegistry() = default;

QList<Catcher *> CatcherRegistry::collect(const KWin::LogicalOutput *output) const
{
    // Everything, when nobody named an output.
    const auto wanted = [output](const Catcher *catcher) {
        return !output || catchesOver(catcher, output);
    };

    QList<Catcher *> result;
    result.reserve(int(m_grounds.size() + m_windowCatchers.size()));

    // The ground is below everything, and there is one per output.
    const QList<KWin::LogicalOutput *> screens = KWin::effects->screens();
    for (KWin::LogicalOutput *screen : screens) {
        Catcher *catcher = ground(screen);
        if (catcher && wanted(catcher)) {
            result.append(catcher);
        }
    }

    size_t seen = 0;
    const QList<KWin::EffectWindow *> windows = KWin::effects->stackingOrder();
    for (KWin::EffectWindow *window : windows) {
        Catcher *catcher = catcherFor(window);
        if (!catcher) {
            continue;
        }
        ++seen;

        // Asked once a frame, here, because this is already the walk over
        // every window: a Catcher nobody can see stops taking Flakes out of
        // the air, and the Flakes carry on down to whatever is below it
        // (Catcher::isCatching). A minimise, a Panel auto-hiding and a
        // desktop switch are one question at one place rather than three
        // signals to keep up with. Asked of every Catcher and not only of the
        // ones this output wanted, so that the answer does not depend on which
        // output's frame happened to come first.
        catcher->setConcealed(isConcealed(window));

        if (wanted(catcher)) {
            result.append(catcher);
        }
    }

    // A Catcher whose window has dropped out of the stacking order would
    // silently stop catching, so put the stragglers back rather than lose them.
    // Counted rather than searched for: the search below is a scan of the list
    // per Catcher, and this is a walk that happens in every frame.
    if (seen != m_windowCatchers.size()) {
        for (const auto &[window, catcher] : m_windowCatchers) {
            // Asked here too: a Catcher the walk above never reached would
            // otherwise keep catching on whatever it was the last time it was
            // in the stacking order.
            catcher->setConcealed(isConcealed(window));

            if (wanted(catcher.get()) && !result.contains(catcher.get())) {
                result.append(catcher.get());
            }
        }
    }

    return result;
}

QList<Catcher *> CatcherRegistry::catchers() const
{
    return collect(nullptr);
}

QList<Catcher *> CatcherRegistry::catchersFor(KWin::LogicalOutput *output) const
{
    // The same walk as above rather than that walk and then a filter over what
    // it produced: the paint order and the once-a-frame concealment are the
    // same however many outputs there are, and this is only which of them the
    // Flakes of @a output can reach.
    return collect(output);
}

bool CatcherRegistry::isConcealed(const KWin::EffectWindow *window)
{
    // Every way a window can be off screen while its EffectWindow stays exactly
    // where it was. KWin's own isHidden() is only the internal hidden flag --
    // an X11 window that is unmapped, a Panel that has auto-hidden -- and the
    // rest of what "nobody can see it" means lives in a predicate of its own,
    // which is why Window::isShown() is the conjunction of all of them rather
    // than a reading of isHidden(). A minimised window in particular is hidden
    // by none of them, and used to go on catching Flakes on the line where it
    // stood.
    return window->isHidden()
        || window->isMinimized()
        || window->isHiddenByShowDesktop()
        || !window->isOnCurrentDesktop()
        || !window->isOnCurrentActivity();
}

bool CatcherRegistry::catchesOver(const Catcher *catcher, const KWin::LogicalOutput *output)
{
    if (catcher->catcherClass() == CatcherClass::Ground) {
        return catcher->output() == output;
    }

    // Half-open at the bottom, which is where two stacked outputs meet: a
    // window snapped to the top edge of the lower monitor has its landing line
    // exactly on that seam, and it belongs to the output it is on rather than
    // to the one whose bottom row it grazes.
    const QRectF geometry = output->geometryF();
    const qreal line = catcher->geometry().top();
    return line >= geometry.top() && line < geometry.bottom();
}

Catcher *CatcherRegistry::catcherFor(KWin::EffectWindow *window) const
{
    const auto it = m_windowCatchers.find(window);
    return it == m_windowCatchers.end() ? nullptr : it->second.get();
}

Catcher *CatcherRegistry::ground(KWin::LogicalOutput *output) const
{
    const auto it = m_grounds.find(output);
    return it == m_grounds.end() ? nullptr : it->second.get();
}

void CatcherRegistry::settle(KWin::LogicalOutput *output, qreal delta, const Settings &settings)
{
    if (delta <= 0) {
        return;
    }

    if (Catcher *catcher = ground(output)) {
        catcher->settle(delta, settings);
    }

    // Straight down the map rather than through catchers(): settling is the
    // same for every Catcher whatever order it happens in, and this is the one
    // thing here that runs on every frame.
    for (const auto &[window, catcher] : m_windowCatchers) {
        if (catcher->output() == output) {
            catcher->settle(delta, settings);
        }
    }
}

void CatcherRegistry::addOutput(KWin::LogicalOutput *output)
{
    connect(output, &KWin::LogicalOutput::geometryChanged, this, [this, output] {
        outputGeometryChanged(output);
    });

    m_grounds[output] = std::make_unique<Catcher>(CatcherClass::Ground, nullptr, output, groundGeometry(output));
    logCatchers(QStringLiteral("output %1 added").arg(output->name()));
}

void CatcherRegistry::removeOutput(KWin::LogicalOutput *output)
{
    const QString name = output->name();

    disconnect(output, nullptr, this, nullptr);
    m_grounds.erase(output);

    // KWin is still moving windows off the output while this runs, so drop the
    // dangling reference now and ask again once it has settled -- asking now
    // would re-home Catchers onto the output that is about to be deleted.
    for (const auto &[window, catcher] : m_windowCatchers) {
        if (catcher->output() == output) {
            catcher->setOutput(nullptr);
        }
    }

    QMetaObject::invokeMethod(
        this, [this, name] {
            refreshAllWindows();
            logCatchers(QStringLiteral("output %1 removed").arg(name));
        },
        Qt::QueuedConnection);
}

void CatcherRegistry::outputGeometryChanged(KWin::LogicalOutput *output)
{
    if (Catcher *catcher = ground(output)) {
        catcher->setGeometry(groundGeometry(output));
    }
    // Moving or resizing an output can push a Panel against the top edge, or
    // pull it away from one, without the Panel itself moving.
    refreshAllWindows();
}

void CatcherRegistry::trackWindow(KWin::EffectWindow *window)
{
    // Connected for every window, not just Catchers: a Panel that moves away
    // from the top edge becomes one, and a window that moves changes output.
    connect(window, &KWin::EffectWindow::windowFrameGeometryChanged, this, [this, window] {
        refreshWindow(window);
    });

    refreshWindow(window);
}

void CatcherRegistry::forgetWindow(KWin::EffectWindow *window)
{
    disconnect(window, nullptr, this, nullptr);

    if (m_windowCatchers.erase(window) > 0) {
        logCatchers(QStringLiteral("window closed: %1").arg(windowLabel(window)));
    }
}

void CatcherRegistry::refreshWindow(KWin::EffectWindow *window)
{
    const std::optional<CatcherClass> catcherClass = classify(window);
    const auto it = m_windowCatchers.find(window);

    if (!catcherClass) {
        if (it != m_windowCatchers.end()) {
            m_windowCatchers.erase(it);
            logCatchers(QStringLiteral("no longer catches: %1").arg(windowLabel(window)));
        }
        return;
    }

    KWin::LogicalOutput *output = window->screen();
    const QRectF geometry = window->frameGeometry();

    if (it == m_windowCatchers.end()) {
        m_windowCatchers[window] = std::make_unique<Catcher>(*catcherClass, window, output, geometry);
        logCatchers(QStringLiteral("now catches: %1").arg(windowLabel(window)));
        return;
    }

    Catcher *catcher = it->second.get();
    if (catcher->catcherClass() != *catcherClass) {
        // A class change is a different Catcher, not the same one wearing a new
        // hat: its Snowline belonged to what it used to be.
        it->second = std::make_unique<Catcher>(*catcherClass, window, output, geometry);
        logCatchers(QStringLiteral("changed class: %1").arg(windowLabel(window)));
        return;
    }

    // The same Catcher in a new place, which is the whole point of holding a
    // Snowline in local coordinates: it survives a move, a minimise, a desktop
    // switch, a Panel auto-hiding, and a hop to an output of a different scale,
    // because none of those change the Catcher's width (ADR-0001).
    const bool movedOutput = catcher->output() != output;
    catcher->setOutput(output);
    catcher->setGeometry(geometry);

    // A plain move or resize happens every frame of a drag; only say something
    // when the set itself changed shape.
    if (movedOutput) {
        logCatchers(QStringLiteral("moved output: %1").arg(windowLabel(window)));
    }
}

void CatcherRegistry::refreshAllWindows()
{
    const QList<KWin::EffectWindow *> windows = KWin::effects->stackingOrder();
    for (KWin::EffectWindow *window : windows) {
        refreshWindow(window);
    }
}

QString CatcherRegistry::describeAll() const
{
    const QList<Catcher *> all = catchers();
    QStringList lines;
    lines.reserve(all.count());
    for (const Catcher *catcher : all) {
        lines.append(QStringLiteral("\n    ") + catcher->describe());
    }
    return QStringLiteral("%1%2").arg(all.count()).arg(lines.join(QString()));
}

void CatcherRegistry::logCatchers(const QString &reason)
{
    if (!KWIN_EFFECT_SNOW().isDebugEnabled()) {
        return;
    }

    // Every line carries each Snowline's Column count and the sum of its
    // depths, so a report on a move or a resize is also how much snow is
    // standing on the thing that moved.
    qCDebug(KWIN_EFFECT_SNOW).noquote() << "Snow Catchers:" << reason << "--" << describeAll();
}

std::optional<CatcherClass> CatcherRegistry::classify(const KWin::EffectWindow *window)
{
    if (window->isDeleted()) {
        return std::nullopt;
    }

    if (window->isDock()) {
        const KWin::LogicalOutput *output = window->screen();
        if (!output || isTopEdgePanel(window->frameGeometry(), output->geometryF())) {
            return std::nullopt;
        }
        return CatcherClass::Panel;
    }

    // isSpecialWindow() covers the desktop, splashes, notifications, OSDs and
    // applet popups; the rest are surfaces that come and go faster than snow
    // could settle on them, or that are not really surfaces at all.
    if (window->isSpecialWindow() || !window->isManaged() || window->isPopupWindow()
        || window->isLockScreen() || window->isInputMethod() || window->isDNDIcon()) {
        return std::nullopt;
    }

    if (!window->screen()) {
        return std::nullopt;
    }

    return CatcherClass::Window;
}

bool CatcherRegistry::isTopEdgePanel(const QRectF &frameGeometry, const QRectF &outputGeometry)
{
    // A Cap on a Panel pinned under the screen edge reads as a rendering bug
    // rather than as snow, so horizontal Panels against the top of their output
    // catch nothing at all (spec: Behaviour). A floating Panel sits below the
    // edge with its own top edge on show, and a vertical Panel catches on that
    // edge too -- correctly, a thin sliver.
    const bool horizontal = frameGeometry.width() > frameGeometry.height();
    return horizontal && frameGeometry.top() <= outputGeometry.top() + s_topEdgeTolerance;
}

QRectF CatcherRegistry::groundGeometry(const KWin::LogicalOutput *output)
{
    // The ground catches on the bottom edge of its output: a zero-height strip,
    // so that geometry().top() is the landing line for every class alike.
    const QRectF geometry = output->geometryF();
    return QRectF(geometry.left(), geometry.bottom(), geometry.width(), 0);
}

} // namespace Snow
