#ifndef _LINUX_VIRTIO_TEST_H
#define _LINUX_VIRTIO_TEST_H

#include "standard-headers/linux/types.h"
#include "standard-headers/linux/virtio_types.h"
#include "standard-headers/linux/virtio_ids.h"
#include "standard-headers/linux/virtio_config.h"

#define VIRTIO_TEST_F_CAN_PRINT    0

struct virtio_test_config {
    /* Number of pages host wants Guest to give up. */
    uint32_t num_pages;
    /* Number of pages we've actually got in balloon. */
    uint32_t actual;
    /* Event host wants Guest to do */
    uint32_t event;
};

struct virtio_test_stat {
    __virtio16 tag;
    __virtio64 val;
} QEMU_PACKED;

#endif