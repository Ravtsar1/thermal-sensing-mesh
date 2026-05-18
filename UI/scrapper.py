import json
import time
import subprocess
import sys
from datetime import datetime
import argparse
from pathlib import Path

try:
    import serial
except ModuleNotFoundError:
    serial = None

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

# Directory paths for Hot and Cold Storage. These are based on this file's
# location, so the scrapper works from the parent project folder or from UI/.
BASE_DIR = Path(__file__).resolve().parent
DATA_DIR = BASE_DIR / "data"
LIVE_DIR = DATA_DIR / "live"
ARCHIVE_DIR = DATA_DIR / "archive"
SIMULATOR_PATH = BASE_DIR / "simulator.py"

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
    LIVE_DIR.mkdir(parents=True, exist_ok=True)
    ARCHIVE_DIR.mkdir(parents=True, exist_ok=True)

def manage_live_data(filename, header, row_data):
    """
    HOT STORAGE: Appends data to the live CSV and trims it if it exceeds MAX_LIVE_ROWS.
    This ensures app.py runs smoothly without memory overflow.
    """
    filepath = LIVE_DIR / filename
    
    # 1. Ensure file and header exist
    if not filepath.exists():
        with filepath.open("w", encoding="utf-8") as f:
            f.write(header + "\n")
            
    # 2. Append the new row
    with filepath.open("a", encoding="utf-8") as f:
        f.write(row_data + "\n")
        
    # 3. Trim the file if it exceeds the maximum allowed rows
    with filepath.open("r", encoding="utf-8") as f:
        lines = f.readlines()
        
    # (+1 is to account for the header line)
    if len(lines) > MAX_LIVE_ROWS + 1:
        # Keep the header, and take the last MAX_LIVE_ROWS
        trimmed_lines = [lines[0]] + lines[-MAX_LIVE_ROWS:]
        with filepath.open("w", encoding="utf-8") as f:
            f.writelines(trimmed_lines)

def save_to_archive(filename, header, row_data):
    """
    COLD STORAGE: Appends data to a permanent, date-stamped folder.
    Creates a new folder at midnight automatically (Daily Rotation).
    """
    # Get current date in YYYY-MM-DD format
    current_date = datetime.now().strftime("%Y-%m-%d")
    daily_folder = ARCHIVE_DIR / current_date
    
    # Ensure today's folder exists
    daily_folder.mkdir(parents=True, exist_ok=True)
    
    filepath = daily_folder / filename
    
    # Ensure file and header exist
    if not filepath.exists():
        with filepath.open("w", encoding="utf-8") as f:
            f.write(header + "\n")
            
    # Append the row permanently (No trimming)
    with filepath.open("a", encoding="utf-8") as f:
        f.write(row_data + "\n")

def parse_data_line(line):
    """Returns (pc_datetime, payload_text) for timestamped or plain data lines."""
    if "data:" not in line:
        return None

    prefix, payload_text = line.split("data:", 1)
    payload_text = payload_text.strip()
    if not payload_text:
        return None

    pc_datetime = datetime.now()
    if "->" in prefix:
        raw_time = prefix.split("->", 1)[0].strip()
        current_date = pc_datetime.strftime("%Y-%m-%d")
        try:
            pc_datetime = datetime.strptime(f"{current_date} {raw_time}", "%Y-%m-%d %H:%M:%S.%f")
        except ValueError:
            try:
                pc_datetime = datetime.strptime(f"{current_date} {raw_time[:8]}", "%Y-%m-%d %H:%M:%S")
            except ValueError:
                pass

    return pc_datetime, payload_text

# =========================================================
# MAIN SCRAPPING ROUTINE
# =========================================================

def main():
    setup_directories()
    source = None
    simulator_process = None
    
    # Initialize Data Source
    if USE_SIMULATOR:
        print("🔧 Mode: SIMULATOR")
        print("Launching simulator.py in the background...")
        simulator_process = subprocess.Popen(
            [sys.executable, str(SIMULATOR_PATH)],
            stdout=subprocess.PIPE, 
            stderr=subprocess.STDOUT, 
            text=True,
            cwd=BASE_DIR
        )
        source = simulator_process.stdout
    else:
        print(f"🔌 Mode: HARDWARE SERIAL ({SERIAL_PORT} @ {BAUD_RATE})")
        if serial is None:
            print("pyserial is not installed. Run: python -m pip install -r UI\\requirements.txt")
            return

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

            # 2. Filter lines that contain the exact scrapper target array.
            # Supports both Arduino timestamped output:
            #   18:57:40.861 -> data: [...]
            # and plain receiver serial output:
            #   data: [...]
            parsed_line = parse_data_line(line)
            if parsed_line is not None:
                try:
                    pc_datetime, json_str = parsed_line
                    base_log_time = pc_datetime.strftime("%H:%M:%S")

                    payload = json.loads(json_str)
                    if not isinstance(payload, list) or len(payload) < 5:
                        print("Warning: Received incomplete UI payload.")
                        continue

                    # 3. Process Connectivity Array (Index 0)
                    conn_array = payload[0]
                    if isinstance(conn_array, list) and len(conn_array) > 0:
                        conn_str = json.dumps(conn_array, separators=(",", ":"))
                        csv_row = f'{base_log_time},"{conn_str}"'
                        
                        # Double-write strategy
                        manage_live_data("connectivity.csv", "time,connectivity", csv_row)
                        save_to_archive("connectivity.csv", "time,connectivity", csv_row)

                    # 4. Process Sensor Data Arrays (Index 1 to 4).
                    # Simplified firmware sends live values without sensor-side
                    # timestamps, so the PC arrival time is the chart time.
                    for index, sensor_name in SENSOR_MAP.items():
                        sensor_records = payload[index]
                        
                        if isinstance(sensor_records, list) and len(sensor_records) > 0:
                            for record in sensor_records:
                                if not isinstance(record, list) or len(record) < 2:
                                    continue

                                temp_val = float(record[-1])
                                final_time_str = base_log_time
                                
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
            if source is not None:
                source.close()
            if simulator_process is not None:
                simulator_process.terminate()
                try:
                    simulator_process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    simulator_process.kill()
        else:
            if source is not None and source.is_open:
                source.close()

if __name__ == "__main__":
    main()
