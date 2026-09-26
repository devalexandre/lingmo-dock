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

#ifndef WINDOWPREVIEWMODEL_H
#define WINDOWPREVIEWMODEL_H

#include <QAbstractListModel>
#include <QSize>
#include <QTimer>

#include <memory>

#include <NETWM>

#include "windowthumbnail.h"

class ApplicationModel;

// The windows of one dock item, with their thumbnails, for the preview popup.
// Thumbnails are only taken while `capturing` is on (the popup is open).
class WindowPreviewModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(QString appId READ appId WRITE setAppId NOTIFY appIdChanged)
    Q_PROPERTY(bool capturing READ capturing WRITE setCapturing NOTIFY capturingChanged)
    Q_PROPERTY(QSize thumbnailSize READ thumbnailSize WRITE setThumbnailSize NOTIFY thumbnailSizeChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Roles {
        WindowIdRole = Qt::UserRole + 1,
        TitleRole,
        ThumbnailRole,
        MinimizedRole,
        ActiveRole,
        OnCurrentDesktopRole
    };

    explicit WindowPreviewModel(ApplicationModel *apps, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QHash<int, QByteArray> roleNames() const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

    QString appId() const;
    void setAppId(const QString &appId);

    bool capturing() const;
    void setCapturing(bool capturing);

    QSize thumbnailSize() const;
    void setThumbnailSize(const QSize &size);

    int count() const;

    std::shared_ptr<WindowThumbnailStore> store() const;

    Q_INVOKABLE void activate(quint64 wid);
    Q_INVOKABLE void close(quint64 wid);

signals:
    void appIdChanged();
    void capturingChanged();
    void thumbnailSizeChanged();
    void countChanged();

private:
    struct Entry {
        quint64 wid = 0;
        QString title;
        bool minimized = false;
        bool onCurrentDesktop = true;
        int serial = 0;
    };

    void reload();
    Entry createEntry(quint64 wid);
    void readInfo(Entry &entry);
    void captureAll();
    bool capture(Entry &entry);
    int indexOf(quint64 wid) const;

    void onWindowChanged(WId wid, NET::Properties properties, NET::Properties2 properties2);
    void onActiveWindowChanged(WId wid);
    void onWindowRemoved(WId wid);

private:
    ApplicationModel *m_apps;
    std::shared_ptr<WindowThumbnailStore> m_store;
    QList<Entry> m_entries;
    QString m_appId;
    QSize m_thumbnailSize;
    bool m_capturing;
    quint64 m_activeWindow;
    QTimer *m_refreshTimer;
};

#endif // WINDOWPREVIEWMODEL_H
