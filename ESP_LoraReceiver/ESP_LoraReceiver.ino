#include "Config.h"
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ArduinoJson.h>
#include <LoRa.h>
#include <SPI.h>
#include <Wire.h>
#include <string.h>

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// The receiver keeps only the latest delivered value for each sensor.
const char *SENSOR_NAMES[] = {"DS18B20", "DHT11", "DHT22", "BME280"};
const char *UI_SENSOR_NAMES[] = {"BME280", "DHT11", "DHT22", "DS18B20"};
float temperatures[4] = {0.0f, 0.0f, 0.0f, 0.0f};
bool sensorOnline[4] = {false, false, false, false};
uint32_t sensorSequences[4] = {0, 0, 0, 0};
bool uiConnectivity[7] = {false, false, false, false, false, false, false};
float bme280KalmanTemperature = 0.0f;
float dht22BatteryPercent = 0.0f;
float dht11FuzzyValues[4] = {0.0f, 0.0f, 0.0f, 0.0f};
bool bme280KalmanOnline = false;
bool dht22BatteryOnline = false;
bool dht11FuzzyOnline = false;

bool displayReady = false;
bool loraReady = false;
unsigned long lastDisplayMs = 0;
unsigned long lastPacketMs = 0;
unsigned long lastLoraRetryMs = 0;
uint32_t lastSequence = 0;
unsigned long lastUiDisconnectedPrintMs = 0;

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
  // The gateway sends the sensor name inside each TEMP packet.
  for (uint8_t i = 0; i < 4; i++) {
    if (strcmp(SENSOR_NAMES[i], name) == 0) {
      return i;
    }
  }
  return -1;
}

int8_t uiSensorIndexByName(const char *name) {
  for (uint8_t i = 0; i < 4; i++) {
    if (strcmp(UI_SENSOR_NAMES[i], name) == 0) {
      return i;
    }
  }
  return -1;
}

void clearUiConnectivity() {
  for (uint8_t i = 0; i < 7; i++) {
    uiConnectivity[i] = false;
  }
}

void updateUiConnectivity(JsonArray connectivity) {
  // The first six values come from the gateway's learned mesh graph. The
  // seventh value is true whenever this receiver has just received LoRa data.
  clearUiConnectivity();
  for (uint8_t i = 0; i < 6 && i < connectivity.size(); i++) {
    uiConnectivity[i] = (connectivity[i] | 0) == 1;
  }
  uiConnectivity[6] = true;
}

void printUiConnectivityArray() {
  Serial.print("[");
  for (uint8_t i = 0; i < 7; i++) {
    if (i > 0) {
      Serial.print(",");
    }
    Serial.print(uiConnectivity[i] ? 1 : 0);
  }
  Serial.print("]");
}

void printUiValue(float value, bool includeValue) {
  Serial.print("[");

  if (includeValue) {
    // The first value is a placeholder. UI/scrapper.py now uses PC arrival
    // time, so the ESP32 no longer sends sensor-side timestamps.
    Serial.print("[0,");
    Serial.print(value, 1);
    Serial.print("]");
  }

  Serial.print("]");
}

void printUiFuzzyValue(bool includeValue) {
  Serial.print("[");

  if (includeValue) {
    // The first value is the same placeholder used by the other streams.
    Serial.print("[0,");
    Serial.print(dht11FuzzyValues[0], 3);
    Serial.print(",");
    Serial.print(dht11FuzzyValues[1], 3);
    Serial.print(",");
    Serial.print(dht11FuzzyValues[2], 3);
    Serial.print(",");
    Serial.print(dht11FuzzyValues[3], 3);
    Serial.print("]");
  }

  Serial.print("]");
}

void printUiDataLine(const char *sensorName,
                     float temperature,
                     bool hasValue,
                     float kalmanTemperature,
                     bool hasKalman,
                     float batteryPercent,
                     bool hasBattery,
                     bool hasFuzzy) {
  int8_t activeUiSensor = hasValue ? uiSensorIndexByName(sensorName) : -1;

  Serial.print("data: [");
  printUiConnectivityArray();

  for (uint8_t i = 0; i < 4; i++) {
    Serial.print(",");
    printUiValue(temperature, activeUiSensor == i);
  }

  // Extra live telemetry arrays:
  // index 5 = BME280 Kalman-filtered temperature
  // index 6 = DHT22 battery percentage
  // index 7 = DHT11 fuzzy membership values [normal, caution, warning, danger]
  Serial.print(",");
  printUiValue(kalmanTemperature, hasKalman);
  Serial.print(",");
  printUiValue(batteryPercent, hasBattery);
  Serial.print(",");
  printUiFuzzyValue(hasFuzzy);

  Serial.println("]");
}

void printUiDisconnectedDataLine() {
  clearUiConnectivity();
  printUiDataLine("", 0.0f, false, 0.0f, false, 0.0f, false, false);
}

void printUiDisconnectedLineIfNeeded() {
  if (packetIsFresh()) {
    return;
  }

  if (millis() - lastUiDisconnectedPrintMs < UI_DISCONNECTED_PRINT_INTERVAL_MS) {
    return;
  }

  // While no fresh LoRa value is available, keep emitting a valid empty UI
  // packet so the UI does not have to treat Serial silence as a special case.
  lastUiDisconnectedPrintMs = millis();
  printUiDisconnectedDataLine();
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
  // sensor sequence of the latest received live value.
  Serial.printf("Packet %lu received\n", (unsigned long)lastSequence);

  for (uint8_t i = 0; i < 4; i++) {
    Serial.printf("%u %s: ", i + 1, SENSOR_NAMES[i]);
    if (packetIsFresh() && sensorOnline[i]) {
      Serial.printf("%.1f C (seq %lu)\n",
                    temperatures[i],
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
  // ACKing by LoRa sequence lets the gateway know the receiver link is alive.
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
  if (strcmp(type, "TEMP") == 0) {
    // Simplified format: one live value from one sensor. The PC assigns the
    // chart timestamp when this line arrives over Serial.
    const char *sensorName = doc["name"] | "";
    int8_t sensorIndex = sensorIndexByName(sensorName);
    if (sensorIndex < 0) {
      Serial.println("LoRa TEMP ignored: unknown sensor name");
      return;
    }

    sensorSequences[sensorIndex] = doc["seq"] | 0;
    temperatures[sensorIndex] = doc["temp"] | 0.0f;
    sensorOnline[sensorIndex] = true;
    bool hasKalman = strcmp(sensorName, "BME280") == 0 && !doc["kalman"].isNull();
    bool hasBattery = strcmp(sensorName, "DHT22") == 0 && !doc["battery"].isNull();
    bool hasFuzzy = strcmp(sensorName, "DHT11") == 0 && !doc["fuzzy"].isNull();

    if (hasKalman) {
      bme280KalmanTemperature = doc["kalman"] | 0.0f;
      bme280KalmanOnline = true;
    }
    if (hasBattery) {
      dht22BatteryPercent = doc["battery"] | 0.0f;
      dht22BatteryOnline = true;
    }
    if (hasFuzzy) {
      JsonArray fuzzy = doc["fuzzy"].as<JsonArray>();
      if (fuzzy.size() >= 4) {
        for (uint8_t i = 0; i < 4; i++) {
          dht11FuzzyValues[i] = fuzzy[i] | 0.0f;
        }
        dht11FuzzyOnline = true;
      } else {
        hasFuzzy = false;
      }
    }

    updateUiConnectivity(doc["conn"].as<JsonArray>());
    lastSequence = doc["s"] | 0;
    lastPacketMs = millis();

    Serial.println("LoRa in: " + packet);
    Serial.printf("%s live seq %lu = %.1f C\n",
                  sensorName,
                  (unsigned long)sensorSequences[sensorIndex],
                  temperatures[sensorIndex]);
    if (hasKalman) {
      Serial.printf("BME280 kalman = %.1f C\n", bme280KalmanTemperature);
    }
    if (hasBattery) {
      Serial.printf("DHT22 battery = %.1f%%\n", dht22BatteryPercent);
    }
    if (hasFuzzy) {
      Serial.printf("DHT11 fuzzy N %.3f W %.3f S %.3f B %.3f\n",
                    dht11FuzzyValues[0],
                    dht11FuzzyValues[1],
                    dht11FuzzyValues[2],
                    dht11FuzzyValues[3]);
    }
    printTemperatureSummary();
    printUiDataLine(sensorName,
                    temperatures[sensorIndex],
                    true,
                    bme280KalmanTemperature,
                    hasKalman && bme280KalmanOnline,
                    dht22BatteryPercent,
                    hasBattery && dht22BatteryOnline,
                    hasFuzzy && dht11FuzzyOnline);
    updateDisplay();
    sendAck(lastSequence);
    return;
  }
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
  printUiDisconnectedDataLine();
}

void loop() {
  // The receiver is a small state machine: recover LoRa if needed, read at
  // most one waiting packet, then refresh the OLED at a fixed interval.
  retryLoraIfNeeded();
  receiveLoraPacket();
  printUiDisconnectedLineIfNeeded();

  if (millis() - lastDisplayMs >= DISPLAY_INTERVAL_MS) {
    lastDisplayMs = millis();
    updateDisplay();
  }
}
