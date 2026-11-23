// SPDX-License-Identifier: MIT
// Copyright (c) 2026 waya0125
// Simple CLI smoke test for BonDriver_GVMVPHZ.dll
#include <windows.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "../src/IBonDriver2.h"

using CreateBonDriverFn = IBonDriver *(*)(void);

int wmain(int argc, wchar_t **argv) {
    const wchar_t *dll_path = (argc >= 2) ? argv[1] : L"BonDriver_GVMVPHZ.dll";
    int uhf_ch = (argc >= 3) ? _wtoi(argv[2]) : 25;
    int sec = (argc >= 4) ? _wtoi(argv[3]) : 5;
    const wchar_t *out_path = (argc >= 5) ? argv[4] : nullptr;

    if (uhf_ch < 13 || uhf_ch > 62) {
        std::fwprintf(stderr, L"UHF channel must be 13..62 (got %d)\n", uhf_ch);
        return 1;
    }

    HMODULE hmod = LoadLibraryW(dll_path);
    if (!hmod) {
        std::fwprintf(stderr, L"LoadLibraryW(%ls) failed: %lu\n", dll_path, GetLastError());
        return 1;
    }

    auto create_fn = reinterpret_cast<CreateBonDriverFn>(GetProcAddress(hmod, "CreateBonDriver"));
    if (!create_fn) {
        std::fwprintf(stderr, L"GetProcAddress(CreateBonDriver) failed: %lu\n", GetLastError());
        FreeLibrary(hmod);
        return 1;
    }

    auto *bon = dynamic_cast<IBonDriver2 *>(create_fn());
    if (!bon) {
        std::fwprintf(stderr, L"CreateBonDriver returned null or non-IBonDriver2\n");
        FreeLibrary(hmod);
        return 1;
    }

    std::wprintf(L"TunerName: %ls\n", bon->GetTunerName());
    std::wprintf(L"Space[0]: %ls\n", bon->EnumTuningSpace(0));
    std::wprintf(L"Opening tuner...\n");

    if (!bon->OpenTuner()) {
        std::fwprintf(stderr, L"OpenTuner() failed\n");
        bon->Release();
        FreeLibrary(hmod);
        return 1;
    }
    std::wprintf(L"OpenTuner() OK\n");

    DWORD ch_idx = static_cast<DWORD>(uhf_ch - 13);
    std::wprintf(L"SetChannel(0, %lu) [UHF %dch]...\n", ch_idx, uhf_ch);
    if (!bon->SetChannel(0, ch_idx)) {
        std::fwprintf(stderr, L"SetChannel() failed\n");
        bon->CloseTuner();
        bon->Release();
        FreeLibrary(hmod);
        return 1;
    }
    std::wprintf(L"SetChannel() OK, SignalLevel=%.2f dB\n", bon->GetSignalLevel());

    FILE *fp = nullptr;
    if (out_path) {
        _wfopen_s(&fp, out_path, L"wb");
    }

    uint64_t total_bytes = 0;
    uint64_t total_pkts = 0;
    uint64_t sync_ok = 0;
    uint64_t tei_err = 0;
    uint64_t scrambled = 0;

    auto start = std::chrono::steady_clock::now();
    while (std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::steady_clock::now() - start)
               .count() < sec) {
        if (bon->WaitTsStream(500) != WAIT_OBJECT_0) {
            continue;
        }
        BYTE *buf = nullptr;
        DWORD size = 0;
        DWORD remain = 0;
        while (bon->GetTsStream(&buf, &size, &remain)) {
            if (buf && size > 0) {
                total_bytes += size;
                if (fp) std::fwrite(buf, 1, size, fp);
                for (DWORD off = 0; off + 188 <= size; off += 188) {
                    ++total_pkts;
                    if (buf[off] == 0x47) ++sync_ok;
                    if (buf[off + 1] & 0x80) ++tei_err;
                    if ((buf[off + 3] & 0xc0) != 0) ++scrambled;
                }
            }
            if (remain == 0) break;
        }
    }

    if (fp) std::fclose(fp);
    std::wprintf(L"Done: %llu bytes, %llu pkts (sync=%llu, tei=%llu, scrambled=%llu), SignalLevel=%.2f dB\n",
                 static_cast<unsigned long long>(total_bytes),
                 static_cast<unsigned long long>(total_pkts),
                 static_cast<unsigned long long>(sync_ok),
                 static_cast<unsigned long long>(tei_err),
                 static_cast<unsigned long long>(scrambled),
                 bon->GetSignalLevel());

    bon->CloseTuner();
    bon->Release();
    FreeLibrary(hmod);
    return (total_bytes > 0 && sync_ok == total_pkts) ? 0 : 1;
}
