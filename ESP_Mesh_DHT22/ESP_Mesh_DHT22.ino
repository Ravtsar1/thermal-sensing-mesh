#include "Config.h"
#include "../libraries/MeshDebug/src/MeshDebug.h"
#include "../libraries/MeshRouting/src/MeshRouting.h"
#include <DHT.h>
#include <esp_sleep.h>
#include <math.h>

#include "../libraries/MeshDebug/src/MeshDebug.cpp"
#include "../libraries/MeshRouting/src/MeshRouting.cpp"

// Real DHT22 mesh node.
//
// This sketch keeps the same mesh behavior as ESP_Mesh_DHT22DataSim, but the
// temperature comes from the physical DHT22 on the existing data pin.
MeshDebug meshDebug;
MeshRouting meshRouting;
DHT dht(DHT_PIN, DHT22);

unsigned long lastSendMs = 0;
unsigned long readingPublishedAtMs = 0;
unsigned long selectedSleepIntervalMs = SLEEP_FAST_MS;
bool readingPublishedThisWake = false;

RTC_DATA_ATTR float temperatureHistory[TEMPERATURE_HISTORY_SIZE] = {0};
RTC_DATA_ATTR uint8_t temperatureHistoryIndex = 0;
RTC_DATA_ATTR uint8_t temperatureHistoryCount = 0;
RTC_DATA_ATTR float previousTemperatureAverage = 0.0f;
RTC_DATA_ATTR bool hasPreviousTemperatureAverage = false;

float roundOne(float value) {
  return roundf(value * 10.0f) / 10.0f;
}

float temperatureHistoryAverage() {
  if (temperatureHistoryCount == 0) {
    return 0.0f;
  }

  float sum = 0.0f;
  for (uint8_t i = 0; i < temperatureHistoryCount; i++) {
    sum += temperatureHistory[i];
  }
  return sum / temperatureHistoryCount;
}

unsigned long updateAdaptiveSleep(float temperature) {
  bool historyReady = temperatureHistoryCount >= TEMPERATURE_HISTORY_SIZE &&
                      hasPreviousTemperatureAverage;
  float previousAverage = previousTemperatureAverage;

  temperatureHistory[temperatureHistoryIndex] = temperature;
  temperatureHistoryIndex = (temperatureHistoryIndex + 1) % TEMPERATURE_HISTORY_SIZE;
  if (temperatureHistoryCount < TEMPERATURE_HISTORY_SIZE) {
    temperatureHistoryCount++;
  }

  float currentAverage = temperatureHistoryAverage();
  previousTemperatureAverage = currentAverage;
  hasPreviousTemperatureAverage = true;

  if (!historyReady) {
    Serial.printf("Adaptive sleep warming up with %u/%u readings; sleep %lu ms\n",
                  temperatureHistoryCount,
                  TEMPERATURE_HISTORY_SIZE,
                  SLEEP_FAST_MS);
    return SLEEP_FAST_MS;
  }

  float delta = fabsf(currentAverage - previousAverage);
  unsigned long sleepMs = SLEEP_FAST_MS;
  if (delta < TEMPERATURE_STABLE_DELTA_C) {
    sleepMs = SLEEP_STABLE_MS;
  } else if (delta < TEMPERATURE_MODERATE_DELTA_C) {
    sleepMs = SLEEP_MODERATE_MS;
  }

  Serial.printf("Adaptive sleep: avg %.2f C, delta %.2f C, sleep %lu ms\n",
                currentAverage,
                delta,
                sleepMs);
  return sleepMs;
}

float readBatteryPercent() {
  int adc = analogRead(BATTERY_ADC_PIN);
  float voltage = BATTERY_DIVIDER_MULTIPLIER * adc * (BATTERY_ADC_REFERENCE_V / BATTERY_ADC_MAX);
  float correctedVoltage = voltage + BATTERY_VOLTAGE_OFFSET;

  if (correctedVoltage >= BATTERY_FULL_V) {
    return 100.0f;
  }
  if (correctedVoltage <= BATTERY_EMPTY_V) {
    return 0.0f;
  }

  return ((correctedVoltage - BATTERY_EMPTY_V) * 100.0f) /
         (BATTERY_FULL_V - BATTERY_EMPTY_V);
}

bool publishTemperature() {
  float temperature = dht.readTemperature();
  if (isnan(temperature)) {
    Serial.println("DHT22 read failed");
    return false;
  }

  temperature = roundOne(temperature);
  float batteryPercent = readBatteryPercent();
  selectedSleepIntervalMs = updateAdaptiveSleep(temperature);

  // Store only the newest value. MeshRouting sends it with the same DATA
  // packet shape used by the simulated DHT22 node, plus optional battery data.
  meshRouting.addLocalReadingWithBattery(temperature, batteryPercent);

  Serial.printf("%s real temperature saved: %.1f C, battery %.1f%%\n",
                NODE_NAME,
                temperature,
                batteryPercent);

  return true;
}

void enterConfiguredSleep() {
  Serial.printf("%s sleeping for %lu ms\n", NODE_NAME, selectedSleepIntervalMs);
  Serial.flush();

  esp_sleep_enable_timer_wakeup((uint64_t)selectedSleepIntervalMs * 1000ULL);
  esp_deep_sleep_start();
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

  dht.begin();

  meshDebug.setDebug(true);
  meshDebug.onMessage(handleMeshMessage);
  meshDebug.onConnectionsChanged(handleMeshConnectionsChanged);
  meshDebug.begin(MESH_PREFIX, MESH_PASSWORD, MESH_PORT);

  // false means this is not the DS18B20 LoRa gateway.
  meshRouting.begin(&meshDebug, NODE_NAME, false);

  Serial.println("DHT22 real mesh node ready");
}

void loop() {
  meshDebug.update();
  meshRouting.update();
  handleSerialDebugToggle();

  if (!readingPublishedThisWake && millis() - lastSendMs >= SEND_INTERVAL_MS) {
    lastSendMs = millis();
    if (publishTemperature()) {
      readingPublishedThisWake = true;
      readingPublishedAtMs = millis();
    }
  }

  if (readingPublishedThisWake && millis() - readingPublishedAtMs >= AWAKE_AFTER_READING_MS) {
    enterConfiguredSleep();
  }
}
