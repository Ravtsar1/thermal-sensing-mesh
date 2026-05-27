#include "Config.h"
#include "../libraries/MeshDebug/src/MeshDebug.h"
#include "../libraries/MeshRouting/src/MeshRouting.h"
#include <ArduinoJson.h>
#include <DallasTemperature.h>
#include <LoRa.h>
#include <OneWire.h>
#include <SPI.h>
#include <math.h>
#include <string.h>

#include "../libraries/MeshDebug/src/MeshDebug.cpp"
#include "../libraries/MeshRouting/src/MeshRouting.cpp"

struct DeliveredSensor {
  // Tracks whether the gateway has recently heard from each mesh sensor. This
  // drives the DHT/BME indicator LEDs on the gateway board.
  const char *name;
  unsigned long lastDeliveredMs;
};

MeshDebug meshDebug;
MeshRouting meshRouting;
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature ds18b20(&oneWire);

DeliveredSensor deliveredSensors[] = {
    {"DHT11", 0},
    {"DHT22", 0},
    {"BME280", 0},
};

bool loraReady = false;
unsigned long lastSensorReadMs = 0;
unsigned long lastLoraAckMs = 0;
unsigned long lastLoraRetryMs = 0;
uint32_t loraSequence = 0;

float roundOne(float value) {
  return roundf(value * 10.0f) / 10.0f;
}

bool isLoraReceiverFresh() {
  // LED_LORA tells us whether the OLED/LoRa station has ACKed recently.
  return lastLoraAckMs > 0 && (millis() - lastLoraAckMs <= LORA_LINK_TIMEOUT_MS);
}

bool isDeliveredSensorFresh(const char *name) {
  // These LEDs show recent mesh contact with each sensor. A contact can be a
  // LINKS packet or a DATA packet from that sensor.
  for (uint8_t i = 0; i < sizeof(deliveredSensors) / sizeof(deliveredSensors[0]); i++) {
    if (strcmp(deliveredSensors[i].name, name) == 0) {
      return deliveredSensors[i].lastDeliveredMs > 0 &&
             (millis() - deliveredSensors[i].lastDeliveredMs <= REMOTE_SENSOR_TIMEOUT_MS);
    }
  }
  return false;
}

void markSensorDelivered(const char *name) {
  for (uint8_t i = 0; i < sizeof(deliveredSensors) / sizeof(deliveredSensors[0]); i++) {
    if (strcmp(deliveredSensors[i].name, name) == 0) {
      deliveredSensors[i].lastDeliveredMs = millis();
      return;
    }
  }
}

void updateLeds() {
  digitalWrite(LED_LORA, isLoraReceiverFresh() ? HIGH : LOW);
  digitalWrite(LED_DHT11, isDeliveredSensorFresh("DHT11") ? HIGH : LOW);
  digitalWrite(LED_DHT22, isDeliveredSensorFresh("DHT22") ? HIGH : LOW);
  digitalWrite(LED_BME280, isDeliveredSensorFresh("BME280") ? HIGH : LOW);
}

void initLora() {
  // This ESP32 is still both DS18B20 sensor and physical LoRa gateway because
  // the project has no extra ESP32. Routing is distributed, so it is not a
  // central route coordinator.
  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_SS);
  LoRa.setPins(LORA_SS, LORA_RST, LORA_DIO0);

  if (!LoRa.begin(LORA_FREQUENCY)) {
    loraReady = false;
    Serial.println("LoRa init failed; check wiring/frequency");
    return;
  }

  LoRa.setSyncWord(LORA_SYNC_WORD);
  LoRa.setTxPower(17);
  LoRa.receive();

  loraReady = true;
  Serial.println("LoRa gateway ready");
}

void retryLoraIfNeeded() {
  if (loraReady || millis() - lastLoraRetryMs < LORA_RETRY_INTERVAL_MS) {
    return;
  }

  lastLoraRetryMs = millis();
  initLora();
}

bool isAckPacket(const String &packet, uint32_t expectedSequence, bool requireSequenceMatch) {
  // LoRa ACK format from the receiver is {"t":"ACK","s":sequence}.
  // A sequence match is required when we just transmitted a packet.
  StaticJsonDocument<96> doc;
  DeserializationError error = deserializeJson(doc, packet);
  if (error) {
    return false;
  }

  const char *type = doc["t"] | "";
  if (strcmp(type, "ACK") != 0) {
    return false;
  }

  uint32_t sequence = doc["s"] | 0;
  if (requireSequenceMatch && sequence != expectedSequence) {
    return false;
  }

  lastLoraAckMs = millis();
  Serial.printf("LoRa ACK received for packet %lu\n", (unsigned long)sequence);
  return true;
}

bool readLoraAck(uint32_t expectedSequence, bool requireSequenceMatch) {
  if (!loraReady) {
    return false;
  }

  int packetSize = LoRa.parsePacket();
  if (packetSize <= 0) {
    return false;
  }

  String packet;
  while (LoRa.available()) {
    packet += (char)LoRa.read();
  }

  return isAckPacket(packet, expectedSequence, requireSequenceMatch);
}

bool waitForLoraAck(uint32_t expectedSequence) {
  // Keep servicing mesh while waiting for the LoRa ACK so the gateway does not
  // become temporarily deaf to WiFi mesh packets.
  unsigned long startedMs = millis();
  LoRa.receive();

  while (millis() - startedMs < LORA_ACK_TIMEOUT_MS) {
    meshDebug.update();
    if (readLoraAck(expectedSequence, true)) {
      return true;
    }
    delay(5);
  }

  return false;
}

bool sendDataOverLora(const String &meshPacket) {
  // Called by MeshRouting only on the gateway node. It converts one mesh DATA
  // packet into one LoRa TEMP packet and waits briefly for receiver ACK.
  if (!loraReady) {
    return false;
  }

  StaticJsonDocument<640> sourceDoc;
  DeserializationError error = deserializeJson(sourceDoc, meshPacket);
  if (error) {
    Serial.println("Gateway packet ignored: invalid DATA JSON");
    return false;
  }

  const char *type = sourceDoc["t"] | "";
  if (strcmp(type, "DATA") != 0) {
    return false;
  }

  uint32_t sequence = ++loraSequence;
  const char *sensorName = sourceDoc["name"] | "unknown";

  StaticJsonDocument<768> loraDoc;
  // Keep LoRa packet fields short because LoRa airtime is expensive.
  loraDoc["t"] = "TEMP";
  loraDoc["s"] = sequence;
  loraDoc["src"] = sourceDoc["src"].as<uint32_t>();
  loraDoc["name"] = sensorName;
  loraDoc["seq"] = sourceDoc["seq"].as<uint32_t>();
  loraDoc["temp"] = sourceDoc["temp"].as<float>();
  if (!sourceDoc["kalman"].isNull()) {
    loraDoc["kalman"] = sourceDoc["kalman"].as<float>();
  }
  if (!sourceDoc["battery"].isNull()) {
    loraDoc["battery"] = sourceDoc["battery"].as<float>();
  }
  if (!sourceDoc["fuzzy"].isNull()) {
    JsonArray sourceFuzzy = sourceDoc["fuzzy"].as<JsonArray>();
    JsonArray fuzzy = loraDoc.createNestedArray("fuzzy");
    for (JsonVariant value : sourceFuzzy) {
      fuzzy.add(value.as<float>());
    }
  }

  // UI connectivity order:
  // [BME280-DHT11, BME280-DHT22, BME280-DS18B20,
  //  DHT11-DHT22, DHT11-DS18B20, DHT22-DS18B20,
  //  DS18B20-LoRaReceiver].
  bool meshConnectivity[6];
  meshRouting.getProjectConnectivity(meshConnectivity);
  JsonArray connectivity = loraDoc.createNestedArray("conn");
  for (uint8_t i = 0; i < 6; i++) {
    connectivity.add(meshConnectivity[i] ? 1 : 0);
  }
  connectivity.add(isLoraReceiverFresh() ? 1 : 0);

  String packet;
  serializeJson(loraDoc, packet);

  LoRa.idle();
  LoRa.beginPacket();
  LoRa.print(packet);
  LoRa.endPacket();

  Serial.println("LoRa out: " + packet);

  bool delivered = waitForLoraAck(sequence);
  if (!delivered) {
    Serial.printf("LoRa ACK timeout for packet %lu\n", (unsigned long)sequence);
  } else {
    markSensorDelivered(sensorName);
  }

  LoRa.receive();
  return delivered;
}

void readDs18b20() {
  ds18b20.requestTemperatures();
  float temperature = ds18b20.getTempCByIndex(0);

  if (temperature == DEVICE_DISCONNECTED_C) {
    Serial.println("DS18B20 read failed");
    return;
  }

  temperature = roundOne(temperature);

  // Gateway-local DS18B20 readings use the same latest-value pipeline as the
  // remote simulated sensors.
  meshRouting.addLocalReading(temperature);
  Serial.printf("DS18B20 local temperature saved: %.1f C\n", temperature);
}

void handleMeshMessage(uint32_t from, const String &msg) {
  // Receives DATA packets from other mesh nodes and gateway/link beacons.
  StaticJsonDocument<768> doc;
  DeserializationError error = deserializeJson(doc, msg);
  if (!error) {
    const char *type = doc["t"] | "";
    const char *name = doc["name"] | "";

    if ((strcmp(type, "LINKS") == 0 || strcmp(type, "DATA") == 0) &&
        name[0] != '\0') {
      markSensorDelivered(name);
      Serial.printf("Mesh contact from %s via node %lu\n", name, (unsigned long)from);
    }
  }

  meshRouting.handleMessage(from, msg);
}

void handleMeshConnectionsChanged() {
  // Trigger a fresh link report and gateway beacon after topology changes.
  meshRouting.markLinksDirty();
}

void handleSerialDebugToggle() {
  // Type 1 or 0 in Serial Monitor to turn verbose MeshDebug output on/off.
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

  pinMode(LED_LORA, OUTPUT);
  pinMode(LED_DHT11, OUTPUT);
  pinMode(LED_DHT22, OUTPUT);
  pinMode(LED_BME280, OUTPUT);
  updateLeds();

  ds18b20.begin();
  // 12-bit DS18B20 resolution gives 0.0625 C raw steps. The mesh still rounds
  // the transmitted value to one decimal place for compact packets.
  ds18b20.setResolution(12);

  meshDebug.setDebug(false);
  // The gateway is the painlessMesh root, so other nodes restructure toward it
  // faster after a relay node disappears.
  meshDebug.setRootMode(true);
  meshDebug.onMessage(handleMeshMessage);
  meshDebug.onConnectionsChanged(handleMeshConnectionsChanged);
  meshDebug.begin(MESH_PREFIX, MESH_PASSWORD, MESH_PORT);

  // true marks this node as the mesh's LoRa gateway. It will broadcast GW
  // beacons so the simulated sensor nodes can discover its node ID.
  meshRouting.begin(&meshDebug, NODE_NAME, true);
  meshRouting.setGatewaySender(sendDataOverLora);

  initLora();

  Serial.println("DS18B20 mesh LoRa gateway ready");
}

void loop() {
  // Keep mesh update first so routing and callbacks stay responsive.
  meshDebug.update();
  meshRouting.update();
  handleSerialDebugToggle();
  retryLoraIfNeeded();
  readLoraAck(0, false);

  if (millis() - lastSensorReadMs >= SENSOR_INTERVAL_MS) {
    lastSensorReadMs = millis();

    // Save the gateway's own DS18B20 reading into the same latest-value
    // pipeline used by the remote nodes.
    readDs18b20();
  }

  updateLeds();
}
