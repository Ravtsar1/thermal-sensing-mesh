#pragma once

#include <Arduino.h>

// All mesh nodes must use the same mesh settings.
static const char *MESH_PREFIX = "ThermalMesh";
static const char *MESH_PASSWORD = "change-this-password";
static const uint16_t MESH_PORT = 5555;

static const char *NODE_NAME = "DHT11";

// Keep these intervals non-blocking so painlessMesh can update continuously.
static const unsigned long SENSOR_INTERVAL_MS = 2500UL;
static const unsigned long DISPLAY_INTERVAL_MS = 1000UL;
static const unsigned long SD_LOG_INTERVAL_MS = 5000UL;
static const unsigned long BUZZER_DURATION_MS = 500UL;

// Pins copied from the collaborator's DHT11 reference circuit.
static const int LED_RED = 25;
static const int LED_GREEN = 27;
static const int LED_YELLOW = 26;
static const int BUZZER_PIN = 14;

static const int SD_CS = 5;
static const int SD_MOSI = 23;
static const int SD_MISO = 19;
static const int SD_SCK = 18;

static const int OLED_SDA = 21;
static const int OLED_SCL = 22;
static const int OLED_ADDRESS = 0x3C;
static const int SCREEN_WIDTH = 128;
static const int SCREEN_HEIGHT = 64;

static const int DHT_PIN = 4;
static const int BATTERY_PIN = 34;

// Calibration copied from the reference sketch.
static const float TEMPERATURE_OFFSET_C = -3.0f;
static const float HUMIDITY_OFFSET_PERCENT = -5.0f;

// Battery estimate copied from the reference sketch.
static const float BATTERY_ADC_REFERENCE_V = 3.3f;
static const float BATTERY_ADC_MAX = 4095.0f;
static const float BATTERY_DIVIDER_MULTIPLIER = 2.0f;
static const float BATTERY_EMPTY_V = 3.0f;
static const float BATTERY_FULL_V = 3.3f;
