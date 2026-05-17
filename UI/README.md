# Thermal Mesh UI

This folder contains a Streamlit dashboard for viewing simulated or received thermal mesh data.

## Files

- `app.py` runs the dashboard.
- `simulator.py` creates fake sensor and connectivity data for testing the dashboard without ESP32 hardware.
- `requirements.txt` lists the Python packages needed by the dashboard.
- `data/` stores the CSV files read by the dashboard.

## Install Dependencies

You can run the commands from the parent project folder:

```cmd
python -m pip install -r UI\requirements.txt
```

Or from inside this `UI` folder:

```cmd
python -m pip install -r requirements.txt
```

## Run The Dashboard

From the parent project folder:

```cmd
python -m streamlit run UI\app.py
```

From inside this `UI` folder:

```cmd
python -m streamlit run app.py
```

Streamlit will print a local URL, usually:

```text
http://localhost:8501
```

Open that URL in your browser.

## Run The Simulator

Open a second terminal so the dashboard can keep running.

From the parent project folder:

```cmd
python UI\simulator.py
```

From inside this `UI` folder:

```cmd
python simulator.py
```

When the simulator asks:

```text
Do you want to clear existing CSV data before starting? (y/n):
```

Choose `y` to start fresh, or `n` to append new simulated data to the existing CSV files.

## Path Behavior

The UI code uses the location of `app.py` and `simulator.py` to find the `data/` folder. Because of that, the CSV files are read from and written to this folder:

```text
UI/data/
```

This means the program works whether you run it from the parent project folder or from inside the `UI` folder.

## Data Format

Each sensor CSV uses this format:

```csv
time,temp
22:27:05,30.95
```

`connectivity.csv` uses one 7-value array per timestamp:

```csv
time,connectivity
22:27:05,"[1,1,1,1,1,1,1]"
```

The connectivity array order is:

```text
[BME280-DHT11, BME280-DHT22, BME280-DS18B20, DHT11-DHT22, DHT11-DS18B20, DHT22-DS18B20, DS18B20-Receiver]
```
