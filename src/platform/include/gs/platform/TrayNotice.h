#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace gs::platform {

struct TrayMenuItem {
    std::wstring text;
    std::function<void()> chosen;
};

class TrayNotice
{
public:
    explicit TrayNotice(std::function<void()> clicked);
    ~TrayNotice();
    TrayNotice(const TrayNotice&) = delete;
    TrayNotice& operator=(const TrayNotice&) = delete;

    bool show(const std::wstring& title, const std::wstring& text);
    bool showIcon();
    void setActivated(std::function<void()> activated);
    void setMenu(std::vector<TrayMenuItem> items);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

}
