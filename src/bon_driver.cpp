// SPDX-License-Identifier: MIT
// Copyright (c) 2026 waya0125
#include "bon_driver.h"
#include "saigawa.h"

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <thread>

namespace gvmvphz {

namespace {

constexpr uint8_t kStreamEndpoint = 0x83;
constexpr size_t kDefaultUrbCount = 8;
constexpr size_t kDefaultUrbSize = 32768;
constexpr size_t kMaxQueueChunks = 256;
constexpr DWORD kNumUhfChannels = 50; // UHF 13..62
constexpr uint8_t kPipeTypeBulk = 2;  // USBD_PIPE_TYPE::UsbdPipeTypeBulk
constexpr const wchar_t *kIniSection = L"GVMVPHZ";

// Diagnostics go to OutputDebugString (view with Sysinternals DebugView).
void DebugLog(const wchar_t *fmt, ...) {
    wchar_t buf[512];
    int n = swprintf(buf, 512, L"[BonDriver_GVMVPHZ] ");
    if (n < 0) n = 0;
    va_list ap;
    va_start(ap, fmt);
    vswprintf(buf + n, 512 - static_cast<size_t>(n) - 2, fmt, ap);
    va_end(ap);
    buf[510] = L'\0';
    size_t len = wcslen(buf);
    buf[len] = L'\n';
    buf[len + 1] = L'\0';
    OutputDebugStringW(buf);
}

} // namespace

BonDriverGVMVPHZ::BonDriverGVMVPHZ(HMODULE hmodule) : hmodule_(hmodule) {
    LoadIni();
}

// Reads BonDriver_GVMVPHZ.ini (same folder / base name as the DLL, so a
// renamed copy such as BonDriver_GVMVPHZ-1.dll reads BonDriver_GVMVPHZ-1.ini).
void BonDriverGVMVPHZ::LoadIni() {
    cfg_ = Config{};
    cfg_.urb_count = kDefaultUrbCount;
    cfg_.urb_size = kDefaultUrbSize;
    if (!hmodule_) return;

    wchar_t path[MAX_PATH]{};
    DWORD n = GetModuleFileNameW(hmodule_, path, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return;
    std::wstring ini(path);
    const size_t slash = ini.find_last_of(L"\\/");
    const size_t dot = ini.rfind(L'.');
    if (dot != std::wstring::npos && (slash == std::wstring::npos || dot > slash)) {
        ini.replace(dot, std::wstring::npos, L".ini");
    } else {
        ini += L".ini";
    }

    // Read as a string and parse it ourselves so that negative values
    // (DeviceIndex=-1) do not depend on GetPrivateProfileIntW's UINT return.
    wchar_t idx_buf[32]{};
    GetPrivateProfileStringW(kIniSection, L"DeviceIndex", L"0", idx_buf, 32, ini.c_str());
    cfg_.device_index = ParseDeviceIndex(idx_buf, 0);
    cfg_.urb_count = ClampUrbCount(
        static_cast<int>(GetPrivateProfileIntW(kIniSection, L"UrbCount", static_cast<INT>(kDefaultUrbCount), ini.c_str())),
        kDefaultUrbCount);
    cfg_.urb_size = ClampUrbSize(
        static_cast<int>(GetPrivateProfileIntW(kIniSection, L"UrbSize", static_cast<INT>(kDefaultUrbSize), ini.c_str())),
        kDefaultUrbSize);
}

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
    const auto order = CandidateOrder(devs.size(), cfg_.device_index);
    if (order.empty()) {
        DebugLog(L"no device for DeviceIndex=%d (%lu connected)", cfg_.device_index,
                 static_cast<unsigned long>(devs.size()));
        return FALSE;
    }

    std::wstring fw_path;
    for (size_t idx : order) {
        std::wstring dev_path = devs[idx].path;

        // A device opened by another process (another TVTest / EDCB instance)
        // cannot be opened again because WinUSB is exclusive; skip it.
        bool need_dfu = false;
        {
            WinUsbDevice probe;
            if (!probe.Open(dev_path, false)) {
                DebugLog(L"device #%lu: open failed (error %lu), in use?", static_cast<unsigned long>(idx),
                         static_cast<unsigned long>(probe.LastOpenError()));
                continue;
            }
            need_dfu = (probe.BcdDevice() != 0x0100) || saa7164::IsDFUDevice(&probe);
        }

        if (need_dfu) {
            if (fw_path.empty()) fw_path = ResolveFirmwarePath();
            if (fw_path.empty()) {
                DebugLog(L"device #%lu needs firmware but NXP7164F_052204BB.bin was not found",
                         static_cast<unsigned long>(idx));
                return FALSE;
            }
            std::wstring ready_path;
            if (!saa7164::BootFirmware(kVendorID, kProductID, fw_path, dev_path, &ready_path)) {
                DebugLog(L"device #%lu: firmware boot failed", static_cast<unsigned long>(idx));
                continue;
            }
            dev_path = ready_path;
        }

        if (!OpenDeviceLocked(dev_path)) continue;
        DebugLog(L"using device #%lu of %lu (DeviceIndex=%d)", static_cast<unsigned long>(idx),
                 static_cast<unsigned long>(devs.size()), cfg_.device_index);
        return InitOpenedDeviceLocked() ? TRUE : FALSE;
    }

    DebugLog(L"no usable device (DeviceIndex=%d, %lu connected)", cfg_.device_index,
             static_cast<unsigned long>(devs.size()));
    return FALSE;
}

bool BonDriverGVMVPHZ::OpenDeviceLocked(const std::wstring &path) {
    if (!usb_.Open(path, true)) {
        DebugLog(L"open failed (error %lu)", static_cast<unsigned long>(usb_.LastOpenError()));
        return false;
    }

    // Check the bulk stream parameters against what WinUSB reports.
    const UsbLinkInfo link = usb_.QueryLinkInfo(kStreamEndpoint);
    if (link.speed_valid) {
        DebugLog(L"USB speed: %ls (%u)", UsbSpeedName(link.speed), static_cast<unsigned>(link.speed));
        if (link.speed != kUsbSpeedHigh) {
            DebugLog(L"WARNING: not connected at High-Speed; the TS stream (~17-23 Mbps) will not fit");
        }
    } else {
        DebugLog(L"USB speed: unknown (WinUsb_QueryDeviceInformation failed)");
    }

    uint16_t max_packet = 0;
    if (link.pipe.found) {
        max_packet = link.pipe.max_packet_size;
        DebugLog(L"EP 0x%02X: type=%u wMaxPacketSize=%u interval=%u maxBytesPerInterval=%lu (%ls)",
                 static_cast<unsigned>(link.pipe.pipe_id), static_cast<unsigned>(link.pipe.pipe_type),
                 static_cast<unsigned>(link.pipe.max_packet_size), static_cast<unsigned>(link.pipe.interval),
                 static_cast<unsigned long>(link.pipe.max_bytes_per_interval),
                 link.pipe.ex_available ? L"WinUsb_QueryPipeEx" : L"WinUsb_QueryPipe");
        if (link.pipe.pipe_type != kPipeTypeBulk) {
            DebugLog(L"WARNING: EP 0x%02X is not a bulk pipe", static_cast<unsigned>(kStreamEndpoint));
        }
    } else {
        DebugLog(L"EP 0x%02X: pipe information not available", static_cast<unsigned>(kStreamEndpoint));
    }

    stream_urb_size_ = AlignUrbSize(cfg_.urb_size, max_packet);
    if (stream_urb_size_ != cfg_.urb_size) {
        DebugLog(L"UrbSize %lu is not a multiple of wMaxPacketSize %u; using %lu",
                 static_cast<unsigned long>(cfg_.urb_size), static_cast<unsigned>(max_packet),
                 static_cast<unsigned long>(stream_urb_size_));
    }
    DebugLog(L"bulk stream: %lu URBs x %lu bytes", static_cast<unsigned long>(cfg_.urb_count),
             static_cast<unsigned long>(stream_urb_size_));
    return true;
}

bool BonDriverGVMVPHZ::InitOpenedDeviceLocked() {
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
    std::vector<uint8_t> ep_drain(stream_urb_size_);
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
            return false;
        }
        client_->InitInternalBCAS();
    }

    client_->PrepareStreamUnit();

    session_id_ = 0;
    if (!ivdrm::RunAuth(client_.get(), 0, &session_key_)) {
        session_id_ = 1;
        if (!ivdrm::RunAuth(client_.get(), 1, &session_key_)) {
            CloseTunerLocked();
            return false;
        }
    }

    if (!watchdog_.Start(client_.get(), session_key_)) {
        ivdrm::CloseSession(client_.get(), session_id_);
        CloseTunerLocked();
        return false;
    }

    decryptor_ = std::make_unique<ivdrm::TSDecryptor>(session_key_);
    is_open_ = true;
    return true;
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

    PurgeTsStream();
    if (decryptor_) decryptor_->Reset();

    uint32_t ch = 13 + dwChannel;
    uint32_t freq_hz = 473142857u + (ch - 13u) * 6000000u;
    bool locked = false;
    if (!saigawa::TuneISDBT(client_.get(), freq_hz, &locked)) {
        return FALSE;
    }
    cur_channel_ = dwChannel;
    PurgeTsStream();

    if (!streaming_) {
        if (!usb_.StartBulkStream(kStreamEndpoint, cfg_.urb_count, stream_urb_size_,
                                  [this](const uint8_t *data, size_t len) { OnRawChunk(data, len); })) {
            return FALSE;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(40));
        if (!client_->SetState(8, saa7164::kStateRun)) {
            usb_.StopBulkStream();
            return FALSE;
        }
        streaming_ = true;
    }
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
