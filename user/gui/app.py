import tkinter as tk
from tkinter import ttk, messagebox
import os
import sys
import struct
import threading
import time
from collections import deque
import subprocess

# We use Matplotlib for the graph, and tkinter to embed the plot
# You may need to install these libraries: pip install matplotlib
try:
    import matplotlib.pyplot as plt
    from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg
    from matplotlib.figure import Figure
except ImportError:
    print("Error: Matplotlib not found. Please run 'pip install matplotlib'.")
    sys.exit(1)

# --- Configuration and Constants ---
DEVICE_PATH = "/dev/simtemp"
# Note: The sysfs path might vary depending on how the driver registers its class.
# Common paths for miscdevice: /sys/class/misc/simtemp or just /sys/module/nxp_simtemp/parameters/
SYSFS_BASE_PATH = "/sys/class/misc/simtemp/"
SYSFS_CONFIG_PATH = {
    "sampling_ms": SYSFS_BASE_PATH + "/sampling_ms",
    "threshold_mC": SYSFS_BASE_PATH + "/threshold_mC",
    "mode": SYSFS_BASE_PATH + "/mode",
}
# Binary record format: Q (u64 timestamp), i (s32 temp_mC), I (u32 flags)
# Must match the C struct simtemp_sample
STRUCT_FORMAT = 'QiI'
RECORD_SIZE = struct.calcsize(STRUCT_FORMAT)

# Flags (matching the kernel module definitions)
SIMTEMP_FLAG_NEW_SAMPLE = 0x01
SIMTEMP_FLAG_THRESHOLD_CROSSED = 0x02

# Data visualization settings
MAX_DATA_POINTS = 100 # Max points to show on the graph
INITIAL_THRESHOLD_mC = 45000


class SensorMonitorApp:
    def __init__(self, master):
        self.master = master
        master.title("NXP SimTemp Sensor Monitor")
        # Handle graceful shutdown for window close
        master.protocol("WM_DELETE_WINDOW", self.on_close)

        # Capture keyboard interrupt (Ctrl+C)
        master.bind('<Control-c>', lambda e: self.on_close())

        # State Variables
        self.running = threading.Event()
        self.running.set()
        self.temp_data = deque(maxlen=MAX_DATA_POINTS)
        self.time_data = deque(maxlen=MAX_DATA_POINTS)
        self.alert_status = tk.StringVar(value="OK")
        self.threshold_C = tk.DoubleVar(value=INITIAL_THRESHOLD_mC / 1000.0)
        self.sampling_ms = tk.IntVar(value=100)
        self.mode_var = tk.StringVar(value="normal")
        self.fatal_error = None # Channel for thread errors

        # UI Setup
        self.setup_ui(master)

        # Threading Setup
        self.read_thread = threading.Thread(target=self.read_device_loop)
        self.read_thread.daemon = True # Allows thread to exit when main thread exits
        self.read_thread.start()

        # Start UI updates
        self.update_plot()
        self.update_stats()

        # Initial config read (if module is already loaded)
        self.read_initial_config()

    def setup_ui(self, master):
        """Sets up the main application window layout."""
        # --- Main Layout Frames ---
        control_frame = ttk.Frame(master, padding="10")
        control_frame.pack(side=tk.LEFT, fill=tk.Y)

        graph_frame = ttk.Frame(master, padding="10")
        graph_frame.pack(side=tk.RIGHT, fill=tk.BOTH, expand=True)

        # --- Control Panel (Left) ---
        ttk.Label(control_frame, text="Sensor Controls", font=("Inter", 14, "bold")).pack(pady=10)

        # 1. Alert Status
        status_label = ttk.Label(control_frame, text="ALERT STATUS:", font=("Inter", 12))
        status_label.pack(pady=(10, 0))
        self.alert_display = ttk.Label(control_frame, textvariable=self.alert_status, font=("Inter", 18, "bold"), foreground="green")
        self.alert_display.pack()

        # 2. Threshold Control
        ttk.Label(control_frame, text="Threshold (°C):", font=("Inter", 12)).pack(pady=(10, 0))
        self.threshold_entry = ttk.Entry(control_frame, textvariable=self.threshold_C, width=10)
        self.threshold_entry.pack()
        ttk.Button(control_frame, text="Set Threshold", command=self.set_threshold).pack(pady=5)

        # 3. Sampling Control
        ttk.Label(control_frame, text="Sampling (ms):", font=("Inter", 12)).pack(pady=(10, 0))
        self.sampling_entry = ttk.Entry(control_frame, textvariable=self.sampling_ms, width=10)
        self.sampling_entry.pack()
        ttk.Button(control_frame, text="Set Sampling", command=self.set_sampling).pack(pady=5)

        # 4. Mode Selector
        ttk.Label(control_frame, text="Sensor Mode:", font=("Inter", 12)).pack(pady=(10, 0))
        mode_options = ["normal", "noisy", "ramp"]
        self.mode_selector = ttk.OptionMenu(control_frame, self.mode_var, self.mode_var.get(), *mode_options, command=self.set_mode)
        self.mode_selector.pack(pady=5)

        # --- Graph Panel (Right) ---
        self.fig = Figure(figsize=(7, 5), dpi=100)
        self.ax = self.fig.add_subplot(111)
        self.ax.set_title("Live Temperature Data")
        self.ax.set_ylabel("Temperature (°C)")
        self.ax.set_xlabel("Time (s)")

        self.line, = self.ax.plot([], [], label="Temperature", color="#1f77b4")
        self.threshold_line = self.ax.axhline(self.threshold_C.get(), color='r', linestyle='--', label="Threshold")
        self.ax.legend(loc='upper left')

        self.canvas = FigureCanvasTkAgg(self.fig, master=graph_frame)
        self.canvas_widget = self.canvas.get_tk_widget()
        self.canvas_widget.pack(fill=tk.BOTH, expand=True)

    # --- Communication Methods (Sysfs Write) ---
    def write_sysfs(self, path, value):
        """
        Writes a value to a sysfs file using sudo via subprocess,
        as direct file write requires root permission.
        """
        command = f"echo {value} > {path}"
        try:
            # Use sudo to run the shell command that writes to the file
            process = subprocess.run(
                ['sudo', 'sh', '-c', command],
                check=True,
                capture_output=True,
                text=True
            )
            print(f"SysFS write success: {command}")
            return True
        except subprocess.CalledProcessError as e:
            msg = f"Failed to write config via sudo. Check if the module is loaded and sysfs path is correct.\nError: {e.stderr}"
            print(msg)
            messagebox.showerror("SysFS Write Error", msg)
            return False
        except FileNotFoundError:
            # sudo or sh not found (unlikely on Linux)
            messagebox.showerror("Command Error", "Required shell command (sudo/sh) not found.")
            return False

    def read_sysfs(self, path):
        """Helper function to read a string value from a sysfs file."""
        try:
            # Reading permissions are usually looser, so direct file access is used.
            with open(path, 'r') as f:
                return f.read().strip()
        except Exception as e:
            # print(f"Warning: Could not read {path}: {e}")
            return None

    def read_initial_config(self):
        """Reads initial values from sysfs on startup."""
        try:
            # Try to read sampling_ms
            ms = self.read_sysfs(SYSFS_CONFIG_PATH["sampling_ms"])
            if ms:
                self.sampling_ms.set(int(ms))

            # Try to read threshold_mC
            mC = self.read_sysfs(SYSFS_CONFIG_PATH["threshold_mC"])
            if mC:
                self.threshold_C.set(float(mC) / 1000.0)

            # Try to read mode
            mode = self.read_sysfs(SYSFS_CONFIG_PATH["mode"])
            if mode:
                self.mode_var.set(mode)

            print("Initial config read complete.")
        except Exception as e:
            print(f"Could not read initial config. Module might be unloaded. {e}")


    def set_threshold(self):
        """Writes the threshold value to the kernel via sysfs."""
        try:
            temp_C = self.threshold_C.get()
            temp_mC = int(temp_C * 1000)
            if self.write_sysfs(SYSFS_CONFIG_PATH["threshold_mC"], temp_mC):
                self.threshold_line.set_ydata([temp_C])
                self.canvas.draw()
        except ValueError:
            messagebox.showerror("Input Error", "Threshold must be a number.")

    def set_sampling(self):
        """Writes the sampling period to the kernel via sysfs."""
        try:
            ms = self.sampling_ms.get()
            self.write_sysfs(SYSFS_CONFIG_PATH["sampling_ms"], ms)
        except ValueError:
            messagebox.showerror("Input Error", "Sampling must be an integer.")

    def set_mode(self, mode_name):
        """Writes the sensor mode (normal/noisy/ramp) to the kernel via sysfs."""
        self.write_sysfs(SYSFS_CONFIG_PATH["mode"], mode_name)


    # --- Data Reading and Processing ---
    def read_device_loop(self):
        """Runs in a separate thread to continuously read binary data."""
        fd = None
        try:
            # The core issue is that this OS call may block and definitely needs permissions.
            fd = os.open(DEVICE_PATH, os.O_RDONLY | os.O_NONBLOCK)
        except FileNotFoundError:
            self.fatal_error = f"Cannot find device file: {DEVICE_PATH}.\nIs the kernel module loaded?"
            self.running.clear()
            return
        except PermissionError:
            # Store the permission error message for the main thread to display
            # Note: sys.argv[0] gives the path to the executed script (app.py)
            self.fatal_error = f"Permission denied accessing {DEVICE_PATH}. Please run with elevated privileges.\n(E.g., using 'sudo /path/to/venv/bin/python3 {sys.argv[0]}')"
            self.running.clear()
            return
        except Exception as e:
            self.fatal_error = f"Failed to open device: {e}"
            self.running.clear()
            return

        while self.running.is_set():
            try:
                # Use select/poll to wait for data (more efficient than busy-waiting with sleep)
                r, _, _ = select.select([fd], [], [], 0.05) # Wait up to 50ms for readability

                if r:
                    binary_data = os.read(fd, RECORD_SIZE)

                    if len(binary_data) == RECORD_SIZE:
                        self.process_sample(binary_data)

                # Small sleep ensures the loop isn't a tight busy-wait if select fails or data is sparse
                time.sleep(0.01)

            except BlockingIOError:
                # No data ready, continue loop
                time.sleep(0.01)
                continue
            except struct.error as e:
                print(f"Error unpacking binary data: {e}")

            except Exception as e:
                # Log unexpected errors but continue the loop if possible
                print(f"An unexpected error occurred in read loop: {e}")
                time.sleep(0.1)

        if fd is not None:
            os.close(fd)


    def process_sample(self, binary_data):
        """Unpacks binary data and updates the state queues."""
        try:
            timestamp_ns, temp_mC, flags = struct.unpack(STRUCT_FORMAT, binary_data)
        except struct.error:
            # Occurs if read() didn't return the full record size
            return

        # Convert units for display
        timestamp_s = timestamp_ns / 1_000_000_000.0
        temp_C = temp_mC / 1000.0

        # Check for alert status (SIMTEMP_FLAG_THRESHOLD_CROSSED is bit 1, or 0x02)
        is_alert = (flags & SIMTEMP_FLAG_THRESHOLD_CROSSED) != 0

        # Update data queues
        if not self.time_data:
            self.start_time = timestamp_s

        relative_time = timestamp_s - self.start_time
        self.time_data.append(relative_time)
        self.temp_data.append(temp_C)

        # Update alert status (safe across threads via tkinter variables)
        if is_alert:
            self.alert_status.set("ALERT")
            self.alert_display.config(foreground="red")
            print(f"ALERT: {temp_C:.2f}C at {relative_time:.2f}s")
        else:
            self.alert_status.set("OK")
            self.alert_display.config(foreground="green")

        # Ensure we redraw the plot soon
        self.master.event_generate("<<DataUpdated>>")


    # --- UI Update Methods ---
    def update_plot(self):
        """Updates the Matplotlib plot from the data queues."""

        # CRITICAL: Check for fatal error signaled from the worker thread
        if self.fatal_error:
            messagebox.showerror("Fatal Error", self.fatal_error)
            self.on_close()
            return

        if self.time_data:
            self.line.set_data(list(self.time_data), list(self.temp_data))

            # --- FIX: Only set xlim if there are at least two data points ---
            if len(self.time_data) >= 2:
                self.ax.set_xlim(self.time_data[0], self.time_data[-1])
            elif len(self.time_data) == 1:
                # If only one point exists, expand the x-axis slightly to show it
                t = self.time_data[0]
                self.ax.set_xlim(t - 1, t + 1)
            # ------------------------------------------------------------------

            y_min = min(self.temp_data) - 1 if self.temp_data else 30
            y_max = max(self.temp_data) + 1 if self.temp_data else 60

            # Ensure the threshold line is visible
            current_threshold = self.threshold_C.get()
            y_min = min(y_min, current_threshold - 1)
            y_max = max(y_max, current_threshold + 1)

            self.ax.set_ylim(y_min, y_max)

            self.canvas.draw_idle()

        # Schedule the next update
        self.master.after(100, self.update_plot)

    def update_stats(self):
        """A simple method that could be used for other status updates (e.g., stats counter)."""
        # This is where you would periodically read the 'stats' sysfs file.
        # For simplicity, we just schedule the next update.
        self.master.after(500, self.update_stats)

    def on_close(self):
        """Handles graceful shutdown."""
        print("Stopping monitor thread...")
        self.running.clear()  # Signal the reading thread to stop
        self.read_thread.join(timeout=1.0) # Wait for the thread to finish
        self.master.destroy()

# --- Main Application Execution ---
if __name__ == "__main__":
    # Import select only here to avoid ImportError before the main execution block
    try:
        import select
    except ImportError:
        # This should never happen on a standard Linux installation, but for safety:
        print("Error: The 'select' module is required for device polling.")
        sys.exit(1)

    # Wrap the application launch in a try/except block to catch Ctrl+C if the GUI fails to launch
    try:
        root = tk.Tk()
        app = SensorMonitorApp(root)
        # The main loop keeps the GUI responsive and handles events
        root.mainloop()
    except KeyboardInterrupt:
        # If the GUI is closed with Ctrl+C before the main loop starts, handle it gracefully
        if 'app' in locals():
            app.on_close()
        else:
            print("Application quit before main loop started.")
            sys.exit(0)
