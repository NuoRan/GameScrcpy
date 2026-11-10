#include "TrayNotices.h"

#include <utility>

namespace gs::app {

bool TrayNotices::show(const QString& title, const QString& text, NoticeTarget target, const Balloon& balloon)
{
    if (!balloon || !balloon(title, text)) {
        return false;
    }
    m_target = std::move(target);
    return true;
}

}
