#pragma once
#include <stdint.h>

// Minimal QMI8658 6-axis IMU driver on the shared I2C bus (polling, no INT).
namespace imu {

bool begin();
bool ready();
uint8_t address();

// Raw signed counts. Accel: counts / accelCountsPerG() = g.
// Gyro: counts / gyroCountsPerDps() = deg/s.
bool readRaw(int16_t& ax, int16_t& ay, int16_t& az,
             int16_t& gx, int16_t& gy, int16_t& gz);

float accelCountsPerG();
float gyroCountsPerDps();

}  // namespace imu