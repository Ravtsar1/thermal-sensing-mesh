#include "MeshRouting.h"
#include <math.h>
#include <string.h>

MeshRouting::MeshRouting()
    : meshDebug(nullptr),
      localName("node"),
      isGateway(false),
      gatewaySender(nullptr),
      historyCount(0),
      nextReadingSequence(1),
      nextBatchId(1),
      lastReadingTimeMs(0),
      waitingForAck(false),
      pendingBatchId(0),
      pendingToSequence(0),
      pendingSentMs(0),
      lastLinkReportMs(0),
      lastGatewayBeaconMs(0),
      lastHistorySendMs(0),
      lastGatewaySeenMs(0),
      lastGatewayWaitLogMs(0),
      gatewayNodeId(0),
      gatewayTimeOffsetMs(0),
      gatewayTimeReady(false),
      linksDirty(true) {
  for (uint8_t i = 0; i < MAX_GRAPH_NODES; i++) {
    graph[i].used = false;
  }
}

void MeshRouting::begin(MeshDebug *mesh, const char *nodeName, bool gatewayNode) {
  meshDebug = mesh;
  localName = nodeName;
  isGateway = gatewayNode;

  // Gateway nodes already know their own gateway ID. Sensor nodes start with
  // gatewayNodeId = 0 and wait until they hear a GW beacon or gateway LINKS.
  gatewayNodeId = gatewayNode ? meshDebug->getNodeId() : 0;
  lastGatewaySeenMs = gatewayNode ? millis() : 0;
  gatewayTimeReady = gatewayNode;
  gatewayTimeOffsetMs = 0;

  // Force an early LINKS report, but jitter it so multiple ESP32s do not all
  // broadcast at exactly the same millisecond after boot.
  linksDirty = true;
  lastLinkReportMs = millis() - LINK_REPORT_INTERVAL_MS + random(0, LINK_REPORT_JITTER_MS);
  updateSelfLinks();
}

void MeshRouting::setGatewaySender(GatewayBatchSender sender) {
  gatewaySender = sender;
}

void MeshRouting::getProjectConnectivity(bool connectivity[6]) {
  if (connectivity == nullptr) {
    return;
  }

  // The gateway includes connectivity in every LoRa packet. Refresh its own
  // direct neighbor list immediately so the receiver does not see an old
  // cached gateway edge while the gateway LED already reflects the new state.
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
  // Called by the painlessMesh changed-connection callback. The actual LINKS
  // packet is sent from update() so it does not run inside the callback stack.
  linksDirty = true;
}

bool MeshRouting::isTimeReadyForReadings() {
  return isGateway || gatewayTimeReady;
}

void MeshRouting::update() {
  if (meshDebug == nullptr) {
    return;
  }

  // Connectivity must move faster than temperature sampling. Each node reports
  // its current neighbor list often, and topology-change callbacks mark it
  // dirty so a changed link is published on the next loop pass.
  if (linksDirty || millis() - lastLinkReportMs >= LINK_REPORT_INTERVAL_MS) {
    publishLinks();
  }

  // Only the DS18B20 + LoRa gateway sends this. It is the main way sensors
  // learn the large uint32_t node ID they should send history batches to.
  if (isGateway && millis() - lastGatewayBeaconMs >= GATEWAY_BEACON_INTERVAL_MS) {
    publishGatewayBeacon();
  }

  // A source node keeps its history until a BATCH_ACK returns. If that ACK is
  // lost, clear the waiting flag so the same history can be retried.
  if (waitingForAck && millis() - pendingSentMs >= ACK_TIMEOUT_MS) {
    waitingForAck = false;
    Serial.println("Mesh batch ACK timeout; will retry");
  }

  trySendHistory();
}

void MeshRouting::addLocalReading(float temperature) {
  // Ring-buffer behavior: when RAM history is full, keep the newest readings.
  if (historyCount >= MAX_HISTORY_RECORDS) {
    for (uint8_t i = 1; i < historyCount; i++) {
      history[i - 1] = history[i];
    }
    historyCount--;
    Serial.println("History full; oldest reading dropped");
  }

  uint32_t readingTime = monotonicReadingTimeMs();
  ReadingRecord &record = history[historyCount++];
  record.sequence = nextReadingSequence++;
  record.timeMs = readingTime;
  record.temperature = temperature;

  Serial.printf("Saved %s reading #%lu at %lu ms: %.1f C\n",
                localName,
                (unsigned long)record.sequence,
                (unsigned long)record.timeMs,
                temperature);
}

void MeshRouting::handleMessage(uint32_t from, const String &msg) {
  // Handle the tiny gateway beacon before allocating a larger JSON document.
  // This avoids missing GW packets on memory-tight callbacks.
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

  StaticJsonDocument<1536> doc;
  DeserializationError error = deserializeJson(doc, msg);
  if (error) {
    Serial.println("Routing ignored packet: invalid JSON or document too small");
    return;
  }

  const char *type = doc["t"] | "";
  Serial.printf("Routing in <- from %lu: %s\n", (unsigned long)from, type[0] == '\0' ? "unknown" : type);

  if (strcmp(type, "LINKS") == 0) {
    handleLinks(doc);
  } else if (strcmp(type, "DATA_BATCH") == 0) {
    handleDataBatch(doc);
  } else if (strcmp(type, "BATCH_ACK") == 0) {
    handleBatchAck(doc);
  }
}

void MeshRouting::publishLinks() {
  // LINKS is the fast topology report. It does not carry temperature history;
  // it only tells the mesh who this node can currently see.
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
  doc["time"] = authorityTimeMs();

  // layout is a JSON string generated by painlessMesh. It is mainly for
  // diagnostics and optional graph learning; delivery uses sendSingle().
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

  // This tiny packet is the important discovery and time-authority message.
  // Sensors align their local clock offset from this gateway timestamp.
  StaticJsonDocument<192> doc;
  doc["t"] = "GW";
  doc["node"] = meshDebug->getNodeId();
  doc["name"] = localName;
  doc["time"] = authorityTimeMs();

  String packet;
  serializeJson(doc, packet);
  meshDebug->broadcastJson(packet);

  gatewayNodeId = meshDebug->getNodeId();
  lastGatewaySeenMs = millis();
  lastGatewayBeaconMs = millis();

  Serial.println("Gateway beacon out: " + packet);
}

uint32_t MeshRouting::authorityTimeMs() {
  if (isGateway || gatewayTimeReady) {
    int64_t adjusted = (int64_t)millis() + gatewayTimeOffsetMs;
    if (adjusted < 0) {
      return 0;
    }
    if (adjusted > UINT32_MAX) {
      return UINT32_MAX;
    }
    return (uint32_t)adjusted;
  }

  return millis();
}

uint32_t MeshRouting::monotonicReadingTimeMs() {
  uint32_t readingTime = authorityTimeMs();

  if (nextReadingSequence > 1 && readingTime <= lastReadingTimeMs) {
    if (lastReadingTimeMs < UINT32_MAX) {
      readingTime = lastReadingTimeMs + 1;
    } else {
      readingTime = UINT32_MAX;
    }

    Serial.printf("Reading time adjusted to avoid rollback: %lu ms\n",
                  (unsigned long)readingTime);
  }

  lastReadingTimeMs = readingTime;
  return readingTime;
}

void MeshRouting::updateSelfLinks() {
  if (meshDebug == nullptr) {
    return;
  }

  uint32_t neighbors[MAX_NEIGHBORS];
  uint8_t neighborCount = meshDebug->getDirectNeighbors(neighbors, MAX_NEIGHBORS);

  // Reuse updateGraphNode() by building a tiny temporary neighbor array for
  // this node's local view.
  StaticJsonDocument<256> doc;
  JsonArray neighborArray = doc.createNestedArray("n");
  for (uint8_t i = 0; i < neighborCount; i++) {
    neighborArray.add(neighbors[i]);
  }

  updateGraphNode(meshDebug->getNodeId(), localName, isGateway, neighborArray);
}

void MeshRouting::handleLinks(JsonDocument &doc) {
  // Merge another node's local view into this node's adjacency table.
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

  // Add a new graph row if this node ID has not been seen before.
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

  // Store edges both ways. Radio links can be asymmetric in real life, but
  // painlessMesh routes at a higher level and this graph is only advisory.
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
  // Prefer the small, recent GW beacon. This fixed the earlier bug where
  // sensors waited for incomplete topology reports and never sent history.
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

bool MeshRouting::findPathToGateway(uint32_t *path, uint8_t &pathLength) {
  // BFS shortest-path search over the learned adjacency graph. Current sending
  // uses a direct logical path [source, gateway], but this remains useful for
  // debugging and future explicit path instructions.
  pathLength = 0;
  if (meshDebug == nullptr) {
    return false;
  }

  uint32_t start = meshDebug->getNodeId();
  uint32_t gateway = findGatewayId();
  if (gateway == 0) {
    return false;
  }

  if (start == gateway) {
    path[0] = start;
    pathLength = 1;
    return true;
  }

  uint32_t queue[MAX_GRAPH_NODES];
  uint32_t previous[MAX_GRAPH_NODES];
  bool visited[MAX_GRAPH_NODES];
  uint8_t head = 0;
  uint8_t tail = 0;

  for (uint8_t i = 0; i < MAX_GRAPH_NODES; i++) {
    visited[i] = false;
    previous[i] = 0;
  }

  int startIndex = findGraphIndex(start);
  if (startIndex < 0) {
    return false;
  }

  queue[tail++] = start;
  visited[startIndex] = true;

  while (head < tail) {
    uint32_t current = queue[head++];
    if (current == gateway) {
      break;
    }

    uint32_t neighbors[MAX_NEIGHBORS];
    uint8_t neighborCount = collectBfsNeighbors(current, neighbors, MAX_NEIGHBORS);
    for (uint8_t i = 0; i < neighborCount; i++) {
      uint32_t neighbor = neighbors[i];
      int neighborIndex = findGraphIndex(neighbor);
      if (neighborIndex < 0 || visited[neighborIndex] || !graphNodeFresh(graph[neighborIndex])) {
        continue;
      }
      if (tail >= MAX_GRAPH_NODES) {
        break;
      }

      visited[neighborIndex] = true;
      previous[neighborIndex] = current;
      queue[tail++] = neighbor;

      if (neighbor == gateway || tail >= MAX_GRAPH_NODES) {
        break;
      }
    }
  }

  int gatewayIndex = findGraphIndex(gateway);
  if (gatewayIndex < 0 || !visited[gatewayIndex]) {
    if (meshDebug->isConnected(gateway)) {
      path[0] = start;
      path[1] = gateway;
      pathLength = 2;
      Serial.println("Using direct painlessMesh route to gateway");
      return true;
    }
    return false;
  }

  uint32_t reversePath[MAX_PATH_NODES];
  uint8_t reverseLength = 0;
  uint32_t cursor = gateway;

  while (cursor != 0 && reverseLength < MAX_PATH_NODES) {
    reversePath[reverseLength++] = cursor;
    if (cursor == start) {
      break;
    }
    int cursorIndex = findGraphIndex(cursor);
    if (cursorIndex < 0) {
      return false;
    }
    cursor = previous[cursorIndex];
  }

  if (reversePath[reverseLength - 1] != start) {
    return false;
  }

  pathLength = reverseLength;
  for (uint8_t i = 0; i < reverseLength; i++) {
    path[i] = reversePath[reverseLength - 1 - i];
  }

  return true;
}

uint8_t MeshRouting::collectBfsNeighbors(uint32_t nodeId, uint32_t *neighbors, uint8_t maxNeighbors) {
  uint8_t count = 0;
  int index = findGraphIndex(nodeId);

  if (index >= 0 && graphNodeFresh(graph[index])) {
    for (uint8_t i = 0; i < graph[index].neighborCount && count < maxNeighbors; i++) {
      uint32_t neighbor = graph[index].neighbors[i];
      int neighborIndex = findGraphIndex(neighbor);
      if (neighborIndex >= 0 && graphNodeFresh(graph[neighborIndex]) &&
          !neighborAlreadyListed(neighbors, count, neighbor)) {
        neighbors[count++] = neighbor;
      }
    }
  }

  for (uint8_t i = 0; i < MAX_GRAPH_NODES && count < maxNeighbors; i++) {
    if (!graph[i].used || !graphNodeFresh(graph[i])) {
      continue;
    }
    for (uint8_t j = 0; j < graph[i].neighborCount && count < maxNeighbors; j++) {
      if (graph[i].neighbors[j] == nodeId &&
          !neighborAlreadyListed(neighbors, count, graph[i].id)) {
        neighbors[count++] = graph[i].id;
      }
    }
  }

  return count;
}

bool MeshRouting::neighborAlreadyListed(uint32_t *neighbors, uint8_t count, uint32_t nodeId) {
  for (uint8_t i = 0; i < count; i++) {
    if (neighbors[i] == nodeId) {
      return true;
    }
  }
  return false;
}

void MeshRouting::trySendHistory() {
  // Nothing is transmitted until there is stored history and a known gateway.
  if (historyCount == 0 || meshDebug == nullptr) {
    return;
  }

  if (isGateway) {
    if (gatewaySender != nullptr && millis() - lastHistorySendMs >= SEND_RETRY_MS) {
      sendGatewayLocalHistory();
    }
    return;
  }

  if (waitingForAck || millis() - lastHistorySendMs < SEND_RETRY_MS) {
    return;
  }

  uint32_t gateway = findGatewayId();
  if (gateway == 0) {
    if (millis() - lastGatewayWaitLogMs >= SEND_RETRY_MS) {
      lastGatewayWaitLogMs = millis();
      Serial.println("History waiting: no gateway beacon/LINKS seen yet");
    }
    return;
  }

  if (!meshDebug->isConnected(gateway)) {
    if (millis() - lastGatewayWaitLogMs >= SEND_RETRY_MS) {
      lastGatewayWaitLogMs = millis();
      Serial.printf("History waiting: gateway %lu known, but mesh route is not ready\n",
                    (unsigned long)gateway);
    }
    // Try anyway. sendSingle() returns false if painlessMesh truly has no route.
  }

  // Use a logical source->gateway path in the payload, and let painlessMesh
  // choose the physical next hops. This is more reliable than stale manual
  // application-level hop lists when the mesh reshapes itself.
  uint32_t path[MAX_PATH_NODES] = {meshDebug->getNodeId(), gateway};
  uint8_t pathLength = 2;

  // Build one compact packet containing several readings. This saves airtime
  // compared with sending one mesh/LoRa packet per temperature sample.
  String packet;
  uint32_t batchId = 0;
  uint32_t toSequence = 0;
  if (!buildHistoryPacket(path, pathLength, packet, batchId, toSequence)) {
    return;
  }

  lastHistorySendMs = millis();
  if (meshDebug->sendJson(gateway, packet)) {
    waitingForAck = true;
    pendingBatchId = batchId;
    pendingToSequence = toSequence;
    pendingSentMs = millis();
    Serial.printf("History batch %lu sent toward gateway via %lu\n",
                  (unsigned long)batchId,
                  (unsigned long)gateway);
  } else {
    Serial.printf("History batch %lu send failed toward gateway %lu\n",
                  (unsigned long)batchId,
                  (unsigned long)gateway);
  }
}

bool MeshRouting::buildHistoryPacket(uint32_t *path,
                                     uint8_t pathLength,
                                     String &packet,
                                     uint32_t &batchId,
                                     uint32_t &toSequence) {
  if (historyCount == 0 || pathLength == 0) {
    return false;
  }

  uint8_t recordCount = historyCount < BATCH_RECORD_LIMIT ? historyCount : BATCH_RECORD_LIMIT;
  batchId = nextBatchId++;
  toSequence = history[recordCount - 1].sequence;

  StaticJsonDocument<1536> doc;

  // DATA_BATCH is the mesh payload format:
  // - src/name identify the original sensor,
  // - path records the intended logical route,
  // - records holds [sequence, gatewayTimeMs, temperatureC] rows.
  doc["t"] = "DATA_BATCH";
  doc["src"] = meshDebug->getNodeId();
  doc["name"] = localName;
  doc["batch"] = batchId;
  doc["fromSeq"] = history[0].sequence;
  doc["toSeq"] = toSequence;
  doc["hop"] = 0;

  JsonArray pathArray = doc.createNestedArray("path");
  copyPath(pathArray, path, pathLength);

  JsonArray records = doc.createNestedArray("records");
  for (uint8_t i = 0; i < recordCount; i++) {
    JsonArray row = records.createNestedArray();
    row.add(history[i].sequence);
    row.add(history[i].timeMs);
    row.add(roundf(history[i].temperature * 10.0f) / 10.0f);
  }

  serializeJson(doc, packet);
  return true;
}

bool MeshRouting::sendGatewayLocalHistory() {
  // The gateway also stores its own DS18B20 readings and sends them through
  // the same batching/ACK path, except it can call the LoRa sender directly.
  uint32_t path[1] = {meshDebug->getNodeId()};
  String packet;
  uint32_t batchId = 0;
  uint32_t toSequence = 0;

  if (!buildHistoryPacket(path, 1, packet, batchId, toSequence)) {
    return false;
  }

  lastHistorySendMs = millis();
  if (gatewaySender(packet)) {
    dropHistoryThrough(toSequence);
    Serial.printf("Gateway local batch %lu delivered\n", (unsigned long)batchId);
    return true;
  }

  Serial.printf("Gateway local batch %lu not delivered; will retry\n", (unsigned long)batchId);
  return false;
}

void MeshRouting::dropHistoryThrough(uint32_t sequence) {
  // Delete only readings whose sequence is covered by an ACK. Anything newer
  // stays in RAM for the next batch.
  uint8_t dropCount = 0;
  while (dropCount < historyCount && history[dropCount].sequence <= sequence) {
    dropCount++;
  }

  if (dropCount == 0) {
    return;
  }

  for (uint8_t i = dropCount; i < historyCount; i++) {
    history[i - dropCount] = history[i];
  }
  historyCount -= dropCount;

  Serial.printf("Dropped %u ACKed history records; %u remain\n", dropCount, historyCount);
}

void MeshRouting::handleGatewayBeacon(JsonDocument &doc) {
  // GW is deliberately small and frequent. If a sensor can hear this, it has
  // the gateway node ID and the gateway-authority time.
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

  if (!isGateway) {
    uint32_t gatewayTimeMs = doc["time"].as<uint32_t>();
    gatewayTimeOffsetMs = (int64_t)gatewayTimeMs - (int64_t)millis();
    gatewayTimeReady = true;

    Serial.printf("Gateway time synced: gateway %lu ms, local %lu ms, offset %ld ms\n",
                  (unsigned long)gatewayTimeMs,
                  (unsigned long)millis(),
                  (long)gatewayTimeOffsetMs);
  }
}

bool MeshRouting::nameLooksLikeGateway(const char *name) {
  if (name == nullptr) {
    return false;
  }

  // In this project the only physical gateway sketch is the DS18B20 + LoRa
  // node. This fallback helps if a LINKS packet is received before a GW beacon.
  return strcmp(name, "DS18B20") == 0 || strcmp(name, "LoRaGateway") == 0;
}

void MeshRouting::rememberGateway(uint32_t nodeId, const char *reason) {
  if (nodeId == 0) {
    return;
  }

  bool changed = gatewayNodeId != nodeId;
  gatewayNodeId = nodeId;
  lastGatewaySeenMs = millis();

  if (changed) {
    Serial.printf("Gateway discovered from %s: %lu\n", reason, (unsigned long)gatewayNodeId);
  } else {
    Serial.printf("Gateway refreshed from %s: %lu\n", reason, (unsigned long)gatewayNodeId);
  }
}

void MeshRouting::handleDataBatch(JsonDocument &doc) {
  // Normally only the gateway receives DATA_BATCH now because sensors send to
  // gatewayNodeId directly through painlessMesh. The forwarding code remains
  // for compatibility with explicit path packets.
  uint32_t path[MAX_PATH_NODES];
  uint8_t pathLength = readPath(doc, path);
  if (pathLength == 0 || meshDebug == nullptr) {
    return;
  }

  uint8_t hop = doc["hop"].as<uint8_t>();
  uint8_t currentHop = hop;
  uint32_t self = meshDebug->getNodeId();

  if (hop + 1 < pathLength && path[hop + 1] == self) {
    currentHop = hop + 1;
  } else if (hop < pathLength && path[hop] == self) {
    currentHop = hop;
  } else {
    return;
  }

  if (currentHop == pathLength - 1) {
    // Final hop: only the gateway is allowed to turn a mesh DATA_BATCH into a
    // LoRa BATCH. If LoRa delivery fails, no mesh ACK is sent.
    if (!isGateway || gatewaySender == nullptr) {
      return;
    }

    String packet;
    serializeJson(doc, packet);
    if (gatewaySender(packet)) {
      sendBatchAck(doc);
    }
    return;
  }

  forwardDataBatch(doc, currentHop);
}

void MeshRouting::forwardDataBatch(JsonDocument &doc, uint8_t currentHop) {
  uint32_t path[MAX_PATH_NODES];
  uint8_t pathLength = readPath(doc, path);
  if (currentHop + 1 >= pathLength) {
    return;
  }

  doc["hop"] = currentHop;
  String packet;
  serializeJson(doc, packet);
  meshDebug->sendJson(path[currentHop + 1], packet);
}

void MeshRouting::sendBatchAck(JsonDocument &dataDoc) {
  // The gateway sends this ACK only after the LoRa receiver ACKs the packet.
  // That is why source nodes can safely delete ACKed history.
  uint32_t path[MAX_PATH_NODES];
  uint8_t pathLength = readPath(dataDoc, path);
  if (pathLength < 2) {
    return;
  }

  StaticJsonDocument<768> ack;
  ack["t"] = "BATCH_ACK";
  ack["src"] = dataDoc["src"].as<uint32_t>();
  ack["batch"] = dataDoc["batch"].as<uint32_t>();
  ack["toSeq"] = dataDoc["toSeq"].as<uint32_t>();
  ack["hop"] = pathLength - 1;

  JsonArray pathArray = ack.createNestedArray("path");
  copyPath(pathArray, path, pathLength);

  String packet;
  serializeJson(ack, packet);
  meshDebug->sendJson(path[pathLength - 2], packet);
}

void MeshRouting::handleBatchAck(JsonDocument &doc) {
  // BATCH_ACK travels back along the reverse path. At hop 0, the original
  // source deletes the ACKed history records.
  uint32_t path[MAX_PATH_NODES];
  uint8_t pathLength = readPath(doc, path);
  if (pathLength == 0 || meshDebug == nullptr) {
    return;
  }

  uint8_t hop = doc["hop"].as<uint8_t>();
  uint8_t currentHop = hop;
  uint32_t self = meshDebug->getNodeId();

  if (hop > 0 && path[hop - 1] == self) {
    currentHop = hop - 1;
  } else if (hop < pathLength && path[hop] == self) {
    currentHop = hop;
  } else {
    return;
  }

  if (currentHop == 0) {
    uint32_t source = doc["src"].as<uint32_t>();
    uint32_t batch = doc["batch"].as<uint32_t>();
    uint32_t toSequence = doc["toSeq"].as<uint32_t>();

    if (source == meshDebug->getNodeId() && waitingForAck && batch == pendingBatchId) {
      dropHistoryThrough(toSequence);
      waitingForAck = false;
      pendingBatchId = 0;
      pendingToSequence = 0;
      Serial.printf("Batch %lu ACKed through seq %lu\n",
                    (unsigned long)batch,
                    (unsigned long)toSequence);
    }
    return;
  }

  forwardBatchAck(doc, currentHop);
}

void MeshRouting::forwardBatchAck(JsonDocument &doc, uint8_t currentHop) {
  uint32_t path[MAX_PATH_NODES];
  uint8_t pathLength = readPath(doc, path);
  if (currentHop == 0 || currentHop >= pathLength) {
    return;
  }

  doc["hop"] = currentHop;
  String packet;
  serializeJson(doc, packet);
  meshDebug->sendJson(path[currentHop - 1], packet);
}

uint8_t MeshRouting::readPath(JsonDocument &doc, uint32_t *path) {
  JsonArray pathArray = doc["path"].as<JsonArray>();
  uint8_t pathLength = 0;
  for (JsonVariant value : pathArray) {
    if (pathLength >= MAX_PATH_NODES) {
      break;
    }
    uint32_t nodeId = value.as<uint32_t>();
    if (nodeId != 0) {
      path[pathLength++] = nodeId;
    }
  }
  return pathLength;
}

void MeshRouting::copyPath(JsonArray target, uint32_t *path, uint8_t pathLength) {
  for (uint8_t i = 0; i < pathLength; i++) {
    target.add(path[i]);
  }
}
