import time
import random
import csv
import json
from datetime import datetime
from pathlib import Path

# =========================================================
# SYSTEM CONFIGURATION
# =========================================================
BASE_DIR = Path(__file__).resolve().parent
DATA_DIR = BASE_DIR / "data"
SENSORS = ["ds18b20", "dht11", "dht22", "bme280"]

# Defined network topology (source node -> target node)
EDGES = [
    ("bme280", "dht11"),
    ("bme280", "dht22"),
    ("bme280", "ds18b20"),
    ("dht11", "dht22"),
    ("dht11", "ds18b20"),
    ("dht22", "ds18b20"),
    ("ds18b20", "receiver")
]

RECEIVER_EDGE = ("ds18b20", "receiver")

# Logical temperature ranges for realistic simulation (Min, Max)
SENSOR_RANGES = {
    "ds18b20": (25.0, 35.0),
    "dht11": (20.0, 40.0),
    "dht22": (22.0, 28.0),
    "bme280": (26.0, 32.0)
}

# =========================================================
# DATA RESET PROMPT
# =========================================================
DATA_DIR.mkdir(exist_ok=True)

print("🛠️  Thermal Mesh Simulator")
user_choice = input("Do you want to clear existing CSV data before starting? (y/n): ").strip().lower()

if user_choice == 'y':
    for file_path in DATA_DIR.glob("*.csv"):
        file_path.unlink()
    print("✅ Legacy data cleared successfully!\n")
else:
    print("▶️ Appending to existing data logs...\n")

# =========================================================
# CSV HEADER INITIALIZATION
# =========================================================
# Ensure files exist and have proper headers
for sensor in SENSORS:
    file_path = DATA_DIR / f"{sensor}.csv"
    if not file_path.exists():
        with file_path.open("w") as f:
            f.write("time,temp\n")

conn_path = DATA_DIR / "connectivity.csv"
if not conn_path.exists() or not conn_path.read_text(encoding="utf-8").startswith("time,connectivity"):
    with conn_path.open("w") as f:
        f.write("time,connectivity\n")

# =========================================================
# MAIN SIMULATION LOOP
# =========================================================
print("📡 Initiating Telemetry Transmission...")
print("Press Ctrl+C to terminate the process.\n")

try:
    while True:
        # Standardize timestamp format
        current_timestamp = datetime.now().strftime("%H:%M:%S")

        # 1. Generate and Append Sensor Telemetry
        for sensor in SENSORS:
            min_temp, max_temp = SENSOR_RANGES[sensor]
            
            # Generate randomized temperature values within specified limits
            simulated_temp = random.uniform(min_temp, max_temp)
            
            # Safely append data (File is opened and closed immediately)
            with (DATA_DIR / f"{sensor}.csv").open("a") as f:
                f.write(f"{current_timestamp},{simulated_temp:.2f}\n")

        # 2. Generate and Append Network Connectivity Status
        connectivity_values = []
        for source, target in EDGES:
            # Stochastic model: 90% chance of connection (1), 10% packet loss/disconnect (0)
            is_connected = 1 if (source, target) == RECEIVER_EDGE or random.random() > 0.1 else 0
            connectivity_values.append(is_connected)

        with conn_path.open("a", newline="") as f:
            writer = csv.writer(f)
            writer.writerow([current_timestamp, json.dumps(connectivity_values, separators=(",", ":"))])

        print(f"[{current_timestamp}] Telemetry batch transmitted and logged.")
        
        # Transmission interval (2 seconds)
        time.sleep(2)

except KeyboardInterrupt:
    print("\n🛑 Simulation terminated by operator.")
