#ifndef NXP_SIMTEMP_IOCTL_H
#define NXP_SIMTEMP_IOCTL_H

#include "nxp_simtemp.h" /* For SIMTEMP_IOCTL_MAGIC */

/**
 * struct simtemp_config - Configuration structure for atomic updates via ioctl.
 * @sampling_ms:  The new sampling period in milliseconds.
 * @threshold_mC: The new alert threshold in milli-degrees Celsius.
 *
 * This allows user space to set multiple parameters in a single, atomic operation,
 * avoiding race conditions where the sensor might operate with a mix of old
 * and new settings.
 */
struct simtemp_config {
	__u32 sampling_ms;
	__s32 threshold_mC;
};

/*
 * IOCTL Commands
 * _IOW: An ioctl that writes data from user space to the kernel.
 * - Magic number: 'S'
 * - Command number: 1
 * - Argument type: struct simtemp_config
 */
#define SIMTEMP_IOC_SET_CONFIG _IOW(SIMTEMP_IOCTL_MAGIC, 1, struct simtemp_config)

#endif /* NXP_SIMTEMP_IOCTL_H */