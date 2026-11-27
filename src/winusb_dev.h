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

// Pipe information for one endpoint (WinUsb_QueryPipe / WinUsb_QueryPipeEx).
struct UsbPipeInfo {
    bool found = false;              // endpoint exists on the claimed interface
    bool ex_available = false;       // WinUsb_QueryPipeEx succeeded (Windows 8.1+)
    uint8_t pipe_type = 0;           // USBD_PIPE_TYPE (2 = Bulk)
    uint8_t pipe_id = 0;
    uint16_t max_packet_size = 0;    // wMaxPacketSize (512 for High-Speed bulk)
    uint8_t interval = 0;
    uint32_t max_bytes_per_interval = 0; // only valid when ex_available
};

// USB link information used to sanity-check bulk transfer parameters.
struct UsbLinkInfo {
    bool speed_valid = false;        // WinUsb_QueryDeviceInformation(DEVICE_SPEED) succeeded
    uint8_t speed = 0;               // LowSpeed=1 / FullSpeed=2 / HighSpeed=3
    UsbPipeInfo pipe;
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
    // Win32 error of the last failed Open() (e.g. ERROR_ACCESS_DENIED when
    // another process already has the device open).
    DWORD LastOpenError() const { return last_open_error_; }

    // WinUsb_QueryDeviceInformation(DEVICE_SPEED).
    bool QueryDeviceSpeed(uint8_t *speed_out) const;
    // WinUsb_QueryPipeEx (falls back to WinUsb_QueryPipe on Windows 7 / 8).
    bool QueryPipeInfo(uint8_t ep, UsbPipeInfo *out) const;
    UsbLinkInfo QueryLinkInfo(uint8_t ep) const;

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
    DWORD last_open_error_ = ERROR_SUCCESS;

    std::atomic<bool> stream_running_{false};
    std::thread stream_thread_;
};

} // namespace gvmvphz
