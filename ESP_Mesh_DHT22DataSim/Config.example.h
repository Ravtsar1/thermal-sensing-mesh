#pragma once

#include <Arduino.h>

// Clean template for ESP_Mesh_DHT22DataSim/Config.h.
// Arduino IDE: keep this file as a reference and edit Config.h.
static const char *MESH_PREFIX = "ThermalMesh";
static const char *MESH_PASSWORD = "change-this-password";
static const uint16_t MESH_PORT = 5555;

static const char *NODE_NAME = "DHT22";
static const unsigned long SEND_INTERVAL_MS = 2500UL;

static const float SIM_BATTERY_START_PERCENT = 86.0f;
static const float SIM_BATTERY_DRAIN_MIN_PERCENT = 0.0f;
static const float SIM_BATTERY_DRAIN_MAX_PERCENT = 0.2f;

static const unsigned long GATEWAY_ACK_TIMEOUT_MS = 5000UL;
static const uint8_t TEMPERATURE_HISTORY_SIZE = 3;
static const float TEMPERATURE_STABLE_DELTA_C = 0.3f;
static const float TEMPERATURE_MODERATE_DELTA_C = 1.0f;
static const unsigned long SLEEP_FAST_MS = 10000UL;
static const unsigned long SLEEP_MODERATE_MS = 30000UL;
static const unsigned long SLEEP_STABLE_MS = 120000UL;
