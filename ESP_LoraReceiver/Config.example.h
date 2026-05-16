#pragma once

#include <Arduino.h>

// Clean template for ESP_LoraReceiver/Config.h.
// Arduino IDE: keep this file as a reference and edit Config.h.
static const int SCREEN_WIDTH = 128;
static const int SCREEN_HEIGHT = 64;
static const int OLED_SDA = 21;
static const int OLED_SCL = 22;
static const int OLED_ADDRESS = 0x3C;

static const int LORA_SCK = 18;
static const int LORA_MISO = 19;
static const int LORA_MOSI = 23;
static const int LORA_SS = 4;
static const int LORA_RST = 5;
static const int LORA_DIO0 = 2;
static const long LORA_FREQUENCY = 433175000L;
static const byte LORA_SYNC_WORD = 0xF3;

static const unsigned long DISPLAY_INTERVAL_MS = 500UL;
static const unsigned long PACKET_TIMEOUT_MS = 10000UL;
static const unsigned long LORA_RETRY_INTERVAL_MS = 5000UL;
