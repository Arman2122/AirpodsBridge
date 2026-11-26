// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#pragma once

#ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
    #define NOMINMAX
#endif

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <setupapi.h>
#include <winusb.h>

#include <cstdint>
#include <string>
#include <utility>

namespace Service::Win {

class UniqueHandle
{
public:
    UniqueHandle() = default;
    explicit UniqueHandle(HANDLE h) noexcept : _h{h} {}
    ~UniqueHandle()
    {
        Reset();
    }

    UniqueHandle(const UniqueHandle &) = delete;
    UniqueHandle &operator=(const UniqueHandle &) = delete;

    UniqueHandle(UniqueHandle &&other) noexcept : _h{std::exchange(other._h, nullptr)} {}
    UniqueHandle &operator=(UniqueHandle &&other) noexcept
    {
        if (this != &other) {
            Reset();
            _h = std::exchange(other._h, nullptr);
        }
        return *this;
    }

    HANDLE Get() const noexcept
    {
        return _h;
    }

    bool Valid() const noexcept
    {
        return _h != nullptr && _h != INVALID_HANDLE_VALUE;
    }

    void Reset(HANDLE h = nullptr) noexcept
    {
        if (Valid()) {
            CloseHandle(_h);
        }
        _h = h;
    }

    HANDLE Release() noexcept
    {
        return std::exchange(_h, nullptr);
    }

private:
    HANDLE _h{nullptr};
};

class UniqueScHandle
{
public:
    UniqueScHandle() = default;
    explicit UniqueScHandle(SC_HANDLE h) noexcept : _h{h} {}
    ~UniqueScHandle()
    {
        Reset();
    }

    UniqueScHandle(const UniqueScHandle &) = delete;
    UniqueScHandle &operator=(const UniqueScHandle &) = delete;

    UniqueScHandle(UniqueScHandle &&other) noexcept : _h{std::exchange(other._h, nullptr)} {}
    UniqueScHandle &operator=(UniqueScHandle &&other) noexcept
    {
        if (this != &other) {
            Reset();
            _h = std::exchange(other._h, nullptr);
        }
        return *this;
    }

    SC_HANDLE Get() const noexcept
    {
        return _h;
    }

    bool Valid() const noexcept
    {
        return _h != nullptr;
    }

    void Reset(SC_HANDLE h = nullptr) noexcept
    {
        if (_h != nullptr) {
            CloseServiceHandle(_h);
        }
        _h = h;
    }

private:
    SC_HANDLE _h{nullptr};
};

class UniqueDevInfo
{
public:
    UniqueDevInfo() = default;
    explicit UniqueDevInfo(HDEVINFO h) noexcept : _h{h} {}
    ~UniqueDevInfo()
    {
        Reset();
    }

    UniqueDevInfo(const UniqueDevInfo &) = delete;
    UniqueDevInfo &operator=(const UniqueDevInfo &) = delete;

    HDEVINFO Get() const noexcept
    {
        return _h;
    }

    bool Valid() const noexcept
    {
        return _h != INVALID_HANDLE_VALUE && _h != nullptr;
    }

    void Reset(HDEVINFO h = INVALID_HANDLE_VALUE) noexcept
    {
        if (Valid()) {
            SetupDiDestroyDeviceInfoList(_h);
        }
        _h = h;
    }

private:
    HDEVINFO _h{INVALID_HANDLE_VALUE};
};

class UniqueWinUsb
{
public:
    UniqueWinUsb() = default;
    explicit UniqueWinUsb(WINUSB_INTERFACE_HANDLE h) noexcept : _h{h} {}
    ~UniqueWinUsb()
    {
        Reset();
    }

    UniqueWinUsb(const UniqueWinUsb &) = delete;
    UniqueWinUsb &operator=(const UniqueWinUsb &) = delete;

    UniqueWinUsb(UniqueWinUsb &&other) noexcept : _h{std::exchange(other._h, nullptr)} {}
    UniqueWinUsb &operator=(UniqueWinUsb &&other) noexcept
    {
        if (this != &other) {
            Reset();
            _h = std::exchange(other._h, nullptr);
        }
        return *this;
    }

    WINUSB_INTERFACE_HANDLE Get() const noexcept
    {
        return _h;
    }

    bool Valid() const noexcept
    {
        return _h != nullptr;
    }

    void Reset(WINUSB_INTERFACE_HANDLE h = nullptr) noexcept
    {
        if (_h != nullptr) {
            WinUsb_Free(_h);
        }
        _h = h;
    }

private:
    WINUSB_INTERFACE_HANDLE _h{nullptr};
};

class UniqueSocket
{
public:
    UniqueSocket() = default;
    explicit UniqueSocket(SOCKET s) noexcept : _s{s} {}
    ~UniqueSocket()
    {
        Reset();
    }

    UniqueSocket(const UniqueSocket &) = delete;
    UniqueSocket &operator=(const UniqueSocket &) = delete;

    UniqueSocket(UniqueSocket &&other) noexcept : _s{std::exchange(other._s, INVALID_SOCKET)} {}
    UniqueSocket &operator=(UniqueSocket &&other) noexcept
    {
        if (this != &other) {
            Reset();
            _s = std::exchange(other._s, INVALID_SOCKET);
        }
        return *this;
    }

    SOCKET Get() const noexcept
    {
        return _s;
    }

    bool Valid() const noexcept
    {
        return _s != INVALID_SOCKET;
    }

    void Reset(SOCKET s = INVALID_SOCKET) noexcept
    {
        if (_s != INVALID_SOCKET) {
            closesocket(_s);
        }
        _s = s;
    }

    SOCKET Release() noexcept
    {
        return std::exchange(_s, INVALID_SOCKET);
    }

private:
    SOCKET _s{INVALID_SOCKET};
};

inline std::string WideToUtf8(std::wstring_view wide)
{
    if (wide.empty()) {
        return {};
    }
    const int needed = WideCharToMultiByte(
        CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
    if (needed <= 0) {
        return {};
    }
    std::string out(static_cast<size_t>(needed), '\0');
    WideCharToMultiByte(
        CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), out.data(), needed, nullptr,
        nullptr);
    return out;
}

inline std::wstring Utf8ToWide(std::string_view utf8)
{
    if (utf8.empty()) {
        return {};
    }
    const int needed =
        MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    if (needed <= 0) {
        return {};
    }
    std::wstring out(static_cast<size_t>(needed), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), out.data(), needed);
    return out;
}

inline std::string FormatWin32Error(DWORD code)
{
    wchar_t *msgBuf = nullptr;
    const DWORD flags =
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;
    const DWORD len = FormatMessageW(
        flags, nullptr, code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPWSTR>(&msgBuf), 0, nullptr);
    std::string result = "Win32(" + std::to_string(code) + ")";
    if (len > 0 && msgBuf != nullptr) {
        std::wstring ws{msgBuf, len};
        while (!ws.empty() && (ws.back() == L'\r' || ws.back() == L'\n' || ws.back() == L' ')) {
            ws.pop_back();
        }
        result += ": " + WideToUtf8(ws);
    }
    if (msgBuf != nullptr) {
        LocalFree(msgBuf);
    }
    return result;
}

inline int64_t SteadyNowMs() noexcept
{
    return static_cast<int64_t>(GetTickCount64());
}

} // namespace Service::Win
