import pandas as pd
import streamlit as st
import plotly.graph_objects as go
from pathlib import Path
from streamlit_autorefresh import st_autorefresh

# =========================================================
# PAGE CONFIGURATION & CONSTANTS
# =========================================================
st.set_page_config(
    page_title="Thermal Mesh Dashboard",
    page_icon="🌡️",
    layout="wide"
)

SENSOR_FILES = {
    "ds18b20": "./data/ds18b20.csv",
    "dht11": "./data/dht11.csv",
    "dht22": "./data/dht22.csv",
    "bme280": "./data/bme280.csv"
}

CONNECTIVITY_FILE = "./data/connectivity.csv"

# =========================================================
# AUTO REFRESH CONFIGURATION
# =========================================================
# Refresh the dashboard every 1000 milliseconds (1 second)
st_autorefresh(interval=1000, key="refresh")

# =========================================================
# DASHBOARD HEADER
# =========================================================
st.title("Thermal Mesh Monitoring Dashboard")

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
        # Use on_bad_lines='skip' to prevent crashes during concurrent read/write operations
        df = pd.read_csv(filename, on_bad_lines='skip')
        if df.empty:
            sensor_data[sensor_name] = None
        else:
            sensor_data[sensor_name] = df
    except Exception as e:
        st.error(f"Failed to read {filename}: {e}")
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
    
    # Set 'time' as the dataframe index to ensure correct X-axis synchronization
    temp_series = df.set_index("time")["temp"]
    temp_series.name = sensor_name.upper()
    
    if chart_df.empty:
        chart_df = pd.DataFrame(temp_series)
    else:
        # Outer join preserves timestamps across asynchronously updated sensors
        chart_df = chart_df.join(temp_series, how="outer")

if not chart_df.empty:
    st.line_chart(chart_df)
else:
    st.warning("No sensor data available for plotting.")

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

        if conn_df.empty:
            st.warning(f"Connectivity file is empty: {CONNECTIVITY_FILE}")
        else:
            latest_timestamp = conn_df["time"].iloc[-1]
            topology_snapshot = conn_df[conn_df["time"] == latest_timestamp]

            # Initialize Plotly Figure
            fig = go.Figure()

            # Define fixed logical coordinates for mesh nodes (Scale: 0.0 to 1.0)
            NODE_POSITIONS = {
                "ds18b20": {"x": 0.5, "y": 1.0},   # Top (Gateway)
                "dht11":   {"x": 0.2, "y": 0.5},   # Left Node
                "dht22":   {"x": 0.8, "y": 0.5},   # Right Node
                "bme280":  {"x": 0.5, "y": 0.0}    # Bottom Node
            }

            # 1. Draw Network Edges (Connections)
            for _, row in topology_snapshot.iterrows():
                source = row["source"]
                target = row["target"]
                is_connected = int(row["connected"])

                x0, y0 = NODE_POSITIONS[source]["x"], NODE_POSITIONS[source]["y"]
                x1, y1 = NODE_POSITIONS[target]["x"], NODE_POSITIONS[target]["y"]

                line_color = "#00cc96" if is_connected == 1 else "#ff4b4b"
                line_dash = "solid" if is_connected == 1 else "dash"
                
                fig.add_trace(go.Scatter(
                    x=[x0, x1], y=[y0, y1],
                    mode="lines",
                    line=dict(color=line_color, width=3, dash=line_dash),
                    hoverinfo="none", # Disable tooltip for lines
                    showlegend=False
                ))

            # 2. Draw Network Nodes (Sensors)
            node_x = []
            node_y = []
            node_colors = []
            node_labels = []

            for sensor_name in NODE_POSITIONS.keys():
                node_x.append(NODE_POSITIONS[sensor_name]["x"])
                node_y.append(NODE_POSITIONS[sensor_name]["y"])
                
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

            # 3. Clean up chart layout for dashboard aesthetics
            fig.update_layout(
                plot_bgcolor="rgba(0,0,0,0)",
                paper_bgcolor="rgba(0,0,0,0)",
                xaxis=dict(showgrid=False, zeroline=False, showticklabels=False, range=[0, 1]),
                yaxis=dict(showgrid=False, zeroline=False, showticklabels=False, range=[-0.2, 1.2]),
                margin=dict(l=0, r=0, t=20, b=20),
                height=400
            )

            # Render utilizing Streamlit's optimized Plotly component
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
                st.write("No data available.")
            else:
                # Display only the last 10 rows to maintain UI responsiveness
                st.dataframe(df.tail(10), use_container_width=True)

with col2:
    with st.expander("View Raw Connectivity Data"):
        if connectivity_path.exists():
            try:
                conn_df = pd.read_csv(CONNECTIVITY_FILE)
                # Display the last 15 recorded connections
                st.dataframe(conn_df.tail(15), use_container_width=True)
            except Exception as e:
                st.error(e)
        else:
            st.write("Connectivity data file is missing.")
