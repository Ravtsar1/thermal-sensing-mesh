#include "Config.h"
#include "../libraries/MeshDebug/src/MeshDebug.h"
#include "../libraries/MeshRouting/src/MeshRouting.h"
#include <esp_sleep.h>
#include <esp_system.h>
#include <math.h>

#include "../libraries/MeshDebug/src/MeshDebug.cpp"
#include "../libraries/MeshRouting/src/MeshRouting.cpp"

// MeshDebug handles the WiFi mesh transport. MeshRouting handles the project
// protocol: latest-value delivery, gateway discovery, and delivery ACKs.
MeshDebug meshDebug;
MeshRouting meshRouting;

unsigned long lastSendMs = 0;
unsigned long readingPublishedAtMs = 0;
unsigned long selectedSleepIntervalMs = SLEEP_FAST_MS;
bool readingPublishedThisWake = false;

RTC_DATA_ATTR float simulatedTemperature = 25.1f;
RTC_DATA_ATTR float simulatedBatteryPercent = SIM_BATTERY_START_PERCENT;
RTC_DATA_ATTR float temperatureHistory[TEMPERATURE_HISTORY_SIZE] = {0};
RTC_DATA_ATTR uint8_t temperatureHistoryIndex = 0;
RTC_DATA_ATTR uint8_t temperatureHistoryCount = 0;
RTC_DATA_ATTR float previousTemperatureAverage = 0.0f;
RTC_DATA_ATTR bool hasPreviousTemperatureAverage = false;

// DHT22 is simulated with a gentle random walk. Replace this function with a
// real sensor read later if you install a physical DHT22.
float nextTemperature(float current, float minimum, float maximum) {
  current += random(-12, 13) / 100.0f;
  return constrain(current, minimum, maximum);
}

void sanitizeRtcState() {
  if (temperatureHistoryCount > TEMPERATURE_HISTORY_SIZE) {
    temperatureHistoryCount = 0;
  }
  if (temperatureHistoryIndex >= TEMPERATURE_HISTORY_SIZE) {
    temperatureHistoryIndex = 0;
  }
  if (isnan(simulatedTemperature) || simulatedTemperature < 15.0f || simulatedTemperature > 45.0f) {
    simulatedTemperature = 25.1f;
  }
  if (isnan(simulatedBatteryPercent) || simulatedBatteryPercent < 0.0f ||
      simulatedBatteryPercent > 100.0f) {
    simulatedBatteryPercent = SIM_BATTERY_START_PERCENT;
  }
  if (isnan(previousTemperatureAverage)) {
    previousTemperatureAverage = 0.0f;
    hasPreviousTemperatureAverage = false;
  }
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

float updateSimulatedBattery() {
  float drain = random((int)(SIM_BATTERY_DRAIN_MIN_PERCENT * 100.0f),
                       (int)(SIM_BATTERY_DRAIN_MAX_PERCENT * 100.0f) + 1) /
                100.0f;
  simulatedBatteryPercent = constrain(simulatedBatteryPercent - drain, 0.0f, 100.0f);
  return simulatedBatteryPercent;
}

bool publishTemperature() {
  simulatedTemperature = nextTemperature(simulatedTemperature, 23.8f, 27.2f);
  simulatedTemperature = roundf(simulatedTemperature * 10.0f) / 10.0f;
  float batteryPercent = updateSimulatedBattery();
  selectedSleepIntervalMs = updateAdaptiveSleep(simulatedTemperature);

  // Store only the newest value. Older unsent values are overwritten on the
  // next cycle, so there is no history buffer or timestamp sync. Battery feeds
  // the UI's DHT22_Battery stream.
  meshRouting.addLocalReadingWithBattery(simulatedTemperature, batteryPercent);

  Serial.printf("%s simulated temperature saved: %.1f C, battery %.1f%%\n",
                NODE_NAME,
                simulatedTemperature,
                batteryPercent);
  return true;
}

void enterConfiguredSleep() {
  Serial.printf("%s simulated node sleeping for %lu ms\n", NODE_NAME, selectedSleepIntervalMs);
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

  randomSeed(esp_random());
  sanitizeRtcState();

  meshDebug.setDebug(true);
  meshDebug.onMessage(handleMeshMessage);
  meshDebug.onConnectionsChanged(handleMeshConnectionsChanged);
  meshDebug.begin(MESH_PREFIX, MESH_PASSWORD, MESH_PORT);

  // false means this is not the gateway.
  meshRouting.begin(&meshDebug, NODE_NAME, false);

  Serial.println("DHT22 simulated mesh node ready");
}

void loop() {
  // Keeps mesh routing and callbacks alive.
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
