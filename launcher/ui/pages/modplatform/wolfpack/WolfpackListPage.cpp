// SPDX-License-Identifier: GPL-3.0-only
#include "WolfpackListPage.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QListWidget>
#include <QVBoxLayout>

#include "Application.h"
#include "InstanceImportTask.h"
#include "net/Download.h"
#include "net/NetJob.h"
#include "ui/dialogs/NewInstanceDialog.h"

namespace {
const QString kPacksManifestUrl = "https://wolfpackmc.s3.amazonaws.com/packs.json";
}

WolfpackListPage::WolfpackListPage(NewInstanceDialog* dialog, QWidget* parent) : QWidget(parent), m_dialog(dialog)
{
    auto layout = new QVBoxLayout(this);
    m_status = new QLabel(tr("Loading pack list..."), this);
    m_list = new QListWidget(this);
    layout->addWidget(m_status);
    layout->addWidget(m_list);
    setLayout(layout);

    connect(m_list, &QListWidget::currentItemChanged, this, &WolfpackListPage::selectionChanged);
}

void WolfpackListPage::openedImpl()
{
    if (!m_fetched)
        fetchPackList();
}

void WolfpackListPage::fetchPackList()
{
    m_fetched = true;
    m_status->setText(tr("Loading pack list..."));

    auto job = makeShared<NetJob>("Wolfpack::FetchPackList", APPLICATION->network());
    auto [action, response] = Net::Download::makeByteArray(QUrl(kPacksManifestUrl));
    job->addNetAction(action);

    connect(job.get(), &Task::failed, this, [this](const QString& reason) { m_status->setText(tr("Failed to load pack list: %1").arg(reason)); });
    connect(job.get(), &Task::succeeded, this, [this, response] {
        QJsonParseError parseError{};
        auto doc = QJsonDocument::fromJson(*response, &parseError);
        if (parseError.error != QJsonParseError::NoError) {
            m_status->setText(tr("Failed to parse pack list."));
            return;
        }

        m_list->clear();
        for (const auto& packValue : doc.object()["packs"].toArray()) {
            auto pack = packValue.toObject();
            auto name = pack["name"].toString();
            auto description = pack["description"].toString();
            auto mrpackUrl = pack["mrpackUrl"].toString();
            if (name.isEmpty() || mrpackUrl.isEmpty())
                continue;

            auto item = new QListWidgetItem(name, m_list);
            item->setData(Qt::UserRole, mrpackUrl);
            item->setData(Qt::UserRole + 1, description);
            item->setToolTip(description);
        }

        if (m_list->count() == 0) {
            m_status->setText(tr("No active packs available."));
        } else {
            m_status->setText(tr("Select a pack to install:"));
            m_list->setCurrentRow(0);
        }
    });

    m_fetchJob = job;
    job->start();
}

void WolfpackListPage::selectionChanged()
{
    auto item = m_list->currentItem();
    if (!item) {
        m_dialog->setSuggestedPack();
        return;
    }

    auto name = item->text();
    auto mrpackUrl = item->data(Qt::UserRole).toString();

    m_dialog->setSuggestedPack(name, new InstanceImportTask(QUrl(mrpackUrl), this));
    m_dialog->setSuggestedIcon("default");
}
