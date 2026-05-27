#include "Config.h"
#include "../libraries/MeshDebug/src/MeshDebug.h"
#include "../libraries/MeshRouting/src/MeshRouting.h"
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <DHT.h>
#include <SD.h>
#include <SPI.h>
#include <Wire.h>
#include <math.h>
#include <string.h>

#include "../libraries/MeshDebug/src/MeshDebug.cpp"
#include "../libraries/MeshRouting/src/MeshRouting.cpp"

struct FuzzyStatus {
  float normal;
  float caution;
  float warning;
  float danger;
  const char *label;
};

MeshDebug meshDebug;
MeshRouting meshRouting;
DHT dht(DHT_PIN, DHT11);
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

unsigned long lastSensorReadMs = 0;
unsigned long lastDisplayMs = 0;
unsigned long lastLogMs = 0;
unsigned long buzzerOffAtMs = 0;

bool displayReady = false;
bool sdReady = false;
bool hasReading = false;
bool dhtError = false;

float latestTemperature = 0.0f;
float latestHumidity = 0.0f;
float latestBatteryPercent = 0.0f;
FuzzyStatus latestFuzzy = {0.0f, 0.0f, 0.0f, 0.0f, "UNKNOWN"};

float roundOne(float value) {
  return roundf(value * 10.0f) / 10.0f;
}

float roundThree(float value) {
  return roundf(value * 1000.0f) / 1000.0f;
}

float clampFraction(float value) {
  return constrain(value, 0.0f, 1.0f);
}

float readBatteryVoltage() {
  int adcValue = analogRead(BATTERY_PIN);
  float adcVoltage = (adcValue / BATTERY_ADC_MAX) * BATTERY_ADC_REFERENCE_V;
  return adcVoltage * BATTERY_DIVIDER_MULTIPLIER;
}

float getBatteryPercent(float voltage) {
  if (voltage >= BATTERY_FULL_V) {
    return 100.0f;
  }
  if (voltage <= BATTERY_EMPTY_V) {
    return 0.0f;
  }

  return ((voltage - BATTERY_EMPTY_V) * 100.0f) /
         (BATTERY_FULL_V - BATTERY_EMPTY_V);
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

  fuzzy.normal = roundThree(clampFraction(fuzzy.normal));
  fuzzy.caution = roundThree(clampFraction(fuzzy.caution));
  fuzzy.warning = roundThree(clampFraction(fuzzy.warning));
  fuzzy.danger = roundThree(clampFraction(fuzzy.danger));

  fuzzy.label = "NORMAL";
  float maxValue = fuzzy.normal;
  if (fuzzy.caution > maxValue) {
    maxValue = fuzzy.caution;
    fuzzy.label = "CAUTION";
  }
  if (fuzzy.warning > maxValue) {
    maxValue = fuzzy.warning;
    fuzzy.label = "WARNING";
  }
  if (fuzzy.danger > maxValue) {
    fuzzy.label = "DANGER";
  }

  return fuzzy;
}

void drawBatteryIcon(int x, int y, int percent) {
  int width = 40;
  int height = 15;
  int fill = map(constrain(percent, 0, 100), 0, 100, 0, width - 4);

  display.drawRect(x, y, width, height, SSD1306_WHITE);
  display.fillRect(x + width, y + 4, 3, 7, SSD1306_WHITE);
  display.fillRect(x + 2, y + 2, fill, height - 4, SSD1306_WHITE);
}

void startBuzzer() {
  digitalWrite(BUZZER_PIN, HIGH);
  buzzerOffAtMs = millis() + BUZZER_DURATION_MS;
}

void updateBuzzer() {
  if (buzzerOffAtMs != 0 && millis() >= buzzerOffAtMs) {
    digitalWrite(BUZZER_PIN, LOW);
    buzzerOffAtMs = 0;
  }
}

void updateStatusOutputs() {
  digitalWrite(LED_RED, LOW);
  digitalWrite(LED_GREEN, LOW);
  digitalWrite(LED_YELLOW, LOW);

  if (!hasReading || dhtError) {
    digitalWrite(LED_RED, HIGH);
    return;
  }

  if (strcmp(latestFuzzy.label, "NORMAL") == 0) {
    digitalWrite(LED_GREEN, HIGH);
  } else if (strcmp(latestFuzzy.label, "CAUTION") == 0) {
    digitalWrite(LED_YELLOW, HIGH);
  } else {
    digitalWrite(LED_RED, HIGH);
  }

  if (strcmp(latestFuzzy.label, "DANGER") == 0 || latestBatteryPercent <= 10.0f) {
    startBuzzer();
  }
}

bool readAndPublishSensor() {
  float temperature = dht.readTemperature();
  float humidity = dht.readHumidity();

  if (isnan(temperature) || isnan(humidity)) {
    dhtError = true;
    Serial.println("DHT11 read failed");
    updateStatusOutputs();
    return false;
  }

  dhtError = false;
  latestTemperature = roundOne(temperature + TEMPERATURE_OFFSET_C);
  latestHumidity = constrain(roundOne(humidity + HUMIDITY_OFFSET_PERCENT), 0.0f, 100.0f);
  latestBatteryPercent = getBatteryPercent(readBatteryVoltage());
  latestFuzzy = calculateFuzzyStatus(latestTemperature);
  hasReading = true;

  meshRouting.addLocalReadingWithFuzzy(latestTemperature,
                                       latestFuzzy.normal,
                                       latestFuzzy.caution,
                                       latestFuzzy.warning,
                                       latestFuzzy.danger);

  Serial.printf("DHT11 real: %.1f C, %.1f%% RH, battery %.1f%%, fuzzy normal %.3f caution %.3f warning %.3f danger %.3f (%s)\n",
                latestTemperature,
                latestHumidity,
                latestBatteryPercent,
                latestFuzzy.normal,
                latestFuzzy.caution,
                latestFuzzy.warning,
                latestFuzzy.danger,
                latestFuzzy.label);

  updateStatusOutputs();
  return true;
}

void refreshDisplay() {
  if (!displayReady) {
    return;
  }

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);

  if (dhtError) {
    display.println("DHT ERROR");
  } else if (!hasReading) {
    display.println("Waiting DHT11...");
  } else {
    display.print("Temp : ");
    display.print(latestTemperature, 1);
    display.println(" C");

    display.print("Hum  : ");
    display.print(latestHumidity, 1);
    display.println(" %");

    display.print("Status:");
    display.println(latestFuzzy.label);

    display.print("N:");
    display.print(latestFuzzy.normal * 100.0f, 0);
    display.print(" C:");
    display.print(latestFuzzy.caution * 100.0f, 0);
    display.println("%");

    display.print("W:");
    display.print(latestFuzzy.warning * 100.0f, 0);
    display.print(" D:");
    display.print(latestFuzzy.danger * 100.0f, 0);
    display.println("%");

    drawBatteryIcon(5, 52, (int)latestBatteryPercent);
    display.setCursor(50, 54);
    display.print("Bat:");
    display.print(latestBatteryPercent, 0);
    display.print("%");
  }

  display.display();
}

void logToSdIfNeeded() {
  if (!sdReady || !hasReading || millis() - lastLogMs < SD_LOG_INTERVAL_MS) {
    return;
  }

  lastLogMs = millis();
  File dataFile = SD.open("/log.txt", FILE_APPEND);
  if (!dataFile) {
    Serial.println("SD log skipped: open failed");
    return;
  }

  dataFile.print("Temp:");
  dataFile.print(latestTemperature);
  dataFile.print(" Hum:");
  dataFile.print(latestHumidity);
  dataFile.print(" Status:");
  dataFile.print(latestFuzzy.label);
  dataFile.print(" Normal:");
  dataFile.print(latestFuzzy.normal);
  dataFile.print(" Caution:");
  dataFile.print(latestFuzzy.caution);
  dataFile.print(" Warning:");
  dataFile.print(latestFuzzy.warning);
  dataFile.print(" Danger:");
  dataFile.print(latestFuzzy.danger);
  dataFile.print(" Battery:");
  dataFile.print(latestBatteryPercent);
  dataFile.println("%");
  dataFile.close();
}

void handleMeshMessage(uint32_t from, const String &msg) {
  meshRouting.handleMessage(from, msg);
}

void handleMeshConnectionsChanged() {
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

void initDisplay() {
  Wire.begin(OLED_SDA, OLED_SCL);
  displayReady = display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS);
  if (!displayReady) {
    Serial.println("OLED init failed");
    return;
  }

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("DHT11 mesh node");
  display.println("Starting...");
  display.display();
}

void initSdCard() {
  SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  sdReady = SD.begin(SD_CS);
  Serial.println(sdReady ? "SD card ready" : "SD card not available");
}

void setup() {
  Serial.begin(115200);
  delay(200);

  pinMode(LED_RED, OUTPUT);
  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_YELLOW, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);
  analogReadResolution(12);

  initDisplay();
  dht.begin();
  initSdCard();

  meshDebug.setDebug(true);
  meshDebug.onMessage(handleMeshMessage);
  meshDebug.onConnectionsChanged(handleMeshConnectionsChanged);
  meshDebug.begin(MESH_PREFIX, MESH_PASSWORD, MESH_PORT);

  meshRouting.begin(&meshDebug, NODE_NAME, false);

  Serial.println("DHT11 real mesh node ready");
  refreshDisplay();
}

void loop() {
  meshDebug.update();
  meshRouting.update();
  handleSerialDebugToggle();
  updateBuzzer();

  if (millis() - lastSensorReadMs >= SENSOR_INTERVAL_MS) {
    lastSensorReadMs = millis();
    readAndPublishSensor();
  }

  if (millis() - lastDisplayMs >= DISPLAY_INTERVAL_MS) {
    lastDisplayMs = millis();
    refreshDisplay();
  }

  logToSdIfNeeded();
}
