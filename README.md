# Thermal Sensing Mesh

Note: this `ui-feature` branch is under active development and may be unstable.

Thermal Sensing Mesh is an ESP32 temperature sensing network that combines a
WiFi mesh with a LoRa uplink. The mesh sensors keep timestamped temperature
history locally, discover whether the LoRa gateway is reachable, and upload
history batches only after a route exists.

This `ui-feature` branch also prepares receiver Serial output for a separate
user interface. The LoRa receiver keeps its existing debug prints, but it also
prints one filterable line beginning with `data:` for both connected and
disconnected UI states.

The current firmware uses five ESP32 roles:

| Folder                   | Role                                            | Sensor behavior              |
| ------------------------ | ----------------------------------------------- | ---------------------------- |
| `ESP_Mesh_DHT11DataSim`  | Mesh sensor node                                | Simulated DHT11 temperature  |
| `ESP_Mesh_DHT22DataSim`  | Mesh sensor node                                | Simulated DHT22 temperature  |
| `ESP_Mesh_BME280DataSim` | Mesh sensor node                                | Simulated BME280 temperature |
| `ESP_Mesh_DS18B20_Lora`  | Mesh gateway + LoRa transmitter                 | Real DS18B20 temperature     |
| `ESP_LoraReceiver`       | LoRa receiver + OLED display + UI Serial output | Receives gateway packets     |

## How The Mesh Works

Each mesh ESP32 uses two shared code modules from this repository: `MeshDebug`
and `MeshRouting`.

`MeshDebug` is a small wrapper around painlessMesh. It starts the mesh, keeps
callbacks consistent, sends JSON packets, and lists direct and known mesh nodes.

`MeshRouting` is the project protocol layer:

1. Every sensor reading is saved first in local RAM history.
2. Each node periodically broadcasts a lightweight `LINKS` packet with its
   connectivity data.
3. The DS18B20 gateway broadcasts `GW` beacons so other nodes can learn the
   gateway node ID.
4. The gateway learns the mesh connectivity graph from `LINKS` packets and the
   painlessMesh layout data inside those packets.
5. When a sensor can reach the gateway, it creates a `DATA_BATCH` packet with
   several history records.
6. The packet includes original sensor ID, sensor name, batch ID,
   gateway-authority timestamps, temperature values, and a logical path.
7. painlessMesh handles the real multi-hop forwarding to the gateway.
8. The gateway converts the mesh `DATA_BATCH` into a shorter LoRa `BATCH`.
9. The LoRa `BATCH` includes the delivered temperature history and a seven-value
   connectivity array for the UI.
10. The LoRa receiver displays the newest value per sensor, prints the `data:`
    UI line, and sends a LoRa `ACK`.
11. Only after the LoRa receiver ACKs does the gateway send a mesh `BATCH_ACK`
    back to the source sensor.
12. The source sensor deletes delivered history only after receiving
    `BATCH_ACK`.

This keeps the mesh from flooding every temperature sample immediately. If a
sensor is disconnected, it keeps storing readings and uploads the saved history
when the gateway becomes reachable again.

## Time Synchronization

The DS18B20 gateway is the authority for project time. It broadcasts a small
`GW` packet every 5 seconds with its current gateway time.

DHT11, DHT22, and BME280 do not take temperature readings until they receive a
`GW` packet. After receiving one, each node calculates this local offset:

```text
gatewayTimeOffset = gatewayTimeFromBeacon - localMillis
```

Then each sensor timestamps readings with:

```text
gatewayTime = localMillis + gatewayTimeOffset
```

If a sensor later disconnects after it has already synchronized once, it keeps
recording with the last known gateway-time offset. When the gateway becomes
reachable again, the next `GW` packet refreshes the offset.

If a non-gateway sensor boots alone and never hears the gateway, it keeps
waiting and does not create temperature history yet. This keeps all recorded
temperature timestamps tied to the gateway's time base.

## UI Serial Output

The UI should read the Serial Monitor from `ESP_LoraReceiver` at `115200` baud
and filter lines that start with:

```text
data:
```

The line after `data:` is a JSON-style array:

```text
data: [connectivity, BME280History, DHT11History, DHT22History, DS18B20History]
```

Example:

```text
data: [[1,0,1,0,1,0,1],[],[[185091,26.9],[187592,26.8]],[],[]]
```

The first array contains seven boolean values represented as `1` or `0`:

| Index | Meaning                                    |
| ----- | ------------------------------------------ |
| `0`   | BME280 connected to DHT11                  |
| `1`   | BME280 connected to DHT22                  |
| `2`   | BME280 connected to DS18B20 gateway        |
| `3`   | DHT11 connected to DHT22                   |
| `4`   | DHT11 connected to DS18B20 gateway         |
| `5`   | DHT22 connected to DS18B20 gateway         |
| `6`   | DS18B20 gateway connected to LoRa receiver |

Each temperature history array contains rows in this format:

```text
[gatewayTimeMs, temperatureC]
```

For example:

```text
[[185091,26.9],[187592,26.8]]
```

The receiver prints one `data:` line for each LoRa `BATCH` it receives. Because
each batch belongs to one source sensor, the matching sensor history array is
filled and the other sensor arrays are usually empty. If a sensor was
disconnected for a while, its next delivered batch may contain several history
rows.

If the receiver has no fresh LoRa batch, it periodically prints this disconnected
UI state:

```text
data: [[0,0,0,0,0,0,0],[],[],[],[]]
```

Other Serial prints such as `LoRa in`, `LoRa ACK out`, and packet summaries are
kept for debugging. UI code should ignore those lines and only parse lines that
begin with `data:`.

## Timing Intervals

### Temperature And Time

| Behavior | Default interval |
| --- | --- |
| DHT11 simulated temperature reading | `2500 ms` |
| DHT22 simulated temperature reading | `2500 ms` |
| BME280 simulated temperature reading | `2500 ms` |
| DS18B20 real temperature reading | `2500 ms` |
| Gateway time beacon used for sensor synchronization | `5000 ms` |

### Mesh Routing

| Behavior | Default interval |
| --- | --- |
| `LINKS` connectivity report | `30000 ms` |
| Initial `LINKS` startup jitter | `0-4000 ms` |
| Learned link freshness timeout | `90000 ms` |
| DS18B20 gateway `GW` beacon | `5000 ms` |
| Learned gateway freshness timeout | `45000 ms` |
| Mesh history send retry | `5000 ms` |
| Mesh `BATCH_ACK` timeout | `12000 ms` |

Connectivity reports are also triggered after painlessMesh reports a topology
change, so a node may publish `LINKS` earlier than the normal 30 second period.

### Gateway And LoRa Receiver

| Behavior | Default interval |
| --- | --- |
| Remote mesh sensor LED freshness on gateway | `30000 ms` |
| LoRa receiver ACK freshness on gateway | `10000 ms` |
| Gateway wait time for LoRa ACK after sending | `450 ms` |
| Gateway LoRa module retry after init failure | `5000 ms` |
| Receiver OLED refresh | `500 ms` |
| Receiver packet freshness timeout | `10000 ms` |
| Receiver LoRa module retry after init failure | `5000 ms` |
| Receiver disconnected UI `data:` line | `1000 ms` |

## Requirements

### Hardware

- 5 ESP32 boards
- 2 SX127x-style LoRa modules, one for gateway and one for receiver
- 1 DS18B20 temperature sensor
- 1 SSD1306 OLED display for the receiver
- LEDs and resistors for gateway status indicators
- External 4.7k pull-up resistor from DS18B20 data to 3.3V

### Arduino IDE Board Support

- ESP32 board package for Arduino IDE

### Arduino Libraries

Install these from Arduino IDE Library Manager:

- `painlessMesh`
- `AsyncTCP`
- `TaskScheduler`
- `ArduinoJson`
- `LoRa`
- `OneWire`
- `DallasTemperature`
- `Adafruit GFX Library`
- `Adafruit SSD1306`

This project also includes two shared local code folders:

- `MeshDebug`, located at `libraries/MeshDebug`
- `MeshRouting`, located at `libraries/MeshRouting`

`MeshDebug` contains the painlessMesh wrapper. `MeshRouting` contains the
connectivity, history, batch, ACK, and UI-connectivity protocol.

## Arduino IDE Setup

Each main sketch folder contains a committed `Config.h` and a matching
`Config.example.h`.

The mesh sketches currently refer to `MeshDebug` and `MeshRouting` by relative
path. Keep the repository folder structure unchanged and open the sketches from
inside this project folder:

```cpp
#include "../libraries/MeshDebug/src/MeshDebug.h"
#include "../libraries/MeshRouting/src/MeshRouting.h"
```

The matching `.cpp` files are also included by relative path in the mesh
sketches. This keeps Arduino IDE usable without separately installing
`MeshDebug` and `MeshRouting` as libraries.

Short note: this relative-path setup may change in the future if `MeshDebug`
and `MeshRouting` are packaged as proper Arduino libraries.

Compile/upload normally:

1. Open one sketch folder, for example
   `ESP_Mesh_DHT11DataSim/ESP_Mesh_DHT11DataSim.ino`.
2. Arduino IDE should show `Config.h` as another tab.
3. Edit `Config.h` before uploading if your mesh password, LoRa frequency,
   pins, or timing intervals are different.
4. Keep `MESH_PREFIX`, `MESH_PASSWORD`, and `MESH_PORT` the same on all mesh
   nodes.
5. Keep `LORA_FREQUENCY` and `LORA_SYNC_WORD` the same on
   `ESP_Mesh_DS18B20_Lora` and `ESP_LoraReceiver`.
6. Click Verify or Upload as usual.

`Config.example.h` is only a clean reference template. The sketches include
`Config.h`.

## Upload Guide

Upload one firmware role to each ESP32:

1. `ESP_Mesh_DS18B20_Lora` to the DS18B20 + LoRa gateway ESP32.
2. `ESP_LoraReceiver` to the OLED + LoRa receiver ESP32.
3. `ESP_Mesh_DHT11DataSim` to the DHT11 simulated node ESP32.
4. `ESP_Mesh_DHT22DataSim` to the DHT22 simulated node ESP32.
5. `ESP_Mesh_BME280DataSim` to the BME280 simulated node ESP32.

Open Serial Monitor at `115200` baud. Mesh sketches also support the debug
toggle described below.

## Serial Debug Toggle

The four mesh sketches read simple commands from Serial Monitor:

- Send `1` to enable verbose `MeshDebug` output.
- Send `0` to disable verbose `MeshDebug` output.

This feature is available in:

- `ESP_Mesh_DHT11DataSim`
- `ESP_Mesh_DHT22DataSim`
- `ESP_Mesh_BME280DataSim`
- `ESP_Mesh_DS18B20_Lora`

The LoRa receiver sketch does not implement this toggle.

When enabled, `MeshDebug` prints extra transport-level mesh information such as
mesh startup, JSON broadcasts, single-node sends, incoming mesh messages, new
mesh nodes, connection changes, and painlessMesh internal time adjustments.

When disabled, those `MeshDebug` messages are hidden. It does not stop the
program, disable the mesh, change routing behavior, or turn off every Serial
message. Sensor readings, routing/history logs, gateway logs, LoRa logs,
receiver logs, and UI `data:` lines may still print because they are produced
outside `MeshDebug`.

The simulated DHT11, DHT22, and BME280 mesh nodes start with verbose
`MeshDebug` output enabled. The DS18B20 gateway starts with it disabled to keep
the gateway Serial output quieter.

## Radio Frequency Note

The default LoRa frequency in `Config.h` is `433175000L` or 433.175 MHz. This
was chosen as a public default inside the commonly referenced Indonesian
433.05-434.79 MHz SRD/LPWAN range, but you must still verify current local
rules, output power, antenna gain, duty cycle, and module certification before
transmitting.

Regulation references can change. Start from the current official Indonesian
rules, such as [Permenkominfo No. 2 Tahun 2025 on peraturan.go.id](https://www.peraturan.go.id/files/permenkominfo-no-2-tahun-2025.pdf).

## Project Layout

```text
Thermal Sensing Mesh/
  ESP_Mesh_DHT11DataSim/      Simulated DHT11 mesh node
  ESP_Mesh_DHT22DataSim/      Simulated DHT22 mesh node
  ESP_Mesh_BME280DataSim/     Simulated BME280 mesh node
  ESP_Mesh_DS18B20_Lora/      DS18B20 mesh gateway and LoRa transmitter
  ESP_LoraReceiver/           LoRa receiver, OLED display, and UI Serial output
  libraries/MeshDebug/        Shared mesh debug/transport code
  libraries/MeshRouting/      Shared mesh routing/history code
```

## GitHub Compile Check

This repo includes a GitHub Actions compile check for the main ESP32 sketches.
It only checks whether the firmware builds; it does not upload to hardware or
change firmware behavior.

## Current Limitations

- DHT11, DHT22, and BME280 are simulated in the main mesh sketches.
- DS18B20 is the only real temperature sensor in the current gateway sketch.
- History is stored in RAM, so it is lost if a node resets.
- The receiver `data:` line represents the LoRa batch that just arrived. It is
  not a database of every past batch from every sensor.
- The mesh sketches depend on the repository folder structure because
  `MeshDebug` and `MeshRouting` are included by relative path.

## License

This project is released under the MIT License. See `LICENSE`.
