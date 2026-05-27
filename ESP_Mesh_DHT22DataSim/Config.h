#pragma once

#include <Arduino.h>

// Public default settings for Arduino IDE users.
// Edit this tab before uploading if you want a different mesh name, password,
// port, or reading interval. All mesh nodes must use the same mesh settings.
static const char *MESH_PREFIX = "ThermalMesh";
static const char *MESH_PASSWORD = "change-this-password";
static const uint16_t MESH_PORT = 5555;

static const char *NODE_NAME = "DHT22";
static const unsigned long SEND_INTERVAL_MS = 2500UL;

// Simulated battery starts here and slowly decreases on each reading.
static const float SIM_BATTERY_START_PERCENT = 86.0f;
static const float SIM_BATTERY_DRAIN_MIN_PERCENT = 0.0f;
static const float SIM_BATTERY_DRAIN_MAX_PERCENT = 0.2f;

// Adaptive sleep matches the real DHT22 behavior, but the fastest sleep is
// 10 seconds instead of the older reference sketch's 5 seconds.
static const unsigned long AWAKE_AFTER_READING_MS = 5000UL;
static const uint8_t TEMPERATURE_HISTORY_SIZE = 3;
static const float TEMPERATURE_STABLE_DELTA_C = 0.3f;
static const float TEMPERATURE_MODERATE_DELTA_C = 1.0f;
static const unsigned long SLEEP_FAST_MS = 10000UL;
static const unsigned long SLEEP_MODERATE_MS = 30000UL;
static const unsigned long SLEEP_STABLE_MS = 120000UL;
