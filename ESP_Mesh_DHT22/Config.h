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

// After each successful reading, keep the ESP32 awake long enough for mesh
// routing, then deep-sleep for an adaptive duration.
static const unsigned long AWAKE_AFTER_READING_MS = 5000UL;
static const uint8_t TEMPERATURE_HISTORY_SIZE = 3;
static const float TEMPERATURE_STABLE_DELTA_C = 0.3f;
static const float TEMPERATURE_MODERATE_DELTA_C = 1.0f;
static const unsigned long SLEEP_FAST_MS = 10000UL;
static const unsigned long SLEEP_MODERATE_MS = 30000UL;
static const unsigned long SLEEP_STABLE_MS = 120000UL;

// Keep this pin aligned with the existing real DHT22 circuit.
static const int DHT_PIN = 4;

// Battery estimate copied from the DHT22 reference sketch.
static const int BATTERY_ADC_PIN = 35;
static const float BATTERY_ADC_REFERENCE_V = 3.3f;
static const float BATTERY_ADC_MAX = 4095.0f;
static const float BATTERY_DIVIDER_MULTIPLIER = 2.0f;
static const float BATTERY_VOLTAGE_OFFSET = 0.4f;
static const float BATTERY_EMPTY_V = 3.0f;
static const float BATTERY_FULL_V = 4.2f;
