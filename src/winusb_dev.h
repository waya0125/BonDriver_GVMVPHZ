// SPDX-License-Identifier: MIT
// Copyright (c) 2026 waya0125
#pragma once

#include <windows.h>
#include <winusb.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <thread>
#include <vector>

namespace gvmvphz {

constexpr uint16_t kVendorID = 0x04bb;
constexpr uint16_t kProductID = 0x0522;

struct UsbDeviceInfo {
    std::wstring path;
    uint16_t vid = 0;
    uint16_t pid = 0;
    uint16_t bcd_device = 0;
};

// Enumerate WinUSB devices matching VID/PID.
std::vector<UsbDeviceInfo> EnumerateWinUsbDevices(uint16_t vid, uint16_t pid);

class WinUsbDevice {
public:
    WinUsbDevice() = default;
    ~WinUsbDevice();

    WinUsbDevice(const WinUsbDevice &) = delete;
    WinUsbDevice &operator=(const WinUsbDevice &) = delete;

    bool Open(const std::wstring &device_path, bool claim_stream_if = true);
    void Close();
    bool IsOpen() const { return winusb_if0_ != nullptr; }

    uint16_t BcdDevice() const { return bcd_device_; }

    bool ControlTransfer(uint8_t req_type, uint8_t request, uint16_t value, uint16_t index,
                         uint8_t *data, uint16_t length, uint32_t timeout_ms, uint32_t *transferred = nullptr);

    bool WritePipe(uint8_t ep, const uint8_t *data, uint32_t length, uint32_t timeout_ms,
                   uint32_t *transferred = nullptr);
    bool ReadPipe(uint8_t ep, uint8_t *data, uint32_t length, uint32_t timeout_ms,
                  uint32_t *transferred = nullptr);
    bool ResetPipe(uint8_t ep);

    std::string ReadStringDescAscii(uint8_t index);

    using ChunkCallback = std::function<void(const uint8_t *, size_t)>;
    bool StartBulkStream(uint8_t ep, size_t urb_count, size_t urb_size, ChunkCallback on_chunk);
    void StopBulkStream();

private:
    WINUSB_INTERFACE_HANDLE HandleForEndpoint(uint8_t ep) const;
    void StreamLoop(uint8_t ep, size_t urb_count, size_t urb_size, ChunkCallback on_chunk);

    HANDLE dev_handle_ = INVALID_HANDLE_VALUE;
    WINUSB_INTERFACE_HANDLE winusb_if0_ = nullptr;
    WINUSB_INTERFACE_HANDLE winusb_if2_ = nullptr;
    uint16_t bcd_device_ = 0;

    std::atomic<bool> stream_running_{false};
    std::thread stream_thread_;
};

} // namespace gvmvphz
