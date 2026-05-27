#include "Config.h"
#include "../libraries/MeshDebug/src/MeshDebug.h"
#include "../libraries/MeshRouting/src/MeshRouting.h"
#include <Adafruit_BME280.h>
#include <Wire.h>
#include <math.h>

#include "../libraries/MeshDebug/src/MeshDebug.cpp"
#include "../libraries/MeshRouting/src/MeshRouting.cpp"

// Real BME280 mesh node.
//
// This sketch keeps the same mesh behavior as ESP_Mesh_BME280DataSim, but the
// temperature comes from the physical BME280 on the existing I2C pins.
MeshDebug meshDebug;
MeshRouting meshRouting;
Adafruit_BME280 bme;

unsigned long lastSendMs = 0;
unsigned long lastSensorRetryMs = 0;
bool bmeReady = false;
bool kalmanReady = false;

class KalmanFilter {
public:
  KalmanFilter(float q, float r, float p, float initialValue)
      : q(q), r(r), p(p), x(initialValue) {}

  void reset(float value) {
    x = value;
    p = 1.0f;
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

KalmanFilter bmeKalman(0.002f, 0.20f, 1.0f, 28.5f);

float roundOne(float value) {
  return roundf(value * 10.0f) / 10.0f;
}

bool initBme280() {
  // Your collaborator wired the BME280 on GPIO 21/22. Keep those pins so the
  // circuit does not need to change.
  Wire.begin(BME280_SDA_PIN, BME280_SCL_PIN);

  if (bme.begin(0x76, &Wire) || bme.begin(0x77, &Wire)) {
    Serial.println("BME280 sensor ready");
    return true;
  }

  Serial.println("BME280 not found on 0x76 or 0x77");
  return false;
}

void publishTemperature() {
  if (!bmeReady) {
    if (millis() - lastSensorRetryMs >= SENSOR_RETRY_INTERVAL_MS) {
      lastSensorRetryMs = millis();
      bmeReady = initBme280();
    }
    return;
  }

  float temperature = bme.readTemperature();
  if (isnan(temperature)) {
    Serial.println("BME280 read failed");
    bmeReady = false;
    return;
  }

  temperature = roundOne(temperature);
  float calibratedTemperature = (BME280_CALIBRATION_SLOPE * temperature) + BME280_CALIBRATION_OFFSET;
  if (!kalmanReady) {
    bmeKalman.reset(calibratedTemperature);
    kalmanReady = true;
  }
  float kalmanTemperature = roundOne(bmeKalman.update(calibratedTemperature));

  // Store only the newest value. MeshRouting sends it with the same DATA
  // packet shape used by the simulated BME280 node, plus optional kalman data.
  meshRouting.addLocalReadingWithKalman(temperature, kalmanTemperature);

  Serial.printf("%s real temperature saved: %.1f C, kalman %.1f C\n",
                NODE_NAME,
                temperature,
                kalmanTemperature);
}

void handleMeshMessage(uint32_t from, const String &msg) {
  // MeshRouting handles GW, LINKS, DATA, and DATA_ACK packets.
  meshRouting.handleMessage(from, msg);
}

void handleMeshConnectionsChanged() {
  // Ask MeshRouting to publish connectivity soon after topology changes.
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

  bmeReady = initBme280();

  meshDebug.setDebug(true);
  meshDebug.onMessage(handleMeshMessage);
  meshDebug.onConnectionsChanged(handleMeshConnectionsChanged);
  meshDebug.begin(MESH_PREFIX, MESH_PASSWORD, MESH_PORT);

  // false means this is a normal sensor node, not the DS18B20 LoRa gateway.
  meshRouting.begin(&meshDebug, NODE_NAME, false);

  Serial.println("BME280 real mesh node ready");
}

void loop() {
  meshDebug.update();
  meshRouting.update();
  handleSerialDebugToggle();

  if (millis() - lastSendMs >= SEND_INTERVAL_MS) {
    lastSendMs = millis();
    publishTemperature();
  }
}
