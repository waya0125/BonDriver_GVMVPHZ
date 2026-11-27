// SPDX-License-Identifier: MIT
// Copyright (c) 2026 waya0125
#include "winusb_dev.h"

#include <initguid.h>
#include <setupapi.h>
#include <usbiodef.h>

#include <algorithm>
#include <cwctype>
#include <memory>

namespace gvmvphz {

namespace {

std::wstring ToLower(std::wstring s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    return s;
}

// Layout-compatible copy of WINUSB_PIPE_INFORMATION_EX (winusbio.h). Declared
// here so that the build does not depend on NTDDI_VERSION >= NTDDI_WINBLUE.
struct PipeInfoExCompat {
    USBD_PIPE_TYPE PipeType;
    UCHAR PipeId;
    USHORT MaximumPacketSize;
    UCHAR Interval;
    ULONG MaximumBytesPerInterval;
};

using QueryPipeExFn = BOOL(WINAPI *)(WINUSB_INTERFACE_HANDLE, UCHAR, UCHAR, PipeInfoExCompat *);

// WinUsb_QueryPipeEx exists only on Windows 8.1 and later. Resolve it at run
// time so that the DLL still loads on Windows 7 (static import would fail).
QueryPipeExFn ResolveQueryPipeEx() {
    static const QueryPipeExFn fn = []() -> QueryPipeExFn {
        HMODULE h = GetModuleHandleW(L"winusb.dll");
        if (!h) return nullptr;
        return reinterpret_cast<QueryPipeExFn>(reinterpret_cast<void *>(GetProcAddress(h, "WinUsb_QueryPipeEx")));
    }();
    return fn;
}

bool MatchesVidPid(const std::wstring &path, uint16_t vid, uint16_t pid) {
    wchar_t needle[32];
    swprintf_s(needle, L"vid_%04x&pid_%04x", vid, pid);
    return ToLower(path).find(needle) != std::wstring::npos;
}

} // namespace

std::vector<UsbDeviceInfo> EnumerateWinUsbDevices(uint16_t vid, uint16_t pid) {
    std::vector<UsbDeviceInfo> result;

    HDEVINFO dev_info = SetupDiGetClassDevsW(
        &GUID_DEVINTERFACE_USB_DEVICE, nullptr, nullptr,
        DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (dev_info == INVALID_HANDLE_VALUE) {
        return result;
    }

    SP_DEVICE_INTERFACE_DATA iface_data{};
    iface_data.cbSize = sizeof(iface_data);

    for (DWORD index = 0;
         SetupDiEnumDeviceInterfaces(dev_info, nullptr, &GUID_DEVINTERFACE_USB_DEVICE, index, &iface_data);
         ++index) {
        DWORD required_size = 0;
        SetupDiGetDeviceInterfaceDetailW(dev_info, &iface_data, nullptr, 0, &required_size, nullptr);
        if (required_size == 0) {
            continue;
        }
        std::vector<uint8_t> detail_buf(required_size);
        auto *detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W *>(detail_buf.data());
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);

        if (!SetupDiGetDeviceInterfaceDetailW(dev_info, &iface_data, detail, required_size, nullptr, nullptr)) {
            continue;
        }

        std::wstring path(detail->DevicePath);
        if (!MatchesVidPid(path, vid, pid)) {
            continue;
        }

        UsbDeviceInfo info;
        info.path = std::move(path);
        info.vid = vid;
        info.pid = pid;
        result.push_back(std::move(info));
    }

    SetupDiDestroyDeviceInfoList(dev_info);
    return result;
}

WinUsbDevice::~WinUsbDevice() {
    Close();
}

bool WinUsbDevice::Open(const std::wstring &device_path, bool claim_stream_if) {
    Close();

    dev_handle_ = CreateFileW(
        device_path.c_str(),
        GENERIC_WRITE | GENERIC_READ,
        FILE_SHARE_WRITE | FILE_SHARE_READ,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED,
        nullptr);
    if (dev_handle_ == INVALID_HANDLE_VALUE) {
        last_open_error_ = GetLastError();
        return false;
    }

    if (!WinUsb_Initialize(dev_handle_, &winusb_if0_)) {
        DWORD err = GetLastError();
        Close();
        last_open_error_ = err;
        return false;
    }
    last_open_error_ = ERROR_SUCCESS;

    USB_DEVICE_DESCRIPTOR desc{};
    ULONG transferred = 0;
    if (WinUsb_GetDescriptor(winusb_if0_, USB_DEVICE_DESCRIPTOR_TYPE, 0, 0,
                             reinterpret_cast<PUCHAR>(&desc), sizeof(desc), &transferred) &&
        transferred >= sizeof(desc)) {
        bcd_device_ = desc.bcdDevice;
    }

    if (claim_stream_if) {
        // Locate the associated interface that owns EP 0x83 (typically IF2 -> associated index 1).
        for (UCHAR assoc_idx = 0; assoc_idx < 4; ++assoc_idx) {
            WINUSB_INTERFACE_HANDLE assoc = nullptr;
            if (!WinUsb_GetAssociatedInterface(winusb_if0_, assoc_idx, &assoc)) {
                break;
            }
            USB_INTERFACE_DESCRIPTOR if_desc{};
            bool found_ep83 = false;
            if (WinUsb_QueryInterfaceSettings(assoc, 0, &if_desc)) {
                for (UCHAR p = 0; p < if_desc.bNumEndpoints; ++p) {
                    WINUSB_PIPE_INFORMATION pipe_info{};
                    if (WinUsb_QueryPipe(assoc, 0, p, &pipe_info) && pipe_info.PipeId == 0x83) {
                        found_ep83 = true;
                        break;
                    }
                }
            }
            if (found_ep83) {
                winusb_if2_ = assoc;
                break;
            }
            WinUsb_Free(assoc);
        }
    }

    return true;
}

void WinUsbDevice::Close() {
    StopBulkStream();
    if (winusb_if2_) {
        WinUsb_Free(winusb_if2_);
        winusb_if2_ = nullptr;
    }
    if (winusb_if0_) {
        WinUsb_Free(winusb_if0_);
        winusb_if0_ = nullptr;
    }
    if (dev_handle_ != INVALID_HANDLE_VALUE) {
        CloseHandle(dev_handle_);
        dev_handle_ = INVALID_HANDLE_VALUE;
    }
    bcd_device_ = 0;
}

WINUSB_INTERFACE_HANDLE WinUsbDevice::HandleForEndpoint(uint8_t ep) const {
    if ((ep == 0x83 || ep == 0x03) && winusb_if2_ != nullptr) {
        return winusb_if2_;
    }
    return winusb_if0_;
}

bool WinUsbDevice::QueryDeviceSpeed(uint8_t *speed_out) const {
    if (!winusb_if0_ || !speed_out) return false;
    UCHAR speed = 0;
    ULONG len = sizeof(speed);
    if (!WinUsb_QueryDeviceInformation(winusb_if0_, DEVICE_SPEED, &len, &speed) || len < 1) {
        return false;
    }
    *speed_out = static_cast<uint8_t>(speed);
    return true;
}

bool WinUsbDevice::QueryPipeInfo(uint8_t ep, UsbPipeInfo *out) const {
    if (!out) return false;
    *out = UsbPipeInfo{};
    WINUSB_INTERFACE_HANDLE h = HandleForEndpoint(ep);
    if (!h) return false;

    USB_INTERFACE_DESCRIPTOR if_desc{};
    if (!WinUsb_QueryInterfaceSettings(h, 0, &if_desc)) return false;

    for (UCHAR idx = 0; idx < if_desc.bNumEndpoints; ++idx) {
        WINUSB_PIPE_INFORMATION basic{};
        if (!WinUsb_QueryPipe(h, 0, idx, &basic) || basic.PipeId != ep) continue;

        out->found = true;
        out->pipe_type = static_cast<uint8_t>(basic.PipeType);
        out->pipe_id = basic.PipeId;
        out->max_packet_size = basic.MaximumPacketSize;
        out->interval = basic.Interval;

        if (QueryPipeExFn query_ex = ResolveQueryPipeEx()) {
            PipeInfoExCompat ex{};
            if (query_ex(h, 0, idx, &ex) && ex.PipeId == ep) {
                out->ex_available = true;
                out->pipe_type = static_cast<uint8_t>(ex.PipeType);
                out->max_packet_size = ex.MaximumPacketSize;
                out->interval = ex.Interval;
                out->max_bytes_per_interval = ex.MaximumBytesPerInterval;
            }
        }
        return true;
    }
    return false;
}

UsbLinkInfo WinUsbDevice::QueryLinkInfo(uint8_t ep) const {
    UsbLinkInfo info;
    info.speed_valid = QueryDeviceSpeed(&info.speed);
    QueryPipeInfo(ep, &info.pipe);
    return info;
}

bool WinUsbDevice::ControlTransfer(uint8_t req_type, uint8_t request, uint16_t value, uint16_t index,
                                   uint8_t *data, uint16_t length, uint32_t timeout_ms, uint32_t *transferred) {
    if (!winusb_if0_) return false;
    ULONG timeout = timeout_ms;
    WinUsb_SetPipePolicy(winusb_if0_, 0, PIPE_TRANSFER_TIMEOUT, sizeof(timeout), &timeout);

    WINUSB_SETUP_PACKET pkt{};
    pkt.RequestType = req_type;
    pkt.Request = request;
    pkt.Value = value;
    pkt.Index = index;
    pkt.Length = length;

    ULONG xfer = 0;
    BOOL ok = WinUsb_ControlTransfer(winusb_if0_, pkt, data, length, &xfer, nullptr);
    if (transferred) *transferred = static_cast<uint32_t>(xfer);
    return ok != FALSE;
}

bool WinUsbDevice::WritePipe(uint8_t ep, const uint8_t *data, uint32_t length, uint32_t timeout_ms,
                             uint32_t *transferred) {
    WINUSB_INTERFACE_HANDLE h = HandleForEndpoint(ep);
    if (!h) return false;
    ULONG timeout = timeout_ms;
    WinUsb_SetPipePolicy(h, ep, PIPE_TRANSFER_TIMEOUT, sizeof(timeout), &timeout);

    ULONG xfer = 0;
    BOOL ok = WinUsb_WritePipe(h, ep, const_cast<PUCHAR>(data), length, &xfer, nullptr);
    if (transferred) *transferred = static_cast<uint32_t>(xfer);
    return ok != FALSE;
}

bool WinUsbDevice::ReadPipe(uint8_t ep, uint8_t *data, uint32_t length, uint32_t timeout_ms,
                            uint32_t *transferred) {
    WINUSB_INTERFACE_HANDLE h = HandleForEndpoint(ep);
    if (!h) return false;
    ULONG timeout = timeout_ms;
    WinUsb_SetPipePolicy(h, ep, PIPE_TRANSFER_TIMEOUT, sizeof(timeout), &timeout);

    ULONG xfer = 0;
    BOOL ok = WinUsb_ReadPipe(h, ep, data, length, &xfer, nullptr);
    if (transferred) *transferred = static_cast<uint32_t>(xfer);
    return ok != FALSE;
}

bool WinUsbDevice::ResetPipe(uint8_t ep) {
    WINUSB_INTERFACE_HANDLE h = HandleForEndpoint(ep);
    if (!h) return false;
    return WinUsb_ResetPipe(h, ep) != FALSE;
}

std::string WinUsbDevice::ReadStringDescAscii(uint8_t index) {
    uint8_t buf[128]{};
    const uint16_t langs[2] = {0x0409, 0x0000};
    for (uint16_t lang : langs) {
        uint32_t n = 0;
        if (!ControlTransfer(0x80, 0x06, static_cast<uint16_t>(0x0300u | index), lang,
                             buf, sizeof(buf), 2000, &n) ||
            n < 2) {
            continue;
        }
        std::string out;
        for (uint32_t i = 2; i + 1 < n; i += 2) {
            if (buf[i + 1] == 0 && buf[i] >= 0x20 && buf[i] < 0x7f) {
                out.push_back(static_cast<char>(buf[i]));
            }
        }
        if (!out.empty()) return out;
    }
    return {};
}

bool WinUsbDevice::StartBulkStream(uint8_t ep, size_t urb_count, size_t urb_size, ChunkCallback on_chunk) {
    StopBulkStream();
    WINUSB_INTERFACE_HANDLE h = HandleForEndpoint(ep);
    if (!h || urb_count == 0 || urb_size == 0) return false;

    ULONG zero_timeout = 0;
    WinUsb_SetPipePolicy(h, ep, PIPE_TRANSFER_TIMEOUT, sizeof(zero_timeout), &zero_timeout);
    UCHAR raw_io = 1;
    WinUsb_SetPipePolicy(h, ep, RAW_IO, sizeof(raw_io), &raw_io);

    stream_running_.store(true);
    stream_thread_ = std::thread([this, ep, urb_count, urb_size, cb = std::move(on_chunk)]() mutable {
        StreamLoop(ep, urb_count, urb_size, std::move(cb));
    });
    return true;
}

void WinUsbDevice::StopBulkStream() {
    if (stream_running_.exchange(false)) {
        if (winusb_if2_) {
            WinUsb_AbortPipe(winusb_if2_, 0x83);
        }
    }
    if (stream_thread_.joinable()) {
        stream_thread_.join();
    }
}

void WinUsbDevice::StreamLoop(uint8_t ep, size_t urb_count, size_t urb_size, ChunkCallback on_chunk) {
    WINUSB_INTERFACE_HANDLE h = HandleForEndpoint(ep);
    if (!h) return;

    struct Slot {
        std::vector<uint8_t> buf;
        OVERLAPPED ov{};
        bool pending = false;
    };

    std::vector<Slot> slots(urb_count);
    std::vector<HANDLE> events(urb_count, nullptr);
    for (size_t i = 0; i < urb_count; ++i) {
        slots[i].buf.resize(urb_size);
        events[i] = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        slots[i].ov.hEvent = events[i];
    }

    auto submit = [&](size_t idx) {
        ResetEvent(slots[idx].ov.hEvent);
        slots[idx].ov.Internal = 0;
        slots[idx].ov.InternalHigh = 0;
        slots[idx].ov.Offset = 0;
        slots[idx].ov.OffsetHigh = 0;
        ULONG dummy = 0;
        BOOL ok = WinUsb_ReadPipe(h, ep, slots[idx].buf.data(), static_cast<ULONG>(urb_size),
                                  &dummy, &slots[idx].ov);
        if (ok || GetLastError() == ERROR_IO_PENDING) {
            slots[idx].pending = true;
        } else {
            slots[idx].pending = false;
        }
    };

    for (size_t i = 0; i < urb_count; ++i) {
        submit(i);
    }

    while (stream_running_.load()) {
        DWORD wait = WaitForMultipleObjects(static_cast<DWORD>(urb_count), events.data(), FALSE, 100);
        if (wait == WAIT_TIMEOUT) {
            continue;
        }
        if (wait < WAIT_OBJECT_0 || wait >= WAIT_OBJECT_0 + urb_count) {
            break;
        }
        size_t idx = static_cast<size_t>(wait - WAIT_OBJECT_0);
        ULONG xfer = 0;
        BOOL ok = WinUsb_GetOverlappedResult(h, &slots[idx].ov, &xfer, FALSE);
        slots[idx].pending = false;
        if (ok && xfer > 0 && on_chunk) {
            on_chunk(slots[idx].buf.data(), static_cast<size_t>(xfer));
        }
        if (stream_running_.load()) {
            submit(idx);
        }
    }

    WinUsb_AbortPipe(h, ep);
    for (size_t i = 0; i < urb_count; ++i) {
        if (slots[i].pending) {
            ULONG dummy = 0;
            WinUsb_GetOverlappedResult(h, &slots[i].ov, &dummy, TRUE);
        }
        if (events[i]) {
            CloseHandle(events[i]);
        }
    }
}

} // namespace gvmvphz
