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

#pragma once

#include <QObject>
#include <QString>
#include <functional>

#include "tasks/Task.h"

// Downloads the latest Wolfpack `mcupdater` build from the WolfpackMC/updater GitHub Releases
// "latest" release and caches it locally, so WolfpackUpdate doesn't require a manually
// configured updater path.
class WolfpackUpdaterFetch : public QObject {
    Q_OBJECT
   public:
    enum class VerifyResult {
        Match,        // local binary's hash matches the latest release
        Mismatch,     // local binary's hash differs from the latest release
        CheckFailed,  // couldn't determine (network error, or release has no published digest)
    };

    // Local path the cached updater binary lives at (platform-specific filename).
    static QString cachedBinaryPath();

    // Downloads the latest release asset to cachedBinaryPath(), replacing any existing copy.
    // Self-deletes once finished; safe to fire-and-forget. onDone(true) on success.
    static void fetchLatest(std::function<void(bool ok)> onDone);

    // Compares the sha256 digest of the latest release asset (as published on the GitHub
    // release) against the cached binary on disk, without downloading it.
    static void verifyLatest(std::function<void(VerifyResult result)> onDone);

   private:
    explicit WolfpackUpdaterFetch(std::function<void(bool ok)> onDone);
    explicit WolfpackUpdaterFetch(std::function<void(VerifyResult result)> onVerifyDone);

    void start();
    void startVerify();
    void finish(bool ok);
    void finishVerify(VerifyResult result);

    std::function<void(bool ok)> m_onDone;
    std::function<void(VerifyResult result)> m_onVerifyDone;
    Task::Ptr m_job;
};
