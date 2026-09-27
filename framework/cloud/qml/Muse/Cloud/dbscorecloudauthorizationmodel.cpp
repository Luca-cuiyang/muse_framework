/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-CLA-applies
 *
 * MuseScore Studio
 * Music Composition & Notation
 *
 * Copyright (C) 2021 MuseScore Limited and others
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "dbscorecloudauthorizationmodel.h"

using namespace muse::cloud;

DBScoreCloudAuthorizationModel::DBScoreCloudAuthorizationModel(QObject* parent)
    : QObject(parent), Contextable(muse::iocCtxForQmlObject(this))
{
}

void DBScoreCloudAuthorizationModel::load()
{
    emit userAuthorizedChanged();

    dbScoreCloudService()->authorization()->userAuthorized().ch.onReceive(this, [this](bool) {
        emit userAuthorizedChanged();
    });
}

bool DBScoreCloudAuthorizationModel::userAuthorized() const
{
    return dbScoreCloudService()->authorization()->userAuthorized().val;
}

void DBScoreCloudAuthorizationModel::createAccount()
{
    dbScoreCloudService()->authorization()->signUp();
}

void DBScoreCloudAuthorizationModel::signIn()
{
    dbScoreCloudService()->authorization()->signIn();
}

void DBScoreCloudAuthorizationModel::signOut()
{
    dbScoreCloudService()->authorization()->signOut();
}
