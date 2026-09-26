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

#include "windowthumbnail.h"

#include <QGuiApplication>
#include <QtGui/qguiapplication_platform.h>

#include <KX11Extras>

#include <xcb/xcb.h>
#include <xcb/composite.h>
#include <xcb/render.h>

#include <cstdlib>

namespace
{

template<typename T>
struct XcbReply
{
    explicit XcbReply(T *reply = nullptr) : data(reply) {}
    ~XcbReply() { free(data); }
    XcbReply(const XcbReply &) = delete;
    XcbReply &operator=(const XcbReply &) = delete;
    T *operator->() const { return data; }
    explicit operator bool() const { return data != nullptr; }
    T *data;
};

xcb_connection_t *connection()
{
    if (auto native = qApp->nativeInterface<QNativeInterface::QX11Application>())
        return native->connection();

    return nullptr;
}

bool checkRequest(xcb_connection_t *c, xcb_void_cookie_t cookie)
{
    xcb_generic_error_t *error = xcb_request_check(c, cookie);
    if (error) {
        free(error);
        return false;
    }

    return true;
}

xcb_render_fixed_t toFixed(double value)
{
    return static_cast<xcb_render_fixed_t>(value * 65536.0);
}

// What the server told us about XRender, asked for once
struct RenderInfo
{
    bool ready = false;
    xcb_render_pictformat_t argb32 = XCB_NONE;
    QHash<xcb_visualid_t, xcb_render_pictformat_t> visualFormats;
};

const RenderInfo &renderInfo(xcb_connection_t *c)
{
    static RenderInfo info;
    static bool queried = false;

    if (queried)
        return info;
    queried = true;

    const xcb_query_extension_reply_t *ext = xcb_get_extension_data(c, &xcb_render_id);
    if (!ext || !ext->present)
        return info;

    XcbReply<xcb_render_query_version_reply_t> version(
        xcb_render_query_version_reply(c, xcb_render_query_version(c, 0, 11), nullptr));
    XcbReply<xcb_render_query_pict_formats_reply_t> formats(
        xcb_render_query_pict_formats_reply(c, xcb_render_query_pict_formats(c), nullptr));
    if (!version || !formats)
        return info;

    for (auto it = xcb_render_query_pict_formats_formats_iterator(formats.data); it.rem; xcb_render_pictforminfo_next(&it)) {
        const xcb_render_pictforminfo_t *f = it.data;
        if (f->type == XCB_RENDER_PICT_TYPE_DIRECT && f->depth == 32
                && f->direct.alpha_mask == 0xff && f->direct.alpha_shift == 24
                && f->direct.red_mask == 0xff && f->direct.red_shift == 16
                && f->direct.green_mask == 0xff && f->direct.green_shift == 8
                && f->direct.blue_mask == 0xff && f->direct.blue_shift == 0) {
            info.argb32 = f->id;
            break;
        }
    }

    for (auto s = xcb_render_query_pict_formats_screens_iterator(formats.data); s.rem; xcb_render_pictscreen_next(&s)) {
        for (auto d = xcb_render_pictscreen_depths_iterator(s.data); d.rem; xcb_render_pictdepth_next(&d)) {
            for (auto v = xcb_render_pictdepth_visuals_iterator(d.data); v.rem; xcb_render_pictvisual_next(&v))
                info.visualFormats.insert(v.data->visual, v.data->format);
        }
    }

    info.ready = info.argb32 != XCB_NONE;
    return info;
}

bool compositeReady(xcb_connection_t *c)
{
    static int ready = -1;

    if (ready == -1) {
        ready = 0;
        const xcb_query_extension_reply_t *ext = xcb_get_extension_data(c, &xcb_composite_id);
        if (ext && ext->present) {
            XcbReply<xcb_composite_query_version_reply_t> version(
                xcb_composite_query_version_reply(c, xcb_composite_query_version(c, 0, 4), nullptr));
            // NameWindowPixmap arrived in 0.2
            ready = version && (version->major_version > 0 || version->minor_version >= 2);
        }
    }

    return ready == 1;
}

// The window manager's frame around the client: the child of the root window
xcb_window_t topLevel(xcb_connection_t *c, xcb_window_t window, xcb_window_t *root)
{
    xcb_window_t current = window;

    for (int depth = 0; depth < 16; ++depth) {
        XcbReply<xcb_query_tree_reply_t> tree(xcb_query_tree_reply(c, xcb_query_tree(c, current), nullptr));
        if (!tree)
            return XCB_WINDOW_NONE;

        *root = tree->root;
        if (tree->parent == tree->root || tree->parent == XCB_WINDOW_NONE)
            return current;

        current = tree->parent;
    }

    return XCB_WINDOW_NONE;
}

QImage imageFromReply(xcb_get_image_reply_t *reply, int width, int height)
{
    if (reply->depth != 24 && reply->depth != 32)
        return QImage();

    const int length = xcb_get_image_data_length(reply);
    if (length < width * height * 4)
        return QImage();

    const QImage::Format format = reply->depth == 32 ? QImage::Format_ARGB32_Premultiplied
                                                     : QImage::Format_RGB32;
    // Wraps the reply's buffer; copy() detaches it before the reply is freed
    return QImage(xcb_get_image_data(reply), width, height, width * 4, format).copy();
}

// Scales the drawable down on the server, then reads the small result
QImage grabScaled(xcb_connection_t *c, xcb_window_t root, xcb_drawable_t drawable, bool isWindow,
                  xcb_visualid_t visual, const QRect &source, const QSize &target)
{
    const RenderInfo &info = renderInfo(c);
    if (!info.ready || !info.visualFormats.contains(visual))
        return QImage();

    const uint32_t subwindowMode = XCB_SUBWINDOW_MODE_INCLUDE_INFERIORS;
    const xcb_render_picture_t sourcePicture = xcb_generate_id(c);
    if (!checkRequest(c, xcb_render_create_picture_checked(c, sourcePicture, drawable,
                                                             info.visualFormats.value(visual),
                                                             isWindow ? XCB_RENDER_CP_SUBWINDOW_MODE : 0,
                                                             isWindow ? &subwindowMode : nullptr)))
        return QImage();

    QImage result;
    const xcb_pixmap_t pixmap = xcb_generate_id(c);
    const xcb_render_picture_t picture = xcb_generate_id(c);

    if (checkRequest(c, xcb_create_pixmap_checked(c, 32, pixmap, root, target.width(), target.height()))) {
        if (checkRequest(c, xcb_render_create_picture_checked(c, picture, pixmap, info.argb32, 0, nullptr))) {
            // Maps each target pixel back to the source rectangle
            const xcb_render_transform_t transform = {
                toFixed(double(source.width()) / target.width()), 0, toFixed(source.x()),
                0, toFixed(double(source.height()) / target.height()), toFixed(source.y()),
                0, 0, toFixed(1)
            };
            xcb_render_set_picture_transform(c, sourcePicture, transform);
            static const char filter[] = "good";
            xcb_render_set_picture_filter(c, sourcePicture, sizeof(filter) - 1, filter, 0, nullptr);
            xcb_render_composite(c, XCB_RENDER_PICT_OP_SRC, sourcePicture, XCB_NONE, picture,
                                 0, 0, 0, 0, 0, 0, target.width(), target.height());

            XcbReply<xcb_get_image_reply_t> reply(
                xcb_get_image_reply(c, xcb_get_image(c, XCB_IMAGE_FORMAT_Z_PIXMAP, pixmap, 0, 0,
                                                     target.width(), target.height(), ~0u), nullptr));
            if (reply)
                result = imageFromReply(reply.data, target.width(), target.height());

            xcb_render_free_picture(c, picture);
        }
        xcb_free_pixmap(c, pixmap);
    }

    xcb_render_free_picture(c, sourcePicture);
    return result;
}

// No XRender: reads the whole window and scales it here
QImage grabFull(xcb_connection_t *c, xcb_drawable_t drawable, const QRect &source)
{
    XcbReply<xcb_get_image_reply_t> reply(
        xcb_get_image_reply(c, xcb_get_image(c, XCB_IMAGE_FORMAT_Z_PIXMAP, drawable,
                                             source.x(), source.y(),
                                             source.width(), source.height(), ~0u), nullptr));
    if (!reply)
        return QImage();

    return imageFromReply(reply.data, source.width(), source.height());
}

} // namespace

QImage WindowThumbnail::grab(quint64 wid, const QSize &size)
{
    xcb_connection_t *c = connection();
    if (!c || size.isEmpty())
        return QImage();

    // The pixels are read as 32 bit little endian words
    if (xcb_get_setup(c)->image_byte_order != XCB_IMAGE_ORDER_LSB_FIRST)
        return QImage();

    const xcb_window_t window = static_cast<xcb_window_t>(wid);

    XcbReply<xcb_get_window_attributes_reply_t> attributes(
        xcb_get_window_attributes_reply(c, xcb_get_window_attributes(c, window), nullptr));
    XcbReply<xcb_get_geometry_reply_t> geometry(
        xcb_get_geometry_reply(c, xcb_get_geometry(c, window), nullptr));

    // Minimized or on another desktop: KWin has unmapped it, there is nothing to read
    if (!attributes || !geometry || attributes->map_state != XCB_MAP_STATE_VIEWABLE)
        return QImage();

    xcb_window_t root = XCB_WINDOW_NONE;
    const xcb_window_t frame = topLevel(c, window, &root);
    if (frame == XCB_WINDOW_NONE)
        return QImage();

    QRect source(0, 0, geometry->width, geometry->height);
    xcb_drawable_t drawable = window;
    xcb_visualid_t visual = attributes->visual;
    bool isWindow = true;
    xcb_pixmap_t framePixmap = XCB_NONE;

    // The compositor redirects the frames; name the frame's storage and read the
    // client's rectangle out of it, the decoration around it is drawn by KWin itself
    if (KX11Extras::compositingActive() && compositeReady(c)) {
        XcbReply<xcb_get_window_attributes_reply_t> frameAttributes(
            xcb_get_window_attributes_reply(c, xcb_get_window_attributes(c, frame), nullptr));
        XcbReply<xcb_translate_coordinates_reply_t> offset(
            xcb_translate_coordinates_reply(c, xcb_translate_coordinates(c, window, frame, 0, 0), nullptr));

        if (frameAttributes && offset) {
            framePixmap = xcb_generate_id(c);
            if (checkRequest(c, xcb_composite_name_window_pixmap_checked(c, frame, framePixmap))) {
                drawable = framePixmap;
                visual = frameAttributes->visual;
                isWindow = false;
                source.moveTo(offset->dst_x, offset->dst_y);
            } else {
                framePixmap = XCB_NONE;
            }
        }
    }

    QSize target = source.size().scaled(size, Qt::KeepAspectRatio);
    if (target.width() > source.width() || target.height() > source.height())
        target = source.size();
    target = target.expandedTo(QSize(1, 1));

    // The server only filters bilinearly, which turns text to noise at small sizes:
    // it scales to twice the size and the last halving is done smoothly here
    const QSize serverTarget = (target * 2).boundedTo(source.size());

    QImage image = grabScaled(c, root, drawable, isWindow, visual, source, serverTarget);
    if (image.isNull())
        image = grabFull(c, drawable, source);

    if (framePixmap != XCB_NONE)
        xcb_free_pixmap(c, framePixmap);

    if (image.isNull())
        return image;

    if (image.size() != target)
        image = image.scaled(target, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);

    return image;
}

QImage WindowThumbnailStore::image(quint64 wid) const
{
    QMutexLocker locker(&m_mutex);
    return m_images.value(wid);
}

bool WindowThumbnailStore::contains(quint64 wid) const
{
    QMutexLocker locker(&m_mutex);
    return m_images.contains(wid);
}

void WindowThumbnailStore::insert(quint64 wid, const QImage &image)
{
    QMutexLocker locker(&m_mutex);
    m_images.insert(wid, image);
}

void WindowThumbnailStore::remove(quint64 wid)
{
    QMutexLocker locker(&m_mutex);
    m_images.remove(wid);
}

WindowThumbnailProvider::WindowThumbnailProvider(std::shared_ptr<WindowThumbnailStore> store)
    : QQuickImageProvider(QQuickImageProvider::Image)
    , m_store(std::move(store))
{
}

QImage WindowThumbnailProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize)
{
    Q_UNUSED(requestedSize)

    const quint64 wid = id.section(QLatin1Char('/'), 0, 0).toULongLong();
    const QImage image = m_store->image(wid);

    if (size)
        *size = image.size();

    return image;
}
