#pragma once

#include <Arduino.h>

// Clean template for ESP_Mesh_BME280DataSim/Config.h.
// Arduino IDE: keep this file as a reference and edit Config.h.
static const char *MESH_PREFIX = "ThermalMesh";
static const char *MESH_PASSWORD = "change-this-password";
static const uint16_t MESH_PORT = 5555;

static const char *NODE_NAME = "BME280";
static const unsigned long SEND_INTERVAL_MS = 2500UL;
