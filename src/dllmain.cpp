// SPDX-License-Identifier: MIT
// Copyright (c) 2026 waya0125
#include "bon_driver.h"

namespace {
HMODULE g_hmodule = nullptr;
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID /*lpvReserved*/) {
    if (fdwReason == DLL_PROCESS_ATTACH) {
        g_hmodule = hinstDLL;
        DisableThreadLibraryCalls(hinstDLL);
    }
    return TRUE;
}

extern "C" __declspec(dllexport) IBonDriver *CreateBonDriver(void) {
    return new gvmvphz::BonDriverGVMVPHZ(g_hmodule);
}
