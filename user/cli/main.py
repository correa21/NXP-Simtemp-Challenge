import argparse
import os
import struct
import select
import sys
from datetime import datetime, timezone

DEVICE_PATH = "/dev/simtemp"
SYSFS_PATH_BASE = "/sys/class/misc/simtemp"
SAMPLE_FORMAT = "<QiI"  # u64 timestamp_ns, s32 temp_mC, u32 flags
SAMPLE_SIZE = struct.calcsize(SAMPLE_FORMAT)

FLAG_NEW_SAMPLE = 1 << 0
FLAG_THRESHOLD_CROSSED = 1 << 1

class SimTempError(Exception):
    pass

def sysfs_write(attr, value):
    """Writes a value to a sysfs attribute."""
    path = os.path.join(SYSFS_PATH_BASE, attr)
    try:
        with open(path, "w") as f:
            f.write(str(value))
    except (IOError, OSError) as e:
        raise SimTempError(f"Error writing to sysfs '{path}': {e}")

def sysfs_read(attr):
    """Reads a value from a sysfs attribute."""
    path = os.path.join(SYSFS_PATH_BASE, attr)
    try:
        with open(path, "r") as f:
            return f.read().strip()
    except (IOError, OSError) as e:
        raise SimTempError(f"Error reading from sysfs '{path}': {e}")


def run_monitor(dev_fd):
    """Monitors the device using poll() and prints readings."""
    poller = select.poll()
    poller.register(dev_fd, select.POLLIN | select.POLLPRI) # Data and high-priority events

    print("Monitoring /dev/simtemp... Press Ctrl+C to exit.")
    print("-" * 40)

    while True:
        try:
            events = poller.poll() # No timeout, blocks indefinitely
            for fd, event_mask in events:
                if fd == dev_fd.fileno():
                    data = os.read(dev_fd.fileno(), SAMPLE_SIZE)
                    if len(data) == SAMPLE_SIZE:
                        ts_ns, temp_mc, flags = struct.unpack(SAMPLE_FORMAT, data)

                        ts_utc = datetime.fromtimestamp(ts_ns / 1e9, tz=timezone.utc)
                        temp_c = temp_mc / 1000.0

                        alert_msg = ""
                        if flags & FLAG_THRESHOLD_CROSSED:
                            alert_msg = " | *** ALERT ***"

                        print(f"{ts_utc.isoformat(timespec='milliseconds')} | Temp: {temp_c:6.3f}°C{alert_msg}")

        except KeyboardInterrupt:
            print("\nMonitoring stopped.")
            break
        except (IOError, OSError) as e:
            print(f"Device I/O error: {e}", file=sys.stderr)
            break


def run_test_mode():
    """Configures the device for a test and verifies alert behavior."""
    print("--- Running Test Mode ---")

    try:
        # 1. Configure device for a predictable test
        print("Configuring device: period=200ms, threshold=30.0°C, mode=ramp")
        sysfs_write("sampling_ms", 200)
        sysfs_write("threshold_mC", 30000)
        sysfs_write("mode", "ramp") # Ramp mode ensures threshold will be crossed

        # 2. Open device and set up poller
        dev_fd = os.open(DEVICE_PATH, os.O_RDONLY)
        poller = select.poll()
        poller.register(dev_fd, select.POLLPRI) # We only care about the alert event

        print("Waiting for threshold alert...")

        # 3. Wait for the alert event with a timeout
        # With a 200ms period and a ramp starting at 25C, it should take ~11 samples (2.2s)
        # to cross 30C. A 5-second timeout is generous.
        events = poller.poll(5000) # 5000 ms timeout

        if not events:
            print("TEST FAILED: Timed out waiting for threshold alert.", file=sys.stderr)
            os.close(dev_fd)
            return 1

        # 4. Verify the event and the data
        # The key change: loop to read all samples until the trigger sample is found.
        MAX_SAMPLES_TO_READ = 20 # Safety limit, based on your 11 expected samples

        # After poller.poll() returns, the device is readable.
        for _ in range(MAX_SAMPLES_TO_READ):
            data = os.read(dev_fd, SAMPLE_SIZE)

            if len(data) == SAMPLE_SIZE:
                timestamp_ms, temp_mC, flags = struct.unpack(SAMPLE_FORMAT, data)

                # Check if this sample is the one that caused the trigger
                if flags & FLAG_THRESHOLD_CROSSED:
                    print(f"Threshold alert received. Trigger found in sample (Temp: {temp_mC/1000.0:.1f}°C, Flags: {hex(flags)}).")
                    print("TEST PASSED")
                    os.close(dev_fd)
                    return 0

                # Log or skip intermediate samples
                # print(f"Intermediate sample: Temp={temp_mC/1000.0:.1f}C, Flags={hex(flags)}")

            elif len(data) == 0:
                # If read returns 0 bytes, the buffer is empty
                break

            else:
                # Short read but not zero, which is unexpected
                print(f"TEST FAILED: Short read from device ({len(data)} bytes).", file=sys.stderr)
                os.close(dev_fd)
                return 1

        # If the loop finished without finding the flag:
        print("TEST FAILED: Read all available samples but THRESHOLD_CROSSED flag was never set.", file=sys.stderr)
        os.close(dev_fd)
        return 1

    except SimTempError as e:
        print(f"TEST FAILED: An error occurred: {e}", file=sys.stderr)
        return 1
    except Exception as e:
        print(f"TEST FAILED: An unexpected error occurred: {e}", file=sys.stderr)
        return 1


def main():
    parser = argparse.ArgumentParser(description="CLI for the NXP Virtual Temperature Sensor.")
    parser.add_argument("--set-period", type=int, metavar="MS", help="Set sampling period in milliseconds.")
    parser.add_argument("--set-threshold", type=int, metavar="mC", help="Set alert threshold in milli-Celsius.")
    parser.add_argument("--set-mode", choices=["normal", "noisy", "ramp"], help="Set simulation mode.")
    parser.add_argument("--read-stats", action="store_true", help="Read the device statistics.")
    parser.add_argument("--test", action="store_true", help="Run the automated threshold alert test.")

    args = parser.parse_args()

    if not os.path.exists(DEVICE_PATH):
        print(f"Error: Device '{DEVICE_PATH}' not found. Is the module loaded?", file=sys.stderr)
        return 1

    try:
        # Handle configuration arguments
        if args.set_period:
            sysfs_write("sampling_ms", args.set_period)
            print(f"Set sampling period to {args.set_period} ms")
        if args.set_threshold:
            sysfs_write("threshold_mC", args.set_threshold)
            print(f"Set threshold to {args.set_threshold} mC")
        if args.set_mode:
            sysfs_write("mode", args.set_mode)
            print(f"Set mode to '{args.set_mode}'")
        if args.read_stats:
            stats = sysfs_read("stats")
            print(f"Device Stats: {stats}")

        # If no other action is specified, default to monitoring
        if not any([args.set_period, args.set_threshold, args.set_mode, args.read_stats, args.test]):
            with open(DEVICE_PATH, "rb",buffering=0) as dev_fd:
                run_monitor(dev_fd)

        if args.test:
            # sys.exit() is used to return the status code from the test
            sys.exit(run_test_mode())

    except SimTempError as e:
        print(f"Error: {e}", file=sys.stderr)
        return 1

    return 0

if __name__ == "__main__":
    sys.exit(main())
