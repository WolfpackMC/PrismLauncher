/* Copyright 2013-2021 MultiMC Contributors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "WolfpackUpdaterFetch.h"

#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "Application.h"
#include "FileSystem.h"
#include "net/Download.h"
#include "net/NetJob.h"

namespace {
// Fetch by tag rather than GitHub's "latest release" endpoint: that endpoint explicitly
// excludes prereleases, and the rolling build is published as one (to distinguish it from
// real versioned releases in the GitHub UI).
const QString kReleasesApiUrl = "https://api.github.com/repos/WolfpackMC/updater/releases/tags/latest";

#ifdef Q_OS_WIN32
const QString kAssetName = "mcupdater-windows.exe";
const QString kBinaryName = "mcupdater.exe";
#else
const QString kAssetName = "mcupdater-linux";
const QString kBinaryName = "mcupdater";
#endif

// GitHub publishes a "sha256:<hex>" digest for release assets; extracts the hex part for the
// asset matching kAssetName. Returns an empty string if not found or not published.
QString findAssetDigest(const QJsonDocument& doc, QString& assetUrlOut)
{
    for (const auto& assetValue : doc.object()["assets"].toArray()) {
        auto asset = assetValue.toObject();
        if (asset["name"].toString() != kAssetName)
            continue;
        assetUrlOut = asset["browser_download_url"].toString();
        auto digest = asset["digest"].toString();
        return digest.startsWith("sha256:") ? digest.mid(7) : QString();
    }
    return {};
}

QString localBinarySha256()
{
    QFile f(WolfpackUpdaterFetch::cachedBinaryPath());
    if (!f.open(QIODevice::ReadOnly))
        return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&f))
        return {};
    return hash.result().toHex();
}
}  // namespace

QString WolfpackUpdaterFetch::cachedBinaryPath()
{
    return FS::PathCombine(APPLICATION->dataRoot(), "wolfpack", kBinaryName);
}

void WolfpackUpdaterFetch::fetchLatest(std::function<void(bool ok)> onDone)
{
    auto* fetch = new WolfpackUpdaterFetch(onDone);
    fetch->start();
}

void WolfpackUpdaterFetch::verifyLatest(std::function<void(VerifyResult result)> onDone)
{
    auto* fetch = new WolfpackUpdaterFetch(onDone);
    fetch->startVerify();
}

WolfpackUpdaterFetch::WolfpackUpdaterFetch(std::function<void(bool ok)> onDone) : m_onDone(onDone) {}

WolfpackUpdaterFetch::WolfpackUpdaterFetch(std::function<void(VerifyResult result)> onVerifyDone) : m_onVerifyDone(onVerifyDone) {}

void WolfpackUpdaterFetch::start()
{
    FS::ensureFolderPathExists(FS::PathCombine(APPLICATION->dataRoot(), "wolfpack"));

    auto job = makeShared<NetJob>("Wolfpack::CheckLatestRelease", APPLICATION->network());
    auto [action, response] = Net::Download::makeByteArray(QUrl(kReleasesApiUrl));
    job->addNetAction(action);

    connect(job.get(), &Task::failed, this, [this](const QString&) { finish(false); });
    connect(job.get(), &Task::succeeded, this, [this, response] {
        QJsonParseError parseError{};
        auto doc = QJsonDocument::fromJson(*response, &parseError);
        if (parseError.error != QJsonParseError::NoError) {
            finish(false);
            return;
        }

        QString assetUrl;
        findAssetDigest(doc, assetUrl);
        if (assetUrl.isEmpty()) {
            finish(false);
            return;
        }

        auto tmpPath = cachedBinaryPath() + ".tmp";
        auto downloadJob = makeShared<NetJob>("Wolfpack::DownloadUpdater", APPLICATION->network());
        auto dl = Net::Download::makeFile(QUrl(assetUrl), tmpPath);
        downloadJob->addNetAction(dl);

        connect(downloadJob.get(), &Task::failed, this, [this](const QString&) { finish(false); });
        connect(downloadJob.get(), &Task::succeeded, this, [this, tmpPath] {
            auto finalPath = cachedBinaryPath();
#ifndef Q_OS_WIN32
            QFile(tmpPath).setPermissions(QFile(tmpPath).permissions() | QFileDevice::Permissions(0x1111));
#endif
            QFile::remove(finalPath);
            if (!QFile::rename(tmpPath, finalPath)) {
                finish(false);
                return;
            }
            finish(true);
        });

        m_job = downloadJob;
        downloadJob->start();
    });

    m_job = job;
    job->start();
}

void WolfpackUpdaterFetch::finish(bool ok)
{
    if (m_onDone)
        m_onDone(ok);
    deleteLater();
}

void WolfpackUpdaterFetch::startVerify()
{
    auto job = makeShared<NetJob>("Wolfpack::CheckLatestRelease", APPLICATION->network());
    auto [action, response] = Net::Download::makeByteArray(QUrl(kReleasesApiUrl));
    job->addNetAction(action);

    connect(job.get(), &Task::failed, this, [this](const QString&) { finishVerify(VerifyResult::CheckFailed); });
    connect(job.get(), &Task::succeeded, this, [this, response] {
        QJsonParseError parseError{};
        auto doc = QJsonDocument::fromJson(*response, &parseError);
        if (parseError.error != QJsonParseError::NoError) {
            finishVerify(VerifyResult::CheckFailed);
            return;
        }

        QString assetUrl;
        auto remoteDigest = findAssetDigest(doc, assetUrl);
        if (remoteDigest.isEmpty()) {
            finishVerify(VerifyResult::CheckFailed);
            return;
        }

        auto localDigest = localBinarySha256();
        if (localDigest.isEmpty()) {
            finishVerify(VerifyResult::CheckFailed);
            return;
        }

        finishVerify(remoteDigest.compare(localDigest, Qt::CaseInsensitive) == 0 ? VerifyResult::Match : VerifyResult::Mismatch);
    });

    m_job = job;
    job->start();
}

void WolfpackUpdaterFetch::finishVerify(VerifyResult result)
{
    if (m_onVerifyDone)
        m_onVerifyDone(result);
    deleteLater();
}
