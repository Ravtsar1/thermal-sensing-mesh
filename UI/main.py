import argparse
import subprocess
import sys
import time

def main():
    # 1. Command Line Arguments Configuration
    parser = argparse.ArgumentParser(description="Thermal Mesh System Manager")
    subparsers = parser.add_subparsers(dest="mode", help="Select execution mode")

    # Mode 1: Simulation
    sim_parser = subparsers.add_parser("simulation", help="Run the dashboard using the internal simulator")

    # Mode 2: Real Hardware
    real_parser = subparsers.add_parser("real", help="Run by reading live data from ESP32 hardware")
    real_parser.add_argument("port", help="Serial Port (Example: COM3, /dev/ttyUSB0)")
    real_parser.add_argument("baudrate", type=int, nargs="?", default=115200, help="Baudrate (Default: 115200)")

    args = parser.parse_args()

    # If the user doesn't provide a valid command, display the help menu and exit
    if not args.mode:
        parser.print_help()
        sys.exit(1)

    print("🚀 Starting Thermal Mesh System...\n")
    processes = []

    try:
        # =========================================================
        # PROCESS 1: LAUNCH STREAMLIT DASHBOARD
        # =========================================================
        print("📊 [1/2] Loading Streamlit Dashboard...")
        # Terminal command: python -m streamlit run app.py
        streamlit_cmd = [sys.executable, "-m", "streamlit", "run", "app.py"]
        p_streamlit = subprocess.Popen(streamlit_cmd)
        processes.append(p_streamlit)

        # Allow Streamlit some time to initialize before flooding it with data
        time.sleep(3) 

        # =========================================================
        # PROCESS 2: LAUNCH DATA SCRAPPER
        # =========================================================
        scrapper_cmd = [sys.executable, "scrapper.py", "--mode", args.mode]
        
        if args.mode == "real":
            scrapper_cmd.extend(["--port", args.port, "--baud", str(args.baudrate)])
            print(f"🔌 [2/2] Launching Scrapper (HARDWARE MODE | {args.port} @ {args.baudrate})...")
        else:
            print("🔧 [2/2] Launching Scrapper (SIMULATION MODE)...")
            
        p_scrapper = subprocess.Popen(scrapper_cmd)
        processes.append(p_scrapper)

        # =========================================================
        # HOLDING LOOP (WAITING FOR USER TERMINATION)
        # =========================================================
        print("\n✅ System is fully operational! Press Ctrl+C in this terminal to shut down everything.")
        p_scrapper.wait()
        p_streamlit.wait()

    except KeyboardInterrupt:
        # =========================================================
        # GRACEFUL SHUTDOWN SEQUENCE
        # =========================================================
        print("\n\n🛑 Stop signal received. Shutting down all services...")
        for p in processes:
            p.terminate()
            p.wait() # Ensure the process is completely dead before exiting
        print("System successfully terminated!")
        sys.exit(0)

if __name__ == "__main__":
    main()