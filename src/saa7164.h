// SPDX-License-Identifier: MIT
// Copyright (c) 2026 waya0125
#pragma once

#include "winusb_dev.h"

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace gvmvphz {

namespace saa7164 {

constexpr uint32_t kSetCur = 0x01;
constexpr uint32_t kGetCur = 0x81;
constexpr uint32_t kGetLen = 0x85;

constexpr size_t kHeaderSize = 11;
constexpr size_t kMaxMessage = 0x10b;

constexpr uint8_t kCmdFlagInterrupt = 0x02;
constexpr uint8_t kCmdFlagContinue = 0x10;
constexpr uint8_t kRespFlagError = 0x01;
constexpr uint8_t kRespFlagContinued = 0x10;

constexpr uint16_t kExuRegisterAccessControl = 0x00;
constexpr uint16_t kExuGPIOControl = 0x01;
constexpr uint16_t kStateControl = 0x03;

constexpr uint8_t kUnitCAS = 9;
constexpr uint16_t kCasGetCardInfo = 0x1e;
constexpr uint16_t kCasGetCardStatus = 0x1f;
constexpr uint16_t kCasGetEvent = 0x20;
constexpr uint16_t kCasCompleteEvent = 0x21;
constexpr uint16_t kCasSetRequest = 0x22;

constexpr uint8_t kStateStop = 0x00;
constexpr uint8_t kStateAcquire = 0x01;
constexpr uint8_t kStatePause = 0x02;
constexpr uint8_t kStateRun = 0x03;

struct Header {
    uint8_t id = 0;
    uint8_t flags = 0;
    uint16_t size = 0;
    uint32_t command = 0;
    uint16_t control_selector = 0;
    uint8_t seq_no = 0;
};

class Client {
public:
    Client(WinUsbDevice *dev, uint8_t ep_out = 0x01, uint8_t ep_in = 0x81, uint32_t timeout_ms = 2000);

    bool Command(uint8_t unit, uint32_t command, uint16_t sel,
                 const std::vector<uint8_t> &data, std::vector<uint8_t> *resp_out = nullptr);

    bool I2CWrite(uint8_t unit, uint8_t reg, const std::vector<uint8_t> &data);
    bool I2CRead(uint8_t unit, uint8_t reg, size_t n, std::vector<uint8_t> *out);

    bool SetGPIO(uint8_t unit, uint32_t pin, uint8_t state);
    bool SetState(uint8_t unit, uint8_t state);

    bool GetCardStatus(uint32_t *status_out);
    void InitInternalBCAS();
    void PrepareStreamUnit();

private:
    WinUsbDevice *dev_;
    uint8_t ep_out_;
    uint8_t ep_in_;
    uint32_t timeout_ms_;
    std::mutex mu_;
    uint8_t seq_ = 0;
};

bool IsDFUDevice(WinUsbDevice *dev);
bool BootFirmware(uint16_t vid, uint16_t pid, const std::wstring &fw_path);

} // namespace saa7164
} // namespace gvmvphz
