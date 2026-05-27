# Thermal Mesh UI

Thermal Mesh UI is a local Streamlit dashboard for the ESP32 LoRa receiver. It
can either read real receiver serial output or generate simulated telemetry for
testing without hardware.

## Files

| File | Purpose |
| --- | --- |
| `main.py` | Starts the dashboard plus the scraper or simulator |
| `app.py` | Streamlit dashboard |
| `scrapper.py` | Reads `data:` serial lines and writes CSV files |
| `simulator.py` | Generates fake `data:` lines for UI testing |
| `requirements.txt` | Python dependencies |

## Install Dependencies

Run this from the parent folder that contains `UI`:

```cmd
python -m pip install -r UI\requirements.txt
```

Or run this from inside the `UI` folder:

```cmd
python -m pip install -r requirements.txt
```

Recommended Python version: 3.10 or newer.

## Run With Simulated Data

From the parent folder:

```cmd
python UI\main.py simulation
```

From inside `UI`:

```cmd
python main.py simulation
```

This starts the simulator, scraper, and dashboard.

## Run With Real Receiver Hardware

Close Arduino Serial Monitor first so Python can open the serial port.

From the parent folder:

```cmd
python UI\main.py real COM8
```

From inside `UI`:

```cmd
python main.py real COM8
```

Replace `COM8` with your receiver port. You can also pass a custom baud rate:

```cmd
python UI\main.py real COM8 115200
```

## Open The Dashboard

After startup, open:

```text
http://localhost:8501
```

Stop everything with `Ctrl+C` in the terminal that started `main.py`.

## Data Format

The scraper ignores normal debug logs and parses only lines containing:

```text
data:
```

The expected payload is:

```text
[connectivity, BME280, DHT11, DHT22, DS18B20, BME280_Kalman, DHT22_Battery, DHT11_Fuzzy]
```

The dashboard stores parsed values in CSV files under `data/live/` and daily
archives under `data/archive/`. These files are local runtime data and are not
intended to be committed to Git.

## CSV Files

| File | Columns |
| --- | --- |
| `connectivity.csv` | `time,connectivity` |
| `bme280.csv` | `time,temp` |
| `dht11.csv` | `time,temp` |
| `dht22.csv` | `time,temp` |
| `ds18b20.csv` | `time,temp` |
| `bme280_kalman.csv` | `time,temp` |
| `dht22_battery.csv` | `time,battery` |
| `dht11_fuzzy.csv` | `time,normal,caution,warning,danger` |

The firmware sends `0` as a placeholder time inside each sensor row. The Python
scraper replaces it with the PC time when the receiver line arrives.

## Clearing The Dashboard View

The **Clear Live Graph** button resets the current dashboard view without
deleting the permanent archive files. The dashboard hides rows captured before
the button click and starts plotting new incoming rows as they arrive.
