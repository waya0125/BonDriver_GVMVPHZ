// SPDX-License-Identifier: MIT
// Copyright (c) 2026 waya0125
#include "ivdrm.h"

#include <windows.h>
#include <bcrypt.h>

#include <chrono>
#include <cstring>

namespace gvmvphz {
namespace ivdrm {

namespace {

constexpr uint8_t kIvdrmDHP[128] = {
    0xff, 0x63, 0xab, 0x76, 0x95, 0x02, 0xc0, 0xf0, 0x02, 0xf6, 0x58, 0x4b, 0xdb, 0x5f, 0x17, 0x7e,
    0x11, 0xa5, 0xe6, 0x52, 0x85, 0x2f, 0x1e, 0xc1, 0x34, 0x8d, 0x05, 0x47, 0xd5, 0xba, 0x7d, 0x79,
    0x2d, 0xd7, 0xb4, 0x56, 0x7e, 0x75, 0x8d, 0x9d, 0xda, 0xbd, 0x19, 0x5c, 0x86, 0x74, 0xd7, 0xe4,
    0xf2, 0x96, 0xa2, 0x4d, 0x29, 0x6e, 0x50, 0x2e, 0xe3, 0x92, 0x5b, 0x20, 0x87, 0xf0, 0x8a, 0x65,
    0xf8, 0x4e, 0x0d, 0xa2, 0x0d, 0x73, 0x48, 0xed, 0x86, 0xf4, 0x0c, 0x22, 0x1f, 0x4d, 0x1e, 0x0c,
    0x20, 0x8e, 0x53, 0x06, 0x39, 0xc6, 0xa2, 0xb6, 0x83, 0x69, 0x90, 0xe1, 0x06, 0x58, 0x2a, 0x70,
    0x15, 0x0c, 0xe7, 0x7c, 0x60, 0x9e, 0xed, 0xfb, 0x68, 0x54, 0xca, 0x55, 0x8f, 0x87, 0xbc, 0xb1,
    0x50, 0x8a, 0xd9, 0x88, 0x1d, 0x17, 0x06, 0x09, 0x9d, 0x16, 0x71, 0x11, 0x86, 0x87, 0xd5, 0xc7,
};

constexpr uint8_t kAuthPlain[16] = {
    'A', 'u', 't', 'h', 'o', 'r', 'i', 'z', 'a', 't', 'i', 'o', 'n', ' ', 'O', 'K'
};

constexpr uint8_t kKeyPacketMarker[12] = {
    'F', 'O', 'R', 'I', 'V', 'F', 'R', 'O', 'M', 'N', 'X', 'P'
};

uint32_t ReadLE32(const uint8_t *p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

void WriteLE32(uint8_t *p, uint32_t v) {
    p[0] = static_cast<uint8_t>(v & 0xff);
    p[1] = static_cast<uint8_t>((v >> 8) & 0xff);
    p[2] = static_cast<uint8_t>((v >> 16) & 0xff);
    p[3] = static_cast<uint8_t>((v >> 24) & 0xff);
}

void WriteLE64(uint8_t *p, uint64_t v) {
    for (int i = 0; i < 8; ++i) {
        p[i] = static_cast<uint8_t>((v >> (i * 8)) & 0xff);
    }
}

// 1024-bit unsigned integer stored in 32 little-endian 32-bit limbs.
struct U1024 {
    uint32_t w[32]{};
};

U1024 FromBigEndian(const uint8_t *b, size_t len) {
    U1024 r{};
    for (size_t i = 0; i < len && i < 128; ++i) {
        uint8_t byte = b[len - 1 - i];
        r.w[i / 4] |= static_cast<uint32_t>(byte) << ((i % 4) * 8);
    }
    return r;
}

std::array<uint8_t, 128> ToBigEndian(const U1024 &a) {
    std::array<uint8_t, 128> out{};
    for (size_t i = 0; i < 128; ++i) {
        out[127 - i] = static_cast<uint8_t>((a.w[i / 4] >> ((i % 4) * 8)) & 0xff);
    }
    return out;
}

int Cmp1024(const U1024 &a, const U1024 &b) {
    for (int i = 31; i >= 0; --i) {
        if (a.w[i] != b.w[i]) return (a.w[i] < b.w[i]) ? -1 : 1;
    }
    return 0;
}

void SubInPlace(U1024 &a, const U1024 &b) {
    uint64_t borrow = 0;
    for (int i = 0; i < 32; ++i) {
        uint64_t diff = static_cast<uint64_t>(a.w[i]) - static_cast<uint64_t>(b.w[i]) - borrow;
        a.w[i] = static_cast<uint32_t>(diff & 0xffffffffu);
        borrow = (diff >> 63) & 1;
    }
}

U1024 MulMod1024(const U1024 &a, const U1024 &b, const U1024 &mod) {
    uint32_t prod[64]{};
    for (int i = 0; i < 32; ++i) {
        uint64_t carry = 0;
        for (int j = 0; j < 32; ++j) {
            uint64_t cur = static_cast<uint64_t>(a.w[i]) * static_cast<uint64_t>(b.w[j]) +
                           prod[i + j] + carry;
            prod[i + j] = static_cast<uint32_t>(cur & 0xffffffffu);
            carry = cur >> 32;
        }
        prod[i + 32] = static_cast<uint32_t>(carry);
    }

    // Binary long division to compute prod % mod.
    U1024 rem{};
    for (int bit = 2047; bit >= 0; --bit) {
        uint32_t carry = (prod[bit / 32] >> (bit % 32)) & 1u;
        for (int i = 0; i < 32; ++i) {
            uint32_t next_carry = rem.w[i] >> 31;
            rem.w[i] = (rem.w[i] << 1) | carry;
            carry = next_carry;
        }
        if (carry || Cmp1024(rem, mod) >= 0) {
            SubInPlace(rem, mod);
        }
    }
    return rem;
}

bool AesEcb16(const uint8_t key[16], const uint8_t in[16], uint8_t out[16], bool encrypt) {
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_KEY_HANDLE hkey = nullptr;
    bool ok = false;

    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_AES_ALGORITHM, nullptr, 0) == 0) {
        if (BCryptSetProperty(alg, BCRYPT_CHAINING_MODE,
                              reinterpret_cast<PUCHAR>(const_cast<wchar_t *>(BCRYPT_CHAIN_MODE_ECB)),
                              sizeof(BCRYPT_CHAIN_MODE_ECB), 0) == 0 &&
            BCryptGenerateSymmetricKey(alg, &hkey, nullptr, 0, const_cast<PUCHAR>(key), 16, 0) == 0) {
            ULONG cb = 0;
            NTSTATUS st = encrypt
                              ? BCryptEncrypt(hkey, const_cast<PUCHAR>(in), 16, nullptr, nullptr, 0, out, 16, &cb, 0)
                              : BCryptDecrypt(hkey, const_cast<PUCHAR>(in), 16, nullptr, nullptr, 0, out, 16, &cb, 0);
            ok = (st == 0 && cb == 16);
        }
    }
    if (hkey) BCryptDestroyKey(hkey);
    if (alg) BCryptCloseAlgorithmProvider(alg, 0);
    return ok;
}

bool AesCbcDecrypt(const uint8_t key[16], const uint8_t iv[16],
                   const uint8_t *in, uint8_t *out, size_t len) {
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_KEY_HANDLE hkey = nullptr;
    bool ok = false;
    uint8_t iv_copy[16];
    std::memcpy(iv_copy, iv, 16);

    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_AES_ALGORITHM, nullptr, 0) == 0) {
        if (BCryptSetProperty(alg, BCRYPT_CHAINING_MODE,
                              reinterpret_cast<PUCHAR>(const_cast<wchar_t *>(BCRYPT_CHAIN_MODE_CBC)),
                              sizeof(BCRYPT_CHAIN_MODE_CBC), 0) == 0 &&
            BCryptGenerateSymmetricKey(alg, &hkey, nullptr, 0, const_cast<PUCHAR>(key), 16, 0) == 0) {
            ULONG cb = 0;
            NTSTATUS st = BCryptDecrypt(hkey, const_cast<PUCHAR>(in), static_cast<ULONG>(len),
                                        nullptr, iv_copy, 16, out, static_cast<ULONG>(len), &cb, 0);
            ok = (st == 0 && cb == len);
        }
    }
    if (hkey) BCryptDestroyKey(hkey);
    if (alg) BCryptCloseAlgorithmProvider(alg, 0);
    return ok;
}

} // namespace

std::array<uint8_t, 128> ModPow1024(const uint8_t *base, size_t base_len,
                                    const uint8_t *exp, size_t exp_len) {
    U1024 mod = FromBigEndian(kIvdrmDHP, sizeof(kIvdrmDHP));
    U1024 b = FromBigEndian(base, base_len);
    if (Cmp1024(b, mod) >= 0) SubInPlace(b, mod);

    U1024 res{};
    res.w[0] = 1;

    for (size_t i = 0; i < exp_len; ++i) {
        uint8_t byte = exp[i];
        for (int bit = 7; bit >= 0; --bit) {
            res = MulMod1024(res, res, mod);
            if ((byte >> bit) & 1u) {
                res = MulMod1024(res, b, mod);
            }
        }
    }
    return ToBigEndian(res);
}

void CloseSession(saa7164::Client *c, uint32_t sid) {
    std::vector<uint8_t> buf(9, 0);
    WriteLE32(buf.data() + 1, sid);
    c->Command(saa7164::kUnitCAS, saa7164::kSetCur, 5, buf);
}

bool RunAuth(saa7164::Client *c, uint32_t session_id, std::array<uint8_t, 16> *session_key_out) {
    CloseSession(c, 0);
    CloseSession(c, 1);

    c->Command(saa7164::kUnitCAS, saa7164::kSetCur, 0, {0});
    c->Command(saa7164::kUnitCAS, saa7164::kGetCur, 1, std::vector<uint8_t>(4, 0));

    std::vector<uint8_t> open_buf(9, 0);
    WriteLE32(open_buf.data() + 1, session_id);
    std::vector<uint8_t> resp;
    if (!c->Command(saa7164::kUnitCAS, saa7164::kSetCur, 2, open_buf, &resp) ||
        resp.size() < 9 || ReadLE32(resp.data() + 5) != 0) {
        return false;
    }

    uint8_t exp_a[16]{};
    if (BCryptGenRandom(nullptr, exp_a, sizeof(exp_a), BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) {
        return false;
    }
    exp_a[0] |= 0x01;
    uint8_t base_two[1] = {2};
    auto pub_a = ModPow1024(base_two, 1, exp_a, sizeof(exp_a));

    std::vector<uint8_t> msg(0x10c, 0);
    WriteLE32(msg.data(), 0x80);
    std::memcpy(msg.data() + 4, pub_a.data(), 128);
    WriteLE32(msg.data() + 0x104, session_id);

    if (!c->Command(saa7164::kUnitCAS, saa7164::kSetCur, 3, msg, &resp) ||
        resp.size() < 0x10c || ReadLE32(resp.data() + 0x108) != 0) {
        return false;
    }
    if (!c->Command(saa7164::kUnitCAS, saa7164::kGetCur, 3, msg, &resp) ||
        resp.size() < 0x10c || ReadLE32(resp.data() + 0x108) != 0) {
        return false;
    }

    std::array<uint8_t, 128> pub_b{};
    std::memcpy(pub_b.data(), resp.data() + 4, 128);

    auto shared_s = ModPow1024(pub_b.data(), pub_b.size(), exp_a, sizeof(exp_a));
    if (session_key_out) {
        std::memcpy(session_key_out->data(), shared_s.data(), 16);
    }

    uint8_t auth_enc[16]{};
    if (!AesEcb16(pub_b.data(), kAuthPlain, auth_enc, true)) {
        return false;
    }

    std::fill(msg.begin(), msg.end(), 0);
    WriteLE32(msg.data(), 0x10);
    std::memcpy(msg.data() + 4, auth_enc, 16);
    WriteLE32(msg.data() + 0x104, session_id);

    if (!c->Command(saa7164::kUnitCAS, saa7164::kSetCur, 4, msg, &resp) ||
        resp.size() < 0x10c || ReadLE32(resp.data() + 0x108) != 0) {
        return false;
    }
    if (!c->Command(saa7164::kUnitCAS, saa7164::kGetCur, 4, msg, &resp) ||
        resp.size() < 0x10c || ReadLE32(resp.data() + 0x108) != 0) {
        return false;
    }

    uint8_t auth_dec[16]{};
    if (!AesEcb16(pub_a.data(), resp.data() + 4, auth_dec, false)) {
        return false;
    }
    return std::memcmp(auth_dec, kAuthPlain, 16) == 0;
}

WatchDog::~WatchDog() {
    Stop();
}

bool WatchDog::Start(saa7164::Client *c, const std::array<uint8_t, 16> &session_key) {
    Stop();
    client_ = c;
    session_key_ = session_key;
    counter_ = 1;

    std::vector<uint8_t> wd_init(12, 0);
    WriteLE32(wd_init.data(), 7);
    WriteLE64(wd_init.data() + 4, counter_);
    client_->Command(saa7164::kUnitCAS, saa7164::kSetCur, 6, wd_init);
    KickOnce();

    running_.store(true);
    thread_ = std::thread([this]() { Loop(); });
    return true;
}

void WatchDog::Stop() {
    running_.store(false);
    if (thread_.joinable()) {
        thread_.join();
    }
}

void WatchDog::KickOnce() {
    ++counter_;
    struct ClientInfo {
        uint32_t id;
        uint32_t magic;
    };
    constexpr ClientInfo kClients[3] = {
        {1, 0x4a73f812u},
        {2, 0xf94c36dcu},
        {4, 0x71c0e12du},
    };
    for (const auto &cl : kClients) {
        uint8_t plain[16]{};
        std::vector<uint8_t> pkt(20, 0);
        WriteLE64(plain, counter_);
        WriteLE32(plain + 8, cl.id);
        WriteLE32(plain + 12, cl.magic);
        if (AesEcb16(session_key_.data(), plain, pkt.data() + 4, true)) {
            client_->Command(saa7164::kUnitCAS, saa7164::kSetCur, 7, pkt);
        }
    }
}

void WatchDog::Loop() {
    while (running_.load()) {
        for (int i = 0; i < 20 && running_.load(); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        if (running_.load()) {
            KickOnce();
        }
    }
}

TSDecryptor::TSDecryptor(const std::array<uint8_t, 16> &session_key)
    : session_key_(session_key) {}

TSDecryptor::~TSDecryptor() = default;

void TSDecryptor::Reset() {
    carry_.clear();
}

void TSDecryptor::Process(const uint8_t *data, size_t len, std::vector<uint8_t> *out) {
    carry_.insert(carry_.end(), data, data + len);
    static constexpr uint8_t kZeroIv[16]{};

    size_t pos = 0;
    while (pos + 188 <= carry_.size()) {
        if (carry_[pos] != 0x47) {
            ++pos;
            continue;
        }
        uint8_t *pkt = carry_.data() + pos;
        uint8_t tsc = (pkt[3] >> 6) & 3u;
        uint16_t pid = (static_cast<uint16_t>(pkt[1] & 0x1fu) << 8) | pkt[2];
        uint8_t afc = pkt[3] & 0x30u;

        if ((tsc == 1 || tsc == 2) && pid == 0x0000 && afc == 0x20) {
            uint8_t dec[176]{};
            if (AesCbcDecrypt(session_key_.data(), kZeroIv, pkt + 4, dec, 176) &&
                std::memcmp(dec + 0x20, kKeyPacketMarker, 12) == 0) {
                std::memcpy(ts_iv_.data(), dec, 16);
                std::memcpy(ts_key_.data(), dec + 0x10, 16);
                has_ts_key_ = true;
            }
        } else if (tsc == 3 && has_ts_key_) {
            if (AesCbcDecrypt(ts_key_.data(), ts_iv_.data(), pkt + 4, pkt + 4, 176)) {
                pkt[3] &= 0x3fu;
                if (pid != 0x1fff && out) {
                    out->insert(out->end(), pkt, pkt + 188);
                }
            }
        } else if (tsc == 0 && pid != 0x1fff && out) {
            out->insert(out->end(), pkt, pkt + 188);
        }
        pos += 188;
    }

    if (pos > 0) {
        carry_.erase(carry_.begin(), carry_.begin() + static_cast<ptrdiff_t>(pos));
    }
}

} // namespace ivdrm
} // namespace gvmvphz
