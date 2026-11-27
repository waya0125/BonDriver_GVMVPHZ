// SPDX-License-Identifier: MIT
// Copyright (c) 2026 waya0125
#include "saa7164.h"
#include "device_select.h"

#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>
#include <thread>

namespace gvmvphz {
namespace saa7164 {

namespace {

uint16_t ReadLE16(const uint8_t *p) {
    return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}

uint32_t ReadLE32(const uint8_t *p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

void WriteLE16(uint8_t *p, uint16_t v) {
    p[0] = static_cast<uint8_t>(v & 0xff);
    p[1] = static_cast<uint8_t>((v >> 8) & 0xff);
}

void WriteLE32(uint8_t *p, uint32_t v) {
    p[0] = static_cast<uint8_t>(v & 0xff);
    p[1] = static_cast<uint8_t>((v >> 8) & 0xff);
    p[2] = static_cast<uint8_t>((v >> 16) & 0xff);
    p[3] = static_cast<uint8_t>((v >> 24) & 0xff);
}

bool UnmarshalHeader(const uint8_t *b, size_t n, Header *h, const uint8_t **payload, size_t *payload_len) {
    if (n < kHeaderSize) return false;
    h->id = b[0];
    h->flags = b[1];
    h->size = ReadLE16(b + 2);
    h->command = ReadLE32(b + 4);
    h->control_selector = ReadLE16(b + 8);
    h->seq_no = b[10];
    *payload = b + kHeaderSize;
    size_t avail = n - kHeaderSize;
    *payload_len = (h->size < avail) ? h->size : avail;
    return true;
}

void SleepMs(int ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

bool UploadDFUStage(WinUsbDevice *dev, const uint8_t *img, size_t len) {
    dev->ResetPipe(0x03);

    uint8_t st0[6]{};
    uint32_t r = 0;
    bool ok = dev->ControlTransfer(0xA1, 3, 0, 0, st0, sizeof(st0), 2000, &r); // DFU_GETSTATUS
    if (ok && r == 6 && st0[4] == 10) { // dfuERROR
        dev->ControlTransfer(0x21, 4, 0, 0, nullptr, 0, 2000); // DFU_CLRSTATUS
        ok = dev->ControlTransfer(0xA1, 3, 0, 0, st0, sizeof(st0), 2000, &r);
    }
    if (ok && r == 6 && st0[4] != 2) {
        dev->ControlTransfer(0x21, 6, 0, 0, nullptr, 0, 2000); // DFU_ABORT
    }
    dev->ControlTransfer(0x21, 6, 0, 0, nullptr, 0, 2000);
    dev->ControlTransfer(0xA1, 3, 0, 0, st0, sizeof(st0), 2000);

    uint16_t block = 1;
    uint8_t pad[4096]{};
    uint8_t status[6]{};
    for (size_t pos = 0; pos < len; ++block) {
        size_t chunk = len - pos;
        if (chunk > 4096) chunk = 4096;
        const uint8_t *ptr = img + pos;
        if (chunk < 4096) {
            std::memset(pad, 0, sizeof(pad));
            std::memcpy(pad, ptr, chunk);
            ptr = pad;
        }
        uint8_t hs_len[2];
        WriteLE16(hs_len, 4096);
        if (!dev->ControlTransfer(0x21, 1, block, 0, hs_len, 2, 2000)) {
            return false;
        }
        if (!dev->WritePipe(0x03, ptr, 4096, 5000)) {
            return false;
        }
        r = 0;
        if (!dev->ControlTransfer(0xA1, 3, 0, 0, status, sizeof(status), 10000, &r)) {
            return false;
        }
        while (r == 6 && status[4] == 4) { // dfuDNBUSY
            int wait_ms = static_cast<int>(status[1]) | (static_cast<int>(status[2]) << 8) |
                          (static_cast<int>(status[3]) << 16);
            if (wait_ms <= 0) wait_ms = 5;
            SleepMs(wait_ms);
            r = 0;
            if (!dev->ControlTransfer(0xA1, 3, 0, 0, status, sizeof(status), 10000, &r)) {
                return false;
            }
        }
        if (r == 6 && status[4] != 5) { // != dfuDNLOAD-IDLE
            return false;
        }
        pos += chunk;
    }

    dev->ControlTransfer(0x21, 1, block, 0, nullptr, 0, 2000);
    uint8_t zero = 0;
    dev->WritePipe(0x03, &zero, 1, 2000);
    dev->ControlTransfer(0xA1, 3, 0, 0, status, sizeof(status), 2000);
    return true;
}

} // namespace

Client::Client(WinUsbDevice *dev, uint8_t ep_out, uint8_t ep_in, uint32_t timeout_ms)
    : dev_(dev), ep_out_(ep_out), ep_in_(ep_in), timeout_ms_(timeout_ms) {}

bool Client::Command(uint8_t unit, uint32_t command, uint16_t sel,
                     const std::vector<uint8_t> &data, std::vector<uint8_t> *resp_out) {
    if (!dev_ || data.empty()) return false;
    constexpr size_t kChunk = kMaxMessage - kHeaderSize;

    std::lock_guard<std::mutex> lk(mu_);
    uint8_t tag = seq_++;

    for (size_t off = 0; off < data.size(); off += kChunk) {
        size_t end = off + kChunk;
        uint8_t flags = kCmdFlagInterrupt;
        if (end < data.size()) {
            flags |= kCmdFlagContinue;
        } else {
            end = data.size();
        }
        size_t part_len = end - off;
        std::vector<uint8_t> frame(kHeaderSize + part_len);
        frame[0] = unit;
        frame[1] = flags;
        WriteLE16(frame.data() + 2, static_cast<uint16_t>(part_len));
        WriteLE32(frame.data() + 4, command);
        WriteLE16(frame.data() + 8, sel);
        frame[10] = tag;
        std::memcpy(frame.data() + kHeaderSize, data.data() + off, part_len);

        if (!dev_->WritePipe(ep_out_, frame.data(), static_cast<uint32_t>(frame.size()), timeout_ms_)) {
            return false;
        }
    }

    std::vector<uint8_t> resp_data;
    uint8_t buf[kMaxMessage];
    int skips = 0;
    for (int i = 0; i < 64; ++i) {
        uint32_t n = 0;
        if (!dev_->ReadPipe(ep_in_, buf, sizeof(buf), timeout_ms_, &n)) {
            return false;
        }
        Header h{};
        const uint8_t *payload = nullptr;
        size_t payload_len = 0;
        if (!UnmarshalHeader(buf, n, &h, &payload, &payload_len)) {
            return false;
        }
        if ((h.id != unit || h.control_selector != sel) && skips < 8) {
            ++skips;
            continue;
        }
        resp_data.insert(resp_data.end(), payload, payload + payload_len);
        if (h.flags & kRespFlagError) {
            return false;
        }
        if ((h.flags & kRespFlagContinued) == 0) {
            if (resp_out) *resp_out = std::move(resp_data);
            return true;
        }
    }
    return false;
}

bool Client::I2CWrite(uint8_t unit, uint8_t reg, const std::vector<uint8_t> &data) {
    if (!Command(unit, kGetLen, kExuRegisterAccessControl, std::vector<uint8_t>(2, 0))) {
        return false;
    }
    std::vector<uint8_t> buf(256, 0);
    WriteLE32(buf.data(), 1);
    WriteLE32(buf.data() + 4, static_cast<uint32_t>(data.size()));
    buf[8] = reg;
    if (!data.empty() && 9 + data.size() <= buf.size()) {
        std::memcpy(buf.data() + 9, data.data(), data.size());
    }
    return Command(unit, kSetCur, kExuRegisterAccessControl, buf);
}

bool Client::I2CRead(uint8_t unit, uint8_t reg, size_t n, std::vector<uint8_t> *out) {
    std::vector<uint8_t> len_resp;
    if (!Command(unit, kGetLen, kExuRegisterAccessControl, std::vector<uint8_t>(2, 0), &len_resp)) {
        return false;
    }
    size_t size = 256;
    if (len_resp.size() >= 2) {
        uint16_t v = ReadLE16(len_resp.data());
        if (v > 0 && v <= 256) size = v;
    }
    std::vector<uint8_t> buf(size, 0);
    WriteLE32(buf.data(), 1);
    WriteLE32(buf.data() + 4, static_cast<uint32_t>(n));
    buf[8] = reg;
    std::vector<uint8_t> resp;
    if (!Command(unit, kGetCur, kExuRegisterAccessControl, buf, &resp)) {
        return false;
    }
    if (resp.size() < 9 + n) return false;
    if (out) {
        out->assign(resp.begin() + 9, resp.begin() + 9 + n);
    }
    return true;
}

bool Client::SetGPIO(uint8_t unit, uint32_t pin, uint8_t state) {
    std::vector<uint8_t> buf(5, 0);
    WriteLE32(buf.data(), pin);
    buf[4] = state;
    return Command(unit, kSetCur, kExuGPIOControl, buf);
}

bool Client::SetState(uint8_t unit, uint8_t state) {
    return Command(unit, kSetCur, kStateControl, std::vector<uint8_t>{state});
}

bool Client::GetCardStatus(uint32_t *status_out) {
    std::vector<uint8_t> resp;
    if (!Command(kUnitCAS, kSetCur, kCasGetCardStatus, std::vector<uint8_t>(4, 0), &resp)) {
        return false;
    }
    if (resp.size() < 4) return false;
    if (status_out) *status_out = ReadLE32(resp.data());
    return true;
}

void Client::InitInternalBCAS() {
    uint32_t st = 0;
    if (!GetCardStatus(&st)) return;

    std::vector<uint8_t> info(262, 0);
    info[8] = 1;
    info[16] = 8;
    info[17] = 1;
    info[24] = 1;
    Command(kUnitCAS, kSetCur, kCasGetCardInfo, info);
    if (st == 2) return;

    uint32_t seq = 0;
    for (int d = 0; d < 4; ++d) {
        std::vector<uint8_t> ev;
        if (!Command(kUnitCAS, kGetCur, kCasGetEvent, std::vector<uint8_t>(540, 0), &ev) ||
            ev.size() < 8 || ReadLE32(ev.data() + 4) != 0) {
            break;
        }
        ++seq;
        std::vector<uint8_t> comp(12, 0);
        WriteLE32(comp.data() + 4, seq);
        Command(kUnitCAS, kSetCur, kCasCompleteEvent, comp);
    }

    std::vector<uint8_t> req(272, 0);
    WriteLE32(req.data() + 4, 0x30);
    req[8] = 0x30;
    if (!Command(kUnitCAS, kSetCur, kCasSetRequest, req)) return;

    SleepMs(60);
    for (int tries = 0; tries < 20; ++tries) {
        std::vector<uint8_t> ev;
        if (Command(kUnitCAS, kGetCur, kCasGetEvent, std::vector<uint8_t>(540, 0), &ev) &&
            ev.size() >= 8 && ReadLE32(ev.data() + 4) == 0) {
            ++seq;
            std::vector<uint8_t> comp(12, 0);
            WriteLE32(comp.data() + 4, seq);
            Command(kUnitCAS, kSetCur, kCasCompleteEvent, comp);
            Command(kUnitCAS, kGetCur, kCasGetEvent, std::vector<uint8_t>(540, 0));
            break;
        }
        SleepMs(30);
    }
}

void Client::PrepareStreamUnit() {
    constexpr uint8_t kPort = 8;
    SetState(kPort, kStateAcquire);
    SetState(kPort, kStateStop);
    std::vector<uint8_t> probe = {0x00, 0x00, 0x01, 0x01};
    Command(kPort, kSetCur, 1, probe);
    Command(kPort, kSetCur, 2, probe);
    SetState(kPort, kStateAcquire);
    SetState(kPort, kStatePause);
    Command(kUnitCAS, kSetCur, 22, std::vector<uint8_t>(8, 0));
}

bool IsDFUDevice(WinUsbDevice *dev) {
    if (!dev) return false;
    std::string p2 = dev->ReadStringDescAscii(2);
    std::string p3 = dev->ReadStringDescAscii(3);
    std::string ip = dev->ReadStringDescAscii(0x20);
    auto has_dfu = [](const std::string &s) {
        return s.find("DFU") != std::string::npos || s.find("Boot") != std::string::npos;
    };
    return has_dfu(p2) || has_dfu(p3) || ip.rfind("01", 0) == 0;
}

bool BootFirmware(uint16_t vid, uint16_t pid, const std::wstring &fw_path,
                  const std::wstring &target_path, std::wstring *ready_path_out) {
    std::ifstream ifs(std::filesystem::path(fw_path), std::ios::binary | std::ios::ate);
    if (!ifs) return false;
    std::streamsize sz = ifs.tellg();
    if (sz < 0x20000) return false;
    std::vector<uint8_t> fw(static_cast<size_t>(sz));
    ifs.seekg(0);
    if (!ifs.read(reinterpret_cast<char *>(fw.data()), sz)) return false;

    size_t len1 = (static_cast<size_t>(ReadLE32(fw.data() + 0x10)) +
                   static_cast<size_t>(ReadLE32(fw.data() + 0x14)) + 1) << 4;
    size_t off2 = 0x10 + len1;
    if (off2 + 0x20 > fw.size()) return false;
    size_t len2 = (static_cast<size_t>(ReadLE32(fw.data() + off2)) +
                   static_cast<size_t>(ReadLE32(fw.data() + off2 + 4)) + 1) << 4;
    if (off2 + len2 > fw.size()) return false;

    // Only ever touch the selected device (never devs[0]): with several units
    // connected, the others may be in use by another BonDriver instance.
    std::wstring target = target_path;
    std::set<std::wstring> known;
    for (const auto &d : EnumerateWinUsbDevices(vid, pid)) known.insert(LowerPath(d.path));
    known.insert(LowerPath(target));

    auto enum_paths = [&]() {
        std::vector<std::wstring> paths;
        for (const auto &d : EnumerateWinUsbDevices(vid, pid)) paths.push_back(d.path);
        return paths;
    };

    for (int attempt = 0; attempt < 4; ++attempt) {
        if (!TrackDevicePath(enum_paths(), &target, &known)) {
            SleepMs(500);
            continue;
        }
        WinUsbDevice dev;
        if (!dev.Open(target, false)) {
            SleepMs(500);
            continue;
        }
        if (dev.BcdDevice() == 0x0100 && !IsDFUDevice(&dev)) {
            if (ready_path_out) *ready_path_out = target;
            return true;
        }
        std::string iprod = dev.ReadStringDescAscii(0x20);
        std::string prod2 = dev.ReadStringDescAscii(2);
        std::string prod3 = dev.ReadStringDescAscii(3);
        bool is_stage2 = (iprod.rfind("01", 0) == 0) ||
                         (prod2.find("Boot2") != std::string::npos) ||
                         (prod3.find("Boot2") != std::string::npos);
        if (is_stage2) {
            UploadDFUStage(&dev, fw.data() + off2, len2);
            dev.Close();
            SleepMs(4000);
        } else {
            UploadDFUStage(&dev, fw.data() + 0x10, len1);
            dev.Close();
            SleepMs(2000);
        }
    }

    for (int i = 0; i < 10; ++i) {
        if (TrackDevicePath(enum_paths(), &target, &known)) {
            WinUsbDevice dev;
            if (dev.Open(target, false)) {
                bool ready = (dev.BcdDevice() == 0x0100 && !IsDFUDevice(&dev));
                dev.Close();
                if (ready) {
                    if (ready_path_out) *ready_path_out = target;
                    return true;
                }
            }
        }
        SleepMs(500);
    }
    return false;
}

} // namespace saa7164
} // namespace gvmvphz
