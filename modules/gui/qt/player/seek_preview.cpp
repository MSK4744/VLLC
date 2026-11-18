/*****************************************************************************
 * Copyright (C) 2026 VLC authors and VideoLAN
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * ( at your option ) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/
#include "seek_preview.hpp"

#include <QPainter>
#include <QPainterPath>

#include <vlc_preparser.h>
#include <vlc_input_item.h>
#include <vlc_picture.h>
#include <vlc_block.h>
#include <vlc_fourcc.h>

#include "qt.hpp"
#include "maininterface/mainctx.hpp"

namespace {

struct RequestCtx
{
    SeekPreview* self;
    vlc_object_t* obj;
    int bucket;
    unsigned generation;
};

} // namespace

SeekPreview::SeekPreview(QQuickItem* parent)
    : QQuickPaintedItem(parent)
{
    setAntialiasing(true);
    setOpaquePainting(false);
}

SeekPreview::~SeekPreview()
{
    // cancels and joins pending requests: no callback can run afterwards
    if (m_preparser)
        vlc_preparser_Delete(m_preparser);
    if (m_req)
        vlc_preparser_req_Release(m_req);
}

void SeekPreview::setCtx(MainCtx* ctx)
{
    if (m_ctx == ctx)
        return;
    m_ctx = ctx;
    emit ctxChanged();
}

void SeekPreview::setSource(const QUrl& source)
{
    if (m_source == source)
        return;
    m_source = source;
    reset();
    emit sourceChanged();
}

bool SeekPreview::available() const
{
    static const bool disabled = qEnvironmentVariableIsSet("VLLC_SEEK_PREVIEW_DISABLE");
    // only local files: opening network streams a second time is costly
    return !disabled && m_source.isValid() && m_source.isLocalFile();
}

qreal SeekPreview::aspectRatio() const
{
    if (m_image.isNull() || m_image.height() == 0)
        return 16. / 9.;
    return qreal(m_image.width()) / m_image.height();
}

void SeekPreview::reset()
{
    ++m_generation;
    m_cache.clear();
    m_wantedBucket = -1;
    if (m_preparser && m_req)
        vlc_preparser_Cancel(m_preparser, m_req); // its callback will still come
    setImage(QImage());
}

bool SeekPreview::ensurePreparser()
{
    if (m_preparser)
        return true;
    if (!m_ctx || !m_ctx->getIntf())
        return false;

    vlc_preparser_cfg cfg = {};
    cfg.types = VLC_PREPARSER_TYPE_THUMBNAIL;
    cfg.max_thumbnailer_threads = 1;
    cfg.timeout = VLC_TICK_FROM_SEC(5);
    cfg.external_process = false;
    m_preparser = vlc_preparser_New(VLC_OBJECT(m_ctx->getIntf()), &cfg);
    return m_preparser != nullptr;
}

void SeekPreview::requestAt(double position)
{
    if (!available())
        return;

    const int bucket = qBound(0, int(position * BUCKETS + 0.5), BUCKETS);
    m_wantedBucket = bucket;

    const auto it = m_cache.constFind(bucket);
    if (it != m_cache.constEnd())
    {
        setImage(*it);
        return;
    }

    if (!m_req) // nothing in flight, otherwise picked up when it completes
        submit(bucket);
}

void SeekPreview::submit(int bucket)
{
    assert(!m_req);
    if (!ensurePreparser())
        return;

    const QByteArray uri = m_source.toString(QUrl::FullyEncoded).toUtf8();
    input_item_t* item = input_item_New(uri.constData(), nullptr);
    if (!item)
        return;

    vlc_thumbnailer_arg arg = {};
    arg.seek.type = vlc_thumbnailer_arg::seek::VLC_THUMBNAILER_SEEK_POS;
    arg.seek.pos = double(bucket) / BUCKETS;
    arg.seek.speed = vlc_thumbnailer_arg::seek::VLC_THUMBNAILER_SEEK_FAST;
    arg.hw_dec = false;

    static const vlc_thumbnailer_cbs cbs = { &SeekPreview::onEnded };

    auto ctx = new RequestCtx{ this, VLC_OBJECT(m_ctx->getIntf()), bucket, m_generation };
    vlc_preparser_req* req = vlc_preparser_req_NewThumbnail(m_preparser, item, &arg, &cbs, ctx);
    input_item_Release(item);
    if (!req)
    {
        delete ctx;
        return;
    }

    m_req = req;
    m_inflightBucket = bucket;
    if (vlc_preparser_Submit(m_preparser, req) != VLC_SUCCESS)
    {
        m_req = nullptr;
        m_inflightBucket = -1;
        vlc_preparser_req_Release(req);
        delete ctx;
    }
}

// thumbnailer thread
void SeekPreview::onEnded(vlc_preparser_req* req, int status, picture_t* thumbnail, void* data)
{
    auto ctx = static_cast<RequestCtx*>(data);
    SeekPreview* self = ctx->self;
    const int bucket = ctx->bucket;
    const unsigned generation = ctx->generation;

    QImage image;
    if (status == VLC_SUCCESS && thumbnail)
    {
        block_t* block = nullptr;
        video_format_t fmt;
        if (picture_Export(ctx->obj, &block, &fmt, thumbnail, VLC_CODEC_PNG,
                           THUMB_WIDTH, 0, false) == VLC_SUCCESS && block)
        {
            image.loadFromData(block->p_buffer, int(block->i_buffer), "PNG");
            block_Release(block);

            // debugging aid: VLLC_SEEK_PREVIEW_DUMP=<dir> saves every thumbnail
            static const QString dumpDir = qEnvironmentVariable("VLLC_SEEK_PREVIEW_DUMP");
            if (!dumpDir.isEmpty() && !image.isNull())
                image.save(QStringLiteral("%1/thumb_%2_%3x%4_src%5x%6.png").arg(dumpDir).arg(bucket)
                               .arg(image.width()).arg(image.height())
                               .arg(thumbnail->format.i_visible_width).arg(thumbnail->format.i_visible_height));
        }
    }
    delete ctx;

    QMetaObject::invokeMethod(self, [self, req, bucket, generation, image = std::move(image)]() {
        if (generation != self->m_generation)
        {
            // stale result from a previous media
            if (self->m_req == req)
                self->m_req = nullptr;
            vlc_preparser_req_Release(req);
            if (!self->m_req && self->m_wantedBucket >= 0
                && !self->m_cache.contains(self->m_wantedBucket))
                self->submit(self->m_wantedBucket);
            return;
        }
        self->onThumbnail(req, bucket, image);
    }, Qt::QueuedConnection);
}

void SeekPreview::onThumbnail(vlc_preparser_req* req, int bucket, QImage image)
{
    if (m_req == req)
        m_req = nullptr;
    m_inflightBucket = -1;
    vlc_preparser_req_Release(req);

    if (!image.isNull())
    {
        if (m_cache.size() >= MAX_CACHE)
            m_cache.clear();
        m_cache.insert(bucket, image);
        // show it only if the user is still around this position
        if (m_wantedBucket < 0 || qAbs(m_wantedBucket - bucket) <= 2 || m_image.isNull())
            setImage(image);
    }

    // catch up with the latest requested position
    if (m_wantedBucket >= 0 && m_wantedBucket != bucket)
    {
        const auto it = m_cache.constFind(m_wantedBucket);
        if (it != m_cache.constEnd())
            setImage(*it);
        else
            submit(m_wantedBucket);
    }
}

void SeekPreview::setImage(const QImage& image)
{
    if (image.isNull() && m_image.isNull())
        return;
    m_image = image;
    emit imageChanged();
    update();
}

void SeekPreview::paint(QPainter* painter)
{
    if (m_image.isNull())
        return;

    const QRectF target = boundingRect();
    painter->setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter->setRenderHint(QPainter::Antialiasing, true);
    if (m_radius > 0.)
    {
        QPainterPath path;
        path.addRoundedRect(target, m_radius, m_radius);
        painter->setClipPath(path);
    }
    painter->drawImage(target, m_image);
}
