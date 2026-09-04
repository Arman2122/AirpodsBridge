// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: MIT

#pragma once

#include <memory>

#include <QHash>
#include <QString>
#include <QVariant>

namespace Core::Settings {

class Repository
{
public:
    virtual ~Repository() = default;

    virtual bool Contains(const QString &key) const = 0;
    virtual QVariant Read(const QString &key) const = 0;
    virtual void Write(const QString &key, const QVariant &value) = 0;
    virtual void Remove(const QString &key) = 0;
};

class MemoryRepository final : public Repository
{
public:
    bool Contains(const QString &key) const override;
    QVariant Read(const QString &key) const override;
    void Write(const QString &key, const QVariant &value) override;
    void Remove(const QString &key) override;

private:
    QHash<QString, QVariant> _values;
};

std::unique_ptr<Repository> CreatePersistentRepository();

} // namespace Core::Settings
