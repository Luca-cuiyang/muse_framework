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
#ifndef MUSE_AUDIO_WAVDECODER_H
#define MUSE_AUDIO_WAVDECODER_H

#include <cstdint>
#include <vector>

#include "global/io/iodevice.h"

namespace muse::audio::engine {
class WavDecoder
{
public:
    bool open(muse::io::IODevice* device);

    bool isValid() const { return !m_data.empty(); }
    unsigned int channels() const { return m_channels; }
    unsigned int sampleRate() const { return m_sampleRate; }
    uint64_t frames() const { return m_data.empty() ? 0 : m_data.size() / m_channels; }
    const std::vector<float>& data() const { return m_data; }

private:
    bool readHeader(muse::io::IODevice* device, uint16_t& audioFormat, uint16_t& channels, uint32_t& sampleRate,
                    uint16_t& bitsPerSample, uint32_t& dataSize, size_t& dataOffset);

    unsigned int m_channels = 0;
    unsigned int m_sampleRate = 0;
    std::vector<float> m_data;
};
}

#endif // MUSE_AUDIO_WAVDECODER_H
