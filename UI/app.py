import pandas as pd
import streamlit as st
import plotly.graph_objects as go
import json
import os
from pathlib import Path
try:
    from streamlit_autorefresh import st_autorefresh
except ModuleNotFoundError:
    st_autorefresh = None

# =========================================================
# PAGE CONFIGURATION & CONSTANTS
# =========================================================
st.set_page_config(
    page_title="Thermal Mesh Dashboard",
    page_icon="🌡️",
    layout="wide"
)

# Sesuaikan base directory agar mengarah ke Hot Storage (Live)
BASE_DIR = Path(__file__).resolve().parent
LIVE_DATA_DIR = BASE_DIR / "data" / "live"

SENSOR_FILES = {
    "ds18b20": LIVE_DATA_DIR / "ds18b20.csv",
    "dht11": LIVE_DATA_DIR / "dht11.csv",
    "dht22": LIVE_DATA_DIR / "dht22.csv",
    "bme280": LIVE_DATA_DIR / "bme280.csv"
}

CONNECTIVITY_FILE = LIVE_DATA_DIR / "connectivity.csv"

NODE_POSITIONS = {
    "receiver": {"x": 0.5, "y": 1.2},
    "ds18b20":  {"x": 0.5, "y": 0.85},
    "dht11":    {"x": 0.2, "y": 0.45},
    "dht22":    {"x": 0.8, "y": 0.45},
    "bme280":   {"x": 0.5, "y": 0.05}
}

EXPECTED_EDGES = [
    ("bme280", "dht11"),
    ("bme280", "dht22"),
    ("bme280", "ds18b20"),
    ("dht11", "dht22"),
    ("dht11", "ds18b20"),
    ("dht22", "ds18b20"),
    ("ds18b20", "receiver")
]

def parse_connectivity_array(raw_value):
    try:
        values = json.loads(str(raw_value))
    except json.JSONDecodeError:
        return None

    if not isinstance(values, list) or len(values) != len(EXPECTED_EDGES):
        return None

    parsed_values = []
    for value in values:
        if isinstance(value, bool):
            parsed_values.append(1 if value else 0)
            continue

        if isinstance(value, (int, float)) and int(value) in (0, 1):
            parsed_values.append(int(value))
            continue

        lowered_value = str(value).strip().lower()
        if lowered_value in ("1", "true"):
            parsed_values.append(1)
        elif lowered_value in ("0", "false"):
            parsed_values.append(0)
        else:
            return None

    return parsed_values

# =========================================================
# UTILITY: CLEAR LIVE DATA FUNCTION
# =========================================================
def clear_live_data():
    """Mengosongkan semua CSV di folder live, menyisakan header-nya saja."""
    if not LIVE_DATA_DIR.exists():
        return

    # Clear sensor files
    for sensor_name in SENSOR_FILES.keys():
        file_path = LIVE_DATA_DIR / f"{sensor_name}.csv"
        if file_path.exists():
            with open(file_path, "w", encoding="utf-8") as f:
                f.write("time,temp\n")
    
    # Clear connectivity file
    if CONNECTIVITY_FILE.exists():
        with open(CONNECTIVITY_FILE, "w", encoding="utf-8") as f:
            f.write("time,connectivity\n")

# =========================================================
# SIDEBAR CONTROL PANEL
# =========================================================
with st.sidebar:
    st.header("Control Panel")
    st.write("Manage your dashboard display settings here.")
    
    st.divider()
    
    st.subheader("Data Management")
    if st.button("Clear Live Graph", use_container_width=True, help="Wipes the current dashboard view. Does not affect permanent archive data."):
        clear_live_data()
        # Paksa Streamlit untuk memuat ulang halaman agar grafik langsung kosong
        st.rerun()

# =========================================================
# AUTO REFRESH CONFIGURATION
# =========================================================
if st_autorefresh is not None:
    st_autorefresh(interval=1000, key="refresh")

# =========================================================
# DASHBOARD HEADER
# =========================================================
st.title("🌡️ Thermal Mesh Monitoring Dashboard")

# =========================================================
# DATA LOADING
# =========================================================
sensor_data = {}

for sensor_name, filename in SENSOR_FILES.items():
    path = Path(filename)
    
    if not path.exists():
        sensor_data[sensor_name] = None
        continue

    try:
        df = pd.read_csv(filename, on_bad_lines='skip')
        if not {"time", "temp"}.issubset(df.columns):
            sensor_data[sensor_name] = None
            continue

        df = df.dropna(subset=["time", "temp"]).copy()
        df["temp"] = pd.to_numeric(df["temp"], errors="coerce")
        df = df.dropna(subset=["temp"])

        if df.empty:
            sensor_data[sensor_name] = None
        else:
            sensor_data[sensor_name] = df
    except Exception as e:
        st.error(f"Failed to read {path}: {e}")
        sensor_data[sensor_name] = None

# =========================================================
# METRIC CARDS (CURRENT TEMPERATURES)
# =========================================================
st.header("Current Temperatures")

cols = st.columns(len(SENSOR_FILES))

for i, (sensor_name, df) in enumerate(sensor_data.items()):
    with cols[i]:
        if df is None:
            st.metric(label=sensor_name.upper(), value="OFFLINE")
        else:
            latest_record = df.iloc[-1]
            current_temp = latest_record["temp"]
            last_update = latest_record["time"]
            
            st.metric(label=sensor_name.upper(), value=f"{current_temp:.1f} °C")
            st.caption(f"Last update: {last_update}")

st.divider()

# =========================================================
# TEMPERATURE HISTORY (TIME-SERIES CHART)
# =========================================================
st.header("Temperature History")

chart_df = pd.DataFrame()

for sensor_name, df in sensor_data.items():
    if df is None:
        continue
    
    temp_series = df.set_index("time")["temp"]
    temp_series.name = sensor_name.upper()
    
    if chart_df.empty:
        chart_df = pd.DataFrame(temp_series)
    else:
        chart_df = chart_df.join(temp_series, how="outer")

if not chart_df.empty:
    chart_df = chart_df.ffill()
    st.line_chart(chart_df)
else:
    st.info("No live data available. Waiting for incoming telemetry...")

st.divider()

# =========================================================
# MESH CONNECTIVITY GRAPH (FAST PLOTLY RENDER)
# =========================================================
st.header("Mesh Connectivity")

connectivity_path = Path(CONNECTIVITY_FILE)

if not connectivity_path.exists():
    st.warning(f"Connectivity file not found: {CONNECTIVITY_FILE}")
else:
    try:
        conn_df = pd.read_csv(CONNECTIVITY_FILE, on_bad_lines='skip')

        if not {"time", "connectivity"}.issubset(conn_df.columns):
            st.warning(f"Connectivity file has invalid columns.")
        elif conn_df.empty:
            st.info("Waiting for mesh connectivity data...")
        else:
            conn_df = conn_df.dropna(subset=["time", "connectivity"]).copy()

            if conn_df.empty:
                st.info("Waiting for mesh connectivity data...")
            else:
                latest_timestamp = None
                latest_connectivity = None
                for _, row in conn_df.iloc[::-1].iterrows():
                    parsed_connectivity = parse_connectivity_array(row["connectivity"])
                    if parsed_connectivity is not None:
                        latest_timestamp = row["time"]
                        latest_connectivity = parsed_connectivity
                        break

                if latest_connectivity is not None:
                    topology_rows = []
                    for edge_index, (source, target) in enumerate(EXPECTED_EDGES):
                        topology_rows.append({
                            "time": latest_timestamp,
                            "source": source,
                            "target": target,
                            "connected": latest_connectivity[edge_index]
                        })

                    topology_snapshot = pd.DataFrame(topology_rows)

                    fig = go.Figure()

                    # 1. Draw Network Edges (Connections)
                    for _, row in topology_snapshot.iterrows():
                        source = row["source"]
                        target = row["target"]
                        is_connected = int(row["connected"])

                        if source not in NODE_POSITIONS or target not in NODE_POSITIONS:
                            continue

                        x0, y0 = NODE_POSITIONS[source]["x"], NODE_POSITIONS[source]["y"]
                        x1, y1 = NODE_POSITIONS[target]["x"], NODE_POSITIONS[target]["y"]

                        line_color = "#00cc96" if is_connected == 1 else "#ff4b4b"
                        line_dash = "solid" if is_connected == 1 else "dash"
                        
                        fig.add_trace(go.Scatter(
                            x=[x0, x1], y=[y0, y1],
                            mode="lines",
                            line=dict(color=line_color, width=3, dash=line_dash),
                            hoverinfo="none",
                            showlegend=False
                        ))

                    # 2. Draw Network Nodes (Sensors & Receiver)
                    node_x = []
                    node_y = []
                    node_colors = []
                    node_labels = []

                    receiver_rows = topology_snapshot[
                        (topology_snapshot["source"] == "receiver") |
                        (topology_snapshot["target"] == "receiver")
                    ]
                    receiver_is_connected = not receiver_rows.empty and int(receiver_rows["connected"].max()) == 1

                    for sensor_name in NODE_POSITIONS.keys():
                        node_x.append(NODE_POSITIONS[sensor_name]["x"])
                        node_y.append(NODE_POSITIONS[sensor_name]["y"])

                        if sensor_name == "receiver":
                            node_colors.append("#00cc96" if receiver_is_connected else "#ff4b4b")
                            node_labels.append("<b>RECEIVER</b>")
                            continue
                        
                        df = sensor_data.get(sensor_name)
                        if df is None:
                            node_colors.append("#ff4b4b")
                            node_labels.append(f"<b>{sensor_name.upper()}</b><br>OFFLINE")
                        else:
                            current_temp = df.iloc[-1]["temp"]
                            node_colors.append("#00cc96")
                            node_labels.append(f"<b>{sensor_name.upper()}</b><br>{current_temp:.1f} °C")

                    fig.add_trace(go.Scatter(
                        x=node_x, y=node_y,
                        mode="markers+text",
                        marker=dict(size=45, color=node_colors, line=dict(width=2, color="white")),
                        text=node_labels,
                        textposition="bottom center",
                        textfont=dict(size=14, color="white"),
                        hoverinfo="text",
                        showlegend=False
                    ))

                    # 3. Layout Formatting
                    fig.update_layout(
                        plot_bgcolor="rgba(0,0,0,0)",
                        paper_bgcolor="rgba(0,0,0,0)",
                        xaxis=dict(showgrid=False, zeroline=False, showticklabels=False, range=[0, 1]),
                        yaxis=dict(showgrid=False, zeroline=False, showticklabels=False, range=[-0.1, 1.3]),
                        margin=dict(l=0, r=0, t=20, b=20),
                        height=450
                    )

                    st.plotly_chart(fig, use_container_width=True)
                    st.caption(f"Topology snapshot at time = {latest_timestamp}")

    except Exception as e:
        st.error(f"Error rendering connectivity graph: {e}")

st.divider()

# =========================================================
# RAW DATA DIAGNOSTICS
# =========================================================
st.header("Raw Data Diagnostics")

col1, col2 = st.columns(2)

with col1:
    with st.expander("View Raw Sensor Data"):
        for sensor_name, df in sensor_data.items():
            st.markdown(f"**{sensor_name.upper()}**")
            if df is None:
                st.write("No live data available.")
            else:
                st.dataframe(df.tail(10), use_container_width=True)

with col2:
    with st.expander("View Raw Connectivity Data"):
        if connectivity_path.exists():
            try:
                conn_df = pd.read_csv(CONNECTIVITY_FILE)
                if conn_df.empty:
                    st.write("No live data available.")
                else:
                    st.dataframe(conn_df.tail(15), use_container_width=True)
            except Exception as e:
                st.error(e)
        else:
            st.write("Connectivity data file is missing.")