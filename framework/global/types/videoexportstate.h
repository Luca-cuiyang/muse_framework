/*
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once

#include <atomic>

namespace muse {
//! Video export temporarily re-lays out the score and renders it on a worker
//! thread. UI views must not chase playback ticks during that window.
inline std::atomic_bool videoExportInProgress = false;
}
