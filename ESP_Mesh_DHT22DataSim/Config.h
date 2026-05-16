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
