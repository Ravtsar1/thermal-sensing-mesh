```markdown
# Thermal Mesh UI

This folder contains a Streamlit dashboard for viewing simulated or received thermal mesh data.

## Files

- `main.py` acts as the system process manager to launch both the scrapper and dashboard together.
- `app.py` runs the core Plotly-driven Streamlit dashboard UI.
- `scrapper.py` handles the live data pipeline, parsing incoming telemetry stream and executing time-sync interpolation.
- `simulator.py` creates randomized sensor and connectivity data logs for testing purposes.
- `requirements.txt` lists the Python packages needed by the dashboard and hardware serial communication.
- `data/` stores temporary hot storage (`live/`) and permanent cold storage (`archive/`) telemetry logs.

## Install Dependencies

Ensure you have all the required packages installed, including `pyserial` for hardware communication and `plotly` for rendering. 

You can run the commands from the parent project folder:

```cmd
python -m pip install -r UI\requirements.txt

```

Or from inside this `UI` folder:

```cmd
python -m pip install -r requirements.txt

```

## How to Use

The system uses a unified process manager via `main.py`. You only need to open a **single terminal window** to run the entire data pipeline and UI dashboard simultaneously.

### 1. Running in Simulation Mode

To test the full network pipeline using the background data generator (without physical ESP32 hardware), run:

```cmd
python main.py simulation

```

### 2. Running in Real Hardware Mode

To read live batch telemetry data directly from your physical ESP32 receiver station connected via USB, provide the specific serial port name.

Using default baudrate (115200):

```cmd
python main.py real COM3

```

Using a custom baudrate (e.g., 9600):

```cmd
python main.py real COM3 9600

```

*(Note: Replace `COM3` with your actual serial port identifier, such as `/dev/ttyUSB0` on Linux/Mac systems).*

### 3. Accessing the UI Dashboard

Once the system starts, Streamlit will initialize the local server and print a local URL (usually within 3 seconds):

```text
http://localhost:8501

```

Open that URL in your web browser to view current temperatures, historical chart timelines, and the real-time network topology layout.

### 4. Stopping the System

To safely shut down both the data scrapper and the dashboard without leaving zombie processes in your RAM, simply press **`Ctrl+C`** once inside the active terminal.

## Data Storage Architecture

The system implements a dual-stage data pipeline to isolate live views from historical backups:

* **Hot Storage (`data/live/`):** Houses temporary CSV logs limited to a rolling window of 1,000 rows. This keeps the Streamlit graph engine lightweight and prevents UI lag. Clearing the dashboard view via the UI Control Panel only purges this folder.
* **Cold Storage (`data/archive/`):** Houses the permanent telemetry payload backups. It automatically deploys a new folder timestamped by date at midnight (Daily Rotation) for long-term analytical processing.

## Telemetry Format

Each processed sensor CSV follows this time-series format:

```csv
time,temp
22:27:05,30.95

```

`connectivity.csv` logs network topology state vectors as stringified JSON arrays linked to a local time element:

```csv
time,connectivity
22:27:05,"[1,1,1,1,1,1,1]"

```

The 7-element connectivity array indexes are mapped sequentially as:

```text
Index 0: BME280  <---> DHT11
Index 1: BME280  <---> DHT22
Index 2: BME280  <---> DS18B20
Index 3: DHT11   <---> DHT22
Index 4: DHT11   <---> DS18B20
Index 5: DHT22   <---> DS18B20
Index 6: DS18B20 <---> RECEIVER

```

```

```

