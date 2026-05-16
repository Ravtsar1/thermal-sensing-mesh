#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <painlessMesh.h>

typedef void (*MeshRawMessageCallback)(uint32_t from, const String &msg);
typedef void (*MeshChangedCallback)();

// Thin wrapper around painlessMesh.
// The project code uses this class so every sketch can send JSON, read mesh
// time, and react to connection changes with the same small API.
class MeshDebug {
public:
  MeshDebug();

  void begin(const String &prefix, const String &password, uint16_t port);
  void update();
  void setDebug(bool enable);
  void setGatewayMode(bool enable);
  void onMessage(MeshRawMessageCallback callback);
  void onConnectionsChanged(MeshChangedCallback callback);

  // Legacy helpers kept for older sketches/examples. The new routing layer
  // mostly uses sendJson() and broadcastJson() directly.
  void setMessageType(const String &type);
  void addData(const String &key, float value);
  void addData(const String &key, const String &value);
  void broadcastData();

  // sendJson() uses painlessMesh sendSingle(). The library itself handles
  // multi-hop forwarding, so destination does not need to be a direct neighbor.
  bool sendJson(uint32_t destination, const String &json);
  bool broadcastJson(const String &json, bool includeSelf = false);

  // getDirectNeighbors() is useful for topology reports; getKnownNodes() is
  // useful for checking whether the mesh has any route to another node.
  uint8_t getDirectNeighbors(uint32_t *neighbors, uint8_t maxNeighbors);
  uint8_t getKnownNodes(uint32_t *nodes, uint8_t maxNodes);
  bool isConnected(uint32_t nodeId);
  String getConnectionJson();
  uint32_t getMeshTime();
  uint32_t getNodeId();

private:
  painlessMesh mesh;
  Scheduler userScheduler;

  bool debugEnabled;
  bool gatewayMode;
  String currentType;
  StaticJsonDocument<384> payloadData;
  MeshRawMessageCallback rawMessageCallback;
  MeshChangedCallback changedCallback;

  void receivedCallback(uint32_t from, String &msg);
  void newConnectionCallback(uint32_t nodeId);
  void changedConnectionCallback();
  void nodeTimeAdjustedCallback(int32_t offset);
};
