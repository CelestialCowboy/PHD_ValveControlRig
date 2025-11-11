import tkinter as tk
from tkinter import ttk, scrolledtext
import serial
import serial.tools.list_ports
import threading
import time
import queue

class PressureGUI:
    def __init__(self, root):
        self.root = root
        self.root.title("Pressure Control - REAL-TIME")
        self.root.geometry("680x580")

        self.ser = None
        self.connect_port = tk.StringVar()
        self.baud_rate = 115200
        self.data_queue = queue.Queue()

        self.last_pressures = ["0.00"] * 6

        self.create_widgets()

        # Start reader thread
        self.reader = threading.Thread(target=self.read_serial, daemon=True)
        self.reader.start()

        # Update GUI every 50ms
        self.root.after(50, self.update_display)

    def create_widgets(self):
        # === Connection Frame ===
        conn = ttk.LabelFrame(self.root, text="Serial Connection", padding=10)
        conn.pack(fill="x", padx=10, pady=5)

        ttk.Label(conn, text="Port:").grid(row=0, column=0, sticky="w")
        ports = [p.device for p in serial.tools.list_ports.comports()]
        port_cb = ttk.Combobox(conn, textvariable=self.connect_port, values=ports, width=25)
        port_cb.grid(row=0, column=1, padx=5)

        # Connect Button
        ttk.Button(conn, text="Connect", command=self.connect).grid(row=0, column=2, padx=5)

        # Disconnect Button (initially disabled)
        self.disconnect_btn = ttk.Button(conn, text="Disconnect", command=self.disconnect, state="disabled")
        self.disconnect_btn.grid(row=0, column=3, padx=5)

        # Status Label
        self.status = ttk.Label(conn, text="Disconnected", foreground="red")
        self.status.grid(row=0, column=4, padx=10)

        # === Live Pressures ===
        pres = ttk.LabelFrame(self.root, text="Live Pressures (psi)", padding=15)
        pres.pack(fill="x", padx=10, pady=10)

        self.p_labels = []
        for i in range(6):
            frame = ttk.Frame(pres)
            frame.grid(row=i//3, column=i%3, padx=15, pady=8)

            ttk.Label(frame, text=f"P{i+1}", font=("Arial", 10)).pack()
            lbl = ttk.Label(frame, text="0.00", font=("Consolas", 18, "bold"),
                            foreground="#0066cc", width=8)
            lbl.pack()
            self.p_labels.append(lbl)

        # === Command Input ===
        cmd = ttk.LabelFrame(self.root, text="Control", padding=10)
        cmd.pack(fill="x", padx=10, pady=5)

        self.cmd_entry = ttk.Entry(cmd, width=25, font=("Arial", 10))
        self.cmd_entry.grid(row=0, column=0, padx=5)
        self.cmd_entry.bind("<Return>", lambda e: self.send())

        ttk.Button(cmd, text="Send", command=self.send).grid(row=0, column=1, padx=5)
        ttk.Button(cmd, text="STOP ALL", command=self.stop_all,
                   style="Danger.TButton").grid(row=0, column=2, padx=5)

        style = ttk.Style()
        style.configure("Danger.TButton", foreground="white", background="#d9534f", font=("Arial", 9, "bold"))

        # === Event Log ===
        log = ttk.LabelFrame(self.root, text="Event Log", padding=10)
        log.pack(fill="both", expand=True, padx=10, pady=5)

        self.log = scrolledtext.ScrolledText(log, height=8, font=("Consolas", 9))
        self.log.pack(fill="both", expand=True)

    # ------------------------------------------------------------------ #
    def connect(self):
        port = self.connect_port.get()
        if not port:
            self.event_log("Select a port first!")
            return

        # Close existing connection if any
        if self.ser and self.ser.is_open:
            self.ser.close()

        try:
            self.ser = serial.Serial(
                port=port,
                baudrate=self.baud_rate,
                timeout=1,
                rtscts=False,
                dsrdtr=False,
                xonxoff=False
            )
            time.sleep(2)  # Allow Arduino reset
            self.ser.flushInput()
            self.ser.flushOutput()

            self.status.config(text="Connected", foreground="green")
            self.disconnect_btn.config(state="normal")
            self.event_log(f"Connected: {port}")
        except Exception as e:
            self.ser = None
            self.status.config(text="Connect Failed", foreground="red")
            self.disconnect_btn.config(state="disabled")
            self.event_log(f"Connect error: {e}")

    # ------------------------------------------------------------------ #
    def disconnect(self):
        if self.ser and self.ser.is_open:
            try:
                self.ser.close()
            except:
                pass
        self.ser = None
        self.status.config(text="Disconnected", foreground="red")
        self.disconnect_btn.config(state="disabled")
        self.event_log("Disconnected from serial port")

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
                    pass  # Port closed or error
            time.sleep(0.001)

    # ------------------------------------------------------------------ #
    def update_display(self):
        while not self.data_queue.empty():
            line = self.data_queue.get_nowait()

            # Update pressure labels if valid 6-value line
            parts = line.split('\t')
            if len(parts) == 6 and all(p.replace('.', '').replace('-', '').replace('+', '').isdigit() for p in parts):
                for i, val in enumerate(parts):
                    val = val.strip()
                    if self.last_pressures[i] != val:
                        self.last_pressures[i] = val
                        self.p_labels[i].config(text=val)
                continue

            # Log only events
            if any(k in line for k in ["SET:", "DONE:", "ERR:", "STOP:", "OK:", ">"]):
                self.event_log(line)

        self.root.after(50, self.update_display)

    # ------------------------------------------------------------------ #
    def send(self):
        if not self.ser or not self.ser.is_open:
            self.event_log("Not connected!")
            return
        cmd = self.cmd_entry.get().strip()
        if not cmd:
            return
        try:
            self.ser.write(f"{cmd}\n".encode())
            self.event_log(f"> {cmd}")
            self.cmd_entry.delete(0, tk.END)
        except Exception as e:
            self.event_log(f"Send failed: {e}")

    def stop_all(self):
        if not self.ser or not self.ser.is_open:
            self.event_log("Not connected!")
            return
        try:
            self.ser.write(b"stop\n")
            self.event_log(">>> STOP ALL MOTORS")
        except Exception as e:
            self.event_log(f"Stop failed: {e}")

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