#pragma once

#include <Arduino.h>

// Public default settings for Arduino IDE users.
// Edit this tab before uploading if you want a different mesh name, password,
// port, or reading interval. All mesh nodes must use the same mesh settings.
static const char *MESH_PREFIX = "ThermalMesh";
static const char *MESH_PASSWORD = "change-this-password";
static const uint16_t MESH_PORT = 5555;

static const char *NODE_NAME = "BME280";
static const unsigned long SEND_INTERVAL_MS = 2500UL;
static const unsigned long SENSOR_RETRY_INTERVAL_MS = 5000UL;

// Keep these pins aligned with the existing real BME280 circuit.
static const int BME280_SDA_PIN = 21;
static const int BME280_SCL_PIN = 22;

// Calibration used by the old BME280 reference receiver before Kalman update:
// temp_cal = 1.0377 * rawTemperature - 1.4534
static const float BME280_CALIBRATION_SLOPE = 1.0377f;
static const float BME280_CALIBRATION_OFFSET = -1.4534f;
