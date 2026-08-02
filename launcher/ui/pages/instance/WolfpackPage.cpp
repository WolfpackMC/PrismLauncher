// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Prism Launcher - Minecraft Launcher
 *  Copyright (c) 2022 Jamie Mansfield <jmansfield@cadixdev.org>
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

#include "WolfpackPage.h"
#include "ui_WolfpackPage.h"

WolfpackPage::WolfpackPage(BaseInstance* inst, QWidget* parent) : QWidget(parent), ui(new Ui::WolfpackPage), m_inst(inst)
{
    ui->setupUi(this);
    ui->enabledCheckbox->setChecked(m_inst->isWolfpackInstance());
    ui->profileComboBox->setCurrentIndex(m_inst->wolfpackProfile() == "minimal" ? 1 : 0);
    ui->modpackIdLineEdit->setText(m_inst->wolfpackModpackId());

    if (m_inst->nameImpliesWolfpack()) {
        ui->enabledCheckbox->setEnabled(false);
        ui->enabledCheckbox->setToolTip(
            tr("This instance's name contains \"Wolfpack\" or \"WFP\", so it's automatically treated as a Wolfpack instance."));
    }
}

WolfpackPage::~WolfpackPage()
{
    delete ui;
}

bool WolfpackPage::apply()
{
    m_inst->setWolfpackEnabled(ui->enabledCheckbox->isChecked());
    m_inst->setWolfpackProfile(ui->profileComboBox->currentIndex() == 1 ? "minimal" : "all");
    auto modpackId = ui->modpackIdLineEdit->text().trimmed();
    m_inst->setWolfpackModpackId(modpackId.isEmpty() ? "wfp" : modpackId);
    return true;
}

void WolfpackPage::retranslate()
{
    ui->retranslateUi(this);
}
