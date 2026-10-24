// UsdReplaySource: a .usda or .usdc replay, read with usd-recorder's C++ reader
// (usd_recorder::read_replay, which links OpenUSD), into a ReplayModel. Built only with
// -DREPLAY_PLAYER_WITH_USD=ON.
#pragma once

#include <string>

#include "model/model.h"

namespace rp {

/// Loads a replay file. Throws std::runtime_error (usd_recorder::ReplayError) on a file that
/// cannot be read or is not a usd-replay/1 replay.
ReplayModel loadUsdReplay(const std::string& path);

}  // namespace rp
