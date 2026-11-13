#include "gs/render/VideoItem.h"

#include "VideoGeometry.h"
#include "VideoRenderNode.h"

#include <QMetaObject>
#include <QQuickWindow>
#include <QScreen>
#include <QThread>

#include <algorithm>
#include <cmath>
#include <utility>

namespace gs::render {

QSize VideoItem::frameSize() const
{
    return m_frameSize.isEmpty() ? m_placeholder : croppedSize(m_frameSize, m_crop);
}

void VideoItem::setCrop(const QRectF& crop)
{
    const QRectF next = normalCrop(crop);
    if (next == m_crop) {
        return;
    }
    m_crop = next;
    emit cropChanged();
    updateContentRect();
    update();
}

VideoItem::VideoItem(QQuickItem* parent) : QQuickItem(parent)
{
    setFlag(ItemHasContents, true);
}

VideoItem::~VideoItem()
{
    if (m_slot) {
        m_slot->setWaker({});
    }
}

void VideoItem::setSource(std::shared_ptr<FrameSlot> slot)
{
    if (m_slot) {
        m_slot->setWaker({});
    }
    m_slot = std::move(slot);
    if (m_slot) {
        m_slot->setWaker([this] { QMetaObject::invokeMethod(this, [this] { update(); }, Qt::QueuedConnection); });
    }
    followDisplay();
    update();
}

void VideoItem::setImmediate(bool immediate)
{
    if (immediate == m_immediate) {
        return;
    }
    m_immediate = immediate;
    followDisplay();
}

void VideoItem::setPlaceholderSize(QSize size)
{
    if (size == m_placeholder) {
        return;
    }
    m_placeholder = size;
    updateContentRect();
    update();
}

void VideoItem::itemChange(ItemChange change, const ItemChangeData& value)
{
    QQuickItem::itemChange(change, value);
    if (change != ItemSceneChange) {
        return;
    }
    disconnect(m_screenWatch);
    if (value.window != nullptr) {
        m_screenWatch = connect(value.window, &QWindow::screenChanged, this, &VideoItem::followDisplay);
    }
    followDisplay();
}

void VideoItem::followDisplay()
{
    if (!m_slot) {
        return;
    }
    if (m_immediate) {
        m_slot->setDisplayIntervalUs(0);
        return;
    }
    const QScreen* screen = window() != nullptr ? window()->screen() : nullptr;
    const qreal hertz = screen != nullptr ? screen->refreshRate() : 0;
    m_slot->setDisplayIntervalUs(hertz > 1 ? static_cast<std::int64_t>(std::llround(1'000'000.0 / hertz)) : -1);
}

void VideoItem::setSharpness(qreal sharpness)
{
    const qreal clamped = std::isnan(sharpness) ? 0.0 : std::clamp(sharpness, 0.0, 1.0);
    if (clamped == m_sharpness) {
        return;
    }
    m_sharpness = clamped;
    emit sharpnessChanged();
    update();
}

void VideoItem::setFrozen(bool frozen)
{
    if (frozen == m_frozen) {
        return;
    }
    m_frozen = frozen;
    emit frozenChanged();
    update();
}

void VideoItem::setFilters(std::vector<VideoFilter> filters)
{
    if (filters == m_filters) {
        return;
    }
    m_filters = std::move(filters);
    m_filtersChanged = true;
    update();
}

QSGNode* VideoItem::updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData*)
{
    auto* node = static_cast<VideoRenderNode*>(oldNode);
    if (node == nullptr) {
        node = new VideoRenderNode(window());
        m_filtersChanged = true;
        if (QThread* renderer = QThread::currentThread(); renderer != thread()) {
            renderer->setPriority(QThread::HighPriority);
        }
    }
    if (m_filtersChanged) {
        m_filtersChanged = false;
        node->setFilters(m_filters);
        QMetaObject::invokeMethod(this, [this] { update(); }, Qt::QueuedConnection);
    }
    if (std::vector<FilterStatus> status; node->takeFilterStatus(status)) {
        QMetaObject::invokeMethod(
            this,
            [this, status = std::move(status)]() mutable {
                m_filterStatus = std::move(status);
                emit filterStatusChanged();
            },
            Qt::QueuedConnection);
    }
    node->setSource(m_slot, m_frozen);
    if (const QSize size = node->frameSize(); size != m_frameSize) {
        m_frameSize = size;
        QMetaObject::invokeMethod(this, &VideoItem::updateContentRect, Qt::QueuedConnection);
    }
    node->setBounds(boundingRect());
    node->setCrop(m_crop);
    node->setSharpness(static_cast<float>(m_sharpness));
    node->markDirty(QSGNode::DirtyMaterial);
    return node;
}

void VideoItem::geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry)
{
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    updateContentRect();
    update();
}

void VideoItem::updateContentRect()
{
    const QRectF rect = fitted(frameSize(), boundingRect());
    if (rect != m_contentRect) {
        m_contentRect = rect;
        emit contentRectChanged();
    }
}

}
