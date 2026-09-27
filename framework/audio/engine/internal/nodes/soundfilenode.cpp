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
#include "soundfilenode.h"

#include <algorithm>
#include <cmath>

#include "audio/common/audiosanitizer.h"

using namespace muse::audio;
using namespace muse::audio::engine;

SoundFileNode::SoundFileNode(TrackId /*trackId*/, muse::io::IODevice* device)
{
    ONLY_AUDIO_ENGINE_THREAD;

    if (device) {
        m_decoder.open(device);
    }

    setName("SoundFileSource");
}

void SoundFileNode::onModeChanged(const ProcessMode mode)
{
    ONLY_AUDIO_ENGINE_THREAD;

    if (!isModePlaying(mode)) {
        return;
    }

    m_readyToPlayChanged.notify();
}

void SoundFileNode::onOutputSpecChanged(const OutputSpec& /*spec*/)
{
    ONLY_AUDIO_ENGINE_THREAD;
    // Output spec is read directly from the base node during processing.
}

void SoundFileNode::doSelfProcess(float* buffer, samples_t samplesPerChannel)
{
    ONLY_AUDIO_PROC_THREAD;

    if (!m_decoder.isValid() || !m_outputSpec.isValid()) {
        return;
    }

    const unsigned int outputChannels = m_outputSpec.audioChannelCount;
    const unsigned int inputChannels = m_decoder.channels();
    const double sourceRate = m_decoder.sampleRate();
    const double outputRate = m_outputSpec.sampleRate;
    const double step = sourceRate / outputRate;
    const uint64_t totalFrames = m_decoder.frames();
    const std::vector<float>& data = m_decoder.data();

    if (!isModePlaying(m_mode)) {
        for (samples_t i = 0; i < samplesPerChannel * outputChannels; ++i) {
            buffer[i] = 0.f;
        }
        return;
    }

    for (samples_t i = 0; i < samplesPerChannel; ++i) {
        const double src = m_positionFrame + static_cast<double>(i) * step;
        const uint64_t i0 = static_cast<uint64_t>(src);
        const double frac = src - static_cast<double>(i0);

        for (unsigned int c = 0; c < outputChannels; ++c) {
            const unsigned int ic = c % inputChannels;
            float value = 0.f;

            if (i0 < totalFrames) {
                value = data[i0 * inputChannels + ic];
                if (i0 + 1 < totalFrames) {
                    value += static_cast<float>(frac) * (data[(i0 + 1) * inputChannels + ic] - value);
                }
            }

            buffer[i * outputChannels + c] = value;
        }
    }

    m_positionFrame += static_cast<double>(samplesPerChannel) * step;
}

void SoundFileNode::seek(const TimePosition& position, const bool /*flushSound*/)
{
    ONLY_AUDIO_ENGINE_THREAD;

    if (!position.isValid()) {
        return;
    }

    m_positionFrame = position.time().raw() * m_decoder.sampleRate();
}

void SoundFileNode::flush()
{
}

const AudioInputParams& SoundFileNode::inputParams() const
{
    return m_params;
}

void SoundFileNode::applyInputParams(const AudioInputParams& requiredParams)
{
    if (m_params == requiredParams) {
        return;
    }

    m_params = requiredParams;
    m_paramsChanges.send(m_params);
}

async::Channel<AudioInputParams> SoundFileNode::inputParamsChanged() const
{
    return m_paramsChanges;
}

void SoundFileNode::prepareToPlay()
{
    ONLY_AUDIO_ENGINE_THREAD;
}

bool SoundFileNode::readyToPlay() const
{
    return m_decoder.isValid();
}

async::Notification SoundFileNode::readyToPlayChanged() const
{
    return m_readyToPlayChanged;
}

bool SoundFileNode::hasPendingChunks() const
{
    return false;
}

void SoundFileNode::processInput()
{
}

InputProcessingProgress SoundFileNode::inputProcessingProgress() const
{
    return {};
}

void SoundFileNode::clearCache()
{
}
