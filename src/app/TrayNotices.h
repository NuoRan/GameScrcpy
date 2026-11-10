#pragma once

#include <QString>

#include <cstdint>
#include <functional>

namespace gs::app {

struct NoticeTarget {
    enum class Kind : std::uint8_t { Main, Phone, Task };

    Kind kind = Kind::Main;
    QString serial;
    QString task;

    friend bool operator==(const NoticeTarget&, const NoticeTarget&) = default;
};

class TrayNotices final
{
public:
    using Balloon = std::function<bool(const QString& title, const QString& text)>;

    bool show(const QString& title, const QString& text, NoticeTarget target, const Balloon& balloon);
    [[nodiscard]] const NoticeTarget& target() const noexcept { return m_target; }

private:
    NoticeTarget m_target;
};

}
