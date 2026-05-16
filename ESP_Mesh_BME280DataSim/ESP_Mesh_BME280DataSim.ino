#include "Config.h"
#include "../libraries/MeshDebug/src/MeshDebug.h"
#include "../libraries/MeshRouting/src/MeshRouting.h"
#include <esp_system.h>
#include <math.h>

#include "../libraries/MeshDebug/src/MeshDebug.cpp"
#include "../libraries/MeshRouting/src/MeshRouting.cpp"

// MeshDebug is the transport wrapper. MeshRouting is the store-and-forward
// layer that buffers readings and uploads them when a gateway exists.
MeshDebug meshDebug;
MeshRouting meshRouting;

unsigned long lastSendMs = 0;
float simulatedTemperature = 25.4;

// BME280 is still simulated for now. This small random walk gives a realistic
// looking temperature without requiring the real sensor library or wiring.
float nextTemperature(float current, float minimum, float maximum) {
  current += random(-15, 16) / 100.0f;
  return constrain(current, minimum, maximum);
}

void publishTemperature() {
  simulatedTemperature = nextTemperature(simulatedTemperature, 24.0f, 27.0f);
  simulatedTemperature = roundf(simulatedTemperature * 10.0f) / 10.0f;

  // Do not broadcast temperature directly. Store it first; MeshRouting will
  // send batched history only when the LoRa gateway is reachable.
  meshRouting.addLocalReading(simulatedTemperature);

  Serial.printf("%s simulated temperature saved: %.1f C\n", NODE_NAME, simulatedTemperature);
}

void handleMeshMessage(uint32_t from, const String &msg) {
  // All mesh protocol packets (gateway beacons, links, batches, ACKs) are
  // decoded by MeshRouting so the sketch can stay sensor-focused.
  meshRouting.handleMessage(from, msg);
}

void handleMeshConnectionsChanged() {
  // Send a fresh connectivity report soon after painlessMesh reshapes itself.
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

  // false means this is a normal sensor node, not the LoRa gateway.
  meshRouting.begin(&meshDebug, NODE_NAME, false);

  Serial.println("BME280 simulated mesh node ready");
}

void loop() {
  // meshDebug.update() must run frequently; it drives painlessMesh internals.
  meshDebug.update();
  meshRouting.update();
  handleSerialDebugToggle();

  if (millis() - lastSendMs >= SEND_INTERVAL_MS) {
    lastSendMs = millis();

    // Store a simulated BME280 reading. This does not flood the mesh; it waits
    // in history until MeshRouting sends a batch.
    publishTemperature();
  }
}
