/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-CLA-applies
 *
 * MuseScore Studio
 * Music Composition & Notation
 *
 * Copyright (C) 2025 MuseScore Limited and others
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

#include "musesamplerconfiguration.h"

#include "settings.h"

using namespace muse::musesampler;

static const muse::Settings::Key USE_LEGACY_AUDITION("musesampler", "museSampler/useLegacyAudition");

void MuseSamplerConfiguration::init()
{
    settings()->setDefaultValue(USE_LEGACY_AUDITION, Val(false));
}

muse::io::path_t MuseSamplerConfiguration::libraryPath() const
{
    // Used on the backend
    if (const char* path = std::getenv("MUSESAMPLER_PATH")) {
        return io::path_t(path);
    }

    return defaultLibraryPath();
}

#if defined(Q_OS_LINUX)
muse::io::path_t MuseSamplerConfiguration::defaultLibraryPath() const
{
    return globalConfig()->genericDataPath() + "/MuseSampler/lib/libMuseSamplerCoreLib.so";
}

#elif defined(Q_OS_MAC)
muse::io::path_t MuseSamplerConfiguration::defaultLibraryPath() const
{
    return globalConfig()->genericDataPath() + "/MuseSampler/lib/libMuseSamplerCoreLib.dylib";
}

#else
muse::io::path_t MuseSamplerConfiguration::defaultLibraryPath() const
{
    return globalConfig()->genericDataPath() + "/MuseSampler/lib/MuseSamplerCoreLib.dll";
}

#endif

muse::Version MuseSamplerConfiguration::minSupportedVersion() const
{
    //! 0.105.x 的库在控制台/无网络环境下 init 会抛不可捕获的异常（out_of_range），
    //! 这里抬高最低支持版本，直接跳过这版损坏的 MuseSampler 库。
    //! 架子鼓音色走 GM 鼓组 soundfont，不依赖 MuseSampler。
    return Version(0, 106, 0);
}

bool MuseSamplerConfiguration::shouldShowBuildNumber() const
{
    return globalConfig()->devModeEnabled();
}

bool MuseSamplerConfiguration::useLegacyAudition() const
{
    return settings()->value(USE_LEGACY_AUDITION).toBool();
}
