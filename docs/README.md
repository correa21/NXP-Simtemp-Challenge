# **NXP Simulated Temperature Sensor Project**

This repository contains a solution for the NXP Candidate Challenge, implementing a virtual temperature sensor system for Linux.

**Submission Links:**

- **Git Repository:** https://github.com/correa21/NXP-Simtemp-Challenge
- **Demo Video:** \[PLACEHOLDER_LINK_TO_DEMO_VIDEO\]

## **Project Overview**

The project consists of three main parts:

1. **nxp_simtemp Kernel Module**: A platform driver that simulates a temperature sensor, producing periodic data and exposing it via a character device and sysfs.
2. **User-space CLI**: A Python application to configure the sensor's parameters, read data, and monitor for threshold alerts using poll().
3. **Scripts**: Helper scripts to automate the build, testing, installation, and demonstration of the system.

## **Repository Layout**

```
simtemp/
├─ kernel/         # Kernel module source and build files
├─ user/           # User-space application source (CLI and GUI)
├─ scripts/        # Build, demo, and install scripts
├─ docs/           # Design and project documentation
└─ .gitignore
```

## **Setup and Build**

### **Step 1: Clone and Setup Dependencies**

Before building, you must install kernel headers for your host system and the required Python dependencies.

**Clone the repository (if you haven't):**

```bash
git clone https://github.com/correa21/NXP-Simtemp-Challenge.git
cd NXP-Simtemp-Challenge
```

**Install kernel headers and build essentials on Debian/Ubuntu:**

```bash
sudo apt update
sudo apt install -y linux-headers-$(uname -r) build-essential python3 python3-venv python3-tk
```

**Initialize Python Virtual Environment and install GUI dependencies:**

```bash
python3 -m venv venv
source venv/bin/activate
pip install -r user/gui/requirements.txt
deactivate
```

### **Step 2: Build the Kernel Module**

The build.sh script supports two main modes:

1. **Embedded/DT Mode (Default):** scripts/build.sh \- For deployment on systems with Device Tree support.
2. **PC Testing Mode:** scripts/build.sh PC debug \- Compiles with \-DPC_BUILD flag, enabling local platform device registration for testing on a PC/VM without a Device Tree.

**Example PC Build Command:**

```bash
cd scripts
./build.sh PC debug
```

## **Usage and Demonstration**

### **Automated Demo**

The easiest way to run the system and verify its core functionality is with the run_demo.sh script. This script automates the entire test cycle.

**Important:** This script requires sudo privileges to load/unload kernel modules.

```bash
cd simtemp/scripts
sudo ./run_demo.sh
```

The script performs the following actions:

1. Loads the nxp_simtemp.ko kernel module.
2. Verifies that the /dev/simtemp character device and sysfs attributes are created.
3. Runs the Python CLI in a special **test mode** to verify the poll() alert.
4. Reports **PASS** or **FAIL**.
5. Unloads the kernel module, cleaning up all resources.

### **Installing the CLI (Optional)**

You can install the Python CLI as a system-wide command named simtemp-cli.

```bash
cd simtemp/scripts
sudo ./install_cli.sh
```

To remove it:

```bash
sudo ./uninstall_cli.sh
```

### **Manual Usage**

After loading the module (e.g., sudo insmod kernel/nxp_simtemp.ko), you can interact with the driver.

1. **Check Sysfs:**
   ```bash
   ls -l /sys/class/misc/simtemp/
   # See attributes like: sampling_ms, threshold_mC, mode, stats
   ```
2. Configure and Monitor (using simtemp-cli)  
   (Assumes you have run scripts/install_cli.sh)

   ```bash
   # Set sampling period to 500ms
   simtemp-cli --set-period 500

   # Set alert threshold to 40°C (40000 mC)
   simtemp-cli --set-threshold 40000

   # Check the device statistics
   simtemp-cli --read-stats

   # Monitor readings (default action)
   simtemp-cli
   # 2025-10-23T07:35:01.123Z | Temp:  42.123°C
   # ... (Press Ctrl+C to exit)
   ```

3. Monitor Readings (Manual Python Call)  
   (If you did not install the CLI)
   ```bash
   python3 user/cli/main.py
   ```
4. **Unload the Module:**
   ```bash
   sudo rmmod nxp_simtemp
   ```

### **Launching the GUI (Optional)**

The GUI application requires sudo to access /dev/simtemp and must be run using the virtual environment's Python interpreter.

```bash
# Make sure you are in the project's root directory
sudo ./venv/bin/python3 ./user/gui/app.py
```

**Note:** If running over SSH, you may need to authorize sudo to access your X server:

```bash
sudo xauth add $(/usr/bin/xauth list $DISPLAY)
```

## **API and Implementation Details**

### **Kernel Driver (nxp_simtemp.c)**

- **Device Management**: Implemented as a character device using the miscdevice framework, which automatically creates /dev/simtemp.
- **Data Buffer**: Uses a kfifo structure protected by a spinlock_t to safely buffer samples between the HR-Timer context and user-space reads.
- **Simulation Timer**: Uses a hrtimer for precise, configurable sampling periods.
- **PC Test Mode**: The \#ifdef PC_BUILD block manually registers a static platform_device to trigger the driver's probe function when no Device Tree is present.

### **Character Device Interface**

**Binary Sample Format (kernel/nxp_simtemp.h)**

```c
struct simtemp_sample {
   __u64 timestamp_ns;   // Monotonic timestamp in nanoseconds
   __s32 temp_mC;        // Temperature in milli-degrees Celsius
   __u32 flags;          // Status flags
} __attribute__((packed));
```

| Flag                           | Value | Description                        |
| :----------------------------- | :---- | :--------------------------------- |
| SIMTEMP_FLAG_NEW_SAMPLE        | 0x01  | Always set for a new sample.       |
| SIMTEMP_FLAG_THRESHOLD_CROSSED | 0x02  | Set if temp crossed the threshold. |

## **Next Steps and TODO**

- [ ] **QEMU/i.MX Demo**: Validate with i.MX architecture utilizing QEMU and the Device Tree overlay.
- [ ] **GUI Dashboard**: The optional GUI is included, but could be expanded.
- [ ] **Unit Tests**: Add unit tests for user-space parsing logic.
- [ ] **Linting**: Add a lint.sh script to run checkpatch.pl.
- [ ] **Cross-Compile**: Validate cross-compilation support in the build scripts.
