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
#include "audiofiledecoder.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

#include "global/types/bytearray.h"
#include "log.h"

#include "wavdecoder.h"
#include "vorbisdecoder.h"
#include "timestretcher.h"

#define DR_FLAC_IMPLEMENTATION
#include "thirdparty/dr_flac.h"

#define DR_MP3_IMPLEMENTATION
#include "thirdparty/dr_mp3.h"

#define MINIMP4_IMPLEMENTATION
#define MP4D_INFO_SUPPORTED 1
#include "thirdparty/minimp4.h"

#include "fdk-aac/aacdecoder_lib.h"

using namespace muse::audio::engine;

namespace {
uint16_t readBe16(const uint8_t* p)
{
    return uint16_t(p[0]) << 8 | uint16_t(p[1]);
}

uint32_t readBe32(const uint8_t* p)
{
    return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | uint32_t(p[3]);
}

double readAiff80(const uint8_t* p)
{
    const int exponent = (int(p[0] & 0x7f) << 8) | int(p[1]);
    uint32_t mantissa = readBe32(p + 2);
    if (exponent == 0 && mantissa == 0) {
        return 0.0;
    }

    return double(mantissa) * std::pow(2.0, exponent - 16383 - 31);
}

struct Mp4MemoryCtx {
    const std::vector<uint8_t>* bytes = nullptr;
};

int mp4ReadCallback(int64_t offset, void* buffer, size_t size, void* token)
{
    const Mp4MemoryCtx* ctx = static_cast<const Mp4MemoryCtx*>(token);
    if (!ctx || !ctx->bytes || offset < 0 || size_t(offset) > ctx->bytes->size()) {
        return 0;
    }

    const size_t available = ctx->bytes->size() - size_t(offset);
    const size_t count = std::min(size, available);
    std::memcpy(buffer, ctx->bytes->data() + offset, count);
    return static_cast<int>(count);
}

bool decodeFdkFrame(HANDLE_AACDECODER decoder, const uint8_t* data, size_t size, std::vector<float>& out,
                    unsigned int& channels, unsigned int& sampleRate)
{
    UCHAR* buffer[] = { const_cast<UCHAR*>(data) };
    UINT bufferSize[] = { static_cast<UINT>(size) };
    UINT bytesValid = static_cast<UINT>(size);

    if (aacDecoder_Fill(decoder, buffer, bufferSize, &bytesValid) != AAC_DEC_OK) {
        return false;
    }

    CStreamInfo* info = aacDecoder_GetStreamInfo(decoder);
    if (!info || info->numChannels == 0 || info->frameSize == 0) {
        return false;
    }

    channels = info->numChannels;
    sampleRate = info->sampleRate;

    std::vector<INT_PCM> pcm(info->frameSize * info->numChannels);
    if (aacDecoder_DecodeFrame(decoder, pcm.data(), static_cast<INT>(pcm.size()), 0) != AAC_DEC_OK) {
        return false;
    }

    const size_t oldSize = out.size();
    out.resize(oldSize + pcm.size());
    for (size_t i = 0; i < pcm.size(); ++i) {
        out[oldSize + i] = float(pcm[i]) / 32768.f;
    }

    return true;
}
}

bool AudioFileDecoder::open(muse::io::IODevice* device)
{
    if (!device || !device->isReadable()) {
        return false;
    }

    device->seek(0);
    uint8_t head[12];
    const size_t headSize = device->read(head, sizeof(head));
    device->seek(0);

    if (headSize >= 12 && std::memcmp(head, "RIFF", 4) == 0 && std::memcmp(head + 8, "WAVE", 4) == 0) {
        return decodeWav(device);
    }

    const muse::ByteArray all = device->readAll();
    const std::vector<uint8_t>& bytes = all.constVData();
    if (bytes.empty()) {
        return false;
    }

    if (headSize >= 4 && std::memcmp(head, "FORM", 4) == 0) {
        return decodeAiff(bytes);
    }

    if (bytes.size() >= 4 && std::memcmp(bytes.data(), "fLaC", 4) == 0) {
        return decodeFlac(bytes);
    }

    if (bytes.size() >= 4 && std::memcmp(bytes.data(), "OggS", 4) == 0) {
        return decodeOgg(bytes);
    }

    if (bytes.size() >= 12 && std::memcmp(bytes.data() + 4, "ftyp", 4) == 0) {
        return decodeMp4(bytes);
    }

    if (bytes.size() >= 2 && bytes[0] == 0xff && (bytes[1] & 0xf0) == 0xf0 && (bytes[1] & 0x06) == 0x00) {
        return decodeAdts(bytes);
    }

    if (bytes.size() >= 3 && std::memcmp(bytes.data(), "ID3", 3) == 0) {
        return decodeMp3(bytes);
    }

    if (bytes.size() >= 2 && bytes[0] == 0xff && (bytes[1] & 0xe0) == 0xe0) {
        return decodeMp3(bytes);
    }

    LOGE() << "Unsupported audio format, file size: " << bytes.size()
           << ", first bytes: " << int(head[0]) << " " << int(head[1]) << " " << int(head[2])
           << " " << int(head[3]) << " " << int(head[4]) << " " << int(head[5]);
    return false;
}

bool AudioFileDecoder::decodeWav(muse::io::IODevice* device)
{
    WavDecoder decoder;
    if (!decoder.open(device)) {
        return false;
    }

    m_channels = decoder.channels();
    m_sampleRate = decoder.sampleRate();
    m_data = decoder.data();
    return true;
}

bool AudioFileDecoder::decodeAiff(const std::vector<uint8_t>& bytes)
{
    if (bytes.size() < 12
        || (std::memcmp(bytes.data() + 8, "AIFF", 4) != 0 && std::memcmp(bytes.data() + 8, "AIFC", 4) != 0)) {
        return false;
    }

    size_t pos = 12;
    uint16_t channels = 0;
    uint32_t sampleRate = 0;
    uint16_t bitsPerSample = 0;
    size_t sampleDataOffset = 0;
    size_t sampleDataSize = 0;

    while (pos + 8 <= bytes.size()) {
        const uint32_t chunkSize = readBe32(bytes.data() + pos + 4);
        const size_t chunkStart = pos + 8;

        if (std::memcmp(bytes.data() + pos, "COMM", 4) == 0) {
            if (chunkStart + 18 > bytes.size()) {
                return false;
            }
            channels = readBe16(bytes.data() + chunkStart);
            sampleRate = uint32_t(readAiff80(bytes.data() + chunkStart + 8));
            bitsPerSample = readBe16(bytes.data() + chunkStart + 6);
        } else if (std::memcmp(bytes.data() + pos, "SSND", 4) == 0) {
            if (chunkStart + 8 > bytes.size()) {
                return false;
            }
            const uint32_t offset = readBe32(bytes.data() + chunkStart);
            sampleDataOffset = chunkStart + 8 + offset;
            sampleDataSize = chunkSize - 8 - offset;
        }

        pos = chunkStart + chunkSize + (chunkSize & 1);
    }

    if (channels == 0 || sampleRate == 0 || bitsPerSample == 0 || sampleDataOffset == 0 || sampleDataSize == 0) {
        return false;
    }

    const size_t bytesPerSample = bitsPerSample / 8;
    const size_t totalFrames = sampleDataSize / (channels * bytesPerSample);
    if (totalFrames == 0 || sampleDataOffset + totalFrames * channels * bytesPerSample > bytes.size()) {
        return false;
    }

    m_channels = channels;
    m_sampleRate = sampleRate;
    m_data.resize(totalFrames * channels);

    for (size_t i = 0; i < totalFrames; ++i) {
        for (size_t c = 0; c < channels; ++c) {
            const uint8_t* p = bytes.data() + sampleDataOffset + (i * channels + c) * bytesPerSample;
            float value = 0.f;
            switch (bitsPerSample) {
            case 16: {
                int16_t s = int16_t(readBe16(p));
                value = float(s) / 32768.f;
            } break;
            case 24: {
                int32_t s = int32_t(p[0]) << 16 | int32_t(p[1]) << 8 | int32_t(p[2]);
                if (s & 0x800000) {
                    s |= ~0xffffff;
                }
                value = float(s) / 8388608.f;
            } break;
            case 32: {
                int32_t s = int32_t(readBe32(p));
                value = float(double(s) / 2147483648.0);
            } break;
            default:
                return false;
            }

            m_data[i * channels + c] = value;
        }
    }

    return true;
}

bool AudioFileDecoder::decodeFlac(const std::vector<uint8_t>& bytes)
{
    drflac* flac = drflac_open_memory(bytes.data(), bytes.size(), nullptr);
    if (!flac) {
        return false;
    }

    m_channels = flac->channels;
    m_sampleRate = flac->sampleRate;
    const uint64_t frames = flac->totalPCMFrameCount;
    m_data.resize(frames * m_channels);
    const uint64_t read = drflac_read_pcm_frames_f32(flac, frames, m_data.data());
    drflac_close(flac);

    return read == frames;
}

bool AudioFileDecoder::decodeOgg(const std::vector<uint8_t>& bytes)
{
    muse::audio::codec::VorbisDecoder decoder;
    short* output = nullptr;
    unsigned int channels = 0;
    unsigned int sampleRate = 0;

    const int frames = decoder.decode_memory(bytes.data(), unsigned(bytes.size()), &output, &channels, &sampleRate);
    if (frames <= 0 || !output) {
        return false;
    }

    m_channels = channels;
    m_sampleRate = sampleRate;
    m_data.resize(frames * channels);
    for (int i = 0; i < frames * int(channels); ++i) {
        m_data[i] = float(output[i]) / 32768.f;
    }

    std::free(output);
    return true;
}

bool AudioFileDecoder::decodeMp3(const std::vector<uint8_t>& bytes)
{
    drmp3 mp3;
    if (!drmp3_init_memory(&mp3, bytes.data(), bytes.size(), nullptr)) {
        return false;
    }

    m_channels = mp3.channels;
    m_sampleRate = mp3.sampleRate;
    const uint64_t frames = drmp3_get_pcm_frame_count(&mp3);
    m_data.resize(frames * m_channels);
    const uint64_t read = drmp3_read_pcm_frames_f32(&mp3, frames, m_data.data());
    drmp3_uninit(&mp3);

    return read == frames;
}

bool AudioFileDecoder::decodeMp4(const std::vector<uint8_t>& bytes)
{
    MP4D_demux_t mp4 = {};
    Mp4MemoryCtx ctx;
    ctx.bytes = &bytes;

    if (!MP4D_open(&mp4, mp4ReadCallback, &ctx, int64_t(bytes.size()))) {
        return false;
    }

    int audioTrack = -1;
    for (unsigned int i = 0; i < mp4.track_count; ++i) {
        const MP4D_track_t& track = mp4.track[i];
        const bool isAudio = track.handler_type == 0x736f756e /* 'soun' */
                             && track.dsi && track.dsi_bytes > 0;
        if (isAudio) {
            audioTrack = int(i);
            break;
        }
    }

    if (audioTrack < 0) {
        MP4D_close(&mp4);
        return false;
    }

    const MP4D_track_t& track = mp4.track[audioTrack];

    HANDLE_AACDECODER decoder = aacDecoder_Open(TT_MP4_RAW, 1);
    if (!decoder) {
        MP4D_close(&mp4);
        return false;
    }

    UCHAR* conf[] = { track.dsi };
    UINT confLength[] = { static_cast<UINT>(track.dsi_bytes) };
    if (aacDecoder_ConfigRaw(decoder, conf, confLength) != AAC_DEC_OK) {
        aacDecoder_Close(decoder);
        MP4D_close(&mp4);
        return false;
    }

    std::vector<float> output;
    unsigned int channels = 0;
    unsigned int sampleRate = 0;

    for (unsigned int sample = 0; sample < track.sample_count; ++sample) {
        unsigned int frameBytes = 0;
        const MP4D_file_offset_t offset = MP4D_frame_offset(&mp4, unsigned(audioTrack), sample, &frameBytes, nullptr, nullptr);
        if (offset < 0 || frameBytes == 0 || size_t(offset) + frameBytes > bytes.size()) {
            break;
        }

        decodeFdkFrame(decoder, bytes.data() + offset, frameBytes, output, channels, sampleRate);
    }

    aacDecoder_Close(decoder);
    MP4D_close(&mp4);

    if (output.empty() || channels == 0 || sampleRate == 0) {
        return false;
    }

    m_channels = channels;
    m_sampleRate = sampleRate;
    m_data = std::move(output);
    return true;
}

bool AudioFileDecoder::decodeAdts(const std::vector<uint8_t>& bytes)
{
    HANDLE_AACDECODER decoder = aacDecoder_Open(TT_MP4_ADTS, 1);
    if (!decoder) {
        return false;
    }

    UCHAR* buffer[] = { const_cast<UCHAR*>(bytes.data()) };
    UINT bufferSize[] = { static_cast<UINT>(bytes.size()) };
    UINT bytesValid = static_cast<UINT>(bytes.size());
    if (aacDecoder_Fill(decoder, buffer, bufferSize, &bytesValid) != AAC_DEC_OK) {
        aacDecoder_Close(decoder);
        return false;
    }

    std::vector<float> output;
    unsigned int channels = 0;
    unsigned int sampleRate = 0;

    while (CStreamInfo* info = aacDecoder_GetStreamInfo(decoder)) {
        if (info->numChannels == 0 || info->frameSize == 0) {
            break;
        }

        channels = info->numChannels;
        sampleRate = info->sampleRate;

        std::vector<INT_PCM> pcm(info->frameSize * info->numChannels);
        const AAC_DECODER_ERROR err = aacDecoder_DecodeFrame(decoder, pcm.data(), static_cast<INT>(pcm.size()), 0);
        if (err == AAC_DEC_NOT_ENOUGH_BITS) {
            break;
        }
        if (err != AAC_DEC_OK) {
            aacDecoder_Close(decoder);
            return false;
        }

        const size_t oldSize = output.size();
        output.resize(oldSize + pcm.size());
        for (size_t i = 0; i < pcm.size(); ++i) {
            output[oldSize + i] = float(pcm[i]) / 32768.f;
        }
    }

    aacDecoder_Close(decoder);

    if (output.empty() || channels == 0 || sampleRate == 0) {
        return false;
    }

    m_channels = channels;
    m_sampleRate = sampleRate;
    m_data = std::move(output);
    return true;
}

void AudioFileDecoder::crop(double startSeconds, double endSeconds)
{
    if (!isValid()) {
        return;
    }

    const uint64_t totalFrames = frames();
    uint64_t startFrame = uint64_t(std::max(0.0, startSeconds * m_sampleRate));
    uint64_t endFrame = endSeconds > 0.0 ? uint64_t(endSeconds * m_sampleRate) : totalFrames;

    startFrame = std::min(startFrame, totalFrames);
    endFrame = std::min(endFrame, totalFrames);
    if (endFrame <= startFrame) {
        m_data.clear();
        return;
    }

    const size_t start = startFrame * m_channels;
    const size_t end = endFrame * m_channels;
    m_data = std::vector<float>(m_data.begin() + start, m_data.begin() + end);
}

bool AudioFileDecoder::applyTimeStretch(float speed)
{
    if (!isValid()) {
        return false;
    }

    std::vector<float> stretched;
    if (!stretchAudio(m_data, m_channels, m_sampleRate, speed, stretched)) {
        return false;
    }

    m_data = std::move(stretched);
    return true;
}
