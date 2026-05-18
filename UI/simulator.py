import time
import random
import json
from datetime import datetime

# =========================================================
# SIMULATOR CONFIGURATION
# =========================================================

# Target array index mapping for the scrapper:
# [connectivity, BME280, DHT11, DHT22, DS18B20]
DATA_INDEX = {
    "BME280": 1,
    "DHT11": 2,
    "DHT22": 3,
    "DS18B20": 4
}

DISPLAY_ORDER = ["DS18B20", "DHT11", "DHT22", "BME280"]

packet_seq = 1
sensor_seqs = {sensor: 1 for sensor in DISPLAY_ORDER}
current_turn_idx = 0

last_known_temps = {
    "DS18B20": 27.0,
    "DHT11": 31.2,
    "DHT22": 25.4,
    "BME280": 28.1
}


def get_timestamp():
    """Generates a timestamp in HH:MM:SS.mmm format."""
    return datetime.now().strftime("%H:%M:%S.%f")[:-3]


def log_print(msg):
    """Prints a log message with a timestamp prefix, simulating Arduino Serial Monitor."""
    print(f"{get_timestamp()} -> {msg}", flush=True)


def next_temperature(sensor):
    step = random.uniform(-0.3, 0.3)
    last_known_temps[sensor] = round(last_known_temps[sensor] + step, 1)
    return last_known_temps[sensor]


print("Initializing Simplified Gateway Simulation...")
print("Data pipe is ready for the scrapper.")
print("Press Ctrl+C to terminate.\n")

try:
    while True:
        target_sensor = DISPLAY_ORDER[current_turn_idx]
        current_turn_idx = (current_turn_idx + 1) % len(DISPLAY_ORDER)

        temp_val = next_temperature(target_sensor)
        sensor_seq = sensor_seqs[target_sensor]
        sensor_seqs[target_sensor] += 1

        conn_array = [random.choice([0, 1]) for _ in range(7)]
        conn_array[0] = 1
        conn_array[6] = 1

        lora_json = {
            "t": "TEMP",
            "s": packet_seq,
            "src": 4264595069,
            "name": target_sensor,
            "seq": sensor_seq,
            "temp": temp_val,
            "conn": conn_array
        }

        log_print(f"LoRa in: {json.dumps(lora_json, separators=(',', ':'))}")
        log_print(f"Packet {packet_seq} received")

        for i, sensor in enumerate(DISPLAY_ORDER):
            if sensor == target_sensor:
                log_print(f"{i + 1} {sensor}: {temp_val:.1f} C (seq {sensor_seq})")
            else:
                log_print(f"{i + 1} {sensor}: disconnected")

        # Format: [connectivity, BME280, DHT11, DHT22, DS18B20]
        # The first value in each reading is a placeholder; scrapper.py uses
        # PC arrival time as the chart timestamp in the simplified protocol.
        data_output = [conn_array, [], [], [], []]
        data_output[DATA_INDEX[target_sensor]] = [[0, temp_val]]
        log_print(f"data: {json.dumps(data_output, separators=(',', ':'))}")

        time.sleep(0.05)
        ack_json = {"t": "ACK", "s": packet_seq}
        log_print(f"LoRa ACK out: {json.dumps(ack_json, separators=(',', ':'))}")

        packet_seq += 1
        time.sleep(random.uniform(3.0, 4.0))

except KeyboardInterrupt:
    print("\nSimulation terminated.")
