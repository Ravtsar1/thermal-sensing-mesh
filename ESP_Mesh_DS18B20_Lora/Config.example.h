#pragma once

#include <Arduino.h>

// Clean template for ESP_Mesh_DS18B20_Lora/Config.h.
// Arduino IDE: keep this file as a reference and edit Config.h.
static const char *MESH_PREFIX = "ThermalMesh";
static const char *MESH_PASSWORD = "change-this-password";
static const uint16_t MESH_PORT = 5555;

static const char *NODE_NAME = "DS18B20";

static const unsigned long SENSOR_INTERVAL_MS = 2500UL;
static const unsigned long REMOTE_SENSOR_TIMEOUT_MS = 6000UL;
static const unsigned long LORA_LINK_TIMEOUT_MS = 10000UL;
static const unsigned long LORA_ACK_TIMEOUT_MS = 450UL;
static const unsigned long LORA_RETRY_INTERVAL_MS = 5000UL;

static const int LORA_SCK = 18;
static const int LORA_MISO = 19;
static const int LORA_MOSI = 23;
static const int LORA_SS = 4;
static const int LORA_RST = 5;
static const int LORA_DIO0 = 2;
static const long LORA_FREQUENCY = 433175000L;
static const byte LORA_SYNC_WORD = 0xF3;

static const int ONE_WIRE_BUS = 21;

static const int LED_LORA = 25;
static const int LED_DHT11 = 26;
static const int LED_DHT22 = 32;
static const int LED_BME280 = 33;
