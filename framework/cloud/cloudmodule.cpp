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

#include "cloudmodule.h"

#include "modularity/ioc.h"
#include "interactive/iinteractiveuriregister.h"

#ifdef MUSE_MODULE_CLOUD_DBSCORECLOUD
#include "dbscorecloud/dbscorecloudservice.h"
#endif
#include "dbscoreaudio/dbscoreaudioservice.h"
#include "internal/cloudconfiguration.h"

using namespace muse;
using namespace muse::cloud;
using namespace muse::modularity;

std::string CloudModule::moduleName() const
{
    return "cloud";
}

void CloudModule::registerExports()
{
    m_cloudConfiguration = std::make_shared<CloudConfiguration>(globalCtx());
    globalIoc()->registerExport<ICloudConfiguration>(moduleName(), m_cloudConfiguration);
#ifdef MUSE_MODULE_CLOUD_DBSCORECLOUD
    m_dbScoreCloudService = std::make_shared<DBScoreCloudService>(globalCtx());
    globalIoc()->registerExport<IDBScoreCloudService>(moduleName(), m_dbScoreCloudService);
#endif
    m_dbScoreAudioService = std::make_shared<DBScoreAudioService>(globalCtx());
    globalIoc()->registerExport<IDBScoreAudioService>(moduleName(), m_dbScoreAudioService);
}

void CloudModule::resolveImports()
{
    auto ir = globalIoc()->resolve<interactive::IInteractiveUriRegister>(moduleName());
    if (ir) {
        ir->registerQmlUri(Uri("muse://cloud/requireauthorization"), "Muse.Cloud", "RequireAuthorizationDialog");
    }
}

void CloudModule::onInit(const IApplication::RunMode&)
{
    m_cloudConfiguration->init();
#ifdef MUSE_MODULE_CLOUD_DBSCORECLOUD
    m_dbScoreCloudService->init();
#endif
    m_dbScoreAudioService->init();
}
