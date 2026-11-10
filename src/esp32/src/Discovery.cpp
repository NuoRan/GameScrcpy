#include "gs/esp32/Discovery.h"

#include <windows.h>
#include <windns.h>

#include <algorithm>
#include <condition_variable>
#include <format>
#include <memory>
#include <mutex>
#include <optional>

namespace gs::esp32 {

namespace {

constexpr std::chrono::milliseconds kSettle{500};
constexpr std::chrono::milliseconds kResolveTimeout{1500};
constexpr std::size_t kMostInstances = 16;

std::string narrow(const wchar_t* text)
{
    if (text == nullptr || *text == L'\0') {
        return {};
    }
    const int size = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    if (size <= 1) {
        return {};
    }
    std::string out(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, -1, out.data(), size, nullptr, nullptr);
    out.resize(static_cast<std::size_t>(size - 1));
    return out;
}

struct BrowseContext {
    std::mutex mutex;
    std::condition_variable_any changed;
    std::vector<std::wstring> instances;
    bool finished = false;
};

struct ResolveContext {
    std::mutex mutex;
    std::condition_variable_any changed;
    std::optional<FoundBoard> board;
    bool finished = false;
};

VOID WINAPI onBrowse(DWORD status, PVOID context, PDNS_RECORD records)
{
    auto* browse = static_cast<BrowseContext*>(context);
    {
        const std::lock_guard lock(browse->mutex);
        if (status == ERROR_SUCCESS) {
            for (PDNS_RECORD record = records; record != nullptr; record = record->pNext) {
                if (record->wType != DNS_TYPE_PTR || record->Data.PTR.pNameHost == nullptr) {
                    continue;
                }
                std::wstring name = record->Data.PTR.pNameHost;
                if (std::ranges::find(browse->instances, name) == browse->instances.end() &&
                    browse->instances.size() < kMostInstances) {
                    browse->instances.push_back(std::move(name));
                }
            }
        } else {
            browse->finished = true;
        }
    }
    browse->changed.notify_all();
    if (records != nullptr) {
        DnsRecordListFree(records, DnsFreeRecordList);
    }
}

VOID WINAPI onResolve(DWORD status, PVOID context, PDNS_SERVICE_INSTANCE instance)
{
    auto* resolve = static_cast<ResolveContext*>(context);
    {
        const std::lock_guard lock(resolve->mutex);
        if (status == ERROR_SUCCESS && instance != nullptr) {
            FoundBoard board;
            board.instance = narrow(instance->pszInstanceName);
            board.host = narrow(instance->pszHostName);
            board.port = instance->wPort;
            if (instance->ip4Address != nullptr) {
                const auto* bytes = reinterpret_cast<const std::uint8_t*>(instance->ip4Address);
                board.address = std::format("{}.{}.{}.{}", bytes[0], bytes[1], bytes[2], bytes[3]);
            }
            for (DWORD i = 0; i < instance->dwPropertyCount; ++i) {
                const std::string key = narrow(instance->keys[i]);
                const std::string value = narrow(instance->values[i]);
                if (key == "id") {
                    board.id = value;
                } else if (key == "name") {
                    board.name = value;
                } else if (key == "chip") {
                    board.chip = value;
                }
            }
            resolve->board = std::move(board);
        }
        resolve->finished = true;
    }
    resolve->changed.notify_all();
    if (instance != nullptr) {
        DnsServiceFreeInstance(instance);
    }
}

std::optional<FoundBoard> resolveInstance(const std::wstring& name, const std::stop_token& stop)
{
    auto context = std::make_unique<ResolveContext>();
    std::wstring query = name;
    DNS_SERVICE_RESOLVE_REQUEST request{};
    request.Version = DNS_QUERY_REQUEST_VERSION1;
    request.InterfaceIndex = 0;
    request.QueryName = query.data();
    request.pResolveCompletionCallback = &onResolve;
    request.pQueryContext = context.get();
    DNS_SERVICE_CANCEL cancel{};
    if (DnsServiceResolve(&request, &cancel) != DNS_REQUEST_PENDING) {
        return std::nullopt;
    }
    std::unique_lock lock(context->mutex);
    if (!context->changed.wait_for(lock, stop, kResolveTimeout, [&context] { return context->finished; })) {
        lock.unlock();
        DnsServiceResolveCancel(&cancel);
        lock.lock();
        if (!context->changed.wait_for(lock, kSettle, [&context] { return context->finished; })) {
            lock.unlock();
            (void)context.release();
            return std::nullopt;
        }
    }
    return context->board;
}

}

std::vector<FoundBoard> browseBoards(std::chrono::milliseconds duration, const std::stop_token& stop)
{
    auto context = std::make_unique<BrowseContext>();
    const std::wstring query(kMdnsService);
    DNS_SERVICE_BROWSE_REQUEST request{};
    request.Version = DNS_QUERY_REQUEST_VERSION1;
    request.InterfaceIndex = 0;
    request.QueryName = query.c_str();
    request.pBrowseCallback = &onBrowse;
    request.pQueryContext = context.get();
    DNS_SERVICE_CANCEL cancel{};
    if (DnsServiceBrowse(&request, &cancel) != DNS_REQUEST_PENDING) {
        return {};
    }
    std::vector<std::wstring> instances;
    {
        std::unique_lock lock(context->mutex);
        context->changed.wait_for(lock, stop, duration, [&context] { return context->finished; });
    }
    DnsServiceBrowseCancel(&cancel);
    {
        std::unique_lock lock(context->mutex);
        const bool finished = context->changed.wait_for(lock, kSettle, [&context] { return context->finished; });
        instances = context->instances;
        if (!finished) {
            lock.unlock();
            (void)context.release();
        }
    }
    std::vector<FoundBoard> boards;
    for (const std::wstring& instance : instances) {
        if (stop.stop_requested()) {
            break;
        }
        std::optional<FoundBoard> board = resolveInstance(instance, stop);
        if (board && !board->address.empty() && board->port != 0) {
            boards.push_back(std::move(*board));
        }
    }
    return boards;
}

}
