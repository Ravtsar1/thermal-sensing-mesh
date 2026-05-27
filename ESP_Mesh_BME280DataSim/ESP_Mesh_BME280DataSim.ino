#include "Config.h"
#include "../libraries/MeshDebug/src/MeshDebug.h"
#include "../libraries/MeshRouting/src/MeshRouting.h"
#include <esp_system.h>
#include <math.h>

#include "../libraries/MeshDebug/src/MeshDebug.cpp"
#include "../libraries/MeshRouting/src/MeshRouting.cpp"

// MeshDebug is the transport wrapper. MeshRouting forwards the latest reading
// whenever this node has a route to the gateway.
MeshDebug meshDebug;
MeshRouting meshRouting;

unsigned long lastSendMs = 0;
float simulatedTemperature = 25.4;
bool kalmanReady = false;

class KalmanFilter {
public:
  KalmanFilter(float q, float r, float p, float initialValue)
      : q(q), r(r), p(p), x(initialValue) {}

  void reset(float value) {
    x = value;
    p = SIM_KALMAN_ESTIMATE_ERROR;
  }

  float update(float measurement) {
    p = p + q;
    float gain = p / (p + r);
    x = x + gain * (measurement - x);
    p = (1.0f - gain) * p;
    return x;
  }

private:
  float q;
  float r;
  float p;
  float x;
};

KalmanFilter simulatedKalman(SIM_KALMAN_PROCESS_NOISE,
                             SIM_KALMAN_MEASUREMENT_NOISE,
                             SIM_KALMAN_ESTIMATE_ERROR,
                             simulatedTemperature);

// BME280 is still simulated for now. This small random walk gives a realistic
// looking temperature without requiring the real sensor library or wiring.
float nextTemperature(float current, float minimum, float maximum) {
  current += random(-15, 16) / 100.0f;
  return constrain(current, minimum, maximum);
}

void publishTemperature() {
  simulatedTemperature = nextTemperature(simulatedTemperature, 24.0f, 27.0f);
  simulatedTemperature = roundf(simulatedTemperature * 10.0f) / 10.0f;
  if (!kalmanReady) {
    simulatedKalman.reset(simulatedTemperature);
    kalmanReady = true;
  }
  float kalmanTemperature = roundf(simulatedKalman.update(simulatedTemperature) * 10.0f) / 10.0f;

  // Store only the newest value. Older unsent values are overwritten on the
  // next cycle, so there is no history buffer or timestamp sync. The optional
  // Kalman value feeds the UI's BME280_Kalman stream.
  meshRouting.addLocalReadingWithKalman(simulatedTemperature, kalmanTemperature);

  Serial.printf("%s simulated temperature saved: %.1f C, kalman %.1f C\n",
                NODE_NAME,
                simulatedTemperature,
                kalmanTemperature);
}

void handleMeshMessage(uint32_t from, const String &msg) {
  // All mesh protocol packets (gateway beacons, links, data, ACKs) are
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
    publishTemperature();
  }
}
