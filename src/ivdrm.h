// SPDX-License-Identifier: MIT
// Copyright (c) 2026 waya0125
#pragma once

#include "saa7164.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>

namespace gvmvphz {
namespace ivdrm {

// Big-endian 1024-bit modular exponentiation (base^exp mod ivdrm_dh_p).
std::array<uint8_t, 128> ModPow1024(const uint8_t *base, size_t base_len,
                                    const uint8_t *exp, size_t exp_len);

void CloseSession(saa7164::Client *c, uint32_t sid);
bool RunAuth(saa7164::Client *c, uint32_t session_id, std::array<uint8_t, 16> *session_key_out);

class WatchDog {
public:
    WatchDog() = default;
    ~WatchDog();

    WatchDog(const WatchDog &) = delete;
    WatchDog &operator=(const WatchDog &) = delete;

    bool Start(saa7164::Client *c, const std::array<uint8_t, 16> &session_key);
    void Stop();

private:
    void KickOnce();
    void Loop();

    saa7164::Client *client_ = nullptr;
    std::array<uint8_t, 16> session_key_{};
    uint64_t counter_ = 1;
    std::atomic<bool> running_{false};
    std::thread thread_;
};

class TSDecryptor {
public:
    explicit TSDecryptor(const std::array<uint8_t, 16> &session_key);
    ~TSDecryptor();

    TSDecryptor(const TSDecryptor &) = delete;
    TSDecryptor &operator=(const TSDecryptor &) = delete;

    void Reset();
    void Process(const uint8_t *data, size_t len, std::vector<uint8_t> *out);

private:
    std::array<uint8_t, 16> session_key_{};
    std::array<uint8_t, 16> ts_key_{};
    std::array<uint8_t, 16> ts_iv_{};
    bool has_ts_key_ = false;
    std::vector<uint8_t> carry_;
};

} // namespace ivdrm
} // namespace gvmvphz
