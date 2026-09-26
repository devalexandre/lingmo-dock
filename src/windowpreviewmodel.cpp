/*
 * Copyright (C) 2026 LingmoOS Team.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "windowpreviewmodel.h"
#include "applicationmodel.h"
#include "xwindowinterface.h"

#include <KWindowInfo>
#include <KX11Extras>

// Refresh rate of the thumbnails while the popup is open
static const int RefreshInterval = 500;

WindowPreviewModel::WindowPreviewModel(ApplicationModel *apps, QObject *parent)
    : QAbstractListModel(parent)
    , m_apps(apps)
    , m_store(std::make_shared<WindowThumbnailStore>())
    , m_thumbnailSize(200, 120)
    , m_capturing(false)
    , m_activeWindow(KX11Extras::activeWindow())
    , m_refreshTimer(new QTimer(this))
{
    m_refreshTimer->setInterval(RefreshInterval);
    connect(m_refreshTimer, &QTimer::timeout, this, &WindowPreviewModel::captureAll);

    // The dock item's window list changes with the application model
    connect(m_apps, &QAbstractItemModel::dataChanged, this, &WindowPreviewModel::reload);
    connect(m_apps, &QAbstractItemModel::rowsInserted, this, &WindowPreviewModel::reload);
    connect(m_apps, &QAbstractItemModel::rowsRemoved, this, &WindowPreviewModel::reload);
    connect(m_apps, &QAbstractItemModel::modelReset, this, &WindowPreviewModel::reload);

    connect(KX11Extras::self(), &KX11Extras::windowChanged, this, &WindowPreviewModel::onWindowChanged);
    connect(KX11Extras::self(), &KX11Extras::activeWindowChanged, this, &WindowPreviewModel::onActiveWindowChanged);
    connect(KX11Extras::self(), &KX11Extras::windowRemoved, this, &WindowPreviewModel::onWindowRemoved);
    connect(KX11Extras::self(), &KX11Extras::currentDesktopChanged, this, [this] {
        for (int i = 0; i < m_entries.size(); ++i) {
            readInfo(m_entries[i]);
            emit dataChanged(index(i), index(i), { OnCurrentDesktopRole });
        }
    });
}

int WindowPreviewModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;

    return m_entries.size();
}

QHash<int, QByteArray> WindowPreviewModel::roleNames() const
{
    QHash<int, QByteArray> roles;
    roles[WindowIdRole] = "windowId";
    roles[TitleRole] = "title";
    roles[ThumbnailRole] = "thumbnail";
    roles[MinimizedRole] = "minimized";
    roles[ActiveRole] = "active";
    roles[OnCurrentDesktopRole] = "onCurrentDesktop";
    return roles;
}

QVariant WindowPreviewModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_entries.size())
        return QVariant();

    const Entry &entry = m_entries.at(index.row());

    switch (role) {
    case WindowIdRole:
        return entry.wid;
    case TitleRole:
        return entry.title;
    case ThumbnailRole:
        // Empty when there's no picture yet: the popup shows the icon instead
        if (!m_store->contains(entry.wid))
            return QString();
        return QStringLiteral("image://windowthumbnail/%1/%2").arg(entry.wid).arg(entry.serial);
    case MinimizedRole:
        return entry.minimized;
    case ActiveRole:
        return entry.wid == m_activeWindow;
    case OnCurrentDesktopRole:
        return entry.onCurrentDesktop;
    default:
        return QVariant();
    }
}

QString WindowPreviewModel::appId() const
{
    return m_appId;
}

void WindowPreviewModel::setAppId(const QString &appId)
{
    if (m_appId == appId)
        return;

    m_appId = appId;

    // Another dock item: its windows replace the old ones, all at once so the
    // count doesn't pass through zero on the way
    const int oldCount = m_entries.size();
    const QList<quint64> wids = m_appId.isEmpty() ? QList<quint64>() : m_apps->windowIds(m_appId);

    beginResetModel();
    m_entries.clear();
    for (quint64 wid : wids)
        m_entries.append(createEntry(wid));
    endResetModel();

    if (m_entries.size() != oldCount)
        emit countChanged();

    emit appIdChanged();
}

bool WindowPreviewModel::capturing() const
{
    return m_capturing;
}

void WindowPreviewModel::setCapturing(bool capturing)
{
    if (m_capturing == capturing)
        return;

    m_capturing = capturing;

    if (m_capturing) {
        captureAll();
        m_refreshTimer->start();
    } else {
        m_refreshTimer->stop();
    }

    emit capturingChanged();
}

QSize WindowPreviewModel::thumbnailSize() const
{
    return m_thumbnailSize;
}

void WindowPreviewModel::setThumbnailSize(const QSize &size)
{
    if (m_thumbnailSize == size || size.isEmpty())
        return;

    m_thumbnailSize = size;
    emit thumbnailSizeChanged();
}

int WindowPreviewModel::count() const
{
    return m_entries.size();
}

std::shared_ptr<WindowThumbnailStore> WindowPreviewModel::store() const
{
    return m_store;
}

void WindowPreviewModel::activate(quint64 wid)
{
    // KWin switches to the window's desktop and unminimizes it
    XWindowInterface::instance()->forceActiveWindow(wid);
}

void WindowPreviewModel::close(quint64 wid)
{
    XWindowInterface::instance()->closeWindow(wid);
}

void WindowPreviewModel::reload()
{
    const QList<quint64> wids = m_appId.isEmpty() ? QList<quint64>() : m_apps->windowIds(m_appId);
    const int oldCount = m_entries.size();

    // Gone windows leave, keeping the others' delegates (and pictures) in place
    for (int i = m_entries.size() - 1; i >= 0; --i) {
        if (!wids.contains(m_entries.at(i).wid)) {
            beginRemoveRows(QModelIndex(), i, i);
            m_entries.removeAt(i);
            endRemoveRows();
        }
    }

    // New windows are appended to the item's list, so they go to the end here too
    for (quint64 wid : wids) {
        if (indexOf(wid) != -1)
            continue;

        const Entry entry = createEntry(wid);

        beginInsertRows(QModelIndex(), m_entries.size(), m_entries.size());
        m_entries.append(entry);
        endInsertRows();
    }

    if (m_entries.size() != oldCount)
        emit countChanged();
}

WindowPreviewModel::Entry WindowPreviewModel::createEntry(quint64 wid)
{
    Entry entry;
    entry.wid = wid;
    readInfo(entry);

    if (m_capturing)
        capture(entry);

    return entry;
}

void WindowPreviewModel::readInfo(Entry &entry)
{
    const KWindowInfo info(entry.wid, NET::WMVisibleName | NET::WMName | NET::WMState | NET::XAWMState | NET::WMDesktop);

    entry.title = info.visibleName();
    if (entry.title.isEmpty())
        entry.title = info.name();
    entry.minimized = info.isMinimized();
    entry.onCurrentDesktop = info.isOnCurrentDesktop();
}

void WindowPreviewModel::captureAll()
{
    for (int i = 0; i < m_entries.size(); ++i) {
        if (capture(m_entries[i]))
            emit dataChanged(index(i), index(i), { ThumbnailRole });
    }
}

bool WindowPreviewModel::capture(Entry &entry)
{
    const QImage image = WindowThumbnail::grab(entry.wid, m_thumbnailSize);

    // Not mapped (minimized, another desktop): the last picture, if any, stays
    if (image.isNull())
        return false;

    m_store->insert(entry.wid, image);
    ++entry.serial;
    return true;
}

int WindowPreviewModel::indexOf(quint64 wid) const
{
    for (int i = 0; i < m_entries.size(); ++i) {
        if (m_entries.at(i).wid == wid)
            return i;
    }

    return -1;
}

void WindowPreviewModel::onWindowChanged(WId wid, NET::Properties properties, NET::Properties2 properties2)
{
    Q_UNUSED(properties2)

    const int i = indexOf(wid);
    if (i == -1)
        return;

    if (properties & (NET::WMVisibleName | NET::WMName | NET::WMState | NET::XAWMState | NET::WMDesktop)) {
        readInfo(m_entries[i]);
        emit dataChanged(index(i), index(i), { TitleRole, MinimizedRole, OnCurrentDesktopRole });
    }
}

void WindowPreviewModel::onActiveWindowChanged(WId wid)
{
    const quint64 previous = m_activeWindow;
    m_activeWindow = wid;

    // The window that loses focus is usually still on screen: keep a picture of it
    // for when it gets minimized or left on another desktop. Without a compositor
    // this would read the window that now covers it, so only with one.
    if (previous && !m_capturing && KX11Extras::compositingActive()
            && XWindowInterface::instance()->isAcceptableWindow(previous)) {
        const QImage image = WindowThumbnail::grab(previous, m_thumbnailSize);
        if (!image.isNull())
            m_store->insert(previous, image);
    }

    for (int i = 0; i < m_entries.size(); ++i) {
        if (m_entries.at(i).wid == previous || m_entries.at(i).wid == wid)
            emit dataChanged(index(i), index(i), { ActiveRole });
    }
}

void WindowPreviewModel::onWindowRemoved(WId wid)
{
    m_store->remove(wid);
}
