#pragma once

#include <Arduino.h>

// Public default settings for Arduino IDE users.
// The LoRa frequency and sync word must match ESP_Mesh_DS18B20_Lora/Config.h.
static const int SCREEN_WIDTH = 128;
static const int SCREEN_HEIGHT = 64;
static const int OLED_SDA = 21;
static const int OLED_SCL = 22;
static const int OLED_ADDRESS = 0x3C;

// LoRa wiring:
// ESP32 IO23 -> MOSI, IO19 -> MISO, IO18 -> SCK
// ESP32 IO4  -> NSS,  IO5  -> RST,  IO2  -> DIO0
static const int LORA_SCK = 18;
static const int LORA_MISO = 19;
static const int LORA_MOSI = 23;
static const int LORA_SS = 4;
static const int LORA_RST = 5;
static const int LORA_DIO0 = 2;

// Use the same LoRa frequency and sync word on the gateway and receiver.
// 433.175 MHz is inside the commonly referenced 433.05-434.79 MHz SRD/LPWAN
// range, but always check your local radio rules before transmitting.
static const long LORA_FREQUENCY = 433175000L;
static const byte LORA_SYNC_WORD = 0xF3;

static const unsigned long DISPLAY_INTERVAL_MS = 500UL;
static const unsigned long PACKET_TIMEOUT_MS = 10000UL;
static const unsigned long LORA_RETRY_INTERVAL_MS = 5000UL;
