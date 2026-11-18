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
#ifndef SEEK_PREVIEW_HPP
#define SEEK_PREVIEW_HPP

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <QQuickPaintedItem>
#include <QImage>
#include <QHash>
#include <QUrl>

#include <vlc_common.h>

#include "maininterface/mainctx.hpp"

typedef struct vlc_preparser_t vlc_preparser_t;
typedef struct vlc_preparser_req vlc_preparser_req;

/**
 * Seek bar hover preview (YouTube-like).
 *
 * Generates small thumbnails of the media being played at a given position
 * using a dedicated thumbnailer (it opens the file separately, so playback is
 * never disturbed). At most one thumbnail is generated at a time: while one is
 * being made, only the latest requested position is remembered. Generated
 * thumbnails are cached per media, bucketed along the timeline.
 */
class SeekPreview : public QQuickPaintedItem
{
    Q_OBJECT

    Q_PROPERTY(MainCtx* ctx READ getCtx WRITE setCtx NOTIFY ctxChanged FINAL)
    // media to preview, typically Player.url
    Q_PROPERTY(QUrl source READ source WRITE setSource NOTIFY sourceChanged FINAL)
    // false for media that can't be previewed (non local files)
    Q_PROPERTY(bool available READ available NOTIFY sourceChanged FINAL)
    Q_PROPERTY(bool hasImage READ hasImage NOTIFY imageChanged FINAL)
    // width / height of the current thumbnail (16/9 until known)
    Q_PROPERTY(qreal aspectRatio READ aspectRatio NOTIFY imageChanged FINAL)
    Q_PROPERTY(qreal radius MEMBER m_radius NOTIFY radiusChanged FINAL)

public:
    explicit SeekPreview(QQuickItem* parent = nullptr);
    ~SeekPreview() override;

    MainCtx* getCtx() const { return m_ctx; }
    void setCtx(MainCtx* ctx);

    QUrl source() const { return m_source; }
    void setSource(const QUrl& source);

    bool available() const;
    bool hasImage() const { return !m_image.isNull(); }
    qreal aspectRatio() const;

    void paint(QPainter* painter) override;

    // request the thumbnail at position (0..1) of the media
    Q_INVOKABLE void requestAt(double position);

signals:
    void ctxChanged();
    void sourceChanged();
    void imageChanged();
    void radiusChanged();

private:
    static constexpr int BUCKETS = 300;  // timeline granularity of the cache
    static constexpr int MAX_CACHE = 400;
    static constexpr int THUMB_WIDTH = 320;

    bool ensurePreparser();
    void submit(int bucket);
    void onThumbnail(vlc_preparser_req* req, int bucket, QImage image);
    void setImage(const QImage& image);
    void reset();

    static void onEnded(vlc_preparser_req* req, int status, picture_t* thumbnail, void* data);

    MainCtx* m_ctx = nullptr;
    QUrl m_source;
    vlc_preparser_t* m_preparser = nullptr;
    vlc_preparser_req* m_req = nullptr;    // in-flight request (GUI thread)
    int m_inflightBucket = -1;              // read by the thumbnailer callback
    int m_wantedBucket = -1;
    unsigned m_generation = 0;              // bumped when source changes
    QHash<int, QImage> m_cache;
    QImage m_image;
    qreal m_radius = 0.;
};

#endif // SEEK_PREVIEW_HPP
