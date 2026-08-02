// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Prism Launcher - Minecraft Launcher
 *  Copyright (c) 2022 flowln <flowlnlnln@gmail.com>
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

#include "ResourceFolderLoadTask.h"

#include "Application.h"
#include "FileSystem.h"
#include "minecraft/mod/MetadataHandler.h"

#include <QThread>
#include <QtConcurrentMap>

ResourceFolderLoadTask::ResourceFolderLoadTask(const QDir& resource_dir,
                                               const QDir& index_dir,
                                               bool is_indexed,
                                               bool clean_orphan,
                                               std::function<Resource*(const QFileInfo&)> create_function)
    : Task(false)
    , m_resource_dir(resource_dir)
    , m_index_dir(index_dir)
    , m_is_indexed(is_indexed)
    , m_clean_orphan(clean_orphan)
    , m_create_func(create_function)
    , m_result(new Result())
    , m_thread_to_spawn_into(thread())
{}

void ResourceFolderLoadTask::executeTask()
{
    if (thread() != m_thread_to_spawn_into)
        connect(this, &Task::finished, this->thread(), &QThread::quit);

    if (m_is_indexed) {
        // Read metadata first
        getFromMetadata();
    }

    // Read JAR files that don't have metadata
    m_resource_dir.refresh();

    // Resolve filename collisions first (renames on disk, so must stay sequential).
    QList<QFileInfo> toParse;
    for (auto entry : m_resource_dir.entryInfoList()) {
        auto filePath = entry.absoluteFilePath();
        if (auto app = APPLICATION_DYN; app && app->checkQSavePath(filePath)) {
            continue;
        }
        auto newFilePath = FS::getUniqueResourceName(filePath);
        if (newFilePath != filePath) {
            FS::move(filePath, newFilePath);
            entry = QFileInfo(newFilePath);
        }
        toParse.append(entry);
    }

    // Constructing a Resource stats the file (size, type, dates, etc.), and each one is
    // independent until merged into m_result below, so do that part in parallel. A dedicated
    // pool is used instead of the global one: this task itself already runs on a QThreadPool
    // worker thread, and blocking on the global pool from inside the global pool can starve/
    // deadlock it if there are no free worker slots left.
    QThreadPool parsePool;
    parsePool.setMaxThreadCount(qMax(1, QThread::idealThreadCount()));
    // QObject::moveToThread() may only be called by the thread the object currently lives in,
    // so each Resource must be handed off to m_thread_to_spawn_into from within the very
    // worker thread that constructed it (below) rather than afterwards from whatever thread
    // this task happens to run on.
    auto parsedResources = QtConcurrent::blockingMapped(&parsePool, toParse, [this](const QFileInfo& entry) -> Resource* {
        Resource* resource = m_create_func(entry);
        resource->moveToThread(m_thread_to_spawn_into);
        return resource;
    });

    // Merge results in the original order, same logic/semantics as before.
    for (Resource* resource : parsedResources) {
        if (resource->enabled()) {
            if (m_result->resources.contains(resource->internal_id())) {
                m_result->resources[resource->internal_id()]->setStatus(ResourceStatus::INSTALLED);
                // Delete the object we just created, since a valid one is already in the mods list.
                delete resource;
            } else {
                m_result->resources[resource->internal_id()].reset(resource);
                m_result->resources[resource->internal_id()]->setStatus(ResourceStatus::NO_METADATA);
            }
        } else {
            QString chopped_id = resource->internal_id().chopped(9);
            if (m_result->resources.contains(chopped_id)) {
                m_result->resources[resource->internal_id()].reset(resource);

                auto metadata = m_result->resources[chopped_id]->metadata();
                if (metadata) {
                    resource->setMetadata(*metadata);

                    m_result->resources[resource->internal_id()]->setStatus(ResourceStatus::INSTALLED);
                    m_result->resources.remove(chopped_id);
                }
            } else {
                m_result->resources[resource->internal_id()].reset(resource);
                m_result->resources[resource->internal_id()]->setStatus(ResourceStatus::NO_METADATA);
            }
        }
    }

    // Remove orphan metadata to prevent issues
    // See https://github.com/PolyMC/PolyMC/issues/996
    if (m_clean_orphan) {
        QMutableMapIterator iter(m_result->resources);
        while (iter.hasNext()) {
            auto resource = iter.next().value();
            if (resource->status() == ResourceStatus::NOT_INSTALLED) {
                resource->destroy(m_index_dir, false, false);
                iter.remove();
            }
        }
    }

    // Resources from the parallel pass above already live on m_thread_to_spawn_into; only
    // move the ones from getFromMetadata() (constructed sequentially on this thread).
    for (auto mod : m_result->resources) {
        if (mod->thread() != m_thread_to_spawn_into)
            mod->moveToThread(m_thread_to_spawn_into);
    }

    if (m_aborted)
        emit finished();
    else
        emitSucceeded();
}

void ResourceFolderLoadTask::getFromMetadata()
{
    m_index_dir.refresh();
    for (auto entry : m_index_dir.entryList(QDir::Files)) {
        if (!entry.endsWith(".pw.toml")) {
            continue;
        }

        auto metadata = Metadata::get(m_index_dir, entry);

        if (!metadata.isValid())
            continue;

        auto* resource = m_create_func(QFileInfo(m_resource_dir.filePath(metadata.filename)));
        resource->setMetadata(metadata);
        resource->setStatus(ResourceStatus::NOT_INSTALLED);
        m_result->resources[resource->internal_id()].reset(resource);
    }
}
