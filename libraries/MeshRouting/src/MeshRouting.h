#pragma once

#include "../../MeshDebug/src/MeshDebug.h"
#include <Arduino.h>
#include <ArduinoJson.h>

typedef bool (*GatewayDataSender)(const String &packet);

// Distributed routing for live temperature values.
//
// Every mesh node keeps only its newest temperature reading. Normal sensor
// nodes send that latest value to the DS18B20 gateway when painlessMesh knows
// a route. The gateway forwards one value at a time to the LoRa receiver.
class MeshRouting {
public:
  MeshRouting();

  // gatewayNode = true only on the DS18B20 + LoRa sketch.
  void begin(MeshDebug *mesh, const char *nodeName, bool gatewayNode);

  // Call often from loop(); handles beacons, connectivity reports, and latest
  // value forwarding.
  void update();

  // Feed every incoming mesh JSON packet here from MeshDebug::onMessage().
  void handleMessage(uint32_t from, const String &msg);

  // Called when painlessMesh says the connection layout changed.
  void markLinksDirty();

  // Kept so existing sketches compile. The simplified branch does not require
  // time synchronization before taking readings.
  bool isTimeReadyForReadings();

  // Store the newest local reading. Any older unsent reading is overwritten.
  void addLocalReading(float temperature);
  void addLocalReadingWithKalman(float temperature, float kalmanTemperature);
  void addLocalReadingWithBattery(float temperature, float batteryPercent);
  void addLocalReadingWithFuzzy(float temperature,
                                float normal,
                                float waspada,
                                float siaga,
                                float bahaya);

  // Only the gateway sketch sets this; it forwards DATA packets to LoRa.
  void setGatewaySender(GatewayDataSender sender);

  // Fill the six mesh-link booleans used by the UI branch:
  // [BME280-DHT11, BME280-DHT22, BME280-DS18B20,
  //  DHT11-DHT22, DHT11-DS18B20, DHT22-DS18B20].
  // The LoRa receiver link is not part of the WiFi mesh, so the gateway sketch
  // adds that seventh value when it builds the LoRa packet.
  void getProjectConnectivity(bool connectivity[6]);

private:
  static const uint8_t MAX_GRAPH_NODES = 10;
  static const uint8_t MAX_NEIGHBORS = 8;
  static const unsigned long LINK_REPORT_INTERVAL_MS = 1000;
  static const unsigned long LINK_REPORT_JITTER_MS = 150;
  static const unsigned long LINK_TTL_MS = 3500;
  static const unsigned long GATEWAY_BEACON_INTERVAL_MS = 1000;
  static const unsigned long GATEWAY_TTL_MS = 6000;
  static const unsigned long SEND_FAIL_RETRY_MS = 1000;
  static const unsigned long ROUTE_RECONNECT_INTERVAL_MS = 3000;

  struct GraphNode {
    uint32_t id;
    char name[18];
    bool gateway;
    uint32_t neighbors[MAX_NEIGHBORS];
    uint8_t neighborCount;
    unsigned long lastSeenMs;
    bool used;
  };

  struct LatestReading {
    uint32_t sequence;
    float temperature;
    float kalmanTemperature;
    float batteryPercent;
    float fuzzyNormal;
    float fuzzyWaspada;
    float fuzzySiaga;
    float fuzzyBahaya;
    bool available;
    bool hasKalman;
    bool hasBattery;
    bool hasFuzzy;
  };

  MeshDebug *meshDebug;
  const char *localName;
  bool isGateway;
  GatewayDataSender gatewaySender;

  GraphNode graph[MAX_GRAPH_NODES];
  LatestReading latestReading;
  uint32_t nextReadingSequence;
  uint32_t lastSentSequence;
  unsigned long lastLinkReportMs;
  unsigned long lastGatewayBeaconMs;
  unsigned long lastDataSendMs;
  unsigned long lastGatewaySeenMs;
  unsigned long lastGatewayWaitLogMs;
  unsigned long lastRouteReconnectMs;
  uint32_t gatewayNodeId;
  bool linksDirty;

  void publishLinks();
  void publishGatewayBeacon();
  void updateSelfLinks();
  void updateGraphNode(uint32_t nodeId, const char *name, bool gateway, JsonArray neighbors);
  int findGraphIndex(uint32_t nodeId);
  int ensureGraphNode(uint32_t nodeId);
  bool graphNodeFresh(const GraphNode &node);
  void addNeighbor(GraphNode &node, uint32_t neighborId);
  void addGraphEdge(uint32_t first, uint32_t second);
  uint32_t findFreshNodeIdByName(const char *name);
  bool graphHasFreshEdge(uint32_t first, uint32_t second);
  bool graphListsNeighbor(const GraphNode &node, uint32_t neighborId);
  void learnLayoutJson(const char *layout);
  void learnLayoutNode(JsonObject node);
  uint32_t findGatewayId();
  bool neighborAlreadyListed(uint32_t *neighbors, uint8_t count, uint32_t nodeId);

  void trySendLatestReading();
  bool buildDataPacket(String &packet);

  void handleGatewayBeacon(JsonDocument &doc);
  void handleLinks(JsonDocument &doc);
  void handleData(JsonDocument &doc);
  void handleDataAck(JsonDocument &doc);
  void sendDataAck(JsonDocument &dataDoc);
  bool nameLooksLikeGateway(const char *name);
  void rememberGateway(uint32_t nodeId, const char *reason);
};
