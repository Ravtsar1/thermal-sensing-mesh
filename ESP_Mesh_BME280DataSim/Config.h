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

// Simulated Kalman filter settings. The filter runs on the simulated
// temperature itself, then sends the result as the optional BME280_Kalman
// stream used by the UI.
static const float SIM_KALMAN_PROCESS_NOISE = 0.002f;
static const float SIM_KALMAN_MEASUREMENT_NOISE = 0.20f;
static const float SIM_KALMAN_ESTIMATE_ERROR = 1.0f;
