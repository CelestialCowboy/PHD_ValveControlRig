import tkinter as tk
from tkinter import ttk, scrolledtext, messagebox, filedialog
import serial
import serial.tools.list_ports
import threading
import time
import queue
import numpy as np
import csv
from datetime import datetime
from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg
from matplotlib.figure import Figure

class PressureGUI:
    def __init__(self, root):
        self.root = root
        self.root.title("Pressure Control - 60s + PAUSE + EXPORT")
        self.root.geometry("1050x1000")

        self.ser = None
        self.connect_port = tk.StringVar()
        self.baud_rate = 115200
        self.data_queue = queue.Queue()

        self.last_pressures = ["0.00"] * 6
        self.step_size = tk.IntVar(value=100)
        self.setpoint_vars = [tk.DoubleVar(value=0.0) for _ in range(6)]

        # === Graph Data (60 seconds @ ~10 Hz) ===
        self.history_len = 600  # 60 seconds
        self.time_data = np.linspace(-60, 0, self.history_len)
        self.pressure_data = np.zeros((6, self.history_len))

        # === Live Capture Control ===
        self.live_capture = True  # True = updating, False = paused
        self.paused_data = None  # Snapshot when paused

        # === Export Title ===
        self.export_title_var = tk.StringVar(value="Pressure Test")

        self.create_widgets()

        self.reader = threading.Thread(target=self.read_serial, daemon=True)
        self.reader.start()
        self.root.after(50, self.update_display)

    def create_widgets(self):
        # === LEFT-ALIGNED CONTENT CONTAINER ===
        content = ttk.Frame(self.root)
        content.pack(fill="both", expand=True, padx=25, pady=5)

        # === Connection ===
        conn = ttk.LabelFrame(content, text="Serial Connection", padding=10)
        conn.pack(fill="x", pady=5)
        ttk.Label(conn, text="Port:").grid(row=0, column=0, sticky="w")
        ports = [p.device for p in serial.tools.list_ports.comports()]
        port_cb = ttk.Combobox(conn, textvariable=self.connect_port, values=ports, width=25)
        port_cb.grid(row=0, column=1, padx=5)
        ttk.Button(conn, text="Connect", command=self.connect).grid(row=0, column=2, padx=5)
        self.disconnect_btn = ttk.Button(conn, text="Disconnect", command=self.disconnect, state="disabled")
        self.disconnect_btn.grid(row=0, column=3, padx=5)
        self.status = ttk.Label(conn, text="Disconnected", foreground="red")
        self.status.grid(row=0, column=4, padx=10)

        # === Pressure Control Panel - 3x2 ===
        setp = ttk.LabelFrame(content, text="Pressure Control (psi)", padding=14)
        setp.pack(fill="x", pady=10)
        self.setpoint_entries = []
        self.current_labels = []
        self.set_buttons = []
        for i in range(3):
            self._add_pressure_control(setp, i, row=0, col=i)
        for i in range(3, 6):
            self._add_pressure_control(setp, i, row=1, col=i-3)

        # === Motor Jog Panel ===
        jog = ttk.LabelFrame(content, text="Manual Motor Control", padding=12)
        jog.pack(fill="x", pady=5)
        step_frame = ttk.Frame(jog)
        step_frame.pack(pady=(0, 8))
        ttk.Label(step_frame, text="Steps:").pack(side="left", padx=5)
        ttk.Entry(step_frame, textvariable=self.step_size, width=8, font=("Arial", 10)).pack(side="left", padx=5)
        motor_grid = ttk.Frame(jog)
        motor_grid.pack()
        self.motor_fwd_btns = []
        self.motor_bwd_btns = []
        for i in range(6):
            col = i
            ttk.Label(motor_grid, text=f"M{i+1}", font=("Arial", 9, "bold")).grid(row=0, column=col, pady=(0, 3))
            fwd = ttk.Button(motor_grid, text="Forward", width=10, command=lambda m=i: self.move_motor(m, True))
            fwd.grid(row=1, column=col, padx=4, pady=2)
            self.motor_fwd_btns.append(fwd)
            bwd = ttk.Button(motor_grid, text="Backward", width=10, command=lambda m=i: self.move_motor(m, False))
            bwd.grid(row=2, column=col, padx=4, pady=2)
            self.motor_bwd_btns.append(bwd)

        # === Command Line ===
        cmd = ttk.LabelFrame(content, text="Command Line", padding=10)
        cmd.pack(fill="x", pady=5)
        self.cmd_entry = ttk.Entry(cmd, width=30, font=("Arial", 10))
        self.cmd_entry.grid(row=0, column=0, padx=5)
        self.cmd_entry.bind("<Return>", lambda e: self.send())
        ttk.Button(cmd, text="Send", command=self.send).grid(row=0, column=1, padx=5)
        ttk.Button(cmd, text="STOP ALL", command=self.stop_all, style="Danger.TButton").grid(row=0, column=2, padx=5)
        style = ttk.Style()
        style.configure("Danger.TButton", foreground="white", background="#d9534f", font=("Arial", 9, "bold"))

        # === REAL-TIME GRAPH + CONTROLS ===
        graph_frame = ttk.LabelFrame(content, text="Pressure Over Time (Last 60s)", padding=10)
        graph_frame.pack(fill="both", expand=True, pady=5)

        # Graph controls
        ctrl_frame = ttk.Frame(graph_frame)
        ctrl_frame.pack(fill="x", pady=(0, 5))

        self.pause_btn = ttk.Button(ctrl_frame, text="Pause", command=self.toggle_pause)
        self.pause_btn.pack(side="left", padx=5)

        ttk.Label(ctrl_frame, text="Title:").pack(side="left", padx=(20, 5))
        title_entry = ttk.Entry(ctrl_frame, textvariable=self.export_title_var, width=25)
        title_entry.pack(side="left", padx=5)

        export_btn = ttk.Button(ctrl_frame, text="Export CSV", command=self.export_csv)
        export_btn.pack(side="right", padx=5)

        # Graph
        self.fig = Figure(figsize=(10, 4.5), dpi=100)
        self.ax = self.fig.add_subplot(111)
        self.ax.set_ylim(0, 15)
        self.ax.set_xlim(-60, 0)
        self.ax.set_xlabel("Time (s)")
        self.ax.set_ylabel("Pressure (psi)")
        self.ax.grid(True, alpha=0.3)

        colors = ['#e6194B', '#3cb44b', '#ffe119', '#4363d8', '#f58231', '#911eb4']
        self.lines = []
        for i in range(6):
            line, = self.ax.plot(self.time_data, self.pressure_data[i], label=f"P{i+1}", color=colors[i], linewidth=2)
            self.lines.append(line)
        self.ax.legend(loc='upper left', fontsize=8)

        self.canvas = FigureCanvasTkAgg(self.fig, graph_frame)
        self.canvas.get_tk_widget().pack(fill="both", expand=True)

        # === Event Log ===
        log = ttk.LabelFrame(content, text="Event Log", padding=10)
        log.pack(fill="both", expand=True, pady=5)
        self.log = scrolledtext.ScrolledText(log, height=6, font=("Consolas", 9))
        self.log.pack(fill="both", expand=True)

        self.update_all_button_states()

    # ------------------------------------------------------------------ #
    def _add_pressure_control(self, parent, idx, row, col):
        ttk.Label(parent, text=f"P{idx+1}", font=("Arial", 10, "bold")).grid(
            row=row*3, column=col, pady=(0, 5), padx=10)
        ctrl_frame = ttk.Frame(parent)
        ctrl_frame.grid(row=row*3 + 1, column=col, padx=10, pady=3)
        entry = ttk.Entry(ctrl_frame, textvariable=self.setpoint_vars[idx], width=6, font=("Arial", 10))
        entry.pack(side="left")
        self.setpoint_entries.append(entry)
        ttk.Label(ctrl_frame, text="→", font=("Arial", 10)).pack(side="left", padx=2)
        curr_lbl = ttk.Label(ctrl_frame, text="0.00", width=8, font=("Consolas", 12, "bold"), foreground="#0066cc")
        curr_lbl.pack(side="left")
        self.current_labels.append(curr_lbl)
        btn = ttk.Button(parent, text="Set", width=6, command=lambda i=idx: self.set_pressure(i))
        btn.grid(row=row*3 + 2, column=col, padx=10, pady=2)
        self.set_buttons.append(btn)

    # ------------------------------------------------------------------ #
    def update_all_button_states(self):
        state = "normal" if (self.ser and self.ser.is_open) else "disabled"
        for btn in self.motor_fwd_btns + self.motor_bwd_btns + self.set_buttons:
            btn.config(state=state)
        self.pause_btn.config(state="normal" if self.ser else "disabled")

    # ------------------------------------------------------------------ #
    def toggle_pause(self):
        self.live_capture = not self.live_capture
        self.pause_btn.config(text="Resume" if not self.live_capture else "Pause")
        if not self.live_capture:
            # Snapshot current data
            self.paused_data = self.pressure_data.copy()
            self.event_log("Graph PAUSED")
        else:
            self.event_log("Graph RESUMED")

    # ------------------------------------------------------------------ #
    def export_csv(self):
        if self.pressure_data is None:
            messagebox.showwarning("No Data", "No pressure data to export.")
            return

        # Get title and timestamp
        title = self.export_title_var.get().strip()
        if not title:
            title = "Pressure_Data"
        timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        filename = f"{title}_{timestamp}.csv"

        file_path = filedialog.asksaveasfilename(
            defaultextension=".csv",
            filetypes=[("CSV files", "*.csv")],
            initialfile=filename
        )
        if not file_path:
            return

        try:
            with open(file_path, 'w', newline='') as f:
                writer = csv.writer(f)
                # Header
                header = ["Time (s)"] + [f"P{i+1} (psi)" for i in range(6)]
                writer.writerow(header)
                # Data
                data_to_export = self.paused_data if (not self.live_capture and self.paused_data is not None) else self.pressure_data
                for i in range(self.history_len):
                    row = [f"{self.time_data[i]:.1f}"] + [f"{data_to_export[j, i]:.3f}" for j in range(6)]
                    writer.writerow(row)
            self.event_log(f"Exported: {file_path}")
            messagebox.showinfo("Success", f"Data exported to:\n{file_path}")
        except Exception as e:
            messagebox.showerror("Export Failed", f"Error: {e}")

    # ------------------------------------------------------------------ #
    def set_pressure(self, idx):
        if not self.ser or not self.ser.is_open:
            self.event_log("Not connected!")
            return
        try:
            target = self.setpoint_vars[idx].get()
            if not (0.25 <= target <= 12.5):
                messagebox.showerror("Invalid", f"P{idx+1}: 0.25–12.5 psi")
                return
            cmd = f"P{idx+1}-{target:.2f}"
            self.ser.write(f"{cmd}\n".encode())
            self.event_log(f"> {cmd}")
        except Exception as e:
            self.event_log(f"Set failed: {e}")

    # ------------------------------------------------------------------ #
    def move_motor(self, motor_idx, forward):
        if not self.ser or not self.ser.is_open:
            self.event_log("Not connected!")
            return
        steps = self.step_size.get()
        if steps <= 0:
            self.event_log("Steps > 0")
            return
        cmd = f"M{motor_idx + 1}{'+' if forward else '-'}{steps}"
        self.ser.write(f"{cmd}\n".encode())
        self.event_log(f"> {cmd}")

    # ------------------------------------------------------------------ #
    def connect(self):
        port = self.connect_port.get()
        if not port:
            self.event_log("Select port!")
            return
        if self.ser and self.ser.is_open:
            self.ser.close()
        try:
            self.ser = serial.Serial(port, self.baud_rate, timeout=1,
                                     rtscts=False, dsrdtr=False, xonxoff=False)
            time.sleep(2)
            self.ser.flushInput()
            self.ser.flushOutput()
            self.status.config(text="Connected", foreground="green")
            self.disconnect_btn.config(state="normal")
            self.event_log(f"Connected: {port}")
            self.update_all_button_states()
        except Exception as e:
            self.ser = None
            self.status.config(text="Failed", foreground="red")
            self.disconnect_btn.config(state="disabled")
            self.event_log(f"Error: {e}")
            self.update_all_button_states()

    # ------------------------------------------------------------------ #
    def disconnect(self):
        if self.ser and self.ser.is_open:
            self.ser.close()
        self.ser = None
        self.status.config(text="Disconnected", foreground="red")
        self.disconnect_btn.config(state="disabled")
        self.event_log("Disconnected")
        self.update_all_button_states()

    # ------------------------------------------------------------------ #
    def read_serial(self):
        buffer = ""
        while True:
            if self.ser and self.ser.is_open:
                try:
                    if self.ser.in_waiting > 0:
                        raw = self.ser.read(self.ser.in_waiting)
                        buffer += raw.decode('utf-8', errors='replace')
                        while '\n' in buffer:
                            line, buffer = buffer.split('\n', 1)
                            line = line.strip()
                            if line:
                                self.data_queue.put(line)
                except:
                    pass
            time.sleep(0.001)

    # ------------------------------------------------------------------ #
    def update_display(self):
        updated = False
        while not self.data_queue.empty():
            line = self.data_queue.get_nowait()

            parts = line.split('\t')
            if len(parts) == 6:
                try:
                    vals = [float(p.strip()) for p in parts]
                    if self.live_capture:
                        # Shift and add new data
                        self.pressure_data = np.roll(self.pressure_data, -1, axis=1)
                        self.pressure_data[:, -1] = vals
                    # Always update live labels
                    for i, val in enumerate(vals):
                        formatted = f"{val:.2f}"
                        if self.last_pressures[i] != formatted:
                            self.last_pressures[i] = formatted
                            self.current_labels[i].config(text=formatted)
                    updated = True
                except:
                    pass
                continue

            if any(k in line for k in ["SET:", "DONE:", "ERR:", "STOP:", "OK:", "MOV:", ">"]):
                self.event_log(line)

        # Redraw graph
        if updated:
            data_to_plot = self.paused_data if (not self.live_capture and self.paused_data is not None) else self.pressure_data
            for i, line in enumerate(self.lines):
                line.set_ydata(data_to_plot[i])
            self.canvas.draw()

        self.root.after(50, self.update_display)

    # ------------------------------------------------------------------ #
    def send(self):
        if not self.ser or not self.ser.is_open:
            self.event_log("Not connected!")
            return
        cmd = self.cmd_entry.get().strip()
        if not cmd:
            return
        self.ser.write(f"{cmd}\n".encode())
        self.event_log(f"> {cmd}")
        self.cmd_entry.delete(0, tk.END)

    def stop_all(self):
        if not self.ser or not self.ser.is_open:
            self.event_log("Not connected!")
            return
        self.ser.write(b"stop\n")
        self.event_log(">>> STOP ALL MOTORS")

    def event_log(self, msg):
        ts = time.strftime("%H:%M:%S")
        self.log.insert(tk.END, f"{ts} | {msg}\n")
        self.log.see(tk.END)
        if self.log.count('1.0', 'end', 'lines')[0] > 200:
            self.log.delete('1.0', '50.0')

# ------------------------------------------------------------------ #
if __name__ == "__main__":
    root = tk.Tk()
    app = PressureGUI(root)
    root.mainloop()