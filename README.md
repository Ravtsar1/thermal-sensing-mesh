# Thermal Sensing Mesh

This is the real-temperature branch of Thermal Sensing Mesh.

Thermal Sensing Mesh is an ESP32 monitoring network for distributed temperature
nodes. Several ESP32 boards form a WiFi mesh so nearby nodes can pass data
through each other, and one DS18B20 gateway forwards the newest readings to a
separate LoRa receiver. The receiver prints a filterable `data:` line for the
Python UI dashboard.

This `real-temperature` branch is intended for the real BME280, DHT11, and
DHT22 sensor sketches. For simulated BME280, DHT11, and DHT22 firmware, use the
`main` branch.

## Firmware Roles

Upload one firmware role to each ESP32:

| Folder | Role | Data produced |
| --- | --- | --- |
| `ESP_Mesh_BME280` | Real BME280 mesh node | Measured temperature and Kalman-filtered temperature |
| `ESP_Mesh_DHT11` | Real DHT11 mesh node | Measured temperature and fuzzy status |
| `ESP_Mesh_DHT22` | Real DHT22 mesh node | Measured temperature and battery percentage |
| `ESP_Mesh_DS18B20_Lora` | DS18B20 mesh gateway and LoRa transmitter | Real DS18B20 temperature, forwarded mesh data, connectivity |
| `ESP_LoraReceiver` | LoRa receiver, OLED display, and PC serial bridge | UI serial output and receiver ACKs |

The real BME280 and DHT22 sketches use only their local sensor circuits plus the
ESP32 mesh radio. They do not use the old nRF transmitter/receiver reference
flow.

## Real Sensor Features

`ESP_Mesh_BME280` reads BME280 temperature over I2C, calculates a local Kalman
filtered temperature value, and sends both values through the mesh.

`ESP_Mesh_DHT11` reads DHT11 temperature, calculates fuzzy membership values for
`normal`, `waspada`, `siaga`, and `bahaya`, and sends temperature plus fuzzy
status through the mesh. Its local OLED, SD logging, LED, buzzer, and battery
display behavior remain local node features.

`ESP_Mesh_DHT22` reads DHT22 temperature, estimates battery percentage from its
ADC battery divider, sends both values through the mesh, and uses adaptive deep
sleep. It sleeps for a shorter time when the reading is warming up or changing
quickly, and sleeps longer when the temperature is stable.

## How The Network Works

The real sensor nodes join the same painlessMesh network as the gateway. Every
node periodically broadcasts lightweight connectivity reports. The DS18B20
gateway advertises itself with `GW` beacons, learns the mesh topology, and
forwards live sensor packets to the LoRa receiver.

Sensor nodes keep only the newest value. They do not store history and they do
not send batches. This keeps the UI timing simple: the PC treats each value as
arriving "now" when the receiver prints it.

The shared mesh logic lives in:

- `libraries/MeshDebug`
- `libraries/MeshRouting`

`MeshDebug` wraps painlessMesh setup, message sending, callbacks, and optional
transport debug prints. `MeshRouting` handles connectivity reports, gateway
beacons, route selection, live-value packets, and gateway-path ACKs.

## UI Serial Output

Open the receiver serial port at `115200` baud. The receiver keeps debug prints
such as `LoRa in` and `LoRa ACK out`, but the UI should parse only lines that
start with:

```text
data:
```

The payload shape is:

```text
data: [connectivity, BME280, DHT11, DHT22, DS18B20, BME280_Kalman, DHT22_Battery, DHT11_Fuzzy]
```

Example:

```text
data: [[1,0,1,0,1,0,1],[],[[0,26.9]],[],[],[],[],[[0,0.42,0.58,0,0]]]
```

The first array has seven boolean values represented as `1` or `0`:

| Index | Meaning |
| --- | --- |
| `0` | BME280 connected to DHT11 |
| `1` | BME280 connected to DHT22 |
| `2` | BME280 connected to DS18B20 gateway |
| `3` | DHT11 connected to DHT22 |
| `4` | DHT11 connected to DS18B20 gateway |
| `5` | DHT22 connected to DS18B20 gateway |
| `6` | DS18B20 gateway connected to LoRa receiver |

Each temperature-like array contains at most one row:

```text
[0, value]
```

The first `0` is only a placeholder. The UI replaces it with the PC arrival
time. `BME280_Kalman` uses the same shape as temperature. `DHT22_Battery` uses
the second value as battery percentage from `0` to `100`.

`DHT11_Fuzzy` contains one row:

```text
[0, normal, waspada, siaga, bahaya]
```

If the receiver has no fresh LoRa value, it periodically prints:

```text
data: [[0,0,0,0,0,0,0],[],[],[],[],[],[],[]]
```

## Timing Defaults

| Behavior | Default interval |
| --- | --- |
| Real BME280 temperature and Kalman update | `2500 ms` |
| Real DHT11 temperature and fuzzy update | `2500 ms` |
| Real DHT11 OLED refresh | `1000 ms` |
| Real DHT11 SD log write | `5000 ms` |
| Real DHT22 temperature and battery update after wake | `2500 ms` |
| Real DHT22 awake window after reading | `5000 ms` |
| Real DHT22 fast adaptive sleep | `10000 ms` |
| Real DHT22 moderate adaptive sleep | `30000 ms` |
| Real DHT22 stable adaptive sleep | `120000 ms` |
| DS18B20 gateway temperature update | `2500 ms` |
| Mesh connectivity `LINKS` report | `1000 ms` |
| DS18B20 gateway `GW` beacon | `1000 ms` |
| Gateway route reconnect nudge | `3000 ms` |
| Gateway wait for LoRa ACK | `450 ms` |
| Receiver disconnected UI `data:` print | `1000 ms` |
| Receiver OLED refresh | `500 ms` |

## Arduino Requirements

Install the ESP32 board package in Arduino IDE, then install these libraries
from Library Manager:

- `painlessMesh`
- `AsyncTCP`
- `TaskScheduler`
- `ArduinoJson`
- `LoRa`
- `OneWire`
- `DallasTemperature`
- `DHT sensor library`
- `Adafruit BME280 Library`
- `Adafruit Unified Sensor`
- `Adafruit BusIO`
- `Adafruit GFX Library`
- `Adafruit SSD1306`

The repository also contains local shared code in `libraries/MeshDebug` and
`libraries/MeshRouting`. The sketches include those files by relative path, so
keep the folder structure unchanged when opening sketches in Arduino IDE.

## Arduino IDE Setup

Each sketch folder contains a committed `Config.h`. Some folders also include a
`Config.example.h` reference template.

1. Open the `.ino` file from one sketch folder in Arduino IDE.
2. Edit `Config.h` if your pins, mesh password, LoRa frequency, or timing values
   are different.
3. Keep `MESH_PREFIX`, `MESH_PASSWORD`, and `MESH_PORT` identical on all mesh
   nodes.
4. Keep `LORA_FREQUENCY` and `LORA_SYNC_WORD` identical on
   `ESP_Mesh_DS18B20_Lora` and `ESP_LoraReceiver`.
5. Upload one role to each ESP32.
6. Open Serial Monitor at `115200` baud.

## UI Dashboard

The UI is in the `UI` folder and is shared by both branches. It can run in
simulation mode or read the real receiver serial port.

Install dependencies from the project folder:

```cmd
python -m pip install -r UI\requirements.txt
```

Run the UI with simulated serial data:

```cmd
python UI\main.py simulation
```

Run the UI with a receiver on `COM8`:

```cmd
python UI\main.py real COM8
```

Then open:

```text
http://localhost:8501
```

More UI details are in `UI/README.md`.

## Serial Debug Toggle

Mesh sketches accept simple Serial Monitor commands:

- Send `1` to enable verbose `MeshDebug` output.
- Send `0` to disable verbose `MeshDebug` output.

This changes only the extra mesh transport debug prints. Sensor logs, routing
logs, gateway logs, LoRa logs, receiver logs, and UI `data:` lines may still be
printed by other parts of the program.

## Radio Frequency Note

The public default LoRa frequency is `433175000L` or 433.175 MHz. Verify the
current radio rules, output power, antenna gain, duty cycle, and module
certification requirements for your location before transmitting.

For Indonesia, start from the current official rules, such as
[Permenkominfo No. 2 Tahun 2025](https://www.peraturan.go.id/files/permenkominfo-no-2-tahun-2025.pdf).

## Project Layout

```text
Thermal Sensing Mesh/
  ESP_Mesh_BME280/            Real BME280 mesh node
  ESP_Mesh_DHT11/             Real DHT11 mesh node
  ESP_Mesh_DHT22/             Real DHT22 mesh node
  ESP_Mesh_DS18B20_Lora/      DS18B20 mesh gateway and LoRa transmitter
  ESP_LoraReceiver/           LoRa receiver, OLED display, and UI serial output
  libraries/MeshDebug/        Shared mesh debug/transport code
  libraries/MeshRouting/      Shared mesh routing/live-value protocol
  UI/                         Streamlit dashboard and serial scraper
```

## License

This project is released under the MIT License. See `LICENSE`.
