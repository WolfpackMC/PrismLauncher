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

#include <QDateTime>
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
const qint64 kRefreshThrottleSeconds = 60 * 60;  // 1 hour

#ifdef Q_OS_WIN32
const QString kAssetName = "mcupdater-windows.exe";
const QString kBinaryName = "mcupdater.exe";
#else
const QString kAssetName = "mcupdater-linux";
const QString kBinaryName = "mcupdater";
#endif

QString checkedMarkerPath()
{
    return WolfpackUpdaterFetch::cachedBinaryPath() + ".checked";
}
}  // namespace

QString WolfpackUpdaterFetch::cachedBinaryPath()
{
    return FS::PathCombine(APPLICATION->dataRoot(), "wolfpack", kBinaryName);
}

bool WolfpackUpdaterFetch::recentlyChecked()
{
    QFile marker(checkedMarkerPath());
    if (!marker.open(QIODevice::ReadOnly))
        return false;
    bool ok = false;
    qint64 lastChecked = marker.readAll().trimmed().toLongLong(&ok);
    if (!ok)
        return false;
    return (QDateTime::currentSecsSinceEpoch() - lastChecked) < kRefreshThrottleSeconds;
}

void WolfpackUpdaterFetch::fetchLatest(std::function<void(bool ok)> onDone)
{
    auto* fetch = new WolfpackUpdaterFetch(onDone);
    fetch->start();
}

WolfpackUpdaterFetch::WolfpackUpdaterFetch(std::function<void(bool ok)> onDone) : m_onDone(onDone) {}

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
        for (const auto& assetValue : doc.object()["assets"].toArray()) {
            auto asset = assetValue.toObject();
            if (asset["name"].toString() == kAssetName) {
                assetUrl = asset["browser_download_url"].toString();
                break;
            }
        }
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
    if (ok) {
        QFile marker(checkedMarkerPath());
        if (marker.open(QIODevice::WriteOnly | QIODevice::Truncate))
            marker.write(QByteArray::number(QDateTime::currentSecsSinceEpoch()));
    }
    if (m_onDone)
        m_onDone(ok);
    deleteLater();
}
