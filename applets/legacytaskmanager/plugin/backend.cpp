/***************************************************************************
 *   Copyright (C) 2007 by Robert Knight <robertknight@gmail.com>          *
 *   Copyright (C) 2008 by Alexis Ménard <darktears31@gmail.com>           *
 *   Copyright (C) 2012-2013 by Eike Hein <hein@kde.org>                   *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA .        *
 ***************************************************************************/

#include "backend.h"

#include <groupmanager.h>
#include <taskactions.h>
#include <taskgroup.h>
#include <tasksmodel.h>

#include <QApplication>
#include <QCursor>
#include <QQuickItem>
#include <QQuickWindow>
#include <QScreen>
#include <QTimer>

#include <KAuthorized>
#include <KConfigGroup>
#include <KDesktopFile>
#include <KFileItem>
#include <KLocalizedString>
#include <KRun>
#include <KService>
#include <kwindoweffects.h>

#include <KActivities/Consumer>
#include <KActivities/Stats/Cleaning>
#include <KActivities/Stats/ResultSet>
#include <KActivities/Stats/Terms>

namespace KAStats = KActivities::Stats;

using namespace KAStats;
using namespace KAStats::Terms;

Backend::Backend(QObject* parent) : QObject(parent),
    m_groupManager(new LegacyTaskManager::GroupManager(this)),
    m_tasksModel(new LegacyTaskManager::TasksModel(m_groupManager, this)),
    m_contextMenu(0),
    m_taskManagerItem(0),
    m_toolTipItem(0),
    m_panelWinId(0),
    m_highlightWindows(false),
    m_activitiesConsumer(new KActivities::Consumer(this))
{
    connect(m_groupManager, SIGNAL(launcherListChanged()), this, SLOT(updateLaunchersCache()));
}

Backend::~Backend()
{
    delete m_contextMenu;
}

LegacyTaskManager::GroupManager* Backend::groupManager() const
{
    return m_groupManager;
}

LegacyTaskManager::TasksModel* Backend::tasksModel() const
{
    return m_tasksModel;
}

QQuickItem* Backend::taskManagerItem() const
{
    return m_taskManagerItem;
}

void Backend::setLegacyTaskManagerItem(QQuickItem* item)
{
    if (item != m_taskManagerItem) {
        m_taskManagerItem = item;
        emit taskManagerItemChanged(item);
    }
}

QQuickItem* Backend::toolTipItem() const
{
    return m_toolTipItem;
}

void Backend::setToolTipItem(QQuickItem* item)
{
    if (item != m_toolTipItem) {
        m_toolTipItem = item;

        connect(item, SIGNAL(windowChanged(QQuickWindow*)), this, SLOT(toolTipWindowChanged(QQuickWindow*)));

        emit toolTipItemChanged(item);
    }
}

void Backend::toolTipWindowChanged(QQuickWindow *window)
{
    Q_UNUSED(window)

    updateWindowHighlight();
}

bool Backend::anyTaskNeedsAttention() const
{
    return groupManager()->rootGroup()->demandsAttention();
}

bool Backend::showingContextMenu() const
{
    return m_contextMenu;
}

bool Backend::highlightWindows() const
{
    return m_highlightWindows;
}

void Backend::setHighlightWindows(bool highlight)
{
    if (highlight != m_highlightWindows) {
        m_highlightWindows = highlight;

        updateWindowHighlight();

        emit highlightWindowsChanged(highlight);
    }
}

int Backend::groupingStrategy() const
{
    return m_groupManager->groupingStrategy();
}

void Backend::setGroupingStrategy(int groupingStrategy)
{
    // FIXME TODO: This is fucking terrible.
    m_groupManager->setGroupingStrategy(static_cast<LegacyTaskManager::GroupManager::TaskGroupingStrategy>(groupingStrategy));
}

int Backend::sortingStrategy() const
{
    return m_groupManager->sortingStrategy();
}

void Backend::setSortingStrategy(int sortingStrategy)
{
    // FIXME TODO: This is fucking terrible.
    m_groupManager->setSortingStrategy(static_cast<LegacyTaskManager::GroupManager::TaskSortingStrategy>(sortingStrategy));
}

void Backend::updateLaunchersCache()
{
    const QList<QUrl> launcherList = m_groupManager->launcherList();

    QStringList launchers;

    foreach(const QUrl& launcher, launcherList) {
        launchers.append(launcher.toString());
    }

    QString joined(launchers.join(','));

    if (joined != m_launchers) {
        m_launchers = joined;
        emit launchersChanged();
    }
}

QString Backend::launchers() const
{
    return m_launchers;
}

void Backend::setLaunchers(const QString& launchers)
{
    if (launchers == m_launchers) {
        return;
    }

    m_launchers = launchers;

    QList<QUrl> launcherList;

    foreach(const QString& launcher, launchers.split(',')) {
        launcherList.append(launcher);
    }

    disconnect(m_groupManager, SIGNAL(launcherListChanged()), this, SLOT(updateLaunchersCache()));
    m_groupManager->setLauncherList(launcherList);
    connect(m_groupManager, SIGNAL(launcherListChanged()), this, SLOT(updateLaunchersCache()));

    emit launchersChanged();
}

void Backend::activateItem(int id, bool toggle)
{
    LegacyTaskManager::AbstractGroupableItem* item = m_groupManager->rootGroup()->getMemberById(id);

    if (!item) {
        return;
    }

    if (item->itemType() == LegacyTaskManager::TaskItemType && !item->isStartupItem()) {
        LegacyTaskManager::TaskItem* taskItem = static_cast<LegacyTaskManager::TaskItem*>(item);

        if (toggle) {
            taskItem->task()->activateRaiseOrIconify();
        } else {
            taskItem->task()->activate();
        }
    } else if (item->itemType() == LegacyTaskManager::LauncherItemType) {
        static_cast<LegacyTaskManager::LauncherItem*>(item)->launch();
    }
}

void Backend::activateWindow(int winId)
{
    KWindowSystem::forceActiveWindow(winId);
}

void Backend::closeByWinId(int winId)
{
    LegacyTaskManager::AbstractGroupableItem *item = m_groupManager->rootGroup()->getMemberByWId(winId);

    if (item) {
        item->close();
    }
}

void Backend::closeByItemId(int itemId)
{
    LegacyTaskManager::AbstractGroupableItem *item = m_groupManager->rootGroup()->getMemberById(itemId);

    if (item) {
        item->close();
    }
}

void Backend::launchNewInstance(int id)
{
    LegacyTaskManager::AbstractGroupableItem* item = m_groupManager->rootGroup()->getMemberById(id);

    if (item) {
        item->launchNewInstance();
    }
}

void Backend::itemContextMenu(QQuickItem *item, QObject *configAction)
{
    LegacyTaskManager::AbstractGroupableItem* agItem = m_groupManager->rootGroup()->getMemberById(item->property("itemId").toInt());

    if (!KAuthorized::authorizeKAction("kwin_rmb") || !item || !item->window() || !agItem) {
        return;
    }

    QPointer<QQuickWindow> window = item->window();

    QList <QAction*> actionList;

    QAction *action = static_cast<QAction *>(configAction);

    if (action && action->isEnabled()) {
        actionList << action;
    }

    if (agItem->itemType() == LegacyTaskManager::TaskItemType && !agItem->isStartupItem()) {
        LegacyTaskManager::TaskItem* taskItem = static_cast<LegacyTaskManager::TaskItem*>(agItem);
        m_contextMenu = new LegacyTaskManager::BasicMenu(0, taskItem, m_groupManager, actionList);
        addJumpListActions(taskItem->launcherUrl(), m_contextMenu);
    } else if (agItem->itemType() == LegacyTaskManager::GroupItemType) {
        LegacyTaskManager::TaskGroup* taskGroup = static_cast<LegacyTaskManager::TaskGroup*>(agItem);
        const int maxWidth = 0.8 * (window ? window->screen()->size().width() : 1.0);
        m_contextMenu = new LegacyTaskManager::BasicMenu(0, taskGroup, m_groupManager, actionList, QList <QAction*>(), maxWidth);
        addJumpListActions(taskGroup->launcherUrl(), m_contextMenu);
    } else if (agItem->itemType() == LegacyTaskManager::LauncherItemType) {
        LegacyTaskManager::LauncherItem *launcher = static_cast<LegacyTaskManager::LauncherItem *>(agItem);
        m_contextMenu = new LegacyTaskManager::BasicMenu(0, launcher, m_groupManager, actionList);
        addRecentDocumentActions(launcher, m_contextMenu);
        addJumpListActions(launcher->launcherUrl(), m_contextMenu);
    }

    if (!m_contextMenu) {
        return;
    }

    connect(window.data(), &QObject::destroyed, this, &Backend::showingContextMenuChanged,
        Qt::QueuedConnection);

    connect(m_contextMenu.data(), &QObject::destroyed, this, &Backend::showingContextMenuChanged,
        Qt::QueuedConnection);

    m_contextMenu->adjustSize();

    const bool isVertical = (m_taskManagerItem && m_taskManagerItem->property("vertical").toBool());

    if (!isVertical) {
        m_contextMenu->setMinimumWidth(item->width());
    }

    QScreen *screen = window ? window->screen() : nullptr;
    QPoint pos = window ? window->mapToGlobal(item->mapToScene(QPointF(0, 0)).toPoint()) : QPoint();

    if (screen) {
        if (isVertical) {
            int adjustedX = pos.x() + item->width();
            if (adjustedX + m_contextMenu->width() > screen->geometry().width()) {
                adjustedX = pos.x() - m_contextMenu->width();
            }
            pos.setX(adjustedX);
        } else {
            int adjustedY = pos.y() + item->height();
            if (adjustedY + m_contextMenu->height() > screen->geometry().height()) {
                adjustedY = pos.y() - m_contextMenu->height();
            }
            pos.setY(adjustedY);
        }
    }

    // Ungrab before showing the menu so the Qt Quick View doesn't stumble
    // over the pointer leaving its window while handling a click.
    if (window && window->mouseGrabberItem()) {
        window->mouseGrabberItem()->ungrabMouse();
    }

    // Close menu when the delegate is destroyed.
    connect(item, &QQuickItem::destroyed, m_contextMenu.data(), &LegacyTaskManager::BasicMenu::deleteLater);

    if (m_windowsToHighlight.count()) {
        m_windowsToHighlight.clear();
        updateWindowHighlight();
    }

    // Interrupt the call chain from the delegate in case it's destroyed while
    // we're in QMenu::exec();
    QTimer::singleShot(0, this, [=] {
        if (!m_contextMenu) {
            return;
        }

        QHoverEvent ev(QEvent::HoverLeave, QPoint(0, 0), QPoint(0, 0));
        QGuiApplication::sendEvent(item, &ev);

        emit showingContextMenuChanged();

        QPointer<LegacyTaskManager::BasicMenu> guard(m_contextMenu);
        m_contextMenu->exec(pos);
        if (!guard) {
            return;
        }
        if (window) {
            m_contextMenu->windowHandle()->setTransientParent(window);
        }
        m_contextMenu->deleteLater();
    });
}

void Backend::addJumpListActions(const QUrl &launcherUrl, LegacyTaskManager::BasicMenu *menu) const
{
    if (!menu || !launcherUrl.isValid() || !launcherUrl.isLocalFile() || !KDesktopFile::isDesktopFile(launcherUrl.toLocalFile())) {
        return;
    }

    QAction *firstAction = menu->actions().at(0);

    if (!firstAction) {
        return;
    }

    KDesktopFile desktopFile(launcherUrl.toLocalFile());

    const QStringList &actions = desktopFile.readActions();

    int count = 0;

    foreach (const QString &actionName, actions) {
        const KConfigGroup &actionGroup = desktopFile.actionGroup(actionName);

        if (!actionGroup.isValid() || !actionGroup.exists()) {
            continue;
        }

        const QString &name = actionGroup.readEntry(QStringLiteral("Name"));
        const QString &exec = actionGroup.readEntry(QStringLiteral("Exec"));
        if (name.isEmpty() || exec.isEmpty()) {
            continue;
        }

        QAction *action = new QAction(menu);
        action->setText(name);
        action->setIcon(QIcon::fromTheme(actionGroup.readEntry("Icon")));
        action->setProperty("exec", exec);
        // so we can show the proper application name and icon when it launches
        action->setProperty("applicationName", desktopFile.readName());
        action->setProperty("applicationIcon", desktopFile.readIcon());
        connect(action, &QAction::triggered, this, &Backend::handleJumpListAction);

        menu->insertAction(firstAction, action);

        ++count;
    }

    if (count > 0) {
        menu->insertSeparator(firstAction);
    }
}

void Backend::addRecentDocumentActions(LegacyTaskManager::LauncherItem *launcher, LegacyTaskManager::BasicMenu* menu) const
{
    if (!launcher || !menu || !launcher->launcherUrl().isLocalFile()) {
        return;
    }

    QAction *firstAction = menu->actions().at(0);

    if (!firstAction) {
        return;
    }

    QString desktopName = launcher->launcherUrl().fileName();
    QString storageId = desktopName;

    if (storageId.startsWith("org.kde.")) {
        storageId = storageId.right(storageId.length() - 8);
    }

    if (storageId.endsWith(".desktop")) {
        storageId = storageId.left(storageId.length() - 8);
    }

    auto query = UsedResources
        | RecentlyUsedFirst
        | Agent(storageId)
        | Type::any()
        | Activity::current()
        | Url::file();

    ResultSet results(query);

    ResultSet::const_iterator resultIt;
    resultIt = results.begin();

    int actions = 0;

    while (actions < 5 && resultIt != results.end()) {
        const QString resource = (*resultIt).resource();
        const QUrl url(resource);

        if (!url.isValid()) {
            continue;
        }

        const KFileItem fileItem(url);

        if (!fileItem.isFile()) {
            continue;
        }

        if (actions == 0) {
            menu->insertSection(firstAction, i18n("Recent Documents"));
        }

        QAction *action = new QAction(menu);
        action->setText(url.fileName());
        action->setIcon(QIcon::fromTheme(fileItem.iconName(), QIcon::fromTheme("unknown")));
        action->setProperty("agent", storageId);
        action->setProperty("entryPath", launcher->launcherUrl());
        action->setData(resource);
        connect(action, &QAction::triggered, this, &Backend::handleRecentDocumentAction);

        menu->insertAction(firstAction, action);

        ++resultIt;
        ++actions;
    }

    if (actions > 0) {
        QAction *action = new QAction(menu);
        action->setText(i18n("Forget Recent Documents"));
        action->setProperty("agent", storageId);
        connect(action, &QAction::triggered, this, &Backend::handleRecentDocumentAction);
        menu->insertAction(firstAction, action);

        menu->insertSeparator(firstAction);
    }
}

void Backend::handleJumpListAction() const
{
    const QAction *action = qobject_cast<QAction* >(sender());

    if (!action) {
        return;
    }

    KRun::run(action->property("exec").toString(), {}, nullptr,
              action->property("applicationName").toString(),
              action->property("applicationIcon").toString());
}

void Backend::handleRecentDocumentAction() const
{
    const QAction *action = qobject_cast<QAction* >(sender());

    if (!action) {
        return;
    }

    const QString agent = action->property("agent").toString();

    if (agent.isEmpty()) {
        return;
    }

    const QString desktopPath = action->property("entryPath").toUrl().toLocalFile();
    const QString resource = action->data().toString();

    if (desktopPath.isEmpty() || resource.isEmpty()) {
        auto query = UsedResources
            | Agent(agent)
            | Type::any()
            | Activity::current()
            | Url::file();

        KAStats::forgetResources(query);

        return;
    }

    KService::Ptr service = KService::serviceByDesktopPath(desktopPath);

        qDebug() << service;

    if (!service) {
        return;
    }

    KRun::runService(*service, QList<QUrl>() << QUrl(resource), QApplication::activeWindow());
}

void Backend::itemHovered(int id, bool hovered)
{
    m_windowsToHighlight.clear();

    LegacyTaskManager::AbstractGroupableItem* item = m_groupManager->rootGroup()->getMemberById(id);

    if (item && hovered) {
        m_windowsToHighlight = QList<WId>::fromSet(item->winIds());
    }

    updateWindowHighlight();
}

void Backend::windowHovered(int winId, bool hovered)
{
    m_windowsToHighlight.clear();

    if (hovered) {
        m_windowsToHighlight.append(winId);
    }

    updateWindowHighlight();
}

void Backend::updateWindowHighlight()
{
    if (!m_highlightWindows) {
        if (m_panelWinId) {
            KWindowEffects::highlightWindows(m_panelWinId, QList<WId>());

            m_panelWinId = 0;
        }

        return;
    }

    if (m_taskManagerItem && m_taskManagerItem->window()) {
        m_panelWinId = m_taskManagerItem->window()->winId();
    } else {
        return;
    }

    QList<WId> windows = m_windowsToHighlight;

    if (windows.count() && m_toolTipItem && m_toolTipItem->window()) {
        windows.append(m_toolTipItem->window()->winId());
    }

    KWindowEffects::highlightWindows(m_panelWinId, windows);
}

void Backend::itemMove(int id, int newIndex)
{
    LegacyTaskManager::AbstractGroupableItem *item = m_groupManager->rootGroup()->getMemberById(id);

    if (item) {
        m_groupManager->manualSortingRequest(item, newIndex);
    }
}

void Backend::itemGeometryChanged(QQuickItem *item, int id)
{
    LegacyTaskManager:: AbstractGroupableItem *agItem = m_groupManager->rootGroup()->getMemberById(id);

    if (!item || !item->window() || !agItem || agItem->itemType() != LegacyTaskManager::TaskItemType)
    {
        return;
    }

    LegacyTaskManager::TaskItem *taskItem = static_cast<LegacyTaskManager::TaskItem *>(agItem);

    if (!taskItem->task()) {
        return;
    }

    QRect iconRect(item->x(), item->y(), item->width(), item->height());
    iconRect.moveTopLeft(item->parentItem()->mapToScene(iconRect.topLeft()).toPoint());
    iconRect.moveTopLeft(item->window()->mapToGlobal(iconRect.topLeft()));

    taskItem->task()->publishIconGeometry(iconRect);
}

bool Backend::canPresentWindows() const
{
    return (KWindowSystem::compositingActive() && KWindowEffects::isEffectAvailable(KWindowEffects::PresentWindowsGroup));
}

int Backend::numberOfDesktops() const
{
    return KWindowSystem::numberOfDesktops();
}

int Backend::numberOfActivities() const
{
    return m_activitiesConsumer->activities(KActivities::Info::State::Running).count();
}

void Backend::presentWindows(int groupParentId)
{
    LegacyTaskManager::AbstractGroupableItem *item = m_groupManager->rootGroup()->getMemberById(groupParentId);

    if (item && m_taskManagerItem && m_taskManagerItem->window()) {
        if (m_windowsToHighlight.count()) {
            m_windowsToHighlight.clear();
            updateWindowHighlight();
        }

        KWindowEffects::presentWindows(m_taskManagerItem->window()->winId(), QList<WId>::fromSet(item->winIds()));
    }
}

void Backend::urlDropped(const QUrl& url)
{
    if (!url.isValid() || !url.isLocalFile()) {
        return;
    }

    KDesktopFile desktopFile(url.toLocalFile());

    if (desktopFile.hasApplicationType()) {
        m_groupManager->addLauncher(url);
    }
}