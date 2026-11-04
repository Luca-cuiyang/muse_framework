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
#include "networksfloader.h"

#include <emscripten/val.h>

using namespace muse;
using namespace muse::audio::synth;

RetVal<ByteArray> NetworkSFLoader::load(const Uri& uri)
{
    emscripten::val request = emscripten::val::global("XMLHttpRequest").new_();
    request.call<void>("open", std::string("GET"), uri.toString(), false);
    request.set("responseType", std::string("arraybuffer"));
    request.call<void>("send");

    const int status = request["status"].as<int>();
    if (status < 200 || status >= 300) {
        LOGE() << "failed load sound font: " << uri << ", status: " << status;
        return RetVal<ByteArray>::make_ret(status, "failed load sound font");
    }

    emscripten::val response = request["response"];
    const size_t length = response["byteLength"].as<size_t>();
    ByteArray data(length);
    emscripten::val target = emscripten::val::global("Uint8Array").new_(
        emscripten::typed_memory_view(length, data.data()));
    target.call<void>("set", emscripten::val::global("Uint8Array").new_(response));

    LOGI() << "loaded sound font: " << uri << ", size: " << length;
    return RetVal<ByteArray>::make_ok(std::move(data));
}
