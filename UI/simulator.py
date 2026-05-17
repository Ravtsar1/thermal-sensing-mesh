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

# Transmission order priority (Round-Robin format)
DISPLAY_ORDER = ["DS18B20", "DHT11", "DHT22", "BME280"]

# Internal State Variables
mesh_time_ms = 2500
packet_seq = 1
sensor_seqs = {s: 1 for s in DISPLAY_ORDER}
queues = {s: [] for s in DISPLAY_ORDER}

# Base temperatures for realistic fluctuations
last_known_temps = {
    "DS18B20": 27.0,
    "DHT11": 31.2,
    "DHT22": 25.4,
    "BME280": 28.1
}

# Tracks whose turn it is to transmit in the Round-Robin cycle
current_turn_idx = 0  

# =========================================================
# HELPER FUNCTIONS
# =========================================================

def get_timestamp():
    """Generates a timestamp in HH:MM:SS.mmm format"""
    return datetime.now().strftime("%H:%M:%S.%f")[:-3]

def log_print(msg):
    """Prints a log message with a timestamp prefix, simulating Arduino Serial Monitor"""
    print(f"{get_timestamp()} -> {msg}", flush=True)

# =========================================================
# MAIN SIMULATION LOOP
# =========================================================

print("Initializing Gateway Simulation...")
print("Data pipe is ready for the scrapper.") 
print("Press Ctrl+C to terminate.\n")

try:
    while True:
        # Simulate mesh time delta (approx 2.5 seconds elapsed per loop)
        step_ms = random.randint(2400, 2600)
        mesh_time_ms += step_ms
        
        # 1. RECORD LOCAL GATEWAY DATA (DS18B20)
        # As the local sensor on the gateway, DS18B20 NEVER experiences packet loss.
        temp_ds = round(last_known_temps["DS18B20"] + random.uniform(-0.5, 0.5), 1)
        last_known_temps["DS18B20"] = temp_ds
        queues["DS18B20"].append([sensor_seqs["DS18B20"], mesh_time_ms, temp_ds])
        sensor_seqs["DS18B20"] += 1
        
        # 2. RECORD REMOTE MESH NODE DATA via WiFi
        for sensor in ["BME280", "DHT11", "DHT22"]:
            # Simulate a 30% chance of packet loss over the mesh network
            if random.random() > 0.3: 
                temp_val = round(last_known_temps[sensor] + random.uniform(-0.2, 0.2), 1)
                last_known_temps[sensor] = temp_val
                queues[sensor].append([sensor_seqs[sensor], mesh_time_ms, temp_val])
                sensor_seqs[sensor] += 1

        # 3. LORA TRANSMISSION BASED ON ROUND-ROBIN QUEUE
        target_sensor = DISPLAY_ORDER[current_turn_idx]
        current_turn_idx = (current_turn_idx + 1) % len(DISPLAY_ORDER)
        
        # Only transmit if the target sensor has pending records in its queue
        if len(queues[target_sensor]) > 0:
            batch_records = queues[target_sensor]
            
            # Format payload for scrapper (strip sequence numbers, keep [time, temp])
            clean_records = [[row[1], row[2]] for row in batch_records]
            
            # Log history lines
            for record in batch_records:
                log_print(f"{target_sensor} history seq {record[0]} at {record[1]} ms = {record[2]:.1f} C")
            
            # Generate random 7-bit connectivity array (assuming index 0 and 6 are gateways/always 1)
            conn_array = [random.choice([0, 1]) for _ in range(7)]
            conn_array[0] = 1 
            conn_array[6] = 1 
            
            # Construct incoming LoRa JSON payload
            lora_json = {
                "t": "BATCH",
                "s": packet_seq,
                "src": 4264595069,
                "name": target_sensor,
                "batch": packet_seq,
                "fromSeq": batch_records[0][0],
                "toSeq": batch_records[-1][0],
                "sentAt": mesh_time_ms + random.randint(10, 25),
                "conn": conn_array,
                "records": batch_records
            }
            log_print(f"LoRa in: {json.dumps(lora_json, separators=(',', ':'))}")
            log_print(f"Packet {packet_seq} received")
            
            # Print temperature summary mimicking OLED/Serial output
            for i, sensor in enumerate(DISPLAY_ORDER):
                if sensor == target_sensor:
                    latest_rec = batch_records[-1]
                    log_print(f"{i+1} {sensor}: {latest_rec[2]:.1f} C at {latest_rec[1]} ms (seq {latest_rec[0]})")
                else:
                    log_print(f"{i+1} {sensor}: disconnected")
            
            # ---------------------------------------------------------
            # TARGET SCRAPPER OUTPUT
            # Format: [connectivity, BME280, DHT11, DHT22, DS18B20]
            # ---------------------------------------------------------
            data_output = [conn_array, [], [], [], []]
            data_output[DATA_INDEX[target_sensor]] = clean_records
            log_print(f"data: {json.dumps(data_output, separators=(',', ':'))}")
            
            # ---------------------------------------------------------
            # TRANSMIT ACKNOWLEDGEMENT
            # ---------------------------------------------------------
            time.sleep(0.05) # Simulate microcontroller processing delay
            ack_json = {"t": "ACK", "s": packet_seq}
            log_print(f"LoRa ACK out: {json.dumps(ack_json, separators=(',', ':'))}")
            
            # Clear the queue for this sensor after successful transmission
            queues[target_sensor] = []
            packet_seq += 1

        # Real-time clock delay before the next mesh cycle
        time.sleep(random.uniform(3.0, 4.0))

except KeyboardInterrupt:
    print("\nSimulation terminated.")