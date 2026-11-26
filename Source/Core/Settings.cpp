// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#include "Source/Core/Settings.h"

#include <mutex>
#include <vector>

#include <boost/pfr.hpp>
#include <magic_enum.hpp>

#include "Source/Support/Logger.h"
#include "Source/Core/SettingsRepository.h"

using namespace boost;

namespace Core::Settings {

template <class T>
std::string_view LogSensitiveData(const T &value)
{
    return value != std::decay_t<T>{} ? "** MAYBE HAVE VALUE **" : "** MAYBE NO VALUE **";
}

class Manager : public Helper::Singleton<Manager>
{
protected:
    Manager() : _repository{CreatePersistentRepository()} {}
    friend Helper::Singleton<Manager>;

public:
    LoadResult Load()
    {
        const auto &loadKey = [&](const std::string_view &keyName, auto &value,
                                  bool isSensitive = false, bool isDeprecated = false) {
            // A deprecated key is erased from the store on the next save, so its absence is
            // the expected state rather than a problem worth reporting on every launch.
            if (isDeprecated) {
                return false;
            }

            using ValueType = std::decay_t<decltype(value)>;
            using ValueStorageType =
                std::conditional_t<!std::is_enum_v<ValueType>, ValueType, QString>;

            QString qstrKeyName = QString::fromStdString(std::string{keyName});
            if (!_repository->Contains(qstrKeyName)) {
                if (!isSensitive) {
                    LOG(Warn, "The setting key '{}' not found. Current value '{}'.", keyName,
                        value);
                }
                else {
                    LOG(Warn, "The setting key '{}' not found. Current value '{}'.", keyName,
                        LogSensitiveData(value));
                }
                return false;
            }

            QVariant var = _repository->Read(qstrKeyName);
            if (!var.canConvert<ValueStorageType>() ||
                !var.convert(qMetaTypeId<ValueStorageType>()))
            {
                LOG(Warn, "The value of the key '{}' cannot be convert.", keyName);
                return false;
            }

            if constexpr (!std::is_enum_v<ValueType>) {
                value = var.value<ValueStorageType>();
            }
            else {
                auto optValue =
                    magic_enum::enum_cast<ValueType>(var.value<ValueStorageType>().toStdString());
                if (!optValue.has_value()) {
                    LOG(Warn, "enum_cast the value of the key '{}' failed.", keyName);
                    return false;
                }
                value = optValue.value();
            }

            if (!isSensitive) {
                LOG(Info, "Load key succeeded. Key: '{}', Value: '{}'", keyName, value);
            }
            else {
                LOG(Info, "Load key succeeded. Key: '{}', Value: '{}'", keyName,
                    LogSensitiveData(value));
            }
            return true;
        };

        std::lock_guard<std::mutex> lock{_mutex};

        std::decay_t<decltype(kFieldsAbiVersion)> abi_version = 0;
        if (!loadKey("abi_version", abi_version)) {
            LOG(Warn, "No abi_version key. Load default settings.");
            _fields = Fields{};
            return LoadResult::NoAbiField;
        }
        else {
            if (abi_version > kFieldsAbiVersion ||
                (abi_version < kFieldsAbiVersion && !MigrateRepository(abi_version)))
            {
                LOG(Warn, "The settings abi version is incompatible. Local: '{}', Expect: '{}'",
                    abi_version, kFieldsAbiVersion);
                return LoadResult::AbiIncompatible;
            }

            pfr::for_each_field(_fieldsMeta, [&](auto &field) {
                loadKey(
                    field.GetName(), field.GetValue(_fields), field.IsSensitive(),
                    field.IsDeprecated());
            });
            return LoadResult::Successful;
        }
    }

    void Save(Fields newFields)
    {
        Fields previous, current;
        {
            std::lock_guard<std::mutex> lock{_mutex};
            previous = _fields;
            _fields = std::move(newFields);
            SaveWithoutLock();
            current = _fields;
        }
        NotifyChanged(previous, current);
    }

    void Apply()
    {
        Fields current;
        {
            std::lock_guard<std::mutex> lock{_mutex};
            current = _fields;
        }

        LOG(Info, "Apply: replaying every setting to its watchers.");
        Notify(current, current, true);
    }

    Fields GetCurrent()
    {
        std::lock_guard<std::mutex> lock{_mutex};

        return _fields;
    }

    auto ModifiableAccess()
    {
        return ModifiableSafeAccessor{_mutex, _fields};
    }

    void SetRepository(std::unique_ptr<Repository> repository)
    {
        if (!repository) {
            return;
        }
        std::lock_guard<std::mutex> lock{_mutex};
        _repository = std::move(repository);
        _fields = Fields{};
    }

    Subscription Subscribe(Impl::FieldWatcher watcher)
    {
        std::lock_guard<std::mutex> lock{_watchMutex};

        const auto id = _nextWatchId++;
        _watchers.emplace_back(id, std::move(watcher));
        return Subscription{id};
    }

    void Unsubscribe(uint64_t id)
    {
        std::lock_guard<std::mutex> lock{_watchMutex};

        std::erase_if(_watchers, [id](const auto &entry) { return entry.first == id; });
    }

private:
    MetaFields _fieldsMeta;

    std::mutex _mutex;
    Fields _fields;
    std::unique_ptr<Repository> _repository;

    // Separate from `_mutex`: a watcher may subscribe or unsubscribe from inside a callback,
    // and a callback runs with the settings lock already released.
    std::mutex _watchMutex;
    std::vector<std::pair<uint64_t, Impl::FieldWatcher>> _watchers;
    uint64_t _nextWatchId{1};

    // Forward-only schema migration hook so future `kFieldsAbiVersion` bumps preserve user
    // preferences and device bindings instead of forcing a full reset.
    bool MigrateRepository(uint32_t fromAbi)
    {
        Q_UNUSED(fromAbi);
        return false;
    }

    void SaveWithoutLock()
    {
        const auto &saveKey = [&]<class T>(
                                  const std::string_view &keyName, const T &value,
                                  bool isSensitive = false, bool isDeprecated = false) {
            QString qstrKeyName = QString::fromStdString(std::string{keyName});

            if (isDeprecated) {
                _repository->Remove(qstrKeyName);
                LOG(Info, "Remove deprecated key succeeded. Key: '{}'", keyName);
                return;
            }

            if constexpr (!std::is_enum_v<T>) {
                _repository->Write(qstrKeyName, value);
            }
            else {
                _repository->Write(
                    qstrKeyName, QString::fromStdString(std::string{magic_enum::enum_name(value)}));
            }

            if (!isSensitive) {
                LOG(Info, "Save key succeeded. Key: '{}', Value: {}", keyName, value);
            }
            else {
                LOG(Info, "Save key succeeded. Key: '{}', Value: {}", keyName,
                    LogSensitiveData(value));
            }
        };

        saveKey("abi_version", kFieldsAbiVersion);

        pfr::for_each_field(_fieldsMeta, [&](const auto &fieldMeta) {
            saveKey(
                fieldMeta.GetName(), fieldMeta.GetValue(_fields), fieldMeta.IsSensitive(),
                fieldMeta.IsDeprecated());
        });
    }

    // Both snapshots were taken under `_mutex`; this runs without it.
    void NotifyChanged(const Fields &previous, const Fields &current)
    {
        bool anyChanged = false;
        pfr::for_each_field(_fieldsMeta, [&](const auto &fieldMeta) {
            if (fieldMeta.GetValue(previous) == fieldMeta.GetValue(current)) {
                return;
            }
            anyChanged = true;
            if (!fieldMeta.IsSensitive()) {
                LOG(Info, "Setting changed: '{}' = '{}'", fieldMeta.GetName(),
                    fieldMeta.GetValue(current));
            }
            else {
                LOG(Info, "Setting changed: '{}' = '{}'", fieldMeta.GetName(),
                    LogSensitiveData(fieldMeta.GetValue(current)));
            }
        });

        if (anyChanged) {
            Notify(previous, current, false);
        }
    }

    void Notify(const Fields &previous, const Fields &current, bool initial)
    {
        // Copied out so that a callback may subscribe or unsubscribe without deadlocking,
        // and so that no lock at all is held while user code runs.
        std::vector<Impl::FieldWatcher> watchers;
        {
            std::lock_guard<std::mutex> lock{_watchMutex};
            watchers.reserve(_watchers.size());
            for (const auto &entry : _watchers) {
                watchers.push_back(entry.second);
            }
        }

        for (const auto &watcher : watchers) {
            watcher(previous, current, initial);
        }
    }

    friend class ModifiableSafeAccessor;
};

//////////////////////////////////////////////////

Subscription::~Subscription()
{
    Reset();
}

Subscription &Subscription::operator=(Subscription &&other) noexcept
{
    if (this != &other) {
        Reset();
        _id = std::exchange(other._id, 0);
    }
    return *this;
}

void Subscription::Reset()
{
    if (_id != 0) {
        Manager::GetInstance().Unsubscribe(std::exchange(_id, 0));
    }
}

namespace Impl {

Subscription Subscribe(FieldWatcher watcher)
{
    return Manager::GetInstance().Subscribe(std::move(watcher));
}

} // namespace Impl

//////////////////////////////////////////////////

ModifiableSafeAccessor::ModifiableSafeAccessor(std::mutex &lock, Fields &fields)
    : Impl::BasicSafeAccessor<Fields>{lock, fields}, _oldFields{fields}
{
}

ModifiableSafeAccessor::~ModifiableSafeAccessor()
{
    auto &manager = Manager::GetInstance();
    manager.SaveWithoutLock();

    // Watchers run after the lock is released, so one of them may touch settings itself
    // without deadlocking -- which is why the current values are copied out first.
    const Fields current = _fields;
    _lock.unlock();
    manager.NotifyChanged(_oldFields, current);
}

//////////////////////////////////////////////////

LoadResult Load()
{
    return Manager::GetInstance().Load();
}

void Save(Fields newFields)
{
    return Manager::GetInstance().Save(std::move(newFields));
}

void Apply()
{
    return Manager::GetInstance().Apply();
}

Fields GetCurrent()
{
    return Manager::GetInstance().GetCurrent();
}

Fields GetDefault()
{
    return Fields{};
}

ModifiableSafeAccessor ModifiableAccess()
{
    return Manager::GetInstance().ModifiableAccess();
}

void SetRepository(std::unique_ptr<Repository> repository)
{
    Manager::GetInstance().SetRepository(std::move(repository));
}

} // namespace Core::Settings
