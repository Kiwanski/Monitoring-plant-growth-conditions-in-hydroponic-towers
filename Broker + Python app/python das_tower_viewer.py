import json
import threading
import time
from pathlib import Path
from datetime import datetime, timedelta
import tkinter as tk
from tkinter import ttk, messagebox, filedialog

import pandas as pd
import matplotlib.pyplot as plt
import matplotlib.dates as mdates
from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg, NavigationToolbar2Tk

# ================= CONFIG =================

DATA_FILE = Path(r"C:\Users\msi\OneDrive - Akademia Górniczo-Hutnicza im. Stanisława Staszica w Krakowie\Praca Inżynierska Konrad Iwański\Broker + Python app\das_tower_data.json")
REFRESH_INTERVAL = 2.0 
MAX_POINTS = 5000

SENSORS = {
    "DS18B20 Water Temp (°C)": "temperature_ds18",
    "DHT22 Air Temp (°C)": "temperature_dht",
    "DHT22 Humidity (%)": "humidity",
    "Light BH1750 (lx)": "light",
    "pH": "ph",
    "Water Level (0/1)": "level",
    "Relay 1 (Pump)": "relay_1",
    "Relay 2 (LED)": "relay_2",
}

# ==========================================

class NDJSONReader(threading.Thread):
    def __init__(self, filepath, on_new_data):
        super().__init__(daemon=True)
        self.filepath = filepath
        self.on_new_data = on_new_data
        self._stop_event = threading.Event()
        self._offset = 0
        self._last_mtime = None

    def stop(self):
        self._stop_event.set()

    def run(self):
        while not self._stop_event.is_set():
            if not self.filepath.exists():
                time.sleep(REFRESH_INTERVAL)
                continue
            try:
                mtime = self.filepath.stat().st_mtime
                if self._last_mtime is None or mtime != self._last_mtime:
                    self._last_mtime = mtime
                    self.read_new_lines()
            except Exception:
                pass
            time.sleep(REFRESH_INTERVAL)

    def read_new_lines(self):
        try:
            with self.filepath.open("r", encoding="utf-8") as f:
                f.seek(self._offset)
                lines = f.readlines()
                self._offset = f.tell()

            records = []
            for line in lines:
                try:
                    line = line.strip()
                    if not line: continue
                    record = json.loads(line)
                    if "timestamp" in record:
                        # Wymuszamy format datetime
                        record["timestamp"] = pd.to_datetime(record["timestamp"])
                        records.append(record)
                except Exception:
                    continue
            if records:
                self.on_new_data(records)
        except Exception as e:
            print("File read error:", e)

class DasTowerApp:
    def __init__(self, root):
        self.root = root
        root.title("DAS Tower v3.4 – Full DateTime Fix")
        root.geometry("1200x850")

        self.df = pd.DataFrame()
        self.selected_sensor = tk.StringVar(value=list(SENSORS.keys())[0])
        
        # Zapamiętywanie zakresu osi
        self.manual_xlim = None

        self._build_ui()
        self._start_reader()

    def _build_ui(self):
        top = ttk.Frame(self.root)
        top.pack(fill=tk.X, padx=10, pady=5)

        ttk.Label(top, text="Sensor:").pack(side=tk.LEFT)
        combo = ttk.Combobox(top, textvariable=self.selected_sensor, values=list(SENSORS.keys()), state="readonly", width=30)
        combo.pack(side=tk.LEFT, padx=5)
        combo.bind("<<ComboboxSelected>>", lambda e: self.reset_view_and_refresh())

        ttk.Button(top, text="Refresh", command=self.refresh_plot).pack(side=tk.LEFT, padx=5)
        ttk.Button(top, text="Export PNG", command=self.export_png).pack(side=tk.LEFT, padx=5)
        
        self.big_value = ttk.Label(self.root, text="--", font=("Arial", 26, "bold"))
        self.big_value.pack(pady=5)

        self.chart_frame = ttk.Frame(self.root)
        self.chart_frame.pack(fill=tk.BOTH, expand=True)

        self.fig, self.ax = plt.subplots(figsize=(10, 5))
        self.canvas = FigureCanvasTkAgg(self.fig, master=self.chart_frame)
        self.canvas.get_tk_widget().pack(fill=tk.BOTH, expand=True)

        self.toolbar = NavigationToolbar2Tk(self.canvas, self.chart_frame)
        self.toolbar.update()

        self.status = ttk.Label(self.root, text="Waiting for data...")
        self.status.pack(side=tk.BOTTOM, pady=5)

        self.canvas.mpl_connect("motion_notify_event", self._on_mouse_move)
        
        self.v_line = None
        self.h_line = None
        self.annot = None

    def _start_reader(self):
        self.reader = NDJSONReader(DATA_FILE, self.on_new_data)
        self.reader.start()

    def on_new_data(self, records):
        new_df = pd.DataFrame(records)
        new_df["timestamp"] = pd.to_datetime(new_df["timestamp"])
        self.df = pd.concat([self.df, new_df], ignore_index=True)
        self.df = self.df.tail(MAX_POINTS)
        self.root.after(10, self.refresh_plot)

    def parse_relay_info(self, val):
        if not isinstance(val, dict): return False, 0, 0
        active = val.get("active", False)
        on_ms, off_ms = 0, 0
        for k, v in val.items():
            if k != "active":
                try: on_ms, off_ms = int(k), int(v); break
                except: continue
        return active, on_ms, off_ms

    def _plot_relay_timer(self, key):
        plot_times, plot_values = [], []
        actual_points_x, actual_points_y = [], []
        for i in range(len(self.df)):
            row = self.df.iloc[i]
            active, on_ms, off_ms = self.parse_relay_info(row[key])
            actual_points_x.append(row["timestamp"])
            actual_points_y.append(1 if active else 0)
            t_end = self.df.iloc[i+1]["timestamp"] if i + 1 < len(self.df) else row["timestamp"] + timedelta(minutes=30)
            if on_ms > 0 and off_ms > 0:
                curr_t = row["timestamp"]
                count = 0
                while curr_t < t_end and count < 1000:
                    count += 1
                    plot_times.append(curr_t); plot_values.append(1)
                    curr_t += timedelta(milliseconds=on_ms)
                    if curr_t >= t_end: break
                    plot_times.append(curr_t); plot_values.append(0)
                    curr_t += timedelta(milliseconds=off_ms)
                    if curr_t >= t_end: break
            else:
                plot_times.extend([row["timestamp"], t_end])
                plot_values.extend([1 if active else 0, 1 if active else 0])
        self.ax.step(plot_times, plot_values, where='post', color='#007acc', linewidth=1.5)
        self.ax.scatter(actual_points_x, actual_points_y, color='red', s=120, zorder=5, edgecolors='black')

    def reset_view_and_refresh(self):
        self.manual_xlim = None
        self.refresh_plot()

    def refresh_plot(self):
        if self.df.empty: return
        
        # Sprawdzamy stan zoomu przed czyszczeniem
        if self.toolbar.mode == '': # Tylko gdy nie jesteśmy w trybie lupy/panoramy
            self.manual_xlim = self.ax.get_xlim()

        sensor_display_name = self.selected_sensor.get()
        key = SENSORS[sensor_display_name]
        
        self.ax.clear()
        
        # KLUCZOWE: Wymuszenie osi czasu
        self.ax.xaxis_date()
        
        self.ax.set_title(f"Sensor: {sensor_display_name}", fontsize=14, fontweight='bold', pad=15)

        if "relay" in key:
            self._plot_relay_timer(key)
            self.ax.set_ylim(-0.2, 1.3)
            self.ax.set_yticks([0, 1])
            self.ax.set_yticklabels(["OFF", "ON"])
            active, _, _ = self.parse_relay_info(self.df[key].iloc[-1])
            self.big_value.config(text="ACTIVE" if active else "INACTIVE")
        else:
            data_to_plot = pd.to_numeric(self.df[key], errors='coerce')
            s_min, s_max = data_to_plot.min(), data_to_plot.max()
            
            # Statystyki w legendzie
            s_avg = data_to_plot.mean()
            legend_label = f"Min: {s_min:.1f} | Max: {s_max:.1f} | Avg: {s_avg:.1f}"

            # Rysowanie z jawnym podaniem osi X jako datetime
            self.ax.plot(self.df["timestamp"], data_to_plot, '-', color='#2ca02c', alpha=1.0, label=legend_label)
            self.ax.scatter(self.df["timestamp"], data_to_plot, color='red', s=80, zorder=5)
            self.ax.set_ylabel("Value")
            self.big_value.config(text=f"{self.df[key].iloc[-1]}")

            if not data_to_plot.dropna().empty:
                margin = (s_max - s_min) * 0.1 if s_max != s_min else 1.0
                self.ax.set_ylim(s_min - margin, s_max + margin)

        # Ustawianie zakresu osi X
        t_min, t_max = self.df["timestamp"].min(), self.df["timestamp"].max()
        
        # Jeśli brak manualnego zoomu, ustawiamy zakres automatycznie
        if self.manual_xlim is None or self.manual_xlim[0] < 1.0:
            self.ax.set_xlim(t_min - timedelta(minutes=15), t_max + timedelta(minutes=15))
        else:
            self.ax.set_xlim(self.manual_xlim)

        self.ax.legend(loc='upper right', frameon=True, shadow=True)
        
        # FORMATOWANIE OSI X: Data i Godzina
        # Jeśli zakres jest większy niż 1 dzień, pokazujemy datę
        if (t_max - t_min).days > 0:
            self.ax.xaxis.set_major_formatter(mdates.DateFormatter("%d.%m\n%H:%M:%S"))
        else:
            self.ax.xaxis.set_major_formatter(mdates.DateFormatter("%H:%M:%S"))
            
        self.ax.xaxis.set_major_locator(mdates.AutoDateLocator())
        
        self.ax.grid(True, linestyle=':', alpha=0.6)
        self.fig.autofmt_xdate()

        # Inicjalizacja celownika
        self.v_line = self.ax.axvline(color='grey', linestyle='--', linewidth=0.8, visible=False)
        self.h_line = self.ax.axhline(color='grey', linestyle='--', linewidth=0.8, visible=False)
        self.annot = self.ax.annotate("", xy=(0,0), xytext=(15,15), textcoords="offset points",
                                      bbox=dict(boxstyle="round", fc="white", ec="gray", alpha=0.9),
                                      arrowprops=dict(arrowstyle="->"))
        self.annot.set_visible(False)
        
        self.status.config(text=f"Last Sync: {t_max.strftime('%d.%m %H:%M:%S')} | Rows: {len(self.df)}")
        self.canvas.draw()

    def _on_mouse_move(self, event):
        if not event.inaxes or self.df.empty: return
        try:
            # Pobieramy datę z pozycji kursora
            mouse_dt = mdates.num2date(event.xdata).replace(tzinfo=None)
            
            # Szukamy najbliższego rekordu w czasie
            idx = (self.df['timestamp'] - mouse_dt).abs().idxmin()
            row = self.df.loc[idx]
            key = SENSORS[self.selected_sensor.get()]
            
            val = row[key]
            if isinstance(val, dict):
                active, _, _ = self.parse_relay_info(val)
                y_val, val_str = (1 if active else 0), f"STATE: {'ON' if active else 'OFF'}"
            else:
                y_val, val_str = val, f"VALUE: {val}"

            self.v_line.set_xdata([event.xdata]); self.v_line.set_visible(True)
            self.h_line.set_ydata([event.ydata]); self.h_line.set_visible(True)
            
            self.annot.xy = (mdates.date2num(row['timestamp']), y_val)
            # DODANO: Pełna data i godzina w dymku
            tooltip_text = f"Date: {row['timestamp'].strftime('%d.%m.%Y')}\nTime: {row['timestamp'].strftime('%H:%M:%S')}\n{val_str}"
            self.annot.set_text(tooltip_text)
            self.annot.set_visible(True)
            self.canvas.draw_idle()
        except Exception as e:
            pass

    def export_png(self):
        path = filedialog.asksaveasfilename(defaultextension=".png", filetypes=[("PNG", "*.png")])
        if path:
            if self.v_line: self.v_line.set_visible(False)
            if self.h_line: self.h_line.set_visible(False)
            if self.annot: self.annot.set_visible(False)
            self.fig.savefig(path, dpi=150)
            messagebox.showinfo("Export", "Saved successfully")

    def on_close(self):
        self.reader.stop()
        self.root.destroy()

if __name__ == "__main__":
    root = tk.Tk()
    app = DasTowerApp(root)
    root.protocol("WM_DELETE_WINDOW", app.on_close)
    root.mainloop()