#ifndef NXP_SIMTEMP_H
#define NXP_SIMTEMP_H

#include <linux/types.h>

/* Use a dedicated magic number for ioctl commands to ensure we're talking to the right device */
#define SIMTEMP_IOCTL_MAGIC 'S'

struct simtemp_sample {
	__u64 timestamp_ns;
	__s32 temp_mC;
	__u32 flags;
} __attribute__((packed));

/* Flags for the simtemp_sample.flags field */
#define SIMTEMP_FLAG_NEW_SAMPLE        (1 << 0) /* Always set for a new sample */
#define SIMTEMP_FLAG_THRESHOLD_CROSSED (1 << 1) /* Set if temp crossed the threshold */

#endif /* NXP_SIMTEMP_H */