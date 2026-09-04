// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: MIT

#include "Source/Core/SettingsRepository.h"

#include <QSettings>

#include <Config.h>

namespace Core::Settings {

namespace {

class QSettingsRepository final : public Repository
{
public:
    bool Contains(const QString &key) const override
    {
        return _settings.contains(key);
    }

    QVariant Read(const QString &key) const override
    {
        return _settings.value(key);
    }

    void Write(const QString &key, const QVariant &value) override
    {
        _settings.setValue(key, value);
    }

    void Remove(const QString &key) override
    {
        _settings.remove(key);
    }

private:
    mutable QSettings _settings{QSettings::UserScope, Config::ProgramName, Config::ProgramName};
};

} // namespace

bool MemoryRepository::Contains(const QString &key) const
{
    return _values.contains(key);
}

QVariant MemoryRepository::Read(const QString &key) const
{
    return _values.value(key);
}

void MemoryRepository::Write(const QString &key, const QVariant &value)
{
    _values.insert(key, value);
}

void MemoryRepository::Remove(const QString &key)
{
    _values.remove(key);
}

std::unique_ptr<Repository> CreatePersistentRepository()
{
    return std::make_unique<QSettingsRepository>();
}

} // namespace Core::Settings
