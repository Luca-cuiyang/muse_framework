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
#include "ffmpegdecoder.h"

#include <cstring>
#include <cstdio>
#include <string>

#include "global/dlib.h"

extern "C" {
#include "media/thirdparty/ffmpeg/v8/libavformat/avformat.h"
#include "media/thirdparty/ffmpeg/v8/libavcodec/avcodec.h"
#include "media/thirdparty/ffmpeg/v8/libavutil/avutil.h"
#include "media/thirdparty/ffmpeg/v8/libavutil/opt.h"
}

namespace muse::audio::engine {

namespace {

struct AvApi {
    void* avformat = nullptr;
    void* avcodec = nullptr;
    void* avutil = nullptr;

    int (*avformat_open_input)(AVFormatContext**, const char*, const AVInputFormat*, AVDictionary**) = nullptr;
    int (*avformat_find_stream_info)(AVFormatContext*, AVDictionary**) = nullptr;
    void (*avformat_close_input)(AVFormatContext**) = nullptr;
    void (*avformat_free_context)(AVFormatContext*) = nullptr;
    AVFormatContext* (*avformat_alloc_context)(void) = nullptr;
    int (*av_find_best_stream)(AVFormatContext*, enum AVMediaType, int, int, const AVCodec**, int) = nullptr;
    int (*av_read_frame)(AVFormatContext*, AVPacket*) = nullptr;
    AVIOContext* (*avio_alloc_context)(unsigned char*, int, int, void*,
                                       int (*)(void*, uint8_t*, int),
                                       int (*)(void*, const uint8_t*, int),
                                       int64_t (*)(void*, int64_t, int)) = nullptr;
    void (*avio_context_free)(AVIOContext**) = nullptr;

    const AVCodec* (*avcodec_find_decoder)(enum AVCodecID) = nullptr;
    AVCodecContext* (*avcodec_alloc_context3)(const AVCodec*) = nullptr;
    int (*avcodec_parameters_to_context)(AVCodecContext*, const AVCodecParameters*) = nullptr;
    int (*avcodec_open2)(AVCodecContext*, const AVCodec*, AVDictionary**) = nullptr;
    void (*avcodec_free_context)(AVCodecContext**) = nullptr;
    int (*avcodec_send_packet)(AVCodecContext*, const AVPacket*) = nullptr;
    int (*avcodec_receive_frame)(AVCodecContext*, AVFrame*) = nullptr;

    AVFrame* (*av_frame_alloc)(void) = nullptr;
    void (*av_frame_free)(AVFrame**) = nullptr;
    AVPacket* (*av_packet_alloc)(void) = nullptr;
    void (*av_packet_free)(AVPacket**) = nullptr;
    void (*av_packet_unref)(AVPacket*) = nullptr;
    void* (*av_malloc)(size_t) = nullptr;
    void (*av_free)(void*) = nullptr;
};

template<typename T>
void resolve(void* lib, const char* name, T& out)
{
    out = reinterpret_cast<T>(muse::getLibFunc(lib, name));
}

void* openLib(const char* name)
{
#if defined(Q_OS_WIN)
    return muse::loadLib(io::path_t(name));
#else
    // 先按标准路径逐个尝试，覆盖 Homebrew / 常见安装位置
    const char* dirs[] = {
        "/opt/homebrew/lib/",
        "/usr/local/lib/",
        "/usr/lib/",
        nullptr
    };

    for (int i = 0; dirs[i]; ++i) {
        std::string full = std::string(dirs[i]) + name;
        void* h = muse::loadLib(io::path_t(full));
        if (h) {
            return h;
        }
    }

    // 兜底：交给系统默认搜索路径
    return muse::loadLib(io::path_t(name));
#endif
}

bool isPlanar(AVSampleFormat fmt)
{
    switch (fmt) {
    case AV_SAMPLE_FMT_U8P:
    case AV_SAMPLE_FMT_S16P:
    case AV_SAMPLE_FMT_S32P:
    case AV_SAMPLE_FMT_FLTP:
    case AV_SAMPLE_FMT_DBLP:
    case AV_SAMPLE_FMT_S64P:
        return true;
    default:
        return false;
    }
}

int bytesPerSample(AVSampleFormat fmt)
{
    switch (fmt) {
    case AV_SAMPLE_FMT_U8:
    case AV_SAMPLE_FMT_U8P:
        return 1;
    case AV_SAMPLE_FMT_S16:
    case AV_SAMPLE_FMT_S16P:
        return 2;
    case AV_SAMPLE_FMT_S32:
    case AV_SAMPLE_FMT_S32P:
    case AV_SAMPLE_FMT_FLT:
    case AV_SAMPLE_FMT_FLTP:
        return 4;
    case AV_SAMPLE_FMT_S64:
    case AV_SAMPLE_FMT_S64P:
    case AV_SAMPLE_FMT_DBL:
    case AV_SAMPLE_FMT_DBLP:
        return 8;
    default:
        return 0;
    }
}

// 将一帧 FFmpeg 采样转换为交错 float，追加到 out。
void appendFrame(const AVFrame* frame, std::vector<float>& out)
{
    if (!frame) {
        return;
    }

    const int channels = frame->ch_layout.nb_channels;
    if (channels <= 0 || frame->nb_samples <= 0) {
        return;
    }

    const AVSampleFormat fmt = static_cast<AVSampleFormat>(frame->format);
    const int bps = bytesPerSample(fmt);
    if (bps == 0) {
        return;
    }

    const bool planar = isPlanar(fmt);
    if (planar) {
        if (!frame->extended_data) {
            return;
        }
        for (int ch = 0; ch < channels; ++ch) {
            if (!frame->extended_data[ch]) {
                return;
            }
        }
    } else if (!frame->data[0]) {
        return;
    }

    const size_t oldSize = out.size();
    out.resize(oldSize + size_t(frame->nb_samples) * size_t(channels));

    for (int s = 0; s < frame->nb_samples; ++s) {
        for (int ch = 0; ch < channels; ++ch) {
            // 平面格式每个声道是独立缓冲区，采样步长应为单个采样的字节数；
            // 之前误用 linesize[0]（整个平面的字节数）作步长，会越界读取并崩溃。
            const uint8_t* p = planar
                                   ? frame->extended_data[ch] + size_t(s) * size_t(bps)
                                   : frame->data[0] + (size_t(s) * size_t(channels) + size_t(ch)) * size_t(bps);

            float v = 0.f;
            switch (fmt) {
            case AV_SAMPLE_FMT_FLT:
            case AV_SAMPLE_FMT_FLTP: {
                float f = 0.f;
                std::memcpy(&f, p, sizeof(float));
                v = f;
                break;
            }
            case AV_SAMPLE_FMT_S16:
            case AV_SAMPLE_FMT_S16P: {
                int16_t s16 = 0;
                std::memcpy(&s16, p, sizeof(int16_t));
                v = float(s16) / 32768.f;
                break;
            }
            case AV_SAMPLE_FMT_S32:
            case AV_SAMPLE_FMT_S32P: {
                int32_t s32 = 0;
                std::memcpy(&s32, p, sizeof(int32_t));
                v = float(s32) / 2147483648.f;
                break;
            }
            case AV_SAMPLE_FMT_U8:
            case AV_SAMPLE_FMT_U8P: {
                v = (float(*p) / 255.f) * 2.f - 1.f;
                break;
            }
            default:
                break;
            }

            out[oldSize + size_t(s) * size_t(channels) + size_t(ch)] = v;
        }
    }
}

struct MemIo {
    const uint8_t* data = nullptr;
    size_t size = 0;
    size_t pos = 0;
};

int memRead(void* opaque, uint8_t* buf, int bufSize)
{
    MemIo* m = static_cast<MemIo*>(opaque);
    if (!m || !buf || bufSize <= 0 || m->pos >= m->size) {
        return AVERROR_EOF;
    }

    const size_t remaining = m->size - m->pos;
    const size_t n = remaining < size_t(bufSize) ? remaining : size_t(bufSize);
    std::memcpy(buf, m->data + m->pos, n);
    m->pos += n;
    return n > 0 ? int(n) : AVERROR_EOF;
}

int64_t memSeek(void* opaque, int64_t offset, int whence)
{
    MemIo* m = static_cast<MemIo*>(opaque);
    if (whence == AVSEEK_SIZE) {
        return int64_t(m->size);
    }

    int64_t newPos = offset;
    if (whence == SEEK_CUR) {
        newPos = int64_t(m->pos) + offset;
    } else if (whence == SEEK_END) {
        newPos = int64_t(m->size) + offset;
    }

    if (newPos < 0) {
        newPos = 0;
    }
    if (newPos > int64_t(m->size)) {
        newPos = int64_t(m->size);
    }
    m->pos = size_t(newPos);
    return newPos;
}
}

struct FfmpegDecoder::Impl {
    AvApi api;
};

FfmpegDecoder::~FfmpegDecoder()
{
    if (!m_impl) {
        return;
    }

    if (m_impl->api.avformat) {
        muse::closeLib(m_impl->api.avformat);
    }
    if (m_impl->api.avcodec) {
        muse::closeLib(m_impl->api.avcodec);
    }
    if (m_impl->api.avutil) {
        muse::closeLib(m_impl->api.avutil);
    }

    delete m_impl;
    m_impl = nullptr;
}

bool FfmpegDecoder::decode(const uint8_t* data, size_t size, std::vector<float>& interleaved,
                           unsigned int& channels, unsigned int& sampleRate)
{
    if (!data || size == 0) {
        return false;
    }

    if (!m_impl) {
        m_impl = new Impl();
    }

    AvApi& api = m_impl->api;
    if (!api.avformat) {
#if defined(Q_OS_WIN)
        api.avformat = openLib("avformat-62.dll");
        api.avcodec = openLib("avcodec-62.dll");
        api.avutil = openLib("avutil-60.dll");
#elif defined(Q_OS_LINUX) || defined(Q_OS_FREEBSD)
        api.avformat = openLib("libavformat.so.62");
        api.avcodec = openLib("libavcodec.so.62");
        api.avutil = openLib("libavutil.so.60");
#else
        api.avformat = openLib("libavformat.dylib");
        api.avcodec = openLib("libavcodec.dylib");
        api.avutil = openLib("libavutil.dylib");
#endif
        if (!api.avformat || !api.avcodec || !api.avutil) {
            return false;
        }

        resolve(api.avformat, "avformat_open_input", api.avformat_open_input);
        resolve(api.avformat, "avformat_find_stream_info", api.avformat_find_stream_info);
        resolve(api.avformat, "avformat_close_input", api.avformat_close_input);
        resolve(api.avformat, "avformat_free_context", api.avformat_free_context);
        resolve(api.avformat, "avformat_alloc_context", api.avformat_alloc_context);
        resolve(api.avformat, "av_find_best_stream", api.av_find_best_stream);
        resolve(api.avformat, "av_read_frame", api.av_read_frame);
        resolve(api.avformat, "avio_alloc_context", api.avio_alloc_context);
        resolve(api.avformat, "avio_context_free", api.avio_context_free);
        resolve(api.avcodec, "avcodec_find_decoder", api.avcodec_find_decoder);
        resolve(api.avcodec, "avcodec_alloc_context3", api.avcodec_alloc_context3);
        resolve(api.avcodec, "avcodec_parameters_to_context", api.avcodec_parameters_to_context);
        resolve(api.avcodec, "avcodec_open2", api.avcodec_open2);
        resolve(api.avcodec, "avcodec_free_context", api.avcodec_free_context);
        resolve(api.avcodec, "avcodec_send_packet", api.avcodec_send_packet);
        resolve(api.avcodec, "avcodec_receive_frame", api.avcodec_receive_frame);
        resolve(api.avutil, "av_frame_alloc", api.av_frame_alloc);
        resolve(api.avutil, "av_frame_free", api.av_frame_free);
        resolve(api.avcodec, "av_packet_alloc", api.av_packet_alloc);
        resolve(api.avcodec, "av_packet_free", api.av_packet_free);
        resolve(api.avcodec, "av_packet_unref", api.av_packet_unref);
        resolve(api.avutil, "av_malloc", api.av_malloc);
        resolve(api.avutil, "av_free", api.av_free);

        const bool ok = api.avformat_open_input && api.avformat_find_stream_info && api.avformat_close_input
                        && api.avformat_alloc_context && api.av_find_best_stream && api.av_read_frame
                        && api.avio_alloc_context && api.avcodec_find_decoder && api.avcodec_alloc_context3
                        && api.avcodec_parameters_to_context && api.avcodec_open2 && api.avcodec_free_context
                        && api.avcodec_send_packet && api.avcodec_receive_frame && api.av_frame_alloc
                        && api.av_frame_free && api.av_packet_alloc && api.av_packet_free && api.av_packet_unref
                        && api.av_malloc && api.av_free;
        if (!ok) {
            return false;
        }
    }

    MemIo mem { data, size, 0 };
    const int ioBufferSize = 4096;
    uint8_t* ioBuffer = static_cast<uint8_t*>(api.av_malloc(ioBufferSize));
    if (!ioBuffer) {
        return false;
    }

    AVIOContext* avio = api.avio_alloc_context(ioBuffer, ioBufferSize, 0, &mem, memRead, nullptr, memSeek);
    if (!avio) {
        api.av_free(ioBuffer);
        return false;
    }

    AVFormatContext* fmtCtx = api.avformat_alloc_context();
    if (!fmtCtx) {
        api.avio_context_free(&avio);
        return false;
    }
    fmtCtx->pb = avio;

    int ret = api.avformat_open_input(&fmtCtx, nullptr, nullptr, nullptr);
    if (ret < 0) {
        api.avformat_close_input(&fmtCtx);
        api.avio_context_free(&avio);
        return false;
    }

    ret = api.avformat_find_stream_info(fmtCtx, nullptr);
    if (ret < 0) {
        api.avformat_close_input(&fmtCtx);
        api.avio_context_free(&avio);
        return false;
    }

    const AVCodec* codec = nullptr;
    const int streamIdx = api.av_find_best_stream(fmtCtx, AVMEDIA_TYPE_AUDIO, -1, -1, &codec, 0);
    if (streamIdx < 0 || !codec) {
        api.avformat_close_input(&fmtCtx);
        api.avio_context_free(&avio);
        return false;
    }

    AVStream* stream = fmtCtx->streams[streamIdx];
    AVCodecContext* codecCtx = api.avcodec_alloc_context3(codec);
    if (!codecCtx) {
        api.avformat_close_input(&fmtCtx);
        api.avio_context_free(&avio);
        return false;
    }

    if (api.avcodec_parameters_to_context(codecCtx, stream->codecpar) < 0
        || api.avcodec_open2(codecCtx, codec, nullptr) < 0) {
        api.avcodec_free_context(&codecCtx);
        api.avformat_close_input(&fmtCtx);
        api.avio_context_free(&avio);
        return false;
    }

    channels = static_cast<unsigned int>(codecCtx->ch_layout.nb_channels);
    sampleRate = static_cast<unsigned int>(codecCtx->sample_rate);
    if (channels == 0 || sampleRate == 0) {
        api.avcodec_free_context(&codecCtx);
        api.avformat_close_input(&fmtCtx);
        api.avio_context_free(&avio);
        return false;
    }

    AVPacket* packet = api.av_packet_alloc();
    AVFrame* frame = api.av_frame_alloc();

    while (api.av_read_frame(fmtCtx, packet) >= 0) {
        if (packet->stream_index == streamIdx && api.avcodec_send_packet(codecCtx, packet) == 0) {
            while (api.avcodec_receive_frame(codecCtx, frame) == 0) {
                appendFrame(frame, interleaved);
            }
        }
        api.av_packet_unref(packet);
    }

    api.avcodec_send_packet(codecCtx, nullptr);
    while (api.avcodec_receive_frame(codecCtx, frame) == 0) {
        appendFrame(frame, interleaved);
    }

    api.av_frame_free(&frame);
    api.av_packet_free(&packet);
    api.avcodec_free_context(&codecCtx);
    api.avformat_close_input(&fmtCtx);
    api.avio_context_free(&avio);

    return !interleaved.empty();
}
}
