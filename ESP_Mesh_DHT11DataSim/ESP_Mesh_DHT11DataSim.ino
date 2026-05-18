#include "Config.h"
#include "../libraries/MeshDebug/src/MeshDebug.h"
#include "../libraries/MeshRouting/src/MeshRouting.h"
#include <esp_system.h>
#include <math.h>

#include "../libraries/MeshDebug/src/MeshDebug.cpp"
#include "../libraries/MeshRouting/src/MeshRouting.cpp"

// One MeshDebug instance owns the painlessMesh connection.
// One MeshRouting instance owns latest-value delivery and gateway discovery.
MeshDebug meshDebug;
MeshRouting meshRouting;

unsigned long lastSendMs = 0;
float simulatedTemperature = 25.8;

// DHT11 is still simulated. The random walk prevents every packet from having
// the same value while keeping the output inside a realistic range.
float nextTemperature(float current, float minimum, float maximum) {
  current += random(-20, 21) / 100.0f;
  return constrain(current, minimum, maximum);
}

void publishTemperature() {
  simulatedTemperature = nextTemperature(simulatedTemperature, 24.5f, 28.0f);
  simulatedTemperature = roundf(simulatedTemperature * 10.0f) / 10.0f;

  // Store only the newest value. Older unsent values are overwritten on the
  // next cycle, so there is no history buffer or timestamp sync.
  meshRouting.addLocalReading(simulatedTemperature);

  Serial.printf("%s simulated temperature saved: %.1f C\n", NODE_NAME, simulatedTemperature);
}

void handleMeshMessage(uint32_t from, const String &msg) {
  // MeshRouting handles gateway beacons, connectivity reports, data packets,
  // and delivery ACK packets.
  meshRouting.handleMessage(from, msg);
}

void handleMeshConnectionsChanged() {
  // Force a topology report soon after the mesh connection list changes.
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

  // false means this node is a sensor-only mesh participant.
  meshRouting.begin(&meshDebug, NODE_NAME, false);

  Serial.println("DHT11 simulated mesh node ready");
}

void loop() {
  // Always service painlessMesh before doing slow application work.
  meshDebug.update();
  meshRouting.update();
  handleSerialDebugToggle();

  if (millis() - lastSendMs >= SEND_INTERVAL_MS) {
    lastSendMs = millis();
    publishTemperature();
  }
}
