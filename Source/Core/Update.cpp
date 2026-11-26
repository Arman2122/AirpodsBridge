// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#include "Source/Core/Update.h"

#include <array>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>

#include <QCryptographicHash>
#include <QDesktopServices>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QUrl>

#include <cpr/cpr.h>
#include <nlohmann/json.hpp>

#include <Config.h>
#include "Source/Core/Settings.h"
#include "Source/Support/Assert.h"
#include "Source/Support/Logger.h"
#include "Source/Support/Utils.h"

using json = nlohmann::json;

namespace Core::Update {

namespace {

constexpr int32_t kHttpTimeoutMs = 15'000;
constexpr int32_t kConnectTimeoutMs = 10'000;

std::string UserAgentHeader()
{
    return std::string{Config::ProgramName} + "/" + Config::Version::String + " (" +
           Config::UrlRepository + ")";
}

cpr::Header DefaultApiHeaders(const std::string &etag = {})
{
    cpr::Header headers{
        {"Accept", "application/vnd.github.v3+json"},
        {"User-Agent", UserAgentHeader()},
    };
    if (!etag.empty()) {
        headers.emplace("If-None-Match", etag);
    }
    return headers;
}

// Per-session HTTP metadata cache so periodic checks neither re-query the current
// version's prerelease flag nor consume GitHub rate-limit quota on unchanged releases.
struct HttpCache {
    std::mutex mutex;
    std::optional<bool> isCurrentPreRelease;
    std::string latestEtag;
    std::optional<ReleaseInfo> latestCachedRelease;
};

HttpCache &Cache()
{
    static HttpCache instance;
    return instance;
}

} // namespace

QVersionNumber ToVersionNumber(QString str)
{
    // If "v1.2.3", set to "1.2.3"
    if (str.size() >= 2 && str.at(0).toLower() == 'v' && str.at(1).isNumber()) {
        str = str.remove(0, 1);
    }
    return QVersionNumber::fromString(str);
}

namespace Details {

namespace {

QString ParseChangeLog(const QString &body)
{
    // Two things this has to tolerate, both present in real release bodies:
    //   - decoration between the hashes and the words, e.g. "## :scroll: Change log"
    //   - CRLF, since bodies authored in the GitHub web UI use it and `$` asserts only
    //     before the `\n`, so an unconsumed `\r` would fail the match
    static const QRegularExpression heading{
        R"((?im)^#{1,6}[ \t]+.*?(?:change[ \t]*log|what's[ \t]+changed)[ \t\r]*$)"};

    const auto headingMatch = heading.match(body);
    if (!headingMatch.hasMatch()) {
        return {};
    }

    auto changeLog = body.mid(headingMatch.capturedEnd()).trimmed();
    int endPosition = changeLog.indexOf("\r\n\r\n");
    if (endPosition == -1) {
        endPosition = changeLog.indexOf("\n\n");
    }

    if (endPosition != -1) {
        changeLog = changeLog.left(endPosition);
    }
    return changeLog.trimmed();
}

QString ParseSha256Digest(const json &asset)
{
    if (!asset.contains("digest") || !asset["digest"].is_string()) {
        return {};
    }

    const auto digest = QString::fromStdString(asset["digest"].get<std::string>());
    constexpr auto prefix = "sha256:";
    if (!digest.startsWith(prefix, Qt::CaseInsensitive)) {
        return {};
    }

    const auto hash = digest.mid(QString{prefix}.size());
    if (hash.size() != 64) {
        return {};
    }

    for (const auto character : hash) {
        const bool isHex = character.isDigit() ||
                           (character.toLower() >= QChar{'a'} && character.toLower() <= QChar{'f'});
        if (!isHex) {
            return {};
        }
    }
    return hash.toLower();
}

} // namespace

QString SanitizeAssetFileName(const QString &fileName)
{
    QString normalized = fileName;
    normalized.replace('\\', '/');
    const QString base = QFileInfo{normalized}.fileName().trimmed();
    if (base.isEmpty() || base == "." || base == ".." || base.contains('/')) {
        return {};
    }
    return base;
}

std::optional<ReleaseInfo> ParseSingleReleaseResponse(const std::string &text)
{
    try {
        const auto root = json::parse(text);

        auto tag = QString::fromStdString(root["tag_name"].get<std::string>());
        auto body = QString::fromStdString(root["body"].get<std::string>());
        auto url = QString::fromStdString(root["html_url"].get<std::string>());

        // Check url
        const auto expectedReleaseUrlPrefix = QString{Config::UrlReleases} + "/";
        if (!url.startsWith(expectedReleaseUrlPrefix)) {
            LOG(Warn, "ParseSRResponse: 'html_url' invalid. content: {}", url);
            return std::nullopt;
        }

        // Check body
        QString changeLog;
        if (body.isEmpty()) {
            LOG(Warn, "ParseSRResponse: 'body' is empty.");
        }
        else {
            changeLog = ParseChangeLog(body);
            if (changeLog.isEmpty()) {
                LOG(Trace, "ParseSRResponse: release notes have no structured changelog section.");
            }
        }

        ReleaseInfo info;

        info.version = ToVersionNumber(tag);
        info.url = std::move(url);
        info.changeLog = std::move(changeLog);
        info.isPreRelease = root["prerelease"].get<bool>();

        for (const auto &asset : root["assets"]) {
            auto fileName =
                SanitizeAssetFileName(QString::fromStdString(asset["name"].get<std::string>()));
            auto fileSize = asset["size"].get<size_t>();
            auto downloadUrl = asset["browser_download_url"].get<std::string>();
            auto sha256 = ParseSha256Digest(asset);

            if (fileName.isEmpty() || fileSize == 0 || downloadUrl.empty()) {
                LOG(Warn, "ParseSRResponse: Asset json fields value is empty. Continue.");
                continue;
            }

            // Check url
            const std::string expectedDownloadUrlPrefix =
                std::string{Config::UrlReleases} + "/download/";
            if (downloadUrl.find(expectedDownloadUrlPrefix) != 0) {
                LOG(Warn,
                    "ParseSRResponse: 'browser_download_url' invalid. Continue. content: '{}'",
                    downloadUrl);
                continue;
            }

            LOG(Info, "ParseSRResponse: Asset name: '{}', size: {}, downloadUrl: '{}'.", fileName,
                fileSize, downloadUrl);

#if !defined APB_OS_WIN
    #error "Need to port."
#endif
            // AirpodsBridge-x.x.x-win32.exe
            //
            if (QFileInfo{fileName}.suffix() != "exe") {
                LOG(Warn, "ParseSRResponse: Asset suffix is unsupported. Continue.");
                continue;
            }

            if (fileName.indexOf(CONFIG_CPACK_SYSTEM_NAME) == -1) {
                LOG(Warn, "ParseSRResponse: Asset platform is mismatched. Continue.");
                continue;
            }

            info.fileName = std::move(fileName);
            info.downloadUrl = std::move(downloadUrl);
            info.fileSize = fileSize;
            info.sha256 = std::move(sha256);

            if (info.sha256.isEmpty()) {
                LOG(Warn, "ParseSRResponse: Asset does not contain a valid SHA-256 digest.");
            }

            LOG(Info, "ParseSRResponse: Found matching file.");
            break;
        }

        return info;
    }
    catch (const json::exception &ex) {
        LOG(Warn, "ParseSRResponse: json parse failed. what: '{}', text: '{}'", ex.what(), text);
        return std::nullopt;
    }
}

std::optional<ReleaseInfo> ParseMultipleReleasesResponseFirst(const std::string &text)
{
    try {
        const auto root = json::parse(text);
        if (!root.is_array() || root.empty()) {
            LOG(Warn, "ParseMRResponse: response does not contain any releases.");
            return std::nullopt;
        }
        auto optInfo = ParseSingleReleaseResponse(root.front().dump());
        if (!optInfo.has_value()) {
            LOG(Warn, "One release info parsing failed.");
            return std::nullopt;
        }
        return optInfo.value();
    }
    catch (json::exception &ex) {
        LOG(Warn, "ParseMRResponse: json parse failed. what: '{}', text: '{}'", ex.what(), text);
        return std::nullopt;
    }
}

bool VerifyFileSha256(const QString &filePath, const QString &expectedSha256)
{
    if (expectedSha256.size() != 64) {
        return false;
    }

    QFile file{filePath};
    if (!file.open(QIODevice::ReadOnly)) {
        LOG(Warn, "VerifyFileSha256: Unable to open '{}'. error: '{}'", filePath,
            file.errorString());
        return false;
    }

    QCryptographicHash hash{QCryptographicHash::Sha256};
    if (!hash.addData(&file)) {
        LOG(Warn, "VerifyFileSha256: Unable to read '{}'.", filePath);
        return false;
    }

    const auto actualSha256 = QString::fromLatin1(hash.result().toHex());
    return actualSha256.compare(expectedSha256, Qt::CaseInsensitive) == 0;
}

std::optional<ReleaseInfo> FetchReleaseByVersion(const QVersionNumber &version)
{
    // Repositories differ on whether release tags carry a `v` prefix, and one repository can
    // hold both forms across its history, so try each rather than assuming a convention.
    const std::string plain = version.toString().toStdString();
    const std::array<std::string, 2> tags{"v" + plain, plain};

    for (const auto &tag : tags) {
        const cpr::Response response = cpr::Get(
            cpr::Url{std::string{Config::ApiRepository} + "/releases/tags/" + tag},
            DefaultApiHeaders(), cpr::ConnectTimeout{kConnectTimeoutMs},
            cpr::Timeout{kHttpTimeoutMs});

        if (response.status_code == 200) {
            return Details::ParseSingleReleaseResponse(response.text);
        }

        LOG(Info, "FetchReleaseByVersion: no release for tag '{}'. code: {}", tag,
            response.status_code);
    }

    LOG(Warn, "FetchReleaseByVersion: no release found for version '{}'.", plain);
    return std::nullopt;
}

std::optional<ReleaseInfo> FetchLatestRelease(bool includePreRelease)
{
    const std::string endpoint =
        std::string{Config::ApiRepository} + (includePreRelease ? "/releases" : "/releases/latest");

    std::string etag;
    {
        std::lock_guard<std::mutex> lock{Cache().mutex};
        etag = Cache().latestEtag;
    }

    const cpr::Response response = cpr::Get(
        cpr::Url{endpoint}, DefaultApiHeaders(etag), cpr::ConnectTimeout{kConnectTimeoutMs},
        cpr::Timeout{kHttpTimeoutMs});

    if (response.status_code == 304) {
        std::lock_guard<std::mutex> lock{Cache().mutex};
        if (Cache().latestCachedRelease.has_value()) {
            LOG(Info, "FetchLatestRelease: 304 Not Modified; reusing cached release.");
            return Cache().latestCachedRelease;
        }
    }

    if (response.status_code != 200) {
        LOG(Warn,
            "FetchLatestRelease: GitHub REST API response status code isn't 200. "
            "code: {} error: '{}'",
            response.status_code, response.error.message);
        return std::nullopt;
    }

    auto parsed = includePreRelease ? Details::ParseMultipleReleasesResponseFirst(response.text)
                                    : Details::ParseSingleReleaseResponse(response.text);
    if (parsed.has_value()) {
        std::lock_guard<std::mutex> lock{Cache().mutex};
        if (const auto it = response.header.find("ETag"); it != response.header.end()) {
            Cache().latestEtag = it->second;
        }
        else if (const auto itLower = response.header.find("etag");
                 itLower != response.header.end())
        {
            Cache().latestEtag = itLower->second;
        }
        Cache().latestCachedRelease = parsed;
    }
    return parsed;
}

bool IsCurrentPreRelease()
{
    {
        std::lock_guard<std::mutex> lock{Cache().mutex};
        if (Cache().isCurrentPreRelease.has_value()) {
            return Cache().isCurrentPreRelease.value();
        }
    }

    const auto optInfo = Details::FetchReleaseByVersion(GetLocalVersion());
    const bool result = optInfo.has_value() ? optInfo->isPreRelease : false;
    {
        std::lock_guard<std::mutex> lock{Cache().mutex};
        Cache().isCurrentPreRelease = result;
    }

    LOG(Info, "IsCurrentPreRelease: returns {}.", result);
    return result;
}

bool NeedToUpdate(const ReleaseInfo &info)
{
    return info.version.normalized() > GetLocalVersion().normalized();
}

} // namespace Details

//////////////////////////////////////////////////

bool ReleaseInfo::CanAutoUpdate() const
{
    // Portable installations should update via the portable ZIP on the release page rather
    // than launching the per-user NSIS installer into %LOCALAPPDATA%\Programs.
    if (Utils::File::IsPortableMode()) {
        return false;
    }
    return !fileName.isEmpty() && !downloadUrl.empty() && fileSize != 0 && sha256.size() == 64;
}

void ReleaseInfo::OpenUrl() const
{
    QDesktopServices::openUrl(QUrl{url});
}

//////////////////////////////////////////////////

QVersionNumber GetLocalVersion()
{
    return ToVersionNumber(Config::Version::String);
}

//////////////////////////////////////////////////

CheckResult CheckForUpdate(bool ignoreSkippedVersion)
{
    const auto isCurrentPreRelease = Details::IsCurrentPreRelease();
    LOG(Info, "Update: isCurrentPreRelease: '{}'", isCurrentPreRelease);

    const auto optInfo = Details::FetchLatestRelease(isCurrentPreRelease);
    if (!optInfo.has_value()) {
        LOG(Warn, "Update: FetchLatestRelease() returned nullopt.");
        return {CheckStatus::Failed, std::nullopt};
    }

    const auto &latestInfo = optInfo.value();
    const auto needToUpdate = Details::NeedToUpdate(latestInfo);

    LOG(Info, "Update: Latest version: '{}'", latestInfo.version.toString());
    if (!needToUpdate) {
        LOG(Info, "Update: No need to update.");
        return {CheckStatus::UpToDate, latestInfo};
    }

    LOG(Info, "Update: Need to update.");

    if (!ignoreSkippedVersion &&
        latestInfo.version.toString() == Core::Settings::GetCurrent().skipped_version)
    {
        LOG(Info, "Update: User skipped this new version. Ignore.");
        return {CheckStatus::UpToDate, latestInfo};
    }

    return {CheckStatus::UpdateAvailable, optInfo};
}

std::optional<ReleaseInfo> FetchUpdateRelease()
{
    auto result = CheckForUpdate(false);
    if (result.status == CheckStatus::UpdateAvailable) {
        return std::move(result.release);
    }
    return std::nullopt;
}

//////////////////////////////////////////////////

bool DownloadInstall(const ReleaseInfo &info, const FnProgress &progressCallback)
{
    APB_ASSERT(Details::NeedToUpdate(info));

    if (!info.CanAutoUpdate()) {
        LOG(Warn, "DownloadInstall: Cannot auto update.");
        return false;
    }

    const QString safeFileName = Details::SanitizeAssetFileName(info.fileName);
    if (safeFileName.isEmpty()) {
        LOG(Warn, "DownloadInstall: Unsafe asset filename rejected.");
        return false;
    }

    QTemporaryDir tempPath;
    if (!tempPath.isValid()) {
        auto errorString = tempPath.errorString();
        LOG(Warn, "DownloadInstall: QTemporaryDir construct failed. error: '{}'", errorString);
        return false;
    }

    const QString filePath = QFileInfo{tempPath.filePath(safeFileName)}.absoluteFilePath();

    LOG(Info, "DownloadInstall: Ready to download to '{}'.", filePath);

    // Construct from UTF-16 `std::filesystem::path` so non-ASCII Windows usernames work.
    std::ofstream outFile{std::filesystem::path{filePath.toStdWString()}, std::ios::binary};
    if (!outFile.is_open()) {
        LOG(Warn, "DownloadInstall: Unable to open destination file '{}'.", filePath);
        return false;
    }

    auto response = cpr::Download(
        outFile, cpr::Url{info.downloadUrl}, cpr::Header{{"User-Agent", UserAgentHeader()}},
        cpr::ConnectTimeout{kConnectTimeoutMs},
        cpr::ProgressCallback{[&](cpr::cpr_off_t downloadTotal, cpr::cpr_off_t downloadNow,
                                  cpr::cpr_off_t uploadTotal, cpr::cpr_off_t uploadNow,
                                  intptr_t userdata) {
            Q_UNUSED(uploadTotal);
            Q_UNUSED(uploadNow);
            Q_UNUSED(userdata);
            LOG(Trace, "Downloaded {} / {} bytes.", downloadNow, downloadTotal);
            return progressCallback(downloadNow, downloadTotal);
        }});

    if (response.status_code != 200) {
        LOG(Warn,
            "DownloadInstall: Download response status code is not 200. code: {}, message: '{}'",
            response.status_code, response.error.message);
        return false;
    }

    if (response.downloaded_bytes != info.fileSize) {
        LOG(Warn, "Download: Download file size mismatch. Downloaded: {}, expect: {}",
            response.downloaded_bytes, info.fileSize);
        return false;
    }

    outFile.close();

    if (!Details::VerifyFileSha256(filePath, info.sha256)) {
        LOG(Warn, "DownloadInstall: Downloaded file SHA-256 mismatch.");
        return false;
    }

    tempPath.setAutoRemove(false);

    LOG(Info, "Download: Downloaded succeeded. filePath: '{}', size: {}", filePath,
        response.downloaded_bytes);

    if (!QProcess::startDetached(filePath)) {
        LOG(Warn, "DownloadInstall: Start installer failed.");
        return false;
    }

    return true;
}

//////////////////////////////////////////////////

AsyncChecker::AsyncChecker(FnCallback callback) : _callback{std::move(callback)} {}

AsyncChecker::~AsyncChecker()
{
    Stop();
    _manualThread.request_stop();
    if (_manualThread.joinable()) {
        _manualThread.join();
    }
}

void AsyncChecker::Start()
{
    // clang-format off
    _timer.Start(kInterval, [this] { Checker(); }, true);
    // clang-format on
}

void AsyncChecker::Stop()
{
    _timer.Stop();
}

void AsyncChecker::CheckNow(FnManualCallback onFinished)
{
    if (_manualRunning.exchange(true)) {
        return;
    }

    if (_manualThread.joinable()) {
        _manualThread.join();
    }

    _manualThread =
        std::jthread{[this, onFinished = std::move(onFinished)](std::stop_token stopToken) {
            const auto result = CheckForUpdate(true);
            _manualRunning = false;
            if (!stopToken.stop_requested() && onFinished) {
                onFinished(result);
            }
        }};
}

void AsyncChecker::Checker()
{
    LOG(Info, "Checking update...");

    do {
        const auto optReleaseInfo = Core::Update::FetchUpdateRelease();
        if (!optReleaseInfo.has_value()) {
            break;
        }

        _callback(optReleaseInfo.value(), !_isFirst);
    } while (false);

    _isFirst = false;
}

} // namespace Core::Update
