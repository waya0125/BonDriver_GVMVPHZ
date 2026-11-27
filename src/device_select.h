// SPDX-License-Identifier: MIT
// Copyright (c) 2026 waya0125
//
// Pure (platform independent) helpers for device selection and bulk transfer
// sizing. Kept free of <windows.h> so that they can be unit-tested on Linux.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cwctype>
#include <set>
#include <string>
#include <vector>

namespace gvmvphz {

// DeviceIndex value meaning "open the first device that is not in use".
constexpr int kDeviceIndexAuto = -1;

// Parses the INI DeviceIndex value (read with GetPrivateProfileStringW).
//   leading / trailing spaces and tabs are ignored
//   "-1" (or any negative number) -> kDeviceIndexAuto
//   "0", "1", ...                  -> that index
//   empty / invalid / too large    -> fallback
inline int ParseDeviceIndex(const std::wstring &text, int fallback) {
    size_t b = 0, e = text.size();
    while (b < e && (text[b] == L' ' || text[b] == L'\t')) ++b;
    while (e > b && (text[e - 1] == L' ' || text[e - 1] == L'\t')) --e;
    if (b == e) return fallback;
    bool neg = false;
    if (text[b] == L'+' || text[b] == L'-') {
        neg = (text[b] == L'-');
        ++b;
    }
    if (b == e) return fallback;
    long long v = 0;
    for (size_t i = b; i < e; ++i) {
        if (text[i] < L'0' || text[i] > L'9') return fallback;
        v = v * 10 + (text[i] - L'0');
        if (v > 0x7fffffffLL) return fallback;
    }
    if (neg && v != 0) return kDeviceIndexAuto;
    return static_cast<int>(v);
}

// Returns the enumeration indices to try, in order.
//   device_index >= 0 : only that index (empty when out of range)
//   device_index <  0 : every index 0..count-1 (auto select; first free one wins)
inline std::vector<size_t> CandidateOrder(size_t count, int device_index) {
    std::vector<size_t> order;
    if (device_index >= 0) {
        if (static_cast<size_t>(device_index) < count) {
            order.push_back(static_cast<size_t>(device_index));
        }
        return order;
    }
    order.reserve(count);
    for (size_t i = 0; i < count; ++i) order.push_back(i);
    return order;
}

inline std::wstring LowerPath(std::wstring s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    return s;
}

// Follows one device across the USB re-enumerations that happen during the
// two-stage DFU boot, without opening any other device.
//
//   current : device paths enumerated right now
//   target  : path of the device being booted (updated when it moves)
//   known   : lower-cased paths seen so far (seed with the paths that existed
//             before the boot started)
//
// Returns true and leaves *target pointing at an existing path when the device
// is present: either the same path again (the usual case, since the instance
// path is derived from the USB port when there is no serial number), or a path
// that was never seen before (the device came back under a new instance path).
// Returns false when neither is present (still re-enumerating).
inline bool TrackDevicePath(const std::vector<std::wstring> &current, std::wstring *target,
                            std::set<std::wstring> *known) {
    const std::wstring want = LowerPath(*target);
    for (const auto &p : current) {
        if (LowerPath(p) == want) {
            *target = p;
            return true;
        }
    }
    for (const auto &p : current) {
        std::wstring lp = LowerPath(p);
        if (known->count(lp) == 0) {
            known->insert(lp);
            *target = p;
            return true;
        }
    }
    return false;
}

// Bulk URB size limits accepted from the INI.
constexpr size_t kMinUrbSize = 512;
constexpr size_t kMaxUrbSize = 1024 * 1024;
// WaitForMultipleObjects() can wait on at most 64 handles.
constexpr size_t kMinUrbCount = 1;
constexpr size_t kMaxUrbCount = 64;

inline size_t ClampUrbCount(long long v, size_t fallback) {
    if (v <= 0) return fallback;
    if (static_cast<unsigned long long>(v) > kMaxUrbCount) return kMaxUrbCount;
    return static_cast<size_t>(v);
}

inline size_t ClampUrbSize(long long v, size_t fallback) {
    if (v <= 0) return fallback;
    if (static_cast<unsigned long long>(v) < kMinUrbSize) return kMinUrbSize;
    if (static_cast<unsigned long long>(v) > kMaxUrbSize) return kMaxUrbSize;
    return static_cast<size_t>(v);
}

// A bulk IN read should be a whole multiple of wMaxPacketSize; otherwise a
// full-size packet can straddle the end of the buffer and the transfer fails
// with an overrun. Rounds urb_size up to the next multiple (never down, so
// the buffer only grows). max_packet == 0 (unknown) leaves urb_size as is.
inline size_t AlignUrbSize(size_t urb_size, uint16_t max_packet) {
    if (max_packet == 0 || urb_size == 0) return urb_size;
    const size_t mp = max_packet;
    const size_t rem = urb_size % mp;
    if (rem == 0) return urb_size;
    return urb_size + (mp - rem);
}

// WinUSB DEVICE_SPEED values (winusbio.h): LowSpeed=1, FullSpeed=2, HighSpeed=3.
// winusbio.h has no SuperSpeed value, so anything other than HighSpeed is
// treated as "slower than High-Speed". ISDB-T TS (~17-23 Mbps) needs High-Speed.
constexpr uint8_t kUsbSpeedLow = 0x01;
constexpr uint8_t kUsbSpeedFull = 0x02;
constexpr uint8_t kUsbSpeedHigh = 0x03;

inline const wchar_t *UsbSpeedName(uint8_t speed) {
    switch (speed) {
    case kUsbSpeedLow: return L"LowSpeed";
    case kUsbSpeedFull: return L"FullSpeed";
    case kUsbSpeedHigh: return L"HighSpeed";
    default: return L"Unknown";
    }
}

} // namespace gvmvphz
