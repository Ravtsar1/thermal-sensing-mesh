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

struct FuzzyStatus {
  float normal;
  float caution;
  float warning;
  float danger;
};

// DHT11 is still simulated. The random walk prevents every packet from having
// the same value while keeping the output inside a realistic range.
float nextTemperature(float current, float minimum, float maximum) {
  current += random(-20, 21) / 100.0f;
  return constrain(current, minimum, maximum);
}

float roundThree(float value) {
  return roundf(value * 1000.0f) / 1000.0f;
}

FuzzyStatus calculateFuzzyStatus(float temperature) {
  FuzzyStatus fuzzy;

  if (temperature <= 20.0f) {
    fuzzy.normal = 1.0f;
  } else if (temperature <= 32.0f) {
    fuzzy.normal = (32.0f - temperature) / 12.0f;
  } else {
    fuzzy.normal = 0.0f;
  }

  if (temperature <= 20.0f || temperature >= 37.0f) {
    fuzzy.caution = 0.0f;
  } else if (temperature <= 32.0f) {
    fuzzy.caution = (temperature - 20.0f) / 12.0f;
  } else {
    fuzzy.caution = (37.0f - temperature) / 5.0f;
  }

  if (temperature <= 32.0f || temperature >= 39.0f) {
    fuzzy.warning = 0.0f;
  } else if (temperature <= 37.0f) {
    fuzzy.warning = (temperature - 32.0f) / 5.0f;
  } else {
    fuzzy.warning = (39.0f - temperature) / 2.0f;
  }

  if (temperature <= 37.0f) {
    fuzzy.danger = 0.0f;
  } else if (temperature <= 39.0f) {
    fuzzy.danger = (temperature - 37.0f) / 2.0f;
  } else {
    fuzzy.danger = 1.0f;
  }

  fuzzy.normal = roundThree(constrain(fuzzy.normal, 0.0f, 1.0f));
  fuzzy.caution = roundThree(constrain(fuzzy.caution, 0.0f, 1.0f));
  fuzzy.warning = roundThree(constrain(fuzzy.warning, 0.0f, 1.0f));
  fuzzy.danger = roundThree(constrain(fuzzy.danger, 0.0f, 1.0f));
  return fuzzy;
}

void publishTemperature() {
  simulatedTemperature = nextTemperature(simulatedTemperature, 24.5f, 28.0f);
  simulatedTemperature = roundf(simulatedTemperature * 10.0f) / 10.0f;
  FuzzyStatus fuzzy = calculateFuzzyStatus(simulatedTemperature);

  // Store only the newest value. Older unsent values are overwritten on the
  // next cycle, so there is no history buffer or timestamp sync. Fuzzy values
  // feed the UI's DHT11_Fuzzy stream.
  meshRouting.addLocalReadingWithFuzzy(simulatedTemperature,
                                       fuzzy.normal,
                                       fuzzy.caution,
                                       fuzzy.warning,
                                       fuzzy.danger);

  Serial.printf("%s simulated temperature saved: %.1f C, fuzzy normal %.3f caution %.3f warning %.3f danger %.3f\n",
                NODE_NAME,
                simulatedTemperature,
                fuzzy.normal,
                fuzzy.caution,
                fuzzy.warning,
                fuzzy.danger);
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
