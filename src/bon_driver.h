// SPDX-License-Identifier: MIT
// Copyright (c) 2026 waya0125
#pragma once

#include "IBonDriver2.h"
#include "ivdrm.h"
#include "saa7164.h"
#include "winusb_dev.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace gvmvphz {

class BonDriverGVMVPHZ : public IBonDriver2 {
public:
    explicit BonDriverGVMVPHZ(HMODULE hmodule);
    ~BonDriverGVMVPHZ();

    // IBonDriver2
    LPCTSTR GetTunerName(void) override;
    const BOOL IsTunerOpening(void) override;
    LPCTSTR EnumTuningSpace(const DWORD dwSpace) override;
    LPCTSTR EnumChannelName(const DWORD dwSpace, const DWORD dwChannel) override;
    const BOOL SetChannel(const DWORD dwSpace, const DWORD dwChannel) override;
    const DWORD GetCurSpace(void) override;
    const DWORD GetCurChannel(void) override;

    // IBonDriver
    const BOOL OpenTuner(void) override;
    void CloseTuner(void) override;
    const BOOL SetChannel(const BYTE bCh) override;
    const float GetSignalLevel(void) override;
    const DWORD WaitTsStream(const DWORD dwTimeOut = 0) override;
    const DWORD GetReadyCount(void) override;
    const BOOL GetTsStream(BYTE *pDst, DWORD *pdwSize, DWORD *pdwRemain) override;
    const BOOL GetTsStream(BYTE **ppDst, DWORD *pdwSize, DWORD *pdwRemain) override;
    void PurgeTsStream(void) override;
    void Release(void) override;

private:
    std::wstring ResolveFirmwarePath() const;
    void StopStreamLocked();
    void OnRawChunk(const uint8_t *data, size_t len);

    HMODULE hmodule_ = nullptr;
    std::mutex api_mu_;

    WinUsbDevice usb_;
    std::unique_ptr<saa7164::Client> client_;
    uint32_t session_id_ = 0;
    std::array<uint8_t, 16> session_key_{};
    ivdrm::WatchDog watchdog_;
    std::unique_ptr<ivdrm::TSDecryptor> decryptor_;
    bool is_open_ = false;
    bool streaming_ = false;
    DWORD cur_channel_ = 0; // 0..49 -> UHF 13..62

    std::mutex queue_mu_;
    std::condition_variable queue_cv_;
    std::deque<std::vector<uint8_t>> ts_queue_;
    std::vector<uint8_t> last_chunk_;

    std::wstring ch_name_buf_;
};

} // namespace gvmvphz
