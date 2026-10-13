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
#ifndef MUSE_AUDIO_FFMPEGDECODER_H
#define MUSE_AUDIO_FFMPEGDECODER_H

#include <cstdint>
#include <vector>

namespace muse::audio::engine {

// 自包含的 FFmpeg 音频解码器：运行时 dlopen FFmpeg，把任意音频文件解码为
// 交错排列的 float PCM。用于 M4A/AAC/OGG 等内置解码器不支持的格式。
class FfmpegDecoder
{
public:
    FfmpegDecoder() = default;
    ~FfmpegDecoder();

    FfmpegDecoder(const FfmpegDecoder&) = delete;
    FfmpegDecoder& operator=(const FfmpegDecoder&) = delete;

    // 解码整个文件。成功时 interleaved 为 [frames * channels] 的 float 采样。
    bool decode(const uint8_t* data, size_t size, std::vector<float>& interleaved,
                unsigned int& channels, unsigned int& sampleRate);

private:
    struct Impl;
    Impl* m_impl = nullptr;
};
}

#endif // MUSE_AUDIO_FFMPEGDECODER_H
