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

#ifndef WINDOWTHUMBNAIL_H
#define WINDOWTHUMBNAIL_H

#include <QHash>
#include <QImage>
#include <QMutex>
#include <QQuickImageProvider>

#include <memory>

// Takes a small picture of an X11 window.
//
// With a compositor (KWin) the window's off-screen storage is read through
// XComposite, so the picture is right even when other windows cover it.
// Without one the window is read directly, which only shows its visible parts.
// XRender scales the picture down on the X server, so only a small image
// travels to the dock. Unmapped windows (minimized, on another desktop)
// have nothing to read and give a null image.
namespace WindowThumbnail
{
QImage grab(quint64 wid, const QSize &size);
}

// Last thumbnail taken of each window, shared between the model that takes
// them and the image provider that hands them to QML.
class WindowThumbnailStore
{
public:
    QImage image(quint64 wid) const;
    bool contains(quint64 wid) const;
    void insert(quint64 wid, const QImage &image);
    void remove(quint64 wid);

private:
    mutable QMutex m_mutex;
    QHash<quint64, QImage> m_images;
};

// image://windowthumbnail/<wid>/<serial>: the serial only changes the url so
// QML reloads the picture after each new capture.
class WindowThumbnailProvider : public QQuickImageProvider
{
public:
    explicit WindowThumbnailProvider(std::shared_ptr<WindowThumbnailStore> store);

    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;

private:
    std::shared_ptr<WindowThumbnailStore> m_store;
};

#endif // WINDOWTHUMBNAIL_H
