import pandas as pd
import streamlit as st
import plotly.graph_objects as go
import json
import time
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
ARCHIVE_DATA_DIR = BASE_DIR / "data" / "archive"

SENSOR_FILES = {
    "ds18b20": LIVE_DATA_DIR / "ds18b20.csv",
    "dht11": LIVE_DATA_DIR / "dht11.csv",
    "dht22": LIVE_DATA_DIR / "dht22.csv",
    "bme280": LIVE_DATA_DIR / "bme280.csv"
}

CONNECTIVITY_FILE = LIVE_DATA_DIR / "connectivity.csv"
BME280_KALMAN_FILE = LIVE_DATA_DIR / "bme280_kalman.csv"
DHT22_BATTERY_FILE = LIVE_DATA_DIR / "dht22_battery.csv"
DHT11_FUZZY_FILE = LIVE_DATA_DIR / "dht11_fuzzy.csv"

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

def parse_live_time_column(time_values):
    """
    Live CSV files store only HH:MM:SS. If pandas parses that directly as a
    datetime, it uses 1900-01-01 as the missing date. Attach today's date so
    the dashboard timestamps represent the actual live run.
    """
    today = pd.Timestamp.now().normalize()
    elapsed_time = pd.to_timedelta(time_values.astype(str).str.strip(), errors="coerce")
    return today + elapsed_time

def dashboard_data_path(live_path):
    """
    Prefer today's archive for dashboard plots so a same-day run is not limited
    by the rolling 1,000-row live CSV files. Fall back to live data while the
    archive file does not exist yet.
    """
    live_path = Path(live_path)
    today_folder = pd.Timestamp.now().strftime("%Y-%m-%d")
    archive_path = ARCHIVE_DATA_DIR / today_folder / live_path.name

    if archive_path.exists():
        return archive_path
    return live_path

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

def load_live_value_file(path, value_column):
    path = dashboard_data_path(path)

    if not path.exists():
        return None

    try:
        df = pd.read_csv(path, on_bad_lines='skip')
        if not {"time", value_column}.issubset(df.columns):
            return None

        df = df.dropna(subset=["time", value_column]).copy()
        df[value_column] = pd.to_numeric(df[value_column], errors="coerce")
        df["time_dt"] = parse_live_time_column(df["time"])
        df = df.dropna(subset=[value_column, "time_dt"])
        df = df.sort_values("time_dt")

        if df.empty:
            return None
        return df
    except Exception as e:
        st.error(f"Failed to read {path}: {e}")
        return None

def load_live_multi_value_file(path, value_columns):
    path = dashboard_data_path(path)

    if not path.exists():
        return None

    required_columns = {"time", *value_columns}
    try:
        df = pd.read_csv(path, on_bad_lines='skip')
        if not required_columns.issubset(df.columns):
            return None

        df = df.dropna(subset=list(required_columns)).copy()
        for value_column in value_columns:
            df[value_column] = pd.to_numeric(df[value_column], errors="coerce")

        df["time_dt"] = parse_live_time_column(df["time"])
        df = df.dropna(subset=[*value_columns, "time_dt"])
        df = df.sort_values("time_dt")

        if df.empty:
            return None
        return df
    except Exception as e:
        st.error(f"Failed to read {path}: {e}")
        return None

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

    if BME280_KALMAN_FILE.exists():
        with open(BME280_KALMAN_FILE, "w", encoding="utf-8") as f:
            f.write("time,temp\n")

    if DHT22_BATTERY_FILE.exists():
        with open(DHT22_BATTERY_FILE, "w", encoding="utf-8") as f:
            f.write("time,battery\n")

    if DHT11_FUZZY_FILE.exists():
        with open(DHT11_FUZZY_FILE, "w", encoding="utf-8") as f:
            f.write("time,normal,waspada,siaga,bahaya\n")

# =========================================================
# SIDEBAR CONTROL PANEL
# =========================================================
with st.sidebar:
    st.header("Control Panel")
    st.write("Manage your dashboard display settings here.")

    st.divider()

    st.subheader("Chart Style")
    temperature_plot_style = st.radio(
        "Temperature History",
        ["Lines", "Dots"],
        horizontal=True
    )
    
    st.divider()
    
    st.subheader("Data Management")
    if st.button("Clear Live Graph", width="stretch", help="Wipes the current dashboard view. Does not affect permanent archive data."):
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
    sensor_data[sensor_name] = load_live_value_file(filename, "temp")

bme280_kalman_data = load_live_value_file(BME280_KALMAN_FILE, "temp")
dht22_battery_data = load_live_value_file(DHT22_BATTERY_FILE, "battery")
dht11_fuzzy_data = load_live_multi_value_file(
    DHT11_FUZZY_FILE,
    ["normal", "waspada", "siaga", "bahaya"]
)

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
    
    temp_series = df.groupby("time_dt")["temp"].last()
    temp_series.name = sensor_name.upper()
    
    if chart_df.empty:
        chart_df = pd.DataFrame(temp_series)
    else:
        chart_df = chart_df.join(temp_series, how="outer")

if bme280_kalman_data is not None:
    kalman_series = bme280_kalman_data.groupby("time_dt")["temp"].last()
    kalman_series.name = "BME280 KALMAN"
    if chart_df.empty:
        chart_df = pd.DataFrame(kalman_series)
    else:
        chart_df = chart_df.join(kalman_series, how="outer")

if not chart_df.empty:
    temperature_fig = go.Figure()

    if temperature_plot_style == "Lines":
        plotted_df = chart_df.ffill()
        for sensor_name in plotted_df.columns:
            temperature_fig.add_trace(go.Scatter(
                x=plotted_df.index,
                y=plotted_df[sensor_name],
                mode="lines",
                name=sensor_name
            ))
    else:
        for sensor_name, df in sensor_data.items():
            if df is None:
                continue

            temperature_fig.add_trace(go.Scatter(
                x=df["time_dt"],
                y=df["temp"],
                mode="markers",
                name=sensor_name.upper(),
                marker=dict(size=7)
            ))

        if bme280_kalman_data is not None:
            temperature_fig.add_trace(go.Scatter(
                x=bme280_kalman_data["time_dt"],
                y=bme280_kalman_data["temp"],
                mode="markers",
                name="BME280 KALMAN",
                marker=dict(size=8, symbol="diamond")
            ))

    temperature_fig.update_layout(
        xaxis_title="Time",
        yaxis_title="Temperature (C)",
        xaxis=dict(type="date", tickformat="%H:%M:%S"),
        margin=dict(l=0, r=0, t=10, b=0),
        height=400,
        legend=dict(orientation="h", yanchor="bottom", y=1.02, xanchor="right", x=1)
    )

    st.plotly_chart(temperature_fig, width="stretch")
else:
    st.info("No live data available. Waiting for incoming telemetry...")

st.divider()

# =========================================================
# DHT22 BATTERY GRAPH
# =========================================================
st.header("DHT22 Battery")

if dht22_battery_data is not None:
    battery_fig = go.Figure()
    battery_fig.add_trace(go.Scatter(
        x=dht22_battery_data["time_dt"],
        y=dht22_battery_data["battery"],
        mode="lines+markers",
        name="DHT22 Battery",
        marker=dict(size=7)
    ))

    battery_fig.update_layout(
        xaxis_title="Time",
        yaxis_title="Battery (%)",
        xaxis=dict(type="date", tickformat="%H:%M:%S"),
        yaxis=dict(range=[0, 100], ticksuffix="%"),
        margin=dict(l=0, r=0, t=10, b=0),
        height=260,
        showlegend=False
    )

    st.plotly_chart(battery_fig, width="stretch")
    st.caption(f"Last DHT22 battery update: {dht22_battery_data.iloc[-1]['time']}")
else:
    st.info("No DHT22 battery data available yet.")

st.divider()

# =========================================================
# DHT11 FUZZY STATUS GRAPH
# =========================================================
st.header("DHT11 Fuzzy Status")

if dht11_fuzzy_data is not None:
    fuzzy_fig = go.Figure()
    fuzzy_columns = [
        ("normal", "Normal"),
        ("waspada", "Waspada"),
        ("siaga", "Siaga"),
        ("bahaya", "Bahaya")
    ]

    for column_name, label in fuzzy_columns:
        fuzzy_fig.add_trace(go.Scatter(
            x=dht11_fuzzy_data["time_dt"],
            y=dht11_fuzzy_data[column_name],
            mode="lines",
            name=label
        ))

    fuzzy_fig.update_layout(
        xaxis_title="Time",
        yaxis_title="Membership",
        xaxis=dict(type="date", tickformat="%H:%M:%S"),
        yaxis=dict(range=[0, 1]),
        margin=dict(l=0, r=0, t=10, b=0),
        height=300,
        legend=dict(orientation="h", yanchor="bottom", y=1.02, xanchor="right", x=1)
    )

    st.plotly_chart(fuzzy_fig, width="stretch")
    st.caption(f"Last DHT11 fuzzy update: {dht11_fuzzy_data.iloc[-1]['time']}")
else:
    st.info("No DHT11 fuzzy data available yet.")

st.divider()

# =========================================================
# MESH CONNECTIVITY GRAPH (FAST PLOTLY RENDER)
# =========================================================
st.header("Mesh Connectivity")

connectivity_path = dashboard_data_path(CONNECTIVITY_FILE)

if not connectivity_path.exists():
    st.warning(f"Connectivity file not found: {CONNECTIVITY_FILE}")
else:
    try:
        conn_df = pd.read_csv(connectivity_path, on_bad_lines='skip')

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

                    st.plotly_chart(fig, width="stretch")
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
                st.dataframe(df.tail(10), width="stretch")

        st.markdown("**BME280 KALMAN**")
        if bme280_kalman_data is None:
            st.write("No live data available.")
        else:
            st.dataframe(bme280_kalman_data.tail(10), width="stretch")

        st.markdown("**DHT22 BATTERY**")
        if dht22_battery_data is None:
            st.write("No live data available.")
        else:
            st.dataframe(dht22_battery_data.tail(10), width="stretch")

        st.markdown("**DHT11 FUZZY**")
        if dht11_fuzzy_data is None:
            st.write("No live data available.")
        else:
            st.dataframe(dht11_fuzzy_data.tail(10), width="stretch")

with col2:
    with st.expander("View Raw Connectivity Data"):
        if connectivity_path.exists():
            try:
                conn_df = pd.read_csv(connectivity_path)
                if conn_df.empty:
                    st.write("No live data available.")
                else:
                    st.dataframe(conn_df.tail(15), width="stretch")
            except Exception as e:
                st.error(e)
        else:
            st.write("Connectivity data file is missing.")

# If streamlit-autorefresh is not installed in the active Python environment,
# fall back to Streamlit's built-in rerun mechanism so live CSV updates still
# appear without manually refreshing the browser.
if st_autorefresh is None:
    time.sleep(1)
    st.rerun()
