#include "Config.h"
#include "../libraries/MeshDebug/src/MeshDebug.h"
#include "../libraries/MeshRouting/src/MeshRouting.h"
#include <esp_system.h>
#include <math.h>

#include "../libraries/MeshDebug/src/MeshDebug.cpp"
#include "../libraries/MeshRouting/src/MeshRouting.cpp"

// MeshDebug handles the WiFi mesh transport. MeshRouting handles the project
// protocol: local history, gateway discovery, batching, and ACK deletion.
MeshDebug meshDebug;
MeshRouting meshRouting;

unsigned long lastSendMs = 0;
unsigned long lastTimeWaitLogMs = 0;
float simulatedTemperature = 25.1;

// DHT22 is simulated with a gentle random walk. Replace this function with a
// real sensor read later if you install a physical DHT22.
float nextTemperature(float current, float minimum, float maximum) {
  current += random(-12, 13) / 100.0f;
  return constrain(current, minimum, maximum);
}

void publishTemperature() {
  simulatedTemperature = nextTemperature(simulatedTemperature, 23.8f, 27.2f);
  simulatedTemperature = roundf(simulatedTemperature * 10.0f) / 10.0f;

  // Store locally first; MeshRouting decides when a gateway route exists and
  // sends compact batches instead of flooding every single reading.
  meshRouting.addLocalReading(simulatedTemperature);

  Serial.printf("%s simulated temperature saved: %.1f C\n", NODE_NAME, simulatedTemperature);
}

bool gatewayTimeReadyForTemperature() {
  if (meshRouting.isTimeReadyForReadings()) {
    return true;
  }

  if (millis() - lastTimeWaitLogMs >= SEND_INTERVAL_MS) {
    lastTimeWaitLogMs = millis();
    Serial.println("Temperature waiting: gateway time is not synchronized yet");
  }

  return false;
}

void handleMeshMessage(uint32_t from, const String &msg) {
  // Route protocol handling is centralized in MeshRouting.
  meshRouting.handleMessage(from, msg);
}

void handleMeshConnectionsChanged() {
  // A changed mesh can mean a new path to the gateway, so publish links soon.
  meshRouting.markLinksDirty();
}

void handleSerialDebugToggle() {
  if (!Serial.available()) {
    return;
  }

  String input = Serial.readStringUntil('\n');
  input.trim();

  if (input == "1") {
    meshDebug.setDebug(true);
    Serial.println("Mesh debug on");
  } else if (input == "0") {
    meshDebug.setDebug(false);
    Serial.println("Mesh debug off");
  }
}

void setup() {
  Serial.begin(115200);
  delay(200);

  randomSeed(esp_random());

  meshDebug.setDebug(true);
  meshDebug.onMessage(handleMeshMessage);
  meshDebug.onConnectionsChanged(handleMeshConnectionsChanged);
  meshDebug.begin(MESH_PREFIX, MESH_PASSWORD, MESH_PORT);

  // false means this is not the gateway.
  meshRouting.begin(&meshDebug, NODE_NAME, false);

  Serial.println("DHT22 simulated mesh node ready");
}

void loop() {
  // Keeps mesh routing, sync, and callbacks alive.
  meshDebug.update();
  meshRouting.update();
  handleSerialDebugToggle();

  if (millis() - lastSendMs >= SEND_INTERVAL_MS) {
    lastSendMs = millis();

    if (gatewayTimeReadyForTemperature()) {
      // Add one reading to RAM history. It will be uploaded later if the
      // gateway is reachable through painlessMesh.
      publishTemperature();
    }
  }
}
