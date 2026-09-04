// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: MIT

#pragma once

#include <string>
#include <optional>

#include <QString>
#include <QVersionNumber>

#include "Source/Support/Helper.h"

namespace Core::Update {

using namespace std::chrono_literals;

using FnProgress = std::function<bool(size_t downloaded, size_t total)>;

struct ReleaseInfo {
    bool CanAutoUpdate() const;
    void OpenUrl() const;

    QVersionNumber version;
    QString url;
    QString fileName;
    std::string downloadUrl;
    size_t fileSize{0};
    QString sha256;
    QString changeLog;
    bool isPreRelease{false};
};

namespace Details {

std::optional<ReleaseInfo> ParseSingleReleaseResponse(const std::string &text);
std::optional<ReleaseInfo> ParseMultipleReleasesResponseFirst(const std::string &text);
bool VerifyFileSha256(const QString &filePath, const QString &expectedSha256);

} // namespace Details

QVersionNumber ToVersionNumber(QString str);

QVersionNumber GetLocalVersion();

std::optional<ReleaseInfo> FetchUpdateRelease();
bool DownloadInstall(const ReleaseInfo &info, const FnProgress &progressCallback);

class AsyncChecker
{
public:
    using FnCallback =
        std::function<void(const Core::Update::ReleaseInfo &releaseInfo, bool silent)>;

    AsyncChecker(FnCallback callback);
    ~AsyncChecker();

    void Start();
    void Stop();

private:
    constexpr static auto kInterval = 1h;

    FnCallback _callback;
    Helper::Timer _timer;
    bool _isFirst = true;

    void Checker();
};

} // namespace Core::Update
