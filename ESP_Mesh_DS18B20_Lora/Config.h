#pragma once

#include <Arduino.h>

// Public default settings for Arduino IDE users.
// All mesh nodes must use the same MESH_PREFIX, MESH_PASSWORD, and MESH_PORT.
static const char *MESH_PREFIX = "ThermalMesh";
static const char *MESH_PASSWORD = "change-this-password";
static const uint16_t MESH_PORT = 5555;

static const char *NODE_NAME = "DS18B20";

static const unsigned long SENSOR_INTERVAL_MS = 2500UL;
static const unsigned long REMOTE_SENSOR_TIMEOUT_MS = 6000UL;
static const unsigned long LORA_LINK_TIMEOUT_MS = 10000UL;
static const unsigned long LORA_ACK_TIMEOUT_MS = 450UL;
static const unsigned long LORA_RETRY_INTERVAL_MS = 5000UL;

// LoRa wiring:
// ESP32 IO23 -> MOSI, IO19 -> MISO, IO18 -> SCK
// ESP32 IO4  -> NSS,  IO5  -> RST,  IO2  -> DIO0
static const int LORA_SCK = 18;
static const int LORA_MISO = 19;
static const int LORA_MOSI = 23;
static const int LORA_SS = 4;
static const int LORA_RST = 5;
static const int LORA_DIO0 = 2;

// Use a LoRa frequency permitted in your location, and keep the frequency and
// sync word the same on the gateway and receiver.
static const long LORA_FREQUENCY = 433175000L;
static const byte LORA_SYNC_WORD = 0xF3;

static const int ONE_WIRE_BUS = 21;

static const int LED_LORA = 25;
static const int LED_DHT11 = 26;
static const int LED_DHT22 = 32;
static const int LED_BME280 = 33;
