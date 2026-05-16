#include "Config.h"
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ArduinoJson.h>
#include <LoRa.h>
#include <SPI.h>
#include <Wire.h>
#include <string.h>

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// The receiver keeps only the latest delivered value for each sensor on the
// OLED, but every LoRa BATCH may contain several historical records.
const char *SENSOR_NAMES[] = {"DS18B20", "DHT11", "DHT22", "BME280"};
float temperatures[4] = {0.0f, 0.0f, 0.0f, 0.0f};
bool sensorOnline[4] = {false, false, false, false};
uint32_t sensorTimes[4] = {0, 0, 0, 0};
uint32_t sensorSequences[4] = {0, 0, 0, 0};

bool displayReady = false;
bool loraReady = false;
unsigned long lastDisplayMs = 0;
unsigned long lastPacketMs = 0;
unsigned long lastLoraRetryMs = 0;
uint32_t lastSequence = 0;

bool packetIsFresh() {
  // If no LoRa packet has arrived recently, the OLED shows stale data as "-".
  return lastPacketMs > 0 && (millis() - lastPacketMs <= PACKET_TIMEOUT_MS);
}

String formatTemperature(uint8_t index) {
  if (!packetIsFresh() || !sensorOnline[index]) {
    return "-";
  }

  return String(temperatures[index], 1);
}

int8_t sensorIndexByName(const char *name) {
  // The gateway sends the sensor name inside each BATCH packet.
  for (uint8_t i = 0; i < 4; i++) {
    if (strcmp(SENSOR_NAMES[i], name) == 0) {
      return i;
    }
  }
  return -1;
}

void updateDisplay() {
  if (!displayReady) {
    return;
  }

  // Show two sensor values per row. formatTemperature() hides values when the
  // latest packet is too old, so the display does not pretend stale data is OK.
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);

  display.setCursor(0, 0);
  display.println("Temperature");

  display.setCursor(0, 20);
  display.print("1: ");
  display.print(formatTemperature(0));
  display.print(", 2: ");
  display.println(formatTemperature(1));

  display.setCursor(0, 36);
  display.print("3: ");
  display.print(formatTemperature(2));
  display.print(", 4: ");
  display.println(formatTemperature(3));

  display.display();
}

void printTemperatureSummary() {
  // Serial output is intentionally more detailed than the OLED: it shows the
  // sensor sequence and mesh timestamp of the latest received record.
  Serial.printf("Packet %lu received\n", (unsigned long)lastSequence);

  for (uint8_t i = 0; i < 4; i++) {
    Serial.printf("%u %s: ", i + 1, SENSOR_NAMES[i]);
    if (packetIsFresh() && sensorOnline[i]) {
      Serial.printf("%.1f C at %lu ms (seq %lu)\n",
                    temperatures[i],
                    (unsigned long)sensorTimes[i],
                    (unsigned long)sensorSequences[i]);
    } else {
      Serial.println("disconnected");
    }
  }
}

void initDisplay() {
  Wire.begin(OLED_SDA, OLED_SCL);

  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) {
    displayReady = false;
    Serial.println("OLED init failed");
    return;
  }

  displayReady = true;
  updateDisplay();
}

void initLora() {
  // The receiver is not part of the WiFi mesh in this version. It only talks
  // with the DS18B20 gateway over LoRa and sends LoRa ACKs back.
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
  Serial.println("LoRa receiver ready");
}

void retryLoraIfNeeded() {
  // If the receiver boots before the LoRa module is ready, keep retrying
  // without blocking the OLED/display loop forever.
  if (loraReady || millis() - lastLoraRetryMs < LORA_RETRY_INTERVAL_MS) {
    return;
  }

  lastLoraRetryMs = millis();
  initLora();
}

void sendAck(uint32_t sequence) {
  // ACKing by LoRa sequence lets the gateway know it is safe to ACK the mesh
  // source node, which then deletes that delivered history.
  if (!loraReady) {
    return;
  }

  StaticJsonDocument<96> doc;
  doc["t"] = "ACK";
  doc["s"] = sequence;

  String packet;
  serializeJson(doc, packet);

  LoRa.idle();
  LoRa.beginPacket();
  LoRa.print(packet);
  LoRa.endPacket();
  LoRa.receive();

  Serial.println("LoRa ACK out: " + packet);
}

void handleTemperaturePacket(const String &packet) {
  // All over-the-air packets are JSON. Invalid JSON is ignored instead of
  // crashing or displaying partial data.
  StaticJsonDocument<1536> doc;
  DeserializationError error = deserializeJson(doc, packet);
  if (error) {
    Serial.println("LoRa packet ignored: invalid JSON");
    return;
  }

  const char *type = doc["t"] | "";
  if (strcmp(type, "BATCH") == 0) {
    // New format: one sensor history batch with timestamped records:
    // records = [[sensorSeq, meshTimeMs, temperatureC], ...]
    const char *sensorName = doc["name"] | "";
    int8_t sensorIndex = sensorIndexByName(sensorName);
    if (sensorIndex < 0) {
      Serial.println("LoRa batch ignored: unknown sensor name");
      return;
    }

    JsonArray records = doc["records"].as<JsonArray>();
    if (records.size() == 0) {
      Serial.println("LoRa batch ignored: empty history");
      return;
    }

    for (JsonVariant value : records) {
      JsonArray row = value.as<JsonArray>();
      if (row.size() < 3) {
        continue;
      }

      // Each row overwrites the displayed value, so after the loop the OLED
      // contains the newest record from this sensor's delivered history.
      sensorSequences[sensorIndex] = row[0] | 0;
      sensorTimes[sensorIndex] = row[1] | 0;
      temperatures[sensorIndex] = row[2] | 0.0f;
      sensorOnline[sensorIndex] = true;

      Serial.printf("%s history seq %lu at %lu ms = %.1f C\n",
                    sensorName,
                    (unsigned long)sensorSequences[sensorIndex],
                    (unsigned long)sensorTimes[sensorIndex],
                    temperatures[sensorIndex]);
    }

    lastSequence = doc["s"] | 0;
    lastPacketMs = millis();

    Serial.println("LoRa in: " + packet);
    printTemperatureSummary();
    updateDisplay();
    sendAck(lastSequence);
    return;
  }

  if (strcmp(type, "TEMP") != 0) {
    return;
  }

  // Backward compatibility for the earlier fixed four-sensor packet format.
  JsonArray values = doc["v"].as<JsonArray>();
  JsonArray status = doc["ok"].as<JsonArray>();

  if (values.size() < 4 || status.size() < 4) {
    Serial.println("LoRa packet ignored: incomplete temperature bundle");
    return;
  }

  lastSequence = doc["s"] | 0;

  for (uint8_t i = 0; i < 4; i++) {
    sensorOnline[i] = (status[i] | 0) == 1;
    temperatures[i] = values[i] | 0.0f;
  }

  lastPacketMs = millis();

  Serial.println("LoRa in: " + packet);
  printTemperatureSummary();
  updateDisplay();
  sendAck(lastSequence);
}

void receiveLoraPacket() {
  if (!loraReady) {
    return;
  }

  // LoRa.parsePacket() is non-blocking; when no packet is available the loop
  // continues immediately and the display can still refresh.
  int packetSize = LoRa.parsePacket();
  if (packetSize <= 0) {
    return;
  }

  String packet;
  while (LoRa.available()) {
    packet += (char)LoRa.read();
  }

  handleTemperaturePacket(packet);
}

void setup() {
  Serial.begin(115200);
  delay(200);

  initDisplay();
  initLora();

  Serial.println("LoRa OLED station ready");
}

void loop() {
  // The receiver is a small state machine: recover LoRa if needed, read at
  // most one waiting packet, then refresh the OLED at a fixed interval.
  retryLoraIfNeeded();
  receiveLoraPacket();

  if (millis() - lastDisplayMs >= DISPLAY_INTERVAL_MS) {
    lastDisplayMs = millis();
    updateDisplay();
  }
}
