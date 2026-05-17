import os
import time
import random
from datetime import datetime

# =========================================================
# SYSTEM CONFIGURATION
# =========================================================
DATA_DIR = "./data"
SENSORS = ["ds18b20", "dht11", "dht22", "bme280"]

# Defined network topology (source node -> target node)
EDGES = [
    ("ds18b20", "dht11"),
    ("ds18b20", "dht22"),
    ("dht11", "bme280"),
    ("dht22", "bme280")
]

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
os.makedirs(DATA_DIR, exist_ok=True)

print("🛠️  Thermal Mesh Simulator")
user_choice = input("Do you want to clear existing CSV data before starting? (y/n): ").strip().lower()

if user_choice == 'y':
    for filename in os.listdir(DATA_DIR):
        if filename.endswith(".csv"):
            file_path = os.path.join(DATA_DIR, filename)
            os.remove(file_path)
    print("✅ Legacy data cleared successfully!\n")
else:
    print("▶️ Appending to existing data logs...\n")

# =========================================================
# CSV HEADER INITIALIZATION
# =========================================================
# Ensure files exist and have proper headers
for sensor in SENSORS:
    file_path = f"{DATA_DIR}/{sensor}.csv"
    if not os.path.exists(file_path):
        with open(file_path, "w") as f:
            f.write("time,temp\n")

conn_path = f"{DATA_DIR}/connectivity.csv"
if not os.path.exists(conn_path):
    with open(conn_path, "w") as f:
        f.write("time,source,target,connected\n")

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
            with open(f"{DATA_DIR}/{sensor}.csv", "a") as f:
                f.write(f"{current_timestamp},{simulated_temp:.2f}\n")

        # 2. Generate and Append Network Connectivity Status
        with open(conn_path, "a") as f:
            for source, target in EDGES:
                # Stochastic model: 90% chance of connection (1), 10% packet loss/disconnect (0)
                is_connected = 1 if random.random() > 0.1 else 0
                f.write(f"{current_timestamp},{source},{target},{is_connected}\n")

        print(f"[{current_timestamp}] Telemetry batch transmitted and logged.")
        
        # Transmission interval (2 seconds)
        time.sleep(2)

except KeyboardInterrupt:
    print("\n🛑 Simulation terminated by operator.")
