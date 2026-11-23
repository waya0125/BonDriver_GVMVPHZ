// SPDX-License-Identifier: MIT
// Copyright (c) 2026 waya0125
#include "bon_driver.h"
#include "saigawa.h"

#include <chrono>
#include <cstring>
#include <filesystem>
#include <thread>

namespace gvmvphz {

namespace {

constexpr uint8_t kStreamEndpoint = 0x83;
constexpr size_t kUrbCount = 8;
constexpr size_t kUrbSize = 32768;
constexpr size_t kMaxQueueChunks = 256;
constexpr DWORD kNumUhfChannels = 50; // UHF 13..62

} // namespace

BonDriverGVMVPHZ::BonDriverGVMVPHZ(HMODULE hmodule) : hmodule_(hmodule) {}

BonDriverGVMVPHZ::~BonDriverGVMVPHZ() {
    CloseTuner();
}

std::wstring BonDriverGVMVPHZ::ResolveFirmwarePath() const {
    namespace fs = std::filesystem;
    const wchar_t *kFwName = L"NXP7164F_052204BB.bin";

    wchar_t env_buf[MAX_PATH]{};
    if (GetEnvironmentVariableW(L"GVMVPHZ_FW_PATH", env_buf, MAX_PATH) > 0 && fs::is_regular_file(env_buf)) {
        return env_buf;
    }
    if (GetEnvironmentVariableW(L"GVMVP_FW_PATH", env_buf, MAX_PATH) > 0 && fs::is_regular_file(env_buf)) {
        return env_buf;
    }

    if (hmodule_) {
        wchar_t mod_path[MAX_PATH]{};
        if (GetModuleFileNameW(hmodule_, mod_path, MAX_PATH) > 0) {
            fs::path p = fs::path(mod_path).parent_path() / kFwName;
            if (fs::is_regular_file(p)) return p.wstring();
        }
    }

    fs::path cur = fs::current_path() / kFwName;
    if (fs::is_regular_file(cur)) return cur.wstring();
    return {};
}

LPCTSTR BonDriverGVMVPHZ::GetTunerName(void) {
    return TEXT("GV-MVP/HZ (WinUSB)");
}

const BOOL BonDriverGVMVPHZ::IsTunerOpening(void) {
    std::lock_guard<std::mutex> lk(api_mu_);
    return is_open_ ? TRUE : FALSE;
}

LPCTSTR BonDriverGVMVPHZ::EnumTuningSpace(const DWORD dwSpace) {
    if (dwSpace == 0) {
        return TEXT("UHF");
    }
    return nullptr;
}

LPCTSTR BonDriverGVMVPHZ::EnumChannelName(const DWORD dwSpace, const DWORD dwChannel) {
    if (dwSpace != 0 || dwChannel >= kNumUhfChannels) {
        return nullptr;
    }
    wchar_t buf[32];
    swprintf_s(buf, L"UHF %lu ch", static_cast<unsigned long>(13 + dwChannel));
    ch_name_buf_ = buf;
    return ch_name_buf_.c_str();
}

const BOOL BonDriverGVMVPHZ::OpenTuner(void) {
    std::lock_guard<std::mutex> lk(api_mu_);
    if (is_open_) return TRUE;

    auto devs = EnumerateWinUsbDevices(kVendorID, kProductID);
    if (devs.empty()) return FALSE;

    std::wstring dev_path = devs[0].path;
    bool need_dfu = false;
    {
        WinUsbDevice probe;
        if (!probe.Open(dev_path, false)) return FALSE;
        need_dfu = (probe.BcdDevice() != 0x0100) || saa7164::IsDFUDevice(&probe);
    }

    if (need_dfu) {
        std::wstring fw_path = ResolveFirmwarePath();
        if (fw_path.empty()) return FALSE;
        if (!saa7164::BootFirmware(kVendorID, kProductID, fw_path)) return FALSE;
        devs = EnumerateWinUsbDevices(kVendorID, kProductID);
        if (devs.empty()) return FALSE;
        dev_path = devs[0].path;
    }

    if (!usb_.Open(dev_path, true)) return FALSE;

    // Drain any stale interrupt messages on EP 0x81.
    uint8_t drain[saa7164::kMaxMessage];
    for (int i = 0; i < 16; ++i) {
        if (!usb_.ReadPipe(0x81, drain, sizeof(drain), 50)) break;
    }

    client_ = std::make_unique<saa7164::Client>(&usb_, 0x01, 0x81, 2000);

    // Recover from any leftover streaming state.
    client_->SetState(8, saa7164::kStatePause);
    client_->SetState(8, saa7164::kStateAcquire);
    client_->SetState(8, saa7164::kStateStop);
    std::vector<uint8_t> ep_drain(kUrbSize);
    for (int i = 0; i < 8; ++i) {
        if (!usb_.ReadPipe(kStreamEndpoint, ep_drain.data(), static_cast<uint32_t>(ep_drain.size()), 30)) break;
    }

    // Cold init only when DemodUnit reg 0x80 cannot be read.
    // Never call InitInternalBCAS() on warm reopen (prevents EP 0x81 wedge).
    uint8_t r80 = 0, rb0 = 0;
    bool locked = false;
    if (!saigawa::ReadLock(client_.get(), &r80, &rb0, &locked)) {
        if (!saigawa::Init(client_.get())) {
            CloseTunerLocked();
            return FALSE;
        }
        client_->InitInternalBCAS();
    }

    client_->PrepareStreamUnit();

    session_id_ = 0;
    if (!ivdrm::RunAuth(client_.get(), 0, &session_key_)) {
        session_id_ = 1;
        if (!ivdrm::RunAuth(client_.get(), 1, &session_key_)) {
            CloseTunerLocked();
            return FALSE;
        }
    }

    if (!watchdog_.Start(client_.get(), session_key_)) {
        ivdrm::CloseSession(client_.get(), session_id_);
        CloseTunerLocked();
        return FALSE;
    }

    decryptor_ = std::make_unique<ivdrm::TSDecryptor>(session_key_);
    is_open_ = true;
    return TRUE;
}

void BonDriverGVMVPHZ::StopStreamLocked() {
    if (!streaming_) return;
    if (client_) {
        client_->SetState(8, saa7164::kStatePause);
        client_->SetState(8, saa7164::kStateAcquire);
        client_->SetState(8, saa7164::kStateStop);
    }
    usb_.StopBulkStream();
    streaming_ = false;
}

void BonDriverGVMVPHZ::CloseTunerLocked() {
    StopStreamLocked();
    watchdog_.Stop();
    if (client_ && is_open_) {
        ivdrm::CloseSession(client_.get(), session_id_);
    }
    decryptor_.reset();
    client_.reset();
    usb_.Close();
    is_open_ = false;
    PurgeTsStream();
}

void BonDriverGVMVPHZ::CloseTuner(void) {
    std::lock_guard<std::mutex> lk(api_mu_);
    CloseTunerLocked();
}

const BOOL BonDriverGVMVPHZ::SetChannel(const BYTE bCh) {
    return SetChannel(0, static_cast<DWORD>(bCh));
}

const BOOL BonDriverGVMVPHZ::SetChannel(const DWORD dwSpace, const DWORD dwChannel) {
    std::lock_guard<std::mutex> lk(api_mu_);
    if (!is_open_ || dwSpace != 0 || dwChannel >= kNumUhfChannels) {
        return FALSE;
    }

    StopStreamLocked();
    PurgeTsStream();
    if (decryptor_) decryptor_->Reset();

    uint32_t ch = 13 + dwChannel;
    uint32_t freq_hz = 473142857u + (ch - 13u) * 6000000u;
    bool locked = false;
    if (!saigawa::TuneISDBT(client_.get(), freq_hz, &locked)) {
        return FALSE;
    }
    cur_channel_ = dwChannel;

    // Do not start TS streaming when the channel did not lock (speeds up channel scan
    // and avoids unnecessary USB pipe start/stop cycles on empty channels).
    if (!locked) {
        return TRUE;
    }

    client_->SetState(8, saa7164::kStateAcquire);
    client_->SetState(8, saa7164::kStatePause);
    if (!usb_.StartBulkStream(kStreamEndpoint, kUrbCount, kUrbSize,
                              [this](const uint8_t *data, size_t len) { OnRawChunk(data, len); })) {
        return FALSE;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(40));
    if (!client_->SetState(8, saa7164::kStateRun)) {
        usb_.StopBulkStream();
        return FALSE;
    }
    streaming_ = true;
    return TRUE;
}

const DWORD BonDriverGVMVPHZ::GetCurSpace(void) {
    return 0;
}

const DWORD BonDriverGVMVPHZ::GetCurChannel(void) {
    std::lock_guard<std::mutex> lk(api_mu_);
    return cur_channel_;
}

const float BonDriverGVMVPHZ::GetSignalLevel(void) {
    std::lock_guard<std::mutex> lk(api_mu_);
    if (!is_open_ || !client_) return 0.0f;
    uint8_t r80 = 0, rb0 = 0;
    bool locked = false;
    if (!saigawa::ReadLock(client_.get(), &r80, &rb0, &locked) || !locked) {
        return 0.0f;
    }
    uint8_t strength = 0;
    if (!saigawa::ReadSignalStrength(client_.get(), &strength)) {
        return 0.0f;
    }
    // reg 0x94 behaves inversely (smaller value = stronger signal / lower AGC gain).
    return static_cast<float>(255 - strength) * (35.0f / 255.0f);
}

void BonDriverGVMVPHZ::OnRawChunk(const uint8_t *data, size_t len) {
    if (!decryptor_) return;
    std::vector<uint8_t> decrypted;
    decrypted.reserve(len);
    decryptor_->Process(data, len, &decrypted);
    if (decrypted.empty()) return;

    {
        std::lock_guard<std::mutex> lk(queue_mu_);
        if (ts_queue_.size() >= kMaxQueueChunks) {
            ts_queue_.pop_front();
        }
        ts_queue_.push_back(std::move(decrypted));
    }
    queue_cv_.notify_one();
}

const DWORD BonDriverGVMVPHZ::WaitTsStream(const DWORD dwTimeOut) {
    std::unique_lock<std::mutex> lk(queue_mu_);
    if (!ts_queue_.empty()) return WAIT_OBJECT_0;
    if (dwTimeOut == 0) return WAIT_TIMEOUT;
    if (queue_cv_.wait_for(lk, std::chrono::milliseconds(dwTimeOut),
                           [this]() { return !ts_queue_.empty(); })) {
        return WAIT_OBJECT_0;
    }
    return WAIT_TIMEOUT;
}

const DWORD BonDriverGVMVPHZ::GetReadyCount(void) {
    std::lock_guard<std::mutex> lk(queue_mu_);
    return static_cast<DWORD>(ts_queue_.size());
}

const BOOL BonDriverGVMVPHZ::GetTsStream(BYTE *pDst, DWORD *pdwSize, DWORD *pdwRemain) {
    BYTE *src = nullptr;
    DWORD size = 0;
    if (!GetTsStream(&src, &size, pdwRemain)) {
        if (pdwSize) *pdwSize = 0;
        return FALSE;
    }
    if (pDst && size > 0) {
        std::memcpy(pDst, src, size);
    }
    if (pdwSize) *pdwSize = size;
    return TRUE;
}

const BOOL BonDriverGVMVPHZ::GetTsStream(BYTE **ppDst, DWORD *pdwSize, DWORD *pdwRemain) {
    std::lock_guard<std::mutex> lk(queue_mu_);
    if (ts_queue_.empty()) {
        if (ppDst) *ppDst = nullptr;
        if (pdwSize) *pdwSize = 0;
        if (pdwRemain) *pdwRemain = 0;
        return FALSE;
    }
    last_chunk_ = std::move(ts_queue_.front());
    ts_queue_.pop_front();
    if (ppDst) *ppDst = last_chunk_.data();
    if (pdwSize) *pdwSize = static_cast<DWORD>(last_chunk_.size());
    if (pdwRemain) *pdwRemain = static_cast<DWORD>(ts_queue_.size());
    return TRUE;
}

void BonDriverGVMVPHZ::PurgeTsStream(void) {
    std::lock_guard<std::mutex> lk(queue_mu_);
    ts_queue_.clear();
}

void BonDriverGVMVPHZ::Release(void) {
    delete this;
}

} // namespace gvmvphz
