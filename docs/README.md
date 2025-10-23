# **NXP Simulated Temperature Sensor Project**

A comprehensive Linux kernel driver and user-space application system that simulates a hardware temperature sensor. This project demonstrates kernel module development, device tree integration, character device interfaces, and modern Linux driver development practices.

## **Quick Start & Build Instructions**

### **Step 1: Clone and Setup Dependencies**

Before building, you must install kernel headers for your host system and the required Python dependencies.

#### Clone the repository

```bash
git clone https://github.com/correa21/NXP-Simtemp-Challenge
cd NXP-Simtemp-Challenge
```

#### Install kernel headers and build essentials on Debian/Ubuntu

```bash
sudo apt update
sudo apt install -y linux-headers-$(uname -r) build-essential python3 python3-venv python3-tk
```

#### Initialize Python Virtual Environment and install GUI dependencies

```bash
python3 -m venv venv
source venv/bin/activate
pip install -r user/gui/requirements.txt
deactivate
```

### **Step 2: Build the Kernel Module**

The build.sh script supports two main modes, controlled by command-line arguments:

| Build Target         | Command             | Resulting Binary             | Notes                                                                                       |
| :------------------- | :------------------ | :--------------------------- | :------------------------------------------------------------------------------------------ |
| **Embedded/DT Mode** | ./build.sh          | Standard Module + DT Overlay | For deployment on systems with Device Tree support.                                         |
| **PC Testing Mode**  | ./build.sh PC debug | Module only                  | Compiles with **-DPC_BUILD** flag, enabling local platform device registration for testing. |

**Example PC Build Command:**

```bash
cd scripts
./build.sh PC debug
```

### **Step 3: Run the Demo (Requires sudo)**

The run_demo.sh script executes the required threshold alert test and performs a clean cleanup.

```bash
sudo ./run_demo.sh
```

## **3. Launching the GUI (Requires Elevated Privileges)**

The GUI application must access the device file (/dev/simtemp), which requires root privileges. Since the application is graphical, it also needs permission to access your X server.

1. **Grant Display Access (Run as your normal user):**

   ```bash
   xhost +local:
   ```

2. **Launch the GUI (Must use VENV's Python path and sudo):**\
   **_IMPORTANT_: The path to the venv's python3 must be exact.**\
   **The command below assumes your venv is in the project root.**\

   ```bash
   sudo -E env DISPLAY=$DISPLAY XAUTHORITY=$XAUTHORITY ./venv/bin/python3 ./user/gui/app.py
   ```

   **_NOTE:_** if running through ssh you must first enable X11 forwarding from the local machine, and then run the following commands:

   ```bash
   sudo xauth add $(/usr/bin/xauth list $DISPLAY)
   ```

## **4. API and Implementation Details**

### **Kernel Driver (nxp_simtemp.c)**

- **Device Management**: Implemented as a simple character device using the **miscdevice** framework (MISC_DYNAMIC_MINOR), which automatically creates /dev/simtemp.
- **Data Buffer**: Uses a **kfifo** structure (FIFO_SIZE 256) protected by a **spinlock_t** (sdev->lock) to safely buffer samples between the HR-Timer context and user-space reads.
- **Simulation Timer**: Uses a **hrtimer** for precise, configurable sampling periods.
- **PC Test Mode**: The **#ifdef PC_BUILD** block manually allocates and registers a static platform_device to trigger the driver's probe function when no Device Tree is present.

### **Character Device Interface**

#### **Binary Sample Format (kernel/nxp_simtemp.h)**

```c
struct simtemp_sample {
 **u64 timestamp_ns; // Monotonic timestamp in nanoseconds
 **s32 temp_mC; // Temperature in milli-degrees Celsius
 **u32 flags; // Status flags
} **attribute\_\_((packed));
```

| Flag                           | Value | Description                        |
| :----------------------------- | :---- | :--------------------------------- |
| SIMTEMP_FLAG_NEW_SAMPLE        | 0x01  | Always set for a new sample.       |
| SIMTEMP_FLAG_THRESHOLD_CROSSED | 0x02  | Set if temp crossed the threshold. |

## TODO

- [ ] add lint script
- [ ] Validate Crosscompile support
- [ ] Validate with i.MX architecture utilizing qemu
- [ ] Add unit tests

## **Submission Links**

| Item               | Status                                            |
| :----------------- | :------------------------------------------------ |
| **Git Repository** | https://github.com/correa21/NXP-Simtemp-Challenge |
| **Demo Video**     | $$PLACEHOLDER_LINK_TO_DEMO_VIDEO$$                |
