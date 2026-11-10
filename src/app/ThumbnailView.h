#pragma once

#include <QImage>
#include <QQuickPaintedItem>
#include <QUrl>
#include <QtQml/qqmlregistration.h>

namespace gs::app {

class ThumbnailView : public QQuickPaintedItem
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QImage image READ image WRITE setImage NOTIFY imageChanged FINAL)
    Q_PROPERTY(QUrl source READ source WRITE setSource NOTIFY sourceChanged FINAL)
    Q_PROPERTY(qreal radius READ radius WRITE setRadius NOTIFY radiusChanged FINAL)

public:
    explicit ThumbnailView(QQuickItem* parent = nullptr);

    [[nodiscard]] QImage image() const { return m_image; }
    void setImage(const QImage& image);
    [[nodiscard]] QUrl source() const { return m_source; }
    void setSource(const QUrl& source);
    [[nodiscard]] qreal radius() const { return m_radius; }
    void setRadius(qreal radius);

    void paint(QPainter* painter) override;

signals:
    void imageChanged();
    void sourceChanged();
    void radiusChanged();

private:
    QImage m_image;
    QUrl m_source;
    qreal m_radius = 0;
};

}
