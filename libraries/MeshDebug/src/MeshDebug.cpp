#include "MeshDebug.h"
#include <list>

MeshDebug::MeshDebug()
    : debugEnabled(true),
      gatewayMode(false),
      rootMode(false),
      currentType("generic_sensor"),
      rawMessageCallback(nullptr),
      changedCallback(nullptr) {}

void MeshDebug::setDebug(bool enable) {
  debugEnabled = enable;
}

void MeshDebug::setGatewayMode(bool enable) {
  gatewayMode = enable;
  if (enable) {
    debugEnabled = false;
  }
}

void MeshDebug::setRootMode(bool enable) {
  rootMode = enable;
}

void MeshDebug::onMessage(MeshRawMessageCallback callback) {
  // Application code installs this to receive raw JSON from the mesh. In this
  // project, every sketch forwards the message to MeshRouting::handleMessage().
  rawMessageCallback = callback;
}

void MeshDebug::onConnectionsChanged(MeshChangedCallback callback) {
  // Application code uses this to request a fresh LINKS report after the mesh
  // topology changes.
  changedCallback = callback;
}

void MeshDebug::setMessageType(const String &type) {
  currentType = type;
}

void MeshDebug::addData(const String &key, float value) {
  payloadData[key] = value;
}

void MeshDebug::addData(const String &key, const String &value) {
  payloadData[key] = value;
}

void MeshDebug::begin(const String &prefix, const String &password, uint16_t port) {
  // painlessMesh keeps its own scheduler. update() must be called often in
  // loop() so connection maintenance, time sync, and message delivery run.
  // This project has one natural root: the DS18B20 + LoRa gateway. Telling
  // painlessMesh that a root should exist makes nodes restructure toward that
  // gateway faster after an intermediate relay disappears.
  mesh.setContainsRoot(true);
  mesh.setRoot(rootMode);
  mesh.init(prefix, password, &userScheduler, port);
  mesh.setContainsRoot(true);
  mesh.setRoot(rootMode);
  mesh.onReceive([this](uint32_t from, String &msg) { receivedCallback(from, msg); });
  mesh.onNewConnection([this](uint32_t nodeId) { newConnectionCallback(nodeId); });
  mesh.onDroppedConnection([this](uint32_t nodeId) { droppedConnectionCallback(nodeId); });
  mesh.onChangedConnections([this]() { changedConnectionCallback(); });
  mesh.onNodeTimeAdjusted([this](int32_t offset) { nodeTimeAdjustedCallback(offset); });

  if (debugEnabled) {
    Serial.println("MeshDebug ready");
  }
}

void MeshDebug::update() {
  mesh.update();
}

bool MeshDebug::sendJson(uint32_t destination, const String &json) {
  // sendSingle() is still a mesh send, not a one-radio-hop send. If the mesh
  // knows a route to destination, painlessMesh relays it internally.
  bool sent = mesh.sendSingle(destination, json);
  if (debugEnabled) {
    Serial.printf("Mesh single -> %lu: %s\n", (unsigned long)destination, sent ? "sent" : "failed");
  }
  return sent;
}

bool MeshDebug::broadcastJson(const String &json, bool includeSelf) {
  bool sent = mesh.sendBroadcast(json, includeSelf);
  if (debugEnabled) {
    Serial.println("Mesh broadcast: " + json);
  }
  return sent;
}

bool MeshDebug::requestStationReconnect() {
  // Forces this node to drop its current upstream STA connection. painlessMesh
  // keeps the AP side alive and scans again, which helps a node leave a stale
  // or rootless path after its relay disappears.
  bool requested = mesh.reconnectStation();
  if (debugEnabled && requested) {
    Serial.println("Mesh station reconnect requested");
  }
  return requested;
}

uint32_t MeshDebug::getMeshTime() {
  // Exposes painlessMesh's internal time for diagnostics or older sketches.
  return mesh.getNodeTime();
}

uint32_t MeshDebug::getNodeId() {
  return mesh.getNodeId();
}

String MeshDebug::getConnectionJson() {
  return mesh.subConnectionJson(false);
}

uint8_t MeshDebug::getDirectNeighbors(uint32_t *neighbors, uint8_t maxNeighbors) {
  if (neighbors == nullptr || maxNeighbors == 0) {
    return 0;
  }

  // subConnectionJson() is a local tree view. Its first-level children are the
  // closest nodes in the current layout from this node's point of view.
  String layout = mesh.subConnectionJson(false);
  StaticJsonDocument<1024> doc;
  DeserializationError error = deserializeJson(doc, layout);
  if (error) {
    return 0;
  }

  JsonArray subs = doc["subs"].as<JsonArray>();
  uint8_t count = 0;
  for (JsonVariant value : subs) {
    if (count >= maxNeighbors) {
      break;
    }
    JsonObject sub = value.as<JsonObject>();
    uint32_t nodeId = sub["nodeId"].as<uint32_t>();
    if (nodeId != 0) {
      neighbors[count++] = nodeId;
    }
  }

  return count;
}

uint8_t MeshDebug::getKnownNodes(uint32_t *nodes, uint8_t maxNodes) {
  if (nodes == nullptr || maxNodes == 0) {
    return 0;
  }

  // getNodeList() contains every node painlessMesh currently knows a route to,
  // directly or indirectly. This is the safest test for gateway reachability.
  uint8_t count = 0;
  std::list<uint32_t> nodeList = mesh.getNodeList(false);
  for (std::list<uint32_t>::iterator it = nodeList.begin(); it != nodeList.end(); ++it) {
    if (count >= maxNodes) {
      break;
    }
    if (*it != 0) {
      nodes[count++] = *it;
    }
  }

  return count;
}

bool MeshDebug::isConnected(uint32_t nodeId) {
  return mesh.isConnected(nodeId);
}

void MeshDebug::broadcastData() {
  StaticJsonDocument<768> doc;
  doc["node"] = mesh.getNodeId();
  doc["type"] = currentType;
  doc["time"] = mesh.getNodeTime();
  doc["data"].set(payloadData.as<JsonObject>());

  String jsonString;
  serializeJson(doc, jsonString);
  mesh.sendBroadcast(jsonString);

  if (gatewayMode) {
    Serial.println(jsonString);
  } else if (debugEnabled) {
    Serial.println("Mesh out: " + jsonString);
  }

  payloadData.clear();
}

void MeshDebug::receivedCallback(uint32_t from, String &msg) {
  // First pass the raw packet to the routing layer. Debug printing happens
  // after this so logging never blocks the protocol handler from seeing data.
  if (rawMessageCallback != nullptr) {
    rawMessageCallback(from, msg);
  }

  if (gatewayMode) {
    Serial.println(msg);
    return;
  }

  if (!debugEnabled) {
    return;
  }

  StaticJsonDocument<768> doc;
  DeserializationError error = deserializeJson(doc, msg);
  if (error) {
    Serial.println("Mesh in: invalid JSON");
    return;
  }

  // New routing packets use "t" instead of the old "type"/"data" shape.
  // Print them directly so the Serial Monitor shows GW, LINKS, DATA, and
  // DATA_ACK instead of misleading "unknown/null".
  const char *newType = doc["t"] | "";
  if (newType[0] != '\0') {
    Serial.printf("Mesh in <- from: %lu | t: %s | json: %s\n",
                  (unsigned long)from,
                  newType,
                  msg.c_str());
    return;
  }

  String dataContent;
  serializeJson(doc["data"], dataContent);
  Serial.printf("Mesh in <- from: %lu | type: %s | data: %s\n",
                (unsigned long)from,
                doc["type"] | "unknown",
                dataContent.c_str());
}

void MeshDebug::newConnectionCallback(uint32_t nodeId) {
  // painlessMesh can report a new peer before it reports a full changed-layout
  // event. Notify the routing layer here too so a replacement route is used as
  // soon as it appears.
  if (changedCallback != nullptr) {
    changedCallback();
  }

  if (debugEnabled) {
    Serial.printf("Mesh new node: %lu\n", (unsigned long)nodeId);
  }
}

void MeshDebug::droppedConnectionCallback(uint32_t nodeId) {
  // A dropped relay is exactly when the routing layer should publish a new
  // topology view and retry any newest reading that has not reached gateway.
  if (changedCallback != nullptr) {
    changedCallback();
  }

  if (debugEnabled) {
    Serial.printf("Mesh dropped node: %lu\n", (unsigned long)nodeId);
  }
}

void MeshDebug::changedConnectionCallback() {
  // Tell MeshRouting to publish a fresh view soon. The actual broadcast is not
  // sent here because this function runs inside the painlessMesh callback path.
  if (changedCallback != nullptr) {
    changedCallback();
  }

  if (debugEnabled) {
    Serial.println("Mesh connections changed");
  }
}

void MeshDebug::nodeTimeAdjustedCallback(int32_t offset) {
  if (debugEnabled) {
    Serial.printf("Mesh time adjusted: %u us, offset %d us\n", mesh.getNodeTime(), offset);
  }
}
