/*
 * Virtio GPU PCI Device
 *
 * Copyright Red Hat, Inc. 2013-2014
 *
 * Authors:
 *     Dave Airlie <airlied@redhat.com>
 *     Gerd Hoffmann <kraxel@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.
 * See the COPYING file in the top-level directory.
 */

#ifndef HW_VIRTIO_TEST_PCI_H
#define HW_VIRTIO_TEST_PCI_H

#include "hw/virtio/virtio-pci.h"
#include "qom/object.h"

typedef struct VirtIOTestPCI VirtIOTestPCI;

/*
 * virtio-test-pci: This extends VirtioPCIProxy.
*/

// #define TYPE_VIRTIO_TEST_PCI "virtio-test-pci"
// #define VIRTIO_TEST_PCI(obj) \
//         OBJECT_CHECK(VirtIOTestPCI, (obj), TYPE_VIRTIO_TEST_PCI)

#define TYPE_VIRTIO_TEST_PCI "virtio-test-pci-base"
DECLARE_INSTANCE_CHECKER(VirtIOTestPCI, VIRTIO_TEST_PCI,
                         TYPE_VIRTIO_TEST_PCI)

struct VirtIOTestPCI {
    VirtIOPCIProxy parent_obj;
    VirtIOTest vdev;
};

#endif /* HW_VIRTIO_TEST_PCI_H */