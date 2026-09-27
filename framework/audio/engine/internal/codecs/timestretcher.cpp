/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-Studio-CLA-applies
 *
 * MuseScore Studio
 * Music Composition & Notation
 *
 * Copyright (C) 2026 DB Score contributors
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
#include "timestretcher.h"

#include <cmath>

#include "soundtouch/SoundTouch.h"

using namespace muse::audio::engine;

bool muse::audio::engine::stretchAudio(const std::vector<float>& input, unsigned int channels, unsigned int sampleRate, float speed,
                                       std::vector<float>& output)
{
    if (channels == 0 || sampleRate == 0 || speed <= 0.f) {
        return false;
    }

    if (std::fabs(speed - 1.f) < 0.001f) {
        output = input;
        return true;
    }

    const size_t frames = input.size() / channels;

    soundtouch::SoundTouch stretcher;
    stretcher.setSampleRate(sampleRate);
    stretcher.setChannels(channels);
    stretcher.setTempo(speed);
    stretcher.setPitchSemiTones(0);

    stretcher.putSamples(input.data(), static_cast<uint>(frames));
    stretcher.flush();

    output.clear();
    output.reserve(input.size());

    float chunk[4096];
    while (const uint received = stretcher.receiveSamples(chunk, 4096)) {
        output.insert(output.end(), chunk, chunk + received * channels);
    }

    return true;
}
