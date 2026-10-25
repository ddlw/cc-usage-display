#include <Arduino.h>

#include "hw_imu.h"

#include <Arduino.h>
#include <Wire.h>

namespace imu {
namespace {

constexpr uint8_t kRegWhoAmI = 0x00;
constexpr uint8_t kRegCtrl1 = 0x02;
constexpr uint8_t kRegCtrl2 = 0x03;
constexpr uint8_t kRegCtrl3 = 0x04;
constexpr uint8_t kRegCtrl7 = 0x08;
constexpr uint8_t kRegAxisStart = 0x35;  // AX_L .. GZ_H (12 bytes)

constexpr uint8_t kCandidates[] = {0x6B, 0x6A};

// Accel: +-4 g  -> 8192 counts per g.
constexpr float kAccelCountsPerG = 8192.0f;
// Gyro: +-512 dps -> 64 counts per dps.
constexpr float kGyroCountsPerDps = 64.0f;

uint8_t g_address = 0;
bool g_ready = false;

bool writeRegister(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(g_address);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

bool readRegisters(uint8_t reg, uint8_t* out, size_t length) {
  Wire.beginTransmission(g_address);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) {
    return false;
  }
  if (Wire.requestFrom(static_cast<int>(g_address), static_cast<int>(length)) !=
      static_cast<int>(length)) {
    return false;
  }
  for (size_t i = 0; i < length; ++i) {
    out[i] = static_cast<uint8_t>(Wire.read());
  }
  return true;
}

}  // namespace

bool begin() {
  g_ready = false;
  g_address = 0;
  for (uint8_t candidate : kCandidates) {
    g_address = candidate;
    uint8_t who = 0;
    if (!readRegisters(kRegWhoAmI, &who, 1)) {
      continue;
    }
    if (who != 0x05) {
      continue;
    }
    if (!writeRegister(kRegCtrl1, 0x60)) {  // address auto increment
      continue;
    }
    writeRegister(kRegCtrl2, 0x13);  // +-4 g, 1000 Hz
    writeRegister(kRegCtrl3, 0x43);  // +-512 dps, 1000 Hz
    writeRegister(kRegCtrl7, 0x03);  // enable accel + gyro
    delay(20);
    g_ready = true;
    return true;
  }
  g_address = 0;
  return false;
}

bool ready() { return g_ready; }

uint8_t address() { return g_address; }

bool readRaw(int16_t& ax, int16_t& ay, int16_t& az,
             int16_t& gx, int16_t& gy, int16_t& gz) {
  if (!g_ready) {
    return false;
  }
  uint8_t data[12] = {};
  if (!readRegisters(kRegAxisStart, data, sizeof(data))) {
    return false;
  }
  ax = static_cast<int16_t>(data[0] | (data[1] << 8));
  ay = static_cast<int16_t>(data[2] | (data[3] << 8));
  az = static_cast<int16_t>(data[4] | (data[5] << 8));
  gx = static_cast<int16_t>(data[6] | (data[7] << 8));
  gy = static_cast<int16_t>(data[8] | (data[9] << 8));
  gz = static_cast<int16_t>(data[10] | (data[11] << 8));
  return true;
}

float accelCountsPerG() { return kAccelCountsPerG; }
float gyroCountsPerDps() { return kGyroCountsPerDps; }

}  // namespace imu