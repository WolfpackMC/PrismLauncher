// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QWidget>

#include "tasks/Task.h"
#include "ui/pages/BasePage.h"

class QListWidget;
class QLabel;
class QComboBox;
class QLineEdit;
class NewInstanceDialog;

class WolfpackListPage : public QWidget, public BasePage {
    Q_OBJECT

   public:
    explicit WolfpackListPage(NewInstanceDialog* dialog, QWidget* parent = nullptr);
    virtual ~WolfpackListPage() = default;

    virtual QString displayName() const override { return tr("Wolfpack"); }
    virtual QIcon icon() const override { return QIcon::fromTheme("wolfpack"); }
    virtual QString id() const override { return "wolfpack"; }
    virtual QString helpPage() const override { return QString(); }
    virtual bool shouldDisplay() const override { return true; }

    void openedImpl() override;

   private slots:
    void selectionChanged();

   private:
    void fetchPackList();

    NewInstanceDialog* m_dialog;
    QListWidget* m_list;
    QLabel* m_status;
    QComboBox* m_profileComboBox;
    QLineEdit* m_modpackIdLineEdit;
    Task::Ptr m_fetchJob;
    bool m_fetched = false;
};
