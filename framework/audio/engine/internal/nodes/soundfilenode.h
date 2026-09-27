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
#ifndef MUSE_AUDIO_SOUNDFILENODE_H
#define MUSE_AUDIO_SOUNDFILENODE_H

#include "audiosourcenode.h"

#include "global/io/iodevice.h"

#include "../codecs/wavdecoder.h"

namespace muse::audio::engine {
class SoundFileNode : public AudioSourceNode
{
public:
    explicit SoundFileNode(TrackId trackId, muse::io::IODevice* device, const SoundTrackData& data = SoundTrackData());
    ~SoundFileNode() override = default;

    bool isValid() const { return m_decoder.isValid(); }

    void seek(const TimePosition& position, const bool flushSound = true) override;
    void flush() override;

    const AudioInputParams& inputParams() const override;
    void applyInputParams(const AudioInputParams& requiredParams) override;
    async::Channel<AudioInputParams> inputParamsChanged() const override;

    void prepareToPlay() override;
    bool readyToPlay() const override;
    async::Notification readyToPlayChanged() const override;

    bool hasPendingChunks() const override;
    void processInput() override;
    InputProcessingProgress inputProcessingProgress() const override;

    void clearCache() override;

private:
    void onModeChanged(const ProcessMode mode) override;
    void onOutputSpecChanged(const OutputSpec& spec) override;
    void doSelfProcess(float* buffer, samples_t samplesPerChannel) override;

    WavDecoder m_decoder;
    SoundTrackData m_data;
    AudioInputParams m_params;
    async::Channel<AudioInputParams> m_paramsChanges;
    async::Notification m_readyToPlayChanged;
    double m_positionFrame = 0.0;
    double m_clipEndFrame = 0.0;
};
}

#endif // MUSE_AUDIO_SOUNDFILENODE_H
