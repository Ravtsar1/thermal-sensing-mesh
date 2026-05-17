# Thermal Mesh UI

A real-time thermal mesh monitoring dashboard built with Streamlit, Plotly, and serial telemetry integration.

This system supports both:

* **Simulation Mode** → generates randomized telemetry for testing
* **Real Hardware Mode** → receives live ESP32 telemetry via serial communication

---

# Project Structure

## Core Files

| File               | Description                                                                                 |
| ------------------ | ------------------------------------------------------------------------------------------- |
| `main.py`          | Unified system process manager that launches both the scrapper and dashboard simultaneously |
| `app.py`           | Main Streamlit dashboard UI powered by Plotly                                               |
| `scrapper.py`      | Handles telemetry parsing, synchronization, and CSV pipeline generation                     |
| `simulator.py`     | Generates randomized sensor and connectivity data for testing                               |
| `requirements.txt` | Python dependencies required by the project                                                 |

---

## Data Storage

| Folder          | Purpose                                                      |
| --------------- | ------------------------------------------------------------ |
| `data/live/`    | Temporary rolling telemetry logs for real-time visualization |
| `data/archive/` | Permanent historical telemetry backups                       |

---

# Installation

Install all required dependencies before running the system.

From the parent project directory:

```cmd
python -m pip install -r UI\requirements.txt
```

Or from inside the `UI/` folder:

```cmd
python -m pip install -r requirements.txt
```

---

# How to Use

The system uses a unified execution manager through `main.py`.

You only need **one terminal window** to run the entire telemetry pipeline and dashboard simultaneously.

---

# 1. Simulation Mode

Run the complete telemetry pipeline without physical hardware:

```cmd
python main.py simulation
```

This launches:

* the simulated telemetry generator
* the scrapper pipeline
* the Streamlit dashboard

---

# 2. Real Hardware Mode

To receive live telemetry from an ESP32 receiver connected via USB:

Using the default baudrate (`115200`):

```cmd
python main.py real COM3
```

Using a custom baudrate:

```cmd
python main.py real COM3 9600
```

> Replace `COM3` with your actual serial port identifier.
>
> Linux/macOS example:
>
> ```bash
> /dev/ttyUSB0
> ```

---

# 3. Accessing the Dashboard

Once initialized, Streamlit will automatically start a local web server.

Default address:

```text
http://localhost:8501
```

Open the URL in your browser to access:

* Real-time thermal visualization
* Historical temperature graphs
* Network topology connectivity view
* Sensor telemetry logs

---

# 4. Stopping the System

To safely terminate all running processes:

```text
CTRL + C
```

This cleanly shuts down:

* Streamlit dashboard
* telemetry scrapper
* simulator (if active)

without leaving orphan background processes.

---

# Data Pipeline Architecture

The system implements a dual-stage storage model to separate real-time visualization from long-term archival.

## Hot Storage (`data/live/`)

Temporary rolling CSV logs used directly by the dashboard.

Features:

* limited to a rolling window of 1,000 rows
* optimized for lightweight Streamlit rendering
* prevents graph lag and excessive RAM usage
* cleared through the dashboard control panel

---

## Cold Storage (`data/archive/`)

Permanent telemetry backup storage.

Features:

* automatic daily folder rotation
* timestamped archival directories
* optimized for long-term analytics and offline processing

---

# Telemetry Format

## Sensor CSV Format

Each sensor log follows:

```csv
time,temp
22:27:05,30.95
```

---

## Connectivity CSV Format

`connectivity.csv` stores topology state vectors as JSON arrays:

```csv
time,connectivity
22:27:05,"[1,1,1,1,1,1,1]"
```

---

# Connectivity Index Mapping

The 7-element connectivity vector maps as follows:

```text
Index 0 : BME280  <--> DHT11
Index 1 : BME280  <--> DHT22
Index 2 : BME280  <--> DS18B20
Index 3 : DHT11   <--> DHT22
Index 4 : DHT11   <--> DS18B20
Index 5 : DHT22   <--> DS18B20
Index 6 : DS18B20 <--> RECEIVER
```

---

# Features

* Real-time thermal telemetry dashboard
* Serial telemetry ingestion
* Simulation environment for testing
* Automatic CSV pipeline generation
* Time synchronization interpolation
* Dynamic network topology visualization
* Lightweight rolling-window storage
* Daily archival rotation

---

# Technology Stack

* Python
* Streamlit
* Plotly
* PySerial
* Pandas

---

# Notes

* The dashboard is optimized for local execution.
* Recommended Python version: **3.10+**
* Ensure the serial port is not occupied by another application before running in hardware mode.
