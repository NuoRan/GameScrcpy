#pragma once

#include "gs/render/FrameSlot.h"
#include "gs/render/VideoFilter.h"

#include <QQuickItem>
#include <QRectF>
#include <QSize>
#include <QtQml/qqmlregistration.h>

#include <memory>
#include <vector>

namespace gs::render {

class VideoItem : public QQuickItem
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QRectF contentRect READ contentRect NOTIFY contentRectChanged FINAL)
    Q_PROPERTY(qreal sharpness READ sharpness WRITE setSharpness NOTIFY sharpnessChanged FINAL)
    Q_PROPERTY(bool frozen READ frozen WRITE setFrozen NOTIFY frozenChanged FINAL)
    Q_PROPERTY(QRectF crop READ crop WRITE setCrop NOTIFY cropChanged FINAL)

public:
    explicit VideoItem(QQuickItem* parent = nullptr);
    ~VideoItem() override;

    void setSource(std::shared_ptr<FrameSlot> slot);

    [[nodiscard]] QRectF contentRect() const { return m_contentRect; }
    [[nodiscard]] QSize frameSize() const;
    [[nodiscard]] QSize fullFrameSize() const { return m_frameSize; }
    [[nodiscard]] QRectF crop() const { return m_crop; }
    void setCrop(const QRectF& crop);
    [[nodiscard]] qreal sharpness() const { return m_sharpness; }
    void setSharpness(qreal sharpness);
    [[nodiscard]] bool frozen() const { return m_frozen; }
    void setFrozen(bool frozen);
    void setFilters(std::vector<VideoFilter> filters);
    [[nodiscard]] const std::vector<FilterStatus>& filterStatus() const { return m_filterStatus; }
    void setImmediate(bool immediate);
    void setPlaceholderSize(QSize size);

signals:
    void contentRectChanged();
    void sharpnessChanged();
    void frozenChanged();
    void filterStatusChanged();
    void cropChanged();

protected:
    QSGNode* updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData* data) override;
    void geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) override;
    void itemChange(ItemChange change, const ItemChangeData& value) override;

private:
    void updateContentRect();
    void followDisplay();

    std::shared_ptr<FrameSlot> m_slot;
    QSize m_frameSize;
    QSize m_placeholder;
    QRectF m_contentRect;
    qreal m_sharpness = 0;
    bool m_frozen = false;
    bool m_immediate = false;
    std::vector<VideoFilter> m_filters;
    bool m_filtersChanged = false;
    std::vector<FilterStatus> m_filterStatus;
    QMetaObject::Connection m_screenWatch;
    QRectF m_crop{0.0, 0.0, 1.0, 1.0};
};

}
