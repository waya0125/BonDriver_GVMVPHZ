// SPDX-License-Identifier: MIT
// Copyright (c) 2026 waya0125
// Platform independent unit tests (run on Linux and Windows via ctest).
#include "../src/device_select.h"

#include <cstdio>
#include <set>
#include <string>
#include <vector>

using namespace gvmvphz;

namespace {

int g_failures = 0;

#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                        \
        }                                                                        \
    } while (0)

void TestCandidateOrder() {
    // Fixed index.
    CHECK((CandidateOrder(3, 0) == std::vector<size_t>{0}));
    CHECK((CandidateOrder(3, 2) == std::vector<size_t>{2}));
    CHECK(CandidateOrder(3, 3).empty());
    CHECK(CandidateOrder(0, 0).empty());
    // Auto select: every device in enumeration order.
    CHECK((CandidateOrder(3, kDeviceIndexAuto) == std::vector<size_t>{0, 1, 2}));
    CHECK((CandidateOrder(1, -5) == std::vector<size_t>{0}));
    CHECK(CandidateOrder(0, kDeviceIndexAuto).empty());
}

void TestParseDeviceIndex() {
    CHECK(ParseDeviceIndex(L"0", 0) == 0);
    CHECK(ParseDeviceIndex(L"2", 0) == 2);
    CHECK(ParseDeviceIndex(L" 3\t", 0) == 3);
    CHECK(ParseDeviceIndex(L"+1", 0) == 1);
    CHECK(ParseDeviceIndex(L"-1", 0) == kDeviceIndexAuto);
    CHECK(ParseDeviceIndex(L" -1 ", 0) == kDeviceIndexAuto);
    CHECK(ParseDeviceIndex(L"-7", 0) == kDeviceIndexAuto);
    CHECK(ParseDeviceIndex(L"-0", 0) == 0);
    CHECK(ParseDeviceIndex(L"", 0) == 0);
    CHECK(ParseDeviceIndex(L"-", 0) == 0);
    CHECK(ParseDeviceIndex(L"abc", 0) == 0);
    CHECK(ParseDeviceIndex(L"1x", 0) == 0);
    CHECK(ParseDeviceIndex(L"99999999999", 0) == 0);
}

void TestUrbClamp() {
    CHECK(ClampUrbCount(8, 8) == 8);
    CHECK(ClampUrbCount(0, 8) == 8);
    CHECK(ClampUrbCount(-1, 8) == 8);
    CHECK(ClampUrbCount(100, 8) == kMaxUrbCount);
    CHECK(ClampUrbSize(32768, 32768) == 32768);
    CHECK(ClampUrbSize(0, 32768) == 32768);
    CHECK(ClampUrbSize(100, 32768) == kMinUrbSize);
    CHECK(ClampUrbSize(16 * 1024 * 1024, 32768) == kMaxUrbSize);
}

void TestAlignUrbSize() {
    CHECK(AlignUrbSize(32768, 512) == 32768);   // High-Speed bulk: already aligned
    CHECK(AlignUrbSize(32768, 64) == 32768);    // Full-Speed bulk
    CHECK(AlignUrbSize(30000, 512) == 30208);   // rounded up, never down
    CHECK(AlignUrbSize(512, 1024) == 1024);     // SuperSpeed bulk packet
    CHECK(AlignUrbSize(32768, 0) == 32768);     // unknown packet size: unchanged
}

void TestTrackDevicePath() {
    const std::wstring a = L"\\\\?\\usb#vid_04bb&pid_0522#5&1&0&1#{guid}";
    const std::wstring b = L"\\\\?\\usb#vid_04bb&pid_0522#5&1&0&2#{guid}";
    const std::wstring a2 = L"\\\\?\\usb#vid_04bb&pid_0522#6&9&0&1#{guid}";

    // Same path comes back after re-enumeration (case may differ).
    {
        std::set<std::wstring> known{LowerPath(a), LowerPath(b)};
        std::wstring target = a;
        std::wstring upper = L"\\\\?\\USB#VID_04BB&PID_0522#5&1&0&1#{GUID}";
        CHECK(TrackDevicePath({b, upper}, &target, &known));
        CHECK(target == upper);
    }
    // Still re-enumerating: only the other (busy) unit is present.
    {
        std::set<std::wstring> known{LowerPath(a), LowerPath(b)};
        std::wstring target = a;
        CHECK(!TrackDevicePath({b}, &target, &known));
        CHECK(target == a);
    }
    // Came back under a new instance path: follow it, never pick the other unit.
    {
        std::set<std::wstring> known{LowerPath(a), LowerPath(b)};
        std::wstring target = a;
        CHECK(TrackDevicePath({b, a2}, &target, &known));
        CHECK(target == a2);
        // Next round keeps following the new path.
        CHECK(TrackDevicePath({a2, b}, &target, &known));
        CHECK(target == a2);
    }
}

void TestSpeedName() {
    CHECK(std::wstring(UsbSpeedName(kUsbSpeedHigh)) == L"HighSpeed");
    CHECK(std::wstring(UsbSpeedName(kUsbSpeedFull)) == L"FullSpeed");
    CHECK(std::wstring(UsbSpeedName(0)) == L"Unknown");
}

} // namespace

int main() {
    TestCandidateOrder();
    TestParseDeviceIndex();
    TestUrbClamp();
    TestAlignUrbSize();
    TestTrackDevicePath();
    TestSpeedName();
    if (g_failures == 0) {
        std::printf("test_core: all tests passed\n");
        return 0;
    }
    std::fprintf(stderr, "test_core: %d failure(s)\n", g_failures);
    return 1;
}
