#ifndef _LINUX_VIRTIO_TEST_H
#define _LINUX_VIRTIO_TEST_H

#include <linux/types.h>
#include <linux/virtio_types.h>
#include <linux/virtio_ids.h>
#include <linux/virtio_config.h>

/* The feature bitmap for virtio balloon */
#define VIRTIO_TEST_F_CAN_PRINT 0

struct virtio_test_config {
    /* Number of pages host wants Guest to give up. */
    __u32 num_pages;
    /* Number of pages we've actually got in balloon. */
    __u32 actual;
};

struct virtio_test_stat {
    __virtio16 tag;
    __virtio64 val;
} __attribute__((packed));

#endif /* _LINUX_VIRTIO_TEST_H */
