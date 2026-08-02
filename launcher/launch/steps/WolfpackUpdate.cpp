// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Prism Launcher - Minecraft Launcher
 *  Copyright (C) 2022 Sefa Eyeoglu <contact@scrumplex.net>
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 *      Copyright 2013-2021 MultiMC Contributors
 *
 *      Licensed under the Apache License, Version 2.0 (the "License");
 *      you may not use this file except in compliance with the License.
 *      You may obtain a copy of the License at
 *
 *          http://www.apache.org/licenses/LICENSE-2.0
 *
 *      Unless required by applicable law or agreed to in writing, software
 *      distributed under the License is distributed on an "AS IS" BASIS,
 *      WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *      See the License for the specific language governing permissions and
 *      limitations under the License.
 */

#include "WolfpackUpdate.h"
#include <launch/LaunchTask.h>
#include <QFile>
#include "WolfpackUpdaterFetch.h"

WolfpackUpdate::WolfpackUpdate(LaunchTask* parent) : LaunchStep(parent)
{
    m_instance = m_parent->instance();
    m_process.setProcessEnvironment(m_instance->createEnvironment());
    connect(&m_process, &LoggedProcess::log, this, &WolfpackUpdate::logLines);
    connect(&m_process, &LoggedProcess::stateChanged, this, &WolfpackUpdate::on_state);
}

void WolfpackUpdate::executeTask()
{
    auto cachePath = WolfpackUpdaterFetch::cachedBinaryPath();
    if (!QFile::exists(cachePath)) {
        setStatus(tr("Downloading Wolfpack updater..."));
        WolfpackUpdaterFetch::fetchLatest([this, cachePath](bool ok) {
            if (ok) {
                runUpdater(cachePath);
            } else {
                auto error = tr("Failed to download the Wolfpack updater.");
                emit logLine(error, MessageLevel::Fatal);
                emitFailed(error);
            }
        });
        return;
    }

    setStatus(tr("Checking Wolfpack updater version..."));
    WolfpackUpdaterFetch::verifyLatest([this, cachePath](WolfpackUpdaterFetch::VerifyResult result) {
        if (result != WolfpackUpdaterFetch::VerifyResult::Mismatch) {
            runUpdater(cachePath);
            return;
        }

        emit logLine(tr("Wolfpack updater is outdated, downloading latest version..."), MessageLevel::Launcher);
        WolfpackUpdaterFetch::fetchLatest([this, cachePath](bool ok) {
            if (!ok)
                emit logLine(tr("Failed to update the Wolfpack updater, using cached copy."), MessageLevel::Warning);
            runUpdater(cachePath);
        });
    });
}

void WolfpackUpdate::runUpdater(const QString& path)
{
    m_updaterPath = path;
    QStringList args{ "--profile", m_instance->wolfpackProfile(), "--modpack", m_instance->wolfpackModpackId() };
    emit logLine(tr("Running Wolfpack updater: %1 %2").arg(m_updaterPath, args.join(' ')), MessageLevel::Launcher);
    m_process.start(m_updaterPath, args);
}

void WolfpackUpdate::on_state(LoggedProcess::State state)
{
    auto getError = [this]() { return tr("Wolfpack updater failed with code %1.\n\n").arg(m_process.exitCode()); };
    switch (state) {
        case LoggedProcess::Aborted:
        case LoggedProcess::Crashed:
        case LoggedProcess::FailedToStart: {
            auto error = getError();
            emit logLine(error, MessageLevel::Fatal);
            emitFailed(error);
            return;
        }
        case LoggedProcess::Finished: {
            if (m_process.exitCode() != 0) {
                auto error = getError();
                emit logLine(error, MessageLevel::Fatal);
                emitFailed(error);
            } else {
                emit logLine(tr("Wolfpack updater ran successfully.\n\n"), MessageLevel::Launcher);
                emitSucceeded();
            }
        }
        default:
            break;
    }
}

void WolfpackUpdate::setWorkingDirectory(const QString& wd)
{
    m_process.setWorkingDirectory(wd);
}

bool WolfpackUpdate::abort()
{
    auto state = m_process.state();
    if (state == LoggedProcess::Running || state == LoggedProcess::Starting) {
        m_process.kill();
    }
    return true;
}
