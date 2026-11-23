// SPDX-License-Identifier: MIT
// Copyright (c) 2026 waya0125
#pragma once

#include "saa7164.h"

#include <cstdint>

namespace gvmvphz {
namespace saigawa {

constexpr uint8_t kTunerUnit = 4;
constexpr uint8_t kDemodUnit = 5;

bool Init(saa7164::Client *c);
bool TuneISDBT(saa7164::Client *c, uint32_t freq_hz, bool *locked_out);
bool ReadLock(saa7164::Client *c, uint8_t *r80, uint8_t *rb0, bool *locked);
bool ReadSignalStrength(saa7164::Client *c, uint8_t *strength_out);

} // namespace saigawa
} // namespace gvmvphz
