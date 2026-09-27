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
#include "wavdecoder.h"

#include <cstring>
#include <limits>

using namespace muse::audio::engine;

namespace {
uint16_t readU16(const uint8_t* p)
{
    return uint16_t(p[0]) | (uint16_t(p[1]) << 8);
}

uint32_t readU32(const uint8_t* p)
{
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
}

bool WavDecoder::readHeader(muse::io::IODevice* device, uint16_t& audioFormat, uint16_t& channels, uint32_t& sampleRate,
                            uint16_t& bitsPerSample, uint32_t& dataSize, size_t& dataOffset)
{
    device->seek(0);

    uint8_t riff[12];
    if (device->read(riff, sizeof(riff)) != sizeof(riff) || std::memcmp(riff, "RIFF", 4) != 0
        || std::memcmp(riff + 8, "WAVE", 4) != 0) {
        return false;
    }

    bool foundFmt = false;
    bool foundData = false;

    uint8_t chunkHeader[8];
    while (device->read(chunkHeader, sizeof(chunkHeader)) == sizeof(chunkHeader)) {
        uint32_t chunkSize = readU32(chunkHeader + 4);

        if (std::memcmp(chunkHeader, "fmt ", 4) == 0) {
            if (chunkSize < 16) {
                return false;
            }

            uint8_t fmt[16];
            if (device->read(fmt, sizeof(fmt)) != sizeof(fmt)) {
                return false;
            }

            audioFormat = readU16(fmt);
            channels = readU16(fmt + 2);
            sampleRate = readU32(fmt + 4);
            bitsPerSample = readU16(fmt + 14);
            foundFmt = true;

            if (chunkSize > 16) {
                device->seek(device->pos() + (chunkSize - 16));
            }
        } else if (std::memcmp(chunkHeader, "data", 4) == 0) {
            dataSize = chunkSize;
            dataOffset = device->pos();
            foundData = true;
            break;
        } else {
            device->seek(device->pos() + chunkSize + (chunkSize & 1));
        }
    }

    return foundFmt && foundData;
}

bool WavDecoder::open(muse::io::IODevice* device)
{
    if (!device || !device->isReadable()) {
        return false;
    }

    uint16_t audioFormat = 0;
    uint16_t channels = 0;
    uint16_t bitsPerSample = 0;
    uint32_t sampleRate = 0;
    uint32_t dataSize = 0;
    size_t dataOffset = 0;

    if (!readHeader(device, audioFormat, channels, sampleRate, bitsPerSample, dataSize, dataOffset)) {
        return false;
    }

    if (audioFormat != 1 && audioFormat != 3) {
        // Only PCM and IEEE float are currently supported.
        return false;
    }

    if (channels == 0 || sampleRate == 0 || bitsPerSample == 0) {
        return false;
    }

    m_channels = channels;
    m_sampleRate = sampleRate;

    const size_t bytesPerSample = bitsPerSample / 8;
    const size_t totalFrames = dataSize / (m_channels * bytesPerSample);
    if (totalFrames == 0) {
        return false;
    }

    device->seek(dataOffset);
    std::vector<uint8_t> raw(totalFrames * m_channels * bytesPerSample);
    if (device->read(raw.data(), raw.size()) != raw.size()) {
        return false;
    }

    m_data.resize(totalFrames * m_channels);

    for (size_t i = 0; i < totalFrames; ++i) {
        for (size_t c = 0; c < m_channels; ++c) {
            const uint8_t* p = raw.data() + (i * m_channels + c) * bytesPerSample;
            float value = 0.f;

            if (audioFormat == 3 && bitsPerSample == 32) {
                uint32_t bits = readU32(p);
                float f = 0.f;
                std::memcpy(&f, &bits, sizeof(f));
                value = f;
            } else if (audioFormat == 1) {
                switch (bitsPerSample) {
                case 8: {
                    const float v = float(p[0]) / std::numeric_limits<uint8_t>::max();
                    value = v * 2.f - 1.f;
                } break;
                case 16: {
                    int16_t s = int16_t(readU16(p));
                    value = float(s) / 32768.f;
                } break;
                case 24: {
                    int32_t s = int32_t(p[0]) | (int32_t(p[1]) << 8) | (int32_t(p[2]) << 16);
                    if (s & 0x800000) {
                        s |= ~0xffffff;
                    }
                    value = float(s) / 8388608.f;
                } break;
                case 32: {
                    int32_t s = int32_t(readU32(p));
                    value = float(double(s) / 2147483648.0);
                } break;
                default:
                    return false;
                }
            } else {
                return false;
            }

            m_data[i * m_channels + c] = value;
        }
    }

    return true;
}
