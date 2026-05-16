#pragma once

#include "../../MeshDebug/src/MeshDebug.h"
#include <Arduino.h>
#include <ArduinoJson.h>

typedef bool (*GatewayBatchSender)(const String &packet);

// Distributed routing and history buffer.
//
// Every mesh node runs this same class:
// - normal sensor nodes store timestamped readings,
// - the gateway node broadcasts "GW" beacons,
// - all nodes learn light connectivity information,
// - sensor nodes send history batches to the gateway when it is reachable,
// - readings are deleted only after a BATCH_ACK returns.
class MeshRouting {
public:
  MeshRouting();

  // gatewayNode = true only on the DS18B20 + LoRa sketch.
  void begin(MeshDebug *mesh, const char *nodeName, bool gatewayNode);

  // Call often from loop(); handles beacons, connectivity reports, retries,
  // and delayed history upload.
  void update();

  // Feed every incoming mesh JSON packet here from MeshDebug::onMessage().
  void handleMessage(uint32_t from, const String &msg);

  // Called when painlessMesh says the connection layout changed.
  void markLinksDirty();

  // Save a new local sensor reading with synchronized mesh time.
  void addLocalReading(float temperature);

  // Only the gateway sketch sets this; it forwards DATA_BATCH packets to LoRa.
  void setGatewaySender(GatewayBatchSender sender);

  // Fill the six mesh-link booleans used by the UI branch:
  // [BME280-DHT11, BME280-DHT22, BME280-DS18B20,
  //  DHT11-DHT22, DHT11-DS18B20, DHT22-DS18B20].
  // The LoRa receiver link is not part of the WiFi mesh, so the gateway sketch
  // adds that seventh value when it builds the LoRa packet.
  void getProjectConnectivity(bool connectivity[6]);

private:
  // These limits are deliberately small because the project has five ESP32s.
  // Increasing them is safe, but each increase uses more RAM on every node.
  static const uint8_t MAX_GRAPH_NODES = 10;
  static const uint8_t MAX_NEIGHBORS = 8;
  static const uint8_t MAX_PATH_NODES = 10;
  static const uint8_t MAX_HISTORY_RECORDS = 120;
  static const uint8_t BATCH_RECORD_LIMIT = 4;
  static const unsigned long LINK_REPORT_INTERVAL_MS = 30000;
  static const unsigned long LINK_REPORT_JITTER_MS = 4000;
  static const unsigned long LINK_TTL_MS = 90000;
  static const unsigned long GATEWAY_BEACON_INTERVAL_MS = 5000;
  static const unsigned long GATEWAY_TTL_MS = 45000;
  static const unsigned long ACK_TIMEOUT_MS = 12000;
  static const unsigned long SEND_RETRY_MS = 5000;

  // One row in the local connectivity matrix / adjacency list.
  struct GraphNode {
    uint32_t id;
    char name[18];
    bool gateway;
    uint32_t neighbors[MAX_NEIGHBORS];
    uint8_t neighborCount;
    unsigned long lastSeenMs;
    bool used;
  };

  // A timestamped reading waiting to be delivered to the gateway.
  struct ReadingRecord {
    uint32_t sequence;
    uint32_t timeMs;
    float temperature;
  };

  MeshDebug *meshDebug;
  const char *localName;
  bool isGateway;
  GatewayBatchSender gatewaySender;

  GraphNode graph[MAX_GRAPH_NODES];
  ReadingRecord history[MAX_HISTORY_RECORDS];
  uint8_t historyCount;
  uint32_t nextReadingSequence;
  uint32_t nextBatchId;

  bool waitingForAck;
  uint32_t pendingBatchId;
  uint32_t pendingToSequence;
  unsigned long pendingSentMs;
  unsigned long lastLinkReportMs;
  unsigned long lastGatewayBeaconMs;
  unsigned long lastHistorySendMs;
  unsigned long lastGatewaySeenMs;
  unsigned long lastGatewayWaitLogMs;
  uint32_t gatewayNodeId;
  bool linksDirty;

  // Connectivity/gateway discovery.
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

  // Optional graph path calculation. Current delivery uses painlessMesh's
  // internal routing to the gateway ID, but BFS is kept for topology debugging
  // and future explicit app-level paths.
  bool findPathToGateway(uint32_t *path, uint8_t &pathLength);
  uint8_t collectBfsNeighbors(uint32_t nodeId, uint32_t *neighbors, uint8_t maxNeighbors);
  bool neighborAlreadyListed(uint32_t *neighbors, uint8_t count, uint32_t nodeId);

  // Local history upload and deletion after ACK.
  void trySendHistory();
  bool buildHistoryPacket(uint32_t *path, uint8_t pathLength, String &packet, uint32_t &batchId, uint32_t &toSequence);
  bool sendGatewayLocalHistory();
  void dropHistoryThrough(uint32_t sequence);

  // Packet handlers.
  void handleGatewayBeacon(JsonDocument &doc);
  void handleLinks(JsonDocument &doc);
  bool nameLooksLikeGateway(const char *name);
  void rememberGateway(uint32_t nodeId, const char *reason);
  void handleDataBatch(JsonDocument &doc);
  void handleBatchAck(JsonDocument &doc);
  void forwardDataBatch(JsonDocument &doc, uint8_t currentHop);
  void sendBatchAck(JsonDocument &dataDoc);
  void forwardBatchAck(JsonDocument &doc, uint8_t currentHop);

  uint8_t readPath(JsonDocument &doc, uint32_t *path);
  void copyPath(JsonArray target, uint32_t *path, uint8_t pathLength);
};
