import os
import json
import time
import serial
import subprocess
from datetime import datetime, timedelta
import argparse

# =========================================================
# SCRAPPER CONFIGURATION (CLI ARGUMENTS)
# =========================================================
parser = argparse.ArgumentParser(description="Thermal Mesh Scrapper")
parser.add_argument("--mode", choices=["simulation", "real"], default="simulation")
parser.add_argument("--port", type=str, default="COM3")
parser.add_argument("--baud", type=int, default=115200)
args = parser.parse_args()

USE_SIMULATOR = (args.mode == "simulation")
SERIAL_PORT = args.port
BAUD_RATE = args.baud

# Directory paths for Hot and Cold Storage
BASE_DIR = "./data"
LIVE_DIR = f"{BASE_DIR}/live"
ARCHIVE_DIR = f"{BASE_DIR}/archive"

# Maximum rows to keep in the Live CSV to maintain Streamlit performance
MAX_LIVE_ROWS = 1000

# Target array index mapping
SENSOR_MAP = {
    1: "bme280",
    2: "dht11",
    3: "dht22",
    4: "ds18b20"
}

# =========================================================
# STORAGE HELPER FUNCTIONS
# =========================================================

def setup_directories():
    """Ensures the base storage directories exist before scraping."""
    os.makedirs(LIVE_DIR, exist_ok=True)
    os.makedirs(ARCHIVE_DIR, exist_ok=True)

def manage_live_data(filename, header, row_data):
    """
    HOT STORAGE: Appends data to the live CSV and trims it if it exceeds MAX_LIVE_ROWS.
    This ensures app.py runs smoothly without memory overflow.
    """
    filepath = f"{LIVE_DIR}/{filename}"
    
    # 1. Ensure file and header exist
    if not os.path.exists(filepath):
        with open(filepath, "w", encoding="utf-8") as f:
            f.write(header + "\n")
            
    # 2. Append the new row
    with open(filepath, "a", encoding="utf-8") as f:
        f.write(row_data + "\n")
        
    # 3. Trim the file if it exceeds the maximum allowed rows
    with open(filepath, "r", encoding="utf-8") as f:
        lines = f.readlines()
        
    # (+1 is to account for the header line)
    if len(lines) > MAX_LIVE_ROWS + 1:
        # Keep the header, and take the last MAX_LIVE_ROWS
        trimmed_lines = [lines[0]] + lines[-MAX_LIVE_ROWS:]
        with open(filepath, "w", encoding="utf-8") as f:
            f.writelines(trimmed_lines)

def save_to_archive(filename, header, row_data):
    """
    COLD STORAGE: Appends data to a permanent, date-stamped folder.
    Creates a new folder at midnight automatically (Daily Rotation).
    """
    # Get current date in YYYY-MM-DD format
    current_date = datetime.now().strftime("%Y-%m-%d")
    daily_folder = f"{ARCHIVE_DIR}/{current_date}"
    
    # Ensure today's folder exists
    os.makedirs(daily_folder, exist_ok=True)
    
    filepath = f"{daily_folder}/{filename}"
    
    # Ensure file and header exist
    if not os.path.exists(filepath):
        with open(filepath, "w", encoding="utf-8") as f:
            f.write(header + "\n")
            
    # Append the row permanently (No trimming)
    with open(filepath, "a", encoding="utf-8") as f:
        f.write(row_data + "\n")

# =========================================================
# MAIN SCRAPPING ROUTINE
# =========================================================

def main():
    setup_directories()
    
    # Initialize Data Source
    if USE_SIMULATOR:
        print("🔧 Mode: SIMULATOR")
        print("Launching simulator.py in the background...")
        source = subprocess.Popen(
            ["python", "simulator.py"], 
            stdout=subprocess.PIPE, 
            stderr=subprocess.STDOUT, 
            text=True
        ).stdout
    else:
        print(f"🔌 Mode: HARDWARE SERIAL ({SERIAL_PORT} @ {BAUD_RATE})")
        try:
            source = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=1)
            time.sleep(2) 
        except Exception as e:
            print(f"Failed to open serial port {SERIAL_PORT}: {e}")
            return

    print("📡 Listening for data streams. Press Ctrl+C to terminate.\n")

    try:
        while True:
            # 1. Read line from the active source
            raw_line = source.readline()
            if not raw_line:
                continue

            if not USE_SIMULATOR:
                try:
                    line = raw_line.decode('utf-8').strip()
                except UnicodeDecodeError:
                    continue 
            else:
                line = raw_line.strip()

            print(line)

            # 2. Filter lines that contain the exact scrapper target array
            if " -> data: " in line:
                try:
                    parts = line.split(" -> data: ")
                    
                    # EXTRACT AND PARSE THE PC TIMESTAMP
                    raw_time = parts[0].strip() # Contoh: "18:57:40.861"
                    
                    # Konversi waktu teks dari PC menjadi objek datetime sungguhan
                    current_date = datetime.now().strftime("%Y-%m-%d")
                    try:
                        pc_datetime = datetime.strptime(f"{current_date} {raw_time}", "%Y-%m-%d %H:%M:%S.%f")
                    except ValueError:
                        # Fallback jika kebetulan milidetiknya tidak tercetak
                        log_time = raw_time[:8]
                        pc_datetime = datetime.strptime(f"{current_date} {log_time}", "%Y-%m-%d %H:%M:%S")

                    # Gunakan format waktu bersih (tanpa ms) untuk konektivitas
                    base_log_time = pc_datetime.strftime("%H:%M:%S")
                    
                    json_str = parts[1].strip()
                    payload = json.loads(json_str)

                    # 3. Process Connectivity Array (Index 0)
                    conn_array = payload[0]
                    if len(conn_array) > 0:
                        conn_str = json.dumps(conn_array)
                        csv_row = f'{base_log_time},"{conn_str}"'
                        
                        # Double-write strategy
                        manage_live_data("connectivity.csv", "time,connectivity", csv_row)
                        save_to_archive("connectivity.csv", "time,connectivity", csv_row)

                    # 4. Process Sensor Data Arrays (Index 1 to 4) with Time-Sync Interpolation
                    for index, sensor_name in SENSOR_MAP.items():
                        sensor_records = payload[index]
                        
                        if len(sensor_records) > 0:
                            # Tentukan waktu mesh paling akhir sebagai "Jangkar" (Anchor)
                            latest_mesh_time = sensor_records[-1][0]
                            
                            for record in sensor_records:
                                record_mesh_time = record[0]
                                temp_val = record[1]
                                
                                # Hitung selisih waktu (dalam milidetik) antara data ini dengan data terbaru
                                delta_ms = latest_mesh_time - record_mesh_time
                                
                                # Kurangi waktu PC saat ini dengan selisih tersebut untuk mendapat waktu asli
                                actual_record_time = pc_datetime - timedelta(milliseconds=delta_ms)
                                
                                # Format kembali menjadi HH:MM:SS untuk disimpan ke CSV
                                final_time_str = actual_record_time.strftime("%H:%M:%S")
                                
                                csv_row = f"{final_time_str},{temp_val:.2f}"
                                filename = f"{sensor_name}.csv"
                                
                                # Double-write strategy
                                manage_live_data(filename, "time,temp", csv_row)
                                save_to_archive(filename, "time,temp", csv_row)

                except json.JSONDecodeError:
                    print("Warning: Received malformed JSON string.")
                except Exception as e:
                    print(f"Error processing payload: {e}")

    except KeyboardInterrupt:
        print("\nScrapper terminated by user.")
    finally:
        if USE_SIMULATOR:
            source.close()
        else:
            if source.is_open:
                source.close()

if __name__ == "__main__":
    main()
    