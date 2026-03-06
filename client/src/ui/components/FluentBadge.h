/**
 * @file FluentBadge.h
 * @brief Fluent Focus 状态徽标
 */
#ifndef FLUENTBADGE_H
#define FLUENTBADGE_H

#include <QWidget>

namespace Fluent {

class FluentBadge : public QWidget
{
    Q_OBJECT
public:
    enum Status { Online, Offline, Streaming, Warning };
    Q_ENUM(Status)

    explicit FluentBadge(QWidget* parent = nullptr);
    void setStatus(Status s);
    void setText(const QString& t);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override { return {8, 8}; }

protected:
    void paintEvent(QPaintEvent*) override;

private:
    Status m_status = Offline;
    QString m_text;
};

} // namespace Fluent
#endif
