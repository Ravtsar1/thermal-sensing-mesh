#include "MeshRouting.h"
#include <math.h>
#include <string.h>

MeshRouting::MeshRouting()
    : meshDebug(nullptr),
      localName("node"),
      isGateway(false),
      gatewaySender(nullptr),
      nextReadingSequence(1),
      lastSentSequence(0),
      lastLinkReportMs(0),
      lastGatewayBeaconMs(0),
      lastDataSendMs(0),
      lastGatewaySeenMs(0),
      lastGatewayWaitLogMs(0),
      lastRouteReconnectMs(0),
      gatewayNodeId(0),
      linksDirty(true) {
  latestReading.sequence = 0;
  latestReading.temperature = 0.0f;
  latestReading.kalmanTemperature = 0.0f;
  latestReading.batteryPercent = 0.0f;
  latestReading.fuzzyNormal = 0.0f;
  latestReading.fuzzyCaution = 0.0f;
  latestReading.fuzzyWarning = 0.0f;
  latestReading.fuzzyDanger = 0.0f;
  latestReading.available = false;
  latestReading.hasKalman = false;
  latestReading.hasBattery = false;
  latestReading.hasFuzzy = false;

  for (uint8_t i = 0; i < MAX_GRAPH_NODES; i++) {
    graph[i].used = false;
  }
}

void MeshRouting::begin(MeshDebug *mesh, const char *nodeName, bool gatewayNode) {
  meshDebug = mesh;
  localName = nodeName;
  isGateway = gatewayNode;
  gatewayNodeId = gatewayNode ? meshDebug->getNodeId() : 0;
  lastGatewaySeenMs = gatewayNode ? millis() : 0;

  linksDirty = true;
  lastLinkReportMs = millis() - LINK_REPORT_INTERVAL_MS + random(0, LINK_REPORT_JITTER_MS);
  updateSelfLinks();
}

void MeshRouting::setGatewaySender(GatewayDataSender sender) {
  gatewaySender = sender;
}

void MeshRouting::getProjectConnectivity(bool connectivity[6]) {
  if (connectivity == nullptr) {
    return;
  }

  updateSelfLinks();

  for (uint8_t i = 0; i < 6; i++) {
    connectivity[i] = false;
  }

  const char *pairs[6][2] = {
      {"BME280", "DHT11"},
      {"BME280", "DHT22"},
      {"BME280", "DS18B20"},
      {"DHT11", "DHT22"},
      {"DHT11", "DS18B20"},
      {"DHT22", "DS18B20"},
  };

  for (uint8_t i = 0; i < 6; i++) {
    uint32_t first = findFreshNodeIdByName(pairs[i][0]);
    uint32_t second = findFreshNodeIdByName(pairs[i][1]);
    connectivity[i] = first != 0 && second != 0 && graphHasFreshEdge(first, second);
  }
}

void MeshRouting::markLinksDirty() {
  linksDirty = true;
  lastLinkReportMs = millis() - LINK_REPORT_INTERVAL_MS;

  if (isGateway) {
    lastGatewayBeaconMs = millis() - GATEWAY_BEACON_INTERVAL_MS;
  } else {
    lastDataSendMs = millis() - SEND_FAIL_RETRY_MS;
  }
}

bool MeshRouting::isTimeReadyForReadings() {
  return true;
}

void MeshRouting::update() {
  if (meshDebug == nullptr) {
    return;
  }

  if (linksDirty || millis() - lastLinkReportMs >= LINK_REPORT_INTERVAL_MS) {
    publishLinks();
  }

  if (isGateway && millis() - lastGatewayBeaconMs >= GATEWAY_BEACON_INTERVAL_MS) {
    publishGatewayBeacon();
  }

  trySendLatestReading();
}

void MeshRouting::addLocalReading(float temperature) {
  latestReading.sequence = nextReadingSequence++;
  latestReading.temperature = roundf(temperature * 10.0f) / 10.0f;
  latestReading.hasKalman = false;
  latestReading.hasBattery = false;
  latestReading.hasFuzzy = false;
  latestReading.available = true;

  Serial.printf("Updated %s latest reading #%lu: %.1f C\n",
                localName,
                (unsigned long)latestReading.sequence,
                latestReading.temperature);
}

void MeshRouting::addLocalReadingWithKalman(float temperature, float kalmanTemperature) {
  latestReading.sequence = nextReadingSequence++;
  latestReading.temperature = roundf(temperature * 10.0f) / 10.0f;
  latestReading.kalmanTemperature = roundf(kalmanTemperature * 10.0f) / 10.0f;
  latestReading.hasKalman = true;
  latestReading.hasBattery = false;
  latestReading.hasFuzzy = false;
  latestReading.available = true;

  Serial.printf("Updated %s latest reading #%lu: %.1f C, kalman %.1f C\n",
                localName,
                (unsigned long)latestReading.sequence,
                latestReading.temperature,
                latestReading.kalmanTemperature);
}

void MeshRouting::addLocalReadingWithBattery(float temperature, float batteryPercent) {
  latestReading.sequence = nextReadingSequence++;
  latestReading.temperature = roundf(temperature * 10.0f) / 10.0f;
  latestReading.batteryPercent = constrain(roundf(batteryPercent * 10.0f) / 10.0f, 0.0f, 100.0f);
  latestReading.hasKalman = false;
  latestReading.hasBattery = true;
  latestReading.hasFuzzy = false;
  latestReading.available = true;

  Serial.printf("Updated %s latest reading #%lu: %.1f C, battery %.1f%%\n",
                localName,
                (unsigned long)latestReading.sequence,
                latestReading.temperature,
                latestReading.batteryPercent);
}

void MeshRouting::addLocalReadingWithFuzzy(float temperature,
                                           float normal,
                                           float caution,
                                           float warning,
                                           float danger) {
  latestReading.sequence = nextReadingSequence++;
  latestReading.temperature = roundf(temperature * 10.0f) / 10.0f;
  latestReading.fuzzyNormal = constrain(roundf(normal * 1000.0f) / 1000.0f, 0.0f, 1.0f);
  latestReading.fuzzyCaution = constrain(roundf(caution * 1000.0f) / 1000.0f, 0.0f, 1.0f);
  latestReading.fuzzyWarning = constrain(roundf(warning * 1000.0f) / 1000.0f, 0.0f, 1.0f);
  latestReading.fuzzyDanger = constrain(roundf(danger * 1000.0f) / 1000.0f, 0.0f, 1.0f);
  latestReading.hasKalman = false;
  latestReading.hasBattery = false;
  latestReading.hasFuzzy = true;
  latestReading.available = true;

  Serial.printf("Updated %s latest reading #%lu: %.1f C, fuzzy normal %.2f caution %.2f warning %.2f danger %.2f\n",
                localName,
                (unsigned long)latestReading.sequence,
                latestReading.temperature,
                latestReading.fuzzyNormal,
                latestReading.fuzzyCaution,
                latestReading.fuzzyWarning,
                latestReading.fuzzyDanger);
}

uint32_t MeshRouting::currentReadingSequence() const {
  return latestReading.sequence;
}

bool MeshRouting::latestReadingAcked() const {
  return latestReading.available &&
         latestReading.sequence != 0 &&
         latestReading.sequence == lastSentSequence;
}

void MeshRouting::handleMessage(uint32_t from, const String &msg) {
  if (msg.indexOf("\"t\":\"GW\"") >= 0) {
    StaticJsonDocument<256> gwDoc;
    DeserializationError gwError = deserializeJson(gwDoc, msg);
    if (gwError) {
      Serial.println("Routing ignored GW: invalid JSON");
      return;
    }

    Serial.printf("Routing in <- from %lu: GW\n", (unsigned long)from);
    handleGatewayBeacon(gwDoc);
    return;
  }

  StaticJsonDocument<1024> doc;
  DeserializationError error = deserializeJson(doc, msg);
  if (error) {
    Serial.println("Routing ignored packet: invalid JSON or document too small");
    return;
  }

  const char *type = doc["t"] | "";
  Serial.printf("Routing in <- from %lu: %s\n", (unsigned long)from, type[0] == '\0' ? "unknown" : type);

  if (strcmp(type, "LINKS") == 0) {
    handleLinks(doc);
  } else if (strcmp(type, "DATA") == 0) {
    handleData(doc);
  } else if (strcmp(type, "DATA_ACK") == 0) {
    handleDataAck(doc);
  }
}

void MeshRouting::publishLinks() {
  updateSelfLinks();

  uint32_t neighbors[MAX_NEIGHBORS];
  uint8_t neighborCount = meshDebug->getDirectNeighbors(neighbors, MAX_NEIGHBORS);
  uint32_t knownNodes[MAX_GRAPH_NODES];
  uint8_t knownCount = meshDebug->getKnownNodes(knownNodes, MAX_GRAPH_NODES);

  StaticJsonDocument<1536> doc;
  doc["t"] = "LINKS";
  doc["node"] = meshDebug->getNodeId();
  doc["name"] = localName;
  doc["gw"] = isGateway ? 1 : 0;
  doc["time"] = millis();
  doc["layout"] = meshDebug->getConnectionJson();

  JsonArray neighborArray = doc.createNestedArray("n");
  for (uint8_t i = 0; i < neighborCount; i++) {
    neighborArray.add(neighbors[i]);
  }

  JsonArray knownArray = doc.createNestedArray("known");
  for (uint8_t i = 0; i < knownCount; i++) {
    knownArray.add(knownNodes[i]);
  }

  String packet;
  serializeJson(doc, packet);
  meshDebug->broadcastJson(packet);

  lastLinkReportMs = millis();
  linksDirty = false;

  Serial.printf("Links out: %s\n", packet.c_str());
}

void MeshRouting::publishGatewayBeacon() {
  if (meshDebug == nullptr || !isGateway) {
    return;
  }

  StaticJsonDocument<160> doc;
  doc["t"] = "GW";
  doc["node"] = meshDebug->getNodeId();
  doc["name"] = localName;
  doc["time"] = millis();

  String packet;
  serializeJson(doc, packet);
  meshDebug->broadcastJson(packet);

  gatewayNodeId = meshDebug->getNodeId();
  lastGatewaySeenMs = millis();
  lastGatewayBeaconMs = millis();

  Serial.println("Gateway beacon out: " + packet);
}

void MeshRouting::updateSelfLinks() {
  if (meshDebug == nullptr) {
    return;
  }

  uint32_t neighbors[MAX_NEIGHBORS];
  uint8_t neighborCount = meshDebug->getDirectNeighbors(neighbors, MAX_NEIGHBORS);

  StaticJsonDocument<256> doc;
  JsonArray neighborArray = doc.createNestedArray("n");
  for (uint8_t i = 0; i < neighborCount; i++) {
    neighborArray.add(neighbors[i]);
  }

  updateGraphNode(meshDebug->getNodeId(), localName, isGateway, neighborArray);
}

void MeshRouting::handleLinks(JsonDocument &doc) {
  uint32_t nodeId = doc["node"].as<uint32_t>();
  if (nodeId == 0) {
    return;
  }

  const char *name = doc["name"] | "";
  bool gateway = doc["gw"].as<uint8_t>() == 1 || nameLooksLikeGateway(name);
  JsonArray neighbors = doc["n"].as<JsonArray>();
  updateGraphNode(nodeId, name, gateway, neighbors);

  JsonArray knownNodes = doc["known"].as<JsonArray>();
  for (JsonVariant value : knownNodes) {
    uint32_t knownId = value.as<uint32_t>();
    if (knownId != 0) {
      ensureGraphNode(knownId);
    }
  }

  const char *layout = doc["layout"] | "";
  if (layout[0] != '\0') {
    learnLayoutJson(layout);
  }
}

void MeshRouting::updateGraphNode(uint32_t nodeId, const char *name, bool gateway, JsonArray neighbors) {
  int index = ensureGraphNode(nodeId);
  if (index < 0) {
    return;
  }

  GraphNode &node = graph[index];
  node.id = nodeId;
  node.gateway = gateway;
  node.neighborCount = 0;
  node.lastSeenMs = millis();

  if (name != nullptr && name[0] != '\0') {
    strncpy(node.name, name, sizeof(node.name) - 1);
    node.name[sizeof(node.name) - 1] = '\0';
  }

  if (gateway) {
    rememberGateway(nodeId, "LINKS");
  }

  for (JsonVariant value : neighbors) {
    uint32_t neighborId = value.as<uint32_t>();
    addNeighbor(node, neighborId);
  }
}

int MeshRouting::findGraphIndex(uint32_t nodeId) {
  for (uint8_t i = 0; i < MAX_GRAPH_NODES; i++) {
    if (graph[i].used && graph[i].id == nodeId) {
      return i;
    }
  }
  return -1;
}

int MeshRouting::ensureGraphNode(uint32_t nodeId) {
  int existing = findGraphIndex(nodeId);
  if (existing >= 0) {
    return existing;
  }

  for (uint8_t i = 0; i < MAX_GRAPH_NODES; i++) {
    if (!graph[i].used) {
      graph[i].used = true;
      graph[i].id = nodeId;
      graph[i].name[0] = '\0';
      graph[i].gateway = false;
      graph[i].neighborCount = 0;
      graph[i].lastSeenMs = millis();
      return i;
    }
  }

  Serial.println("Connectivity table full; LINKS ignored");
  return -1;
}

bool MeshRouting::graphNodeFresh(const GraphNode &node) {
  if (!node.used) {
    return false;
  }
  if (meshDebug != nullptr && node.id == meshDebug->getNodeId()) {
    return true;
  }
  return millis() - node.lastSeenMs <= LINK_TTL_MS;
}

void MeshRouting::addNeighbor(GraphNode &node, uint32_t neighborId) {
  if (neighborId == 0 || neighborId == node.id ||
      neighborAlreadyListed(node.neighbors, node.neighborCount, neighborId) ||
      node.neighborCount >= MAX_NEIGHBORS) {
    return;
  }

  node.neighbors[node.neighborCount++] = neighborId;
}

void MeshRouting::addGraphEdge(uint32_t first, uint32_t second) {
  if (first == 0 || second == 0 || first == second) {
    return;
  }

  int firstIndex = ensureGraphNode(first);
  int secondIndex = ensureGraphNode(second);
  if (firstIndex < 0 || secondIndex < 0) {
    return;
  }

  addNeighbor(graph[firstIndex], second);
  addNeighbor(graph[secondIndex], first);
  graph[firstIndex].lastSeenMs = millis();
  graph[secondIndex].lastSeenMs = millis();
}

uint32_t MeshRouting::findFreshNodeIdByName(const char *name) {
  if (name == nullptr || name[0] == '\0') {
    return 0;
  }

  for (uint8_t i = 0; i < MAX_GRAPH_NODES; i++) {
    if (graph[i].used && graphNodeFresh(graph[i]) && strcmp(graph[i].name, name) == 0) {
      return graph[i].id;
    }
  }

  return 0;
}

bool MeshRouting::graphHasFreshEdge(uint32_t first, uint32_t second) {
  int firstIndex = findGraphIndex(first);
  int secondIndex = findGraphIndex(second);

  if (firstIndex < 0 || secondIndex < 0) {
    return false;
  }

  if (!graphNodeFresh(graph[firstIndex]) || !graphNodeFresh(graph[secondIndex])) {
    return false;
  }

  return graphListsNeighbor(graph[firstIndex], second) ||
         graphListsNeighbor(graph[secondIndex], first);
}

bool MeshRouting::graphListsNeighbor(const GraphNode &node, uint32_t neighborId) {
  for (uint8_t i = 0; i < node.neighborCount; i++) {
    if (node.neighbors[i] == neighborId) {
      return true;
    }
  }

  return false;
}

void MeshRouting::learnLayoutJson(const char *layout) {
  StaticJsonDocument<1024> layoutDoc;
  DeserializationError error = deserializeJson(layoutDoc, layout);
  if (error) {
    return;
  }

  JsonObject root = layoutDoc.as<JsonObject>();
  learnLayoutNode(root);
}

void MeshRouting::learnLayoutNode(JsonObject node) {
  uint32_t nodeId = node["nodeId"].as<uint32_t>();
  if (nodeId == 0) {
    return;
  }

  ensureGraphNode(nodeId);

  JsonArray subs = node["subs"].as<JsonArray>();
  for (JsonVariant value : subs) {
    JsonObject child = value.as<JsonObject>();
    uint32_t childId = child["nodeId"].as<uint32_t>();
    if (childId != 0) {
      addGraphEdge(nodeId, childId);
      learnLayoutNode(child);
    }
  }
}

uint32_t MeshRouting::findGatewayId() {
  if (isGateway && meshDebug != nullptr) {
    return meshDebug->getNodeId();
  }

  if (gatewayNodeId != 0 && millis() - lastGatewaySeenMs <= GATEWAY_TTL_MS) {
    return gatewayNodeId;
  }

  for (uint8_t i = 0; i < MAX_GRAPH_NODES; i++) {
    if (graph[i].used && graph[i].gateway && graphNodeFresh(graph[i])) {
      return graph[i].id;
    }
  }
  return 0;
}

bool MeshRouting::neighborAlreadyListed(uint32_t *neighbors, uint8_t count, uint32_t nodeId) {
  for (uint8_t i = 0; i < count; i++) {
    if (neighbors[i] == nodeId) {
      return true;
    }
  }
  return false;
}

void MeshRouting::trySendLatestReading() {
  if (!latestReading.available || latestReading.sequence == lastSentSequence || meshDebug == nullptr) {
    return;
  }

  if (isGateway) {
    if (gatewaySender == nullptr) {
      return;
    }
    if (millis() - lastDataSendMs < SEND_FAIL_RETRY_MS) {
      return;
    }

    String packet;
    if (!buildDataPacket(packet)) {
      return;
    }

    lastDataSendMs = millis();
    bool delivered = gatewaySender(packet);
    lastSentSequence = latestReading.sequence;
    if (delivered) {
      Serial.printf("Gateway local reading #%lu delivered\n", (unsigned long)latestReading.sequence);
    } else {
      Serial.printf("Gateway local reading #%lu sent without receiver ACK\n",
                    (unsigned long)latestReading.sequence);
    }
    return;
  }

  if (millis() - lastDataSendMs < SEND_FAIL_RETRY_MS) {
    return;
  }

  uint32_t gateway = findGatewayId();
  if (gateway == 0) {
    if (millis() - lastGatewayWaitLogMs >= SEND_FAIL_RETRY_MS) {
      lastGatewayWaitLogMs = millis();
      Serial.println("Latest reading waiting: no gateway beacon/LINKS seen yet");
    }
    return;
  }

  if (!meshDebug->isConnected(gateway)) {
    if (millis() - lastGatewayWaitLogMs >= SEND_FAIL_RETRY_MS) {
      lastGatewayWaitLogMs = millis();
      Serial.printf("Latest reading waiting: gateway %lu known, but mesh route is not ready\n",
                    (unsigned long)gateway);
    }
    if (millis() - lastRouteReconnectMs >= ROUTE_RECONNECT_INTERVAL_MS) {
      lastRouteReconnectMs = millis();
      if (meshDebug->requestStationReconnect()) {
        linksDirty = true;
        lastLinkReportMs = millis() - LINK_REPORT_INTERVAL_MS;
        Serial.println("Route recovery: reconnecting station to search for gateway path");
      }
    }
    return;
  }

  String packet;
  if (!buildDataPacket(packet)) {
    return;
  }

  lastDataSendMs = millis();
  if (meshDebug->sendJson(gateway, packet)) {
    Serial.printf("Latest reading #%lu sent toward gateway %lu\n",
                  (unsigned long)latestReading.sequence,
                  (unsigned long)gateway);
  } else {
    Serial.printf("Latest reading #%lu send failed toward gateway %lu\n",
                  (unsigned long)latestReading.sequence,
                  (unsigned long)gateway);
  }
}

bool MeshRouting::buildDataPacket(String &packet) {
  if (!latestReading.available || meshDebug == nullptr) {
    return false;
  }

  StaticJsonDocument<640> doc;
  doc["t"] = "DATA";
  doc["src"] = meshDebug->getNodeId();
  doc["name"] = localName;
  doc["seq"] = latestReading.sequence;
  doc["temp"] = latestReading.temperature;
  if (latestReading.hasKalman) {
    doc["kalman"] = latestReading.kalmanTemperature;
  }
  if (latestReading.hasBattery) {
    doc["battery"] = latestReading.batteryPercent;
  }
  if (latestReading.hasFuzzy) {
    JsonArray fuzzy = doc.createNestedArray("fuzzy");
    fuzzy.add(latestReading.fuzzyNormal);
    fuzzy.add(latestReading.fuzzyCaution);
    fuzzy.add(latestReading.fuzzyWarning);
    fuzzy.add(latestReading.fuzzyDanger);
  }

  serializeJson(doc, packet);
  return true;
}

void MeshRouting::handleGatewayBeacon(JsonDocument &doc) {
  uint32_t nodeId = doc["node"].as<uint32_t>();
  if (nodeId == 0) {
    return;
  }

  const char *name = doc["name"] | "gateway";
  int gatewayIndex = ensureGraphNode(nodeId);
  if (gatewayIndex >= 0) {
    GraphNode &gatewayNode = graph[gatewayIndex];
    gatewayNode.gateway = true;
    gatewayNode.lastSeenMs = millis();
    strncpy(gatewayNode.name, name, sizeof(gatewayNode.name) - 1);
    gatewayNode.name[sizeof(gatewayNode.name) - 1] = '\0';
  }

  rememberGateway(nodeId, "GW");
}

void MeshRouting::handleData(JsonDocument &doc) {
  if (!isGateway || gatewaySender == nullptr) {
    return;
  }

  // ACK as soon as the gateway receives the live value. This ACK now confirms
  // mesh delivery to the gateway only; it is not tied to deleting history and
  // it is not blocked by the separate LoRa receiver ACK.
  sendDataAck(doc);

  String packet;
  serializeJson(doc, packet);
  gatewaySender(packet);
}

void MeshRouting::sendDataAck(JsonDocument &dataDoc) {
  uint32_t source = dataDoc["src"].as<uint32_t>();
  if (source == 0 || meshDebug == nullptr || source == meshDebug->getNodeId()) {
    return;
  }

  StaticJsonDocument<192> ack;
  ack["t"] = "DATA_ACK";
  ack["src"] = source;
  ack["seq"] = dataDoc["seq"].as<uint32_t>();

  String packet;
  serializeJson(ack, packet);
  meshDebug->sendJson(source, packet);
}

void MeshRouting::handleDataAck(JsonDocument &doc) {
  uint32_t source = doc["src"].as<uint32_t>();
  uint32_t sequence = doc["seq"].as<uint32_t>();
  if (meshDebug == nullptr || source != meshDebug->getNodeId()) {
    return;
  }

  if (latestReading.available && sequence == latestReading.sequence) {
    lastSentSequence = sequence;
  }

  Serial.printf("Latest reading #%lu ACKed by gateway path\n",
                (unsigned long)sequence);
}

bool MeshRouting::nameLooksLikeGateway(const char *name) {
  if (name == nullptr) {
    return false;
  }

  return strcmp(name, "DS18B20") == 0 || strcmp(name, "LoRaGateway") == 0;
}

void MeshRouting::rememberGateway(uint32_t nodeId, const char *reason) {
  if (nodeId == 0) {
    return;
  }

  bool routeWasStale = gatewayNodeId == 0 || millis() - lastGatewaySeenMs > GATEWAY_TTL_MS;
  bool changed = gatewayNodeId != nodeId;
  gatewayNodeId = nodeId;
  lastGatewaySeenMs = millis();

  if (changed || routeWasStale) {
    lastDataSendMs = millis() - SEND_FAIL_RETRY_MS;
  }

  if (changed) {
    Serial.printf("Gateway discovered from %s: %lu\n", reason, (unsigned long)gatewayNodeId);
  } else {
    Serial.printf("Gateway refreshed from %s: %lu\n", reason, (unsigned long)gatewayNodeId);
  }
}
