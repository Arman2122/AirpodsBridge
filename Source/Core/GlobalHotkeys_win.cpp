// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#include "Source/Core/GlobalHotkeys.h"

#include <map>

#include <QAbstractNativeEventFilter>
#include <QCoreApplication>
#include <QStringList>
#include <QWindow>

#include "Source/Support/Logger.h"
#include "Source/Support/OS/Windows.h"

namespace Core::GlobalHotkeys {

namespace {

std::optional<uint32_t> ParseKeyToken(const QString &upperToken)
{
    if (upperToken.size() == 1) {
        const QChar ch = upperToken.at(0);
        if ((ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9')) {
            return static_cast<uint32_t>(ch.unicode());
        }
    }

    if (upperToken.startsWith('F') && upperToken.size() <= 3) {
        bool ok = false;
        const int num = upperToken.mid(1).toInt(&ok);
        if (ok && num >= 1 && num <= 12) {
            return static_cast<uint32_t>(VK_F1 + (num - 1));
        }
    }

    if (upperToken == QStringLiteral("SPACE")) {
        return VK_SPACE;
    }
    if (upperToken == QStringLiteral("INSERT")) {
        return VK_INSERT;
    }
    if (upperToken == QStringLiteral("HOME")) {
        return VK_HOME;
    }
    if (upperToken == QStringLiteral("END")) {
        return VK_END;
    }
    if (upperToken == QStringLiteral("PAGEUP")) {
        return VK_PRIOR;
    }
    if (upperToken == QStringLiteral("PAGEDOWN")) {
        return VK_NEXT;
    }
    return std::nullopt;
}

QString FormatKeyToken(uint32_t vk)
{
    if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) {
        return QString{QChar{static_cast<ushort>(vk)}};
    }
    if (vk >= VK_F1 && vk <= VK_F12) {
        return QStringLiteral("F%1").arg(vk - VK_F1 + 1);
    }
    switch (vk) {
    case VK_SPACE:
        return QStringLiteral("Space");
    case VK_INSERT:
        return QStringLiteral("Insert");
    case VK_HOME:
        return QStringLiteral("Home");
    case VK_END:
        return QStringLiteral("End");
    case VK_PRIOR:
        return QStringLiteral("PageUp");
    case VK_NEXT:
        return QStringLiteral("PageDown");
    default:
        return {};
    }
}

UINT ToWin32Modifiers(const HotkeySpec &spec)
{
    UINT mods = MOD_NOREPEAT;
    if (spec.ctrl) {
        mods |= MOD_CONTROL;
    }
    if (spec.alt) {
        mods |= MOD_ALT;
    }
    if (spec.shift) {
        mods |= MOD_SHIFT;
    }
    if (spec.win) {
        mods |= MOD_WIN;
    }
    return mods;
}

} // namespace

std::optional<HotkeySpec> ParseHotkey(const QString &spec)
{
    const QString trimmed = spec.trimmed();
    if (trimmed.isEmpty()) {
        return std::nullopt;
    }

    HotkeySpec parsed;
    const QStringList parts = trimmed.split('+', Qt::SkipEmptyParts);
    if (parts.isEmpty()) {
        return std::nullopt;
    }

    for (const QString &rawPart : parts) {
        const QString token = rawPart.trimmed().toUpper();
        if (token == QStringLiteral("CTRL") || token == QStringLiteral("CONTROL")) {
            parsed.ctrl = true;
        }
        else if (token == QStringLiteral("ALT")) {
            parsed.alt = true;
        }
        else if (token == QStringLiteral("SHIFT")) {
            parsed.shift = true;
        }
        else if (token == QStringLiteral("WIN") || token == QStringLiteral("META")) {
            parsed.win = true;
        }
        else {
            if (parsed.virtualKey != 0) {
                return std::nullopt;
            }
            const auto vk = ParseKeyToken(token);
            if (!vk.has_value()) {
                return std::nullopt;
            }
            parsed.virtualKey = vk.value();
        }
    }

    if (parsed.virtualKey == 0) {
        return std::nullopt;
    }

    const bool hasModifier = parsed.ctrl || parsed.alt || parsed.shift || parsed.win;
    const bool isFunctionKey = parsed.virtualKey >= VK_F1 && parsed.virtualKey <= VK_F12;
    if (!hasModifier && !isFunctionKey) {
        return std::nullopt;
    }
    return parsed;
}

QString FormatHotkey(const HotkeySpec &spec)
{
    const QString key = FormatKeyToken(spec.virtualKey);
    if (key.isEmpty()) {
        return {};
    }

    QStringList parts;
    if (spec.ctrl) {
        parts << QStringLiteral("Ctrl");
    }
    if (spec.alt) {
        parts << QStringLiteral("Alt");
    }
    if (spec.shift) {
        parts << QStringLiteral("Shift");
    }
    if (spec.win) {
        parts << QStringLiteral("Win");
    }
    parts << key;
    return parts.join('+');
}

class Manager::Impl final : public QAbstractNativeEventFilter
{
public:
    explicit Impl(Manager *owner) : _owner{owner}
    {
        _receiver.setFlags(Qt::Tool | Qt::FramelessWindowHint);
        _receiver.setGeometry(0, 0, 1, 1);
        _receiver.create();
        _hwnd = reinterpret_cast<HWND>(_receiver.winId());

        if (QCoreApplication::instance() != nullptr) {
            QCoreApplication::instance()->installNativeEventFilter(this);
        }
    }

    ~Impl() override
    {
        ClearAll();
        if (QCoreApplication::instance() != nullptr) {
            QCoreApplication::instance()->removeNativeEventFilter(this);
        }
    }

    bool SetBinding(Action action, const QString &spec)
    {
        const int id = static_cast<int>(action);
        if (_registered.contains(id)) {
            UnregisterHotKey(_hwnd, id);
            _registered.erase(id);
        }

        if (spec.trimmed().isEmpty()) {
            return true;
        }

        const auto parsed = ParseHotkey(spec);
        if (!parsed.has_value()) {
            LOG(Warn, "GlobalHotkeys: invalid shortcut '{}'.", spec.toStdString());
            return false;
        }

        if (_hwnd == nullptr) {
            return false;
        }

        const UINT mods = ToWin32Modifiers(parsed.value());
        if (RegisterHotKey(_hwnd, id, mods, parsed->virtualKey) == FALSE) {
            LOG(Warn, "GlobalHotkeys: RegisterHotKey('{}') failed (err={}).", spec.toStdString(),
                GetLastError());
            return false;
        }

        _registered[id] = action;
        LOG(Info, "GlobalHotkeys: registered '{}' for action {}.",
            FormatHotkey(parsed.value()).toStdString(), id);
        return true;
    }

    void ClearAll()
    {
        if (_hwnd != nullptr) {
            for (const auto &[id, _] : _registered) {
                UnregisterHotKey(_hwnd, id);
            }
        }
        _registered.clear();
    }

    bool nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result) override
    {
        Q_UNUSED(eventType);
        Q_UNUSED(result);

        const auto *msg = static_cast<const MSG *>(message);
        if (msg->hwnd != _hwnd || msg->message != WM_HOTKEY) {
            return false;
        }

        const int id = static_cast<int>(msg->wParam);
        const auto it = _registered.find(id);
        if (it != _registered.end()) {
            emit _owner->Triggered(it->second);
            return true;
        }
        return false;
    }

private:
    Manager *_owner{nullptr};
    QWindow _receiver;
    HWND _hwnd{nullptr};
    std::map<int, Action> _registered;
};

Manager::Manager(QObject *parent) : QObject{parent}, _impl{std::make_unique<Impl>(this)} {}

Manager::~Manager() = default;

bool Manager::SetBinding(Action action, const QString &spec)
{
    return _impl->SetBinding(action, spec);
}

void Manager::ClearAll()
{
    _impl->ClearAll();
}

} // namespace Core::GlobalHotkeys
