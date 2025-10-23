# **Test Plan**

This document outlines test cases for verifying the nxp_simtemp driver on both a native PC/VM and the target Raspberry Pi.

### **T1: PC \- Basic Load and Unload**

- **Description**: Verify the module (compiled with `PC_BUILD`) loads/unloads cleanly on a PC or VM.
- **Steps**:
  1. Build the module with the PC_BUILD macro enabled (e.g., cd scripts/ && ./build.sh pc_build).
  2. Run `sudo insmod kernel/nxp_simtemp.ko.`
  3. Check dmesg for "NXP Virtual Temperature Sensor initialized".
  4. Verify `/dev/simtemp` and `/sys/class/misc/simtemp/` exist.
  5. Run `sudo rmmod nxp_simtemp.`
  6. Verify nodes are gone and dmesg shows no errors.
- **Expected**: Pass.

### **T2: PC \- Data Path (Read)**

- **Description**: Verify the user-space CLI can read and parse data from the PC_BUILD module.
- **Steps**:
  1. Load module.
  2. Run `python3 user/cli/main.py.`
- **Expected**: The CLI prints formatted lines (timestamp, temperature, alert) at the default 1-second interval.

### **T3: PC \- Configuration Path (Sysfs)**

- **Description**: Verify sysfs config changes the driver's behavior.
- **Steps**:
  1. Load module.
  2. Run `python3 user/cli/main.py` in one terminal.
  3. In a second terminal, run: `echo 200 | sudo tee /sys/class/misc/simtemp/sampling_ms`.
- **Expected**: The CLI output immediately speeds up to 5 samples per second.

### **T4: PC \- Alert Path (Poll)**

- **Description**: Verify the `poll()` mechanism correctly reports a high-priority alert.
- **Steps**:
  1. Load module.
  2. Set mode to ramp: `echo ramp | sudo tee /sys/class/misc/simtemp/mode`.
  3. Set threshold to a low value: `echo 22000 | sudo tee /sys/class/misc/simtemp/threshold_mC`.
  4. Run the automated test: `python3 user/cli/main.py --test`.
- **Expected**: Script reports "TEST PASSED" and exits with status 0\.

### **T5: PC \- Robustness (Invalid Input)**

- **Description**: Verify the driver handles bad input gracefully.
- **Steps**:
  1. Load module.
  2. Attempt to write an invalid value: echo foo | sudo tee /sys/class/misc/simtemp/mode.
  3. Attempt to write an out-of-bounds value: echo 5 | sudo tee /sys/class/misc/simtemp/sampling_ms.
- **Expected**: Both commands fail with "Invalid argument". dmesg shows no crashes.

### **T6: PC \- Concurrency (Locking)**

- **Description**: Verify the driver is stable (no crashes or deadlocks) when read and configured simultaneously. This specifically tests the spinlock implementation for the race condition bug.
- **Steps**:

  1. Load module.
  2. Run `python3 user/cli/main.py` in one terminal.
  3. In a second terminal, run a "config spam" script:
     ```bash
     while true; do
      echo 100 \> /dev/null
      echo 200 \> /dev/null
     done | sudo tee /sys/class/misc/simtemp/sampling_ms
     ```

- **Expected**: The system remains stable. The CLI reader continues to print data (at varying speeds). No kernel panics.

### **T7: Raspberry Pi (Target Build) \- Device Tree Binding**

- **Description**: Verify the driver (compiled without `PC_BUILD`) probes correctly using the Device Tree on the target hardware.
- **Steps**:
  1. Ensure the Device Tree Overlay is loaded on the Pi.
  2. Build the module natively on the Pi. **This build must not define the PC_BUILD macro**.
  3. Run sudo insmod kernel/nxp_simtemp.ko.
- **Expected**: dmesg **must** show the "NXP Virtual Temperature Sensor initialized" message. This proves the probe function was successfully triggered by the Device Tree match.

### **T8: Raspberry Pi \- End-to-End Test**

- **Description**: Verify the full system works on the Pi.
- **Steps**:
  1. Perform steps from T7.
  2. Run `python3 user/cli/main.py` on the Pi.
  3. Change configuration via sysfs.
- **Expected**: The CLI runs, and sysfs configuration works as expected.
