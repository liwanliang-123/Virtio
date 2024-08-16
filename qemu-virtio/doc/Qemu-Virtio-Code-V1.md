# **Linux 用户空间：**

**virtio.c**

```c
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char *argv[])
{
    int fd, retvalue;
    unsigned char databuf[1];  // 0 - 255

    if(argc != 2) {
        printf("ERROR:please enter two parameters!\n");
        printf("eg: ./virtio number\n");
        printf("but the number between 0 and 255!\n");
        return -1;
    }

    databuf[0] = atoi(argv[1]); /* string to number */

    fd = open("/dev/virtio_misc", O_RDWR);
    if(fd < 0) {
        printf("ERROR:virtio_misc open failed!\n");
        return -1;
    }

    retvalue = write(fd, databuf, sizeof(databuf));
    if(retvalue < 0) {
        printf("ERROR:write failed!\r\n");
        close(fd);
        return -1;
    }

    close(fd);

    return 0;
}
```

**运行时需要加上参数：**

**run-qemu**

```
    -device virtio-test-device	\ 
```

# **Linux kernel：**

**srcs/linux-5.4.239/drivers/virtio/Makefile**

```
obj-y += virtio_test.o 
```

**srcs/linux-5.4.239/drivers/virtio/virtio_test.c**

```c
#include <linux/virtio.h>
#include <linux/virtio_test.h>
#include <linux/swap.h>
#include <linux/workqueue.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/oom.h>
#include <linux/wait.h>
#include <linux/mm.h>
#include <linux/mount.h>
#include <linux/magic.h>

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/miscdevice.h>

#define MISC_NAME "virtio_misc"
#define MISC_MINOR  144

struct test_request {
	__virtio32 type;
	__virtio32 arg1;
	__virtio32 arg2;
    __virtio32 arg3;
    char arg4[32];
};

struct  test_response {
	__virtio32 ret;
    __virtio32 arg1;
    __virtio32 arg2;
};

struct virtio_test {
    struct test_request req;
    struct test_response resp;
    struct virtio_device *vdev;
    struct virtqueue *print_vq;
};

static struct virtio_test *vb_dev;

static void print_response_data(struct virtio_test *test)
{
    printk("virtio response ret is %d\n",test->resp.ret);
    printk("virtio response arg1 is %d\n",test->resp.arg1);
    printk("virtio response arg2 is %d\n",test->resp.arg2);
}

/* Called from virtio device, in IRQ context */
static void test_request_done(struct virtqueue *vq)
{
    unsigned int len;
    struct virtio_test *test;
    printk("virtio %s get ack\n", __func__);

	do {
		virtqueue_disable_cb(vq);
		while ((test = virtqueue_get_buf(vq, &len)) != NULL) {
			// request packet will be completed by response packet
            print_response_data(test);
		}
		if (unlikely(virtqueue_is_broken(vq)))
			break;
	} while (!virtqueue_enable_cb(vq));
}

static void build_test_request(struct virtio_test *vb, unsigned char num)
{
	vb->req.type = num++;
    vb->req.arg1 = num++;
    vb->req.arg2 = num++;
    vb->req.arg3 = num++;
    strncpy(vb->req.arg4, "hello back end!", 
                            sizeof(vb->req.arg4));
}

static void virtio_test_submit_request(unsigned char num)
{
    struct virtqueue *vq;
    struct virtio_test *vb;
    struct scatterlist out_sg, in_sg, *sgs[2];

	unsigned int num_out = 0, num_in = 0;

    vb = vb_dev;
    vq = vb->print_vq;

    build_test_request(vb, num);

    sg_init_one(&out_sg, &vb->req, sizeof(vb->req));
    sgs[num_out++] = &out_sg;
	sg_init_one(&in_sg, &vb->resp, sizeof(vb->resp));
	sgs[num_out + num_in++] = &in_sg;

    /* We should always be able to add one buffer to an empty queue. */
	virtqueue_add_sgs(vq, sgs, num_out, num_in, vb, GFP_ATOMIC);
    virtqueue_kick(vq);
}

static int init_vqs(struct virtio_test *vb)
{
    struct virtqueue *vqs[1];
    vq_callback_t *callbacks[] = { test_request_done };
    const char * const names[] = { "virtio_test"};
    int err, nvqs;

    nvqs = virtio_has_feature(vb->vdev, VIRTIO_TEST_F_CAN_PRINT) ? 1 : 0;
    err = virtio_find_vqs(vb->vdev, nvqs, vqs, callbacks, names, NULL);
    if (err)
        return err;

    vb->print_vq = vqs[0];

    return 0;
}

static void remove_common(struct virtio_test *vb)
{
    /* Now we reset the device so we can clean up the queues. */
    vb->vdev->config->reset(vb->vdev);
    vb->vdev->config->del_vqs(vb->vdev);
}

static int virttest_validate(struct virtio_device *vdev)
{
    return 0;
}

static int virtio_misc_open(struct inode *inode, struct file *filp)
{
    return 0;
}

static int virtio_misc_release(struct inode *inode, struct file *filp)
{
    return 0;
}

static ssize_t virtio_misc_write(struct file *filp, const char __user *buf,
			 size_t count, loff_t *ppos)
{
    int ret;
    unsigned char databuf[1];

    ret = copy_from_user(databuf, buf, count);
    if(ret < 0)
        return -EINVAL;

    virtio_test_submit_request(databuf[0]);

    return 0;
}

struct file_operations virtio_misc_fops = {
    .owner = THIS_MODULE,
    .open = virtio_misc_open,
    .release = virtio_misc_release,
    .write = virtio_misc_write,
};

static struct miscdevice virtio_miscdev = {
    .minor = MISC_MINOR,
    .name = MISC_NAME,
    .fops = &virtio_misc_fops,
};

static int virttest_probe(struct virtio_device *vdev)
{
    struct virtio_test *vb;
    int err;

    printk(">>>>>> %s function is called <<<<<<\n", __func__);
    if (!vdev->config->get) {
        return -EINVAL;
    }

    vdev->priv = vb = kmalloc(sizeof(*vb), GFP_KERNEL);
    if (!vb) {
        err = -ENOMEM;
        goto out;
    }

    vb->vdev = vdev;

    err = init_vqs(vb);
    if (err)
        goto out_free_vb;

    virtio_device_ready(vdev);

    vb_dev = vb;

    /* misc driver registered */
    err = misc_register(&virtio_miscdev);
    if(err < 0) {
        printk( "misc register is failed\n");
        goto out_free_misc;
    }
    printk( "misc register has succeeded\n");

    return 0;

out_free_misc:
    misc_deregister(&virtio_miscdev);
out_free_vb:
    kfree(vb);
out:
    return err;
}

static void virttest_remove(struct virtio_device *vdev)
{
    struct virtio_test *vb = vdev->priv;

    remove_common(vb);
    kfree(vb);
    vb_dev = NULL;
    misc_deregister(&virtio_miscdev);
}

static struct virtio_device_id id_table[] = {
    { VIRTIO_ID_TEST, VIRTIO_DEV_ANY_ID },
    { 0 },
};

static unsigned int features[] = {
    VIRTIO_TEST_F_CAN_PRINT,
};

static struct virtio_driver virtio_test_driver = {
    .feature_table = features,
    .feature_table_size = ARRAY_SIZE(features),
    .driver.name =  KBUILD_MODNAME,
    .driver.owner = THIS_MODULE,
    .id_table = id_table,
    .validate = virttest_validate,
    .probe =    virttest_probe,
    .remove =   virttest_remove,
};

module_virtio_driver(virtio_test_driver);
MODULE_DEVICE_TABLE(virtio, id_table);
MODULE_DESCRIPTION("Virtio test driver");
MODULE_LICENSE("GPL");
```

**srcs/linux-5.4.239/include/uapi/linux/virtio_ids.h**

```
#define VIRTIO_ID_TEST         45 /* virtio test */ 
```

**srcs/linux-5.4.239/include/uapi/linux/virtio_test.h**

```c
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
```

# **qemu code：**

**srcs/qemu-7.2.0/hw/virtio/Kconfig**

```plaintext
config VIRTIO_TEST
    bool
    default y
    depends on VIRTIO
```

**srcs/qemu-7.2.0/hw/virtio/meson.build**

```c
virtio_ss.add(when: 'CONFIG_VIRTIO_TEST', if_true: files('virtio-test.c')) 
virtio_pci_ss.add(when: 'CONFIG_VIRTIO_TEST', if_true: files('virtio-test-pci.c'))
```

**srcs/qemu-7.2.0/hw/virtio/virtio-test-pci.c**

```c
/*
 * Virtio Test PCI Bindings
 */

#include "qemu/osdep.h"

#include "exec/memop.h"
#include "hw/virtio/virtio-test.h"
#include "hw/virtio/virtio-test-pci.h"
#include "standard-headers/linux/virtio_pci.h"
#include "hw/virtio/virtio.h"
#include "hw/pci/pci.h"
#include "hw/pci/pci_bus.h"
#include "hw/qdev-properties.h"
#include "qemu/module.h"
#include "hw/loader.h"
#include "sysemu/kvm.h"
#include "qapi/error.h"
#include "hw/virtio/virtio-pci.h"
#include "qemu/range.h"
#include "hw/virtio/virtio-bus.h"

/* virtio-test-pci */
static Property virtio_test_pci_properties[] = {
    DEFINE_PROP_UINT32("class", VirtIOPCIProxy, class_code, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void virtio_test_pci_realize(VirtIOPCIProxy *vpci_dev, Error **errp)
{
    VirtIOTestPCI *dev = VIRTIO_TEST_PCI(vpci_dev);
    DeviceState *vdev = DEVICE(&dev->vdev);

    if (vpci_dev->class_code != PCI_CLASS_OTHERS &&
        vpci_dev->class_code != PCI_CLASS_MEMORY_RAM) {
        vpci_dev->class_code = PCI_CLASS_OTHERS;
    }

    qdev_set_parent_bus(vdev, BUS(&vpci_dev->bus), errp);
    object_property_set_bool(OBJECT(vdev), "realized", true, errp);
}

static void virtio_test_pci_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtioPCIClass *k = VIRTIO_PCI_CLASS(klass);
    PCIDeviceClass *pcidev_k = PCI_DEVICE_CLASS(klass);

    printf("virtio_test_pci_class_init function call\n");

    k->realize = virtio_test_pci_realize;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    dc->props_ = virtio_test_pci_properties;
    pcidev_k->vendor_id = PCI_VENDOR_ID_REDHAT_QUMRANET;
    pcidev_k->device_id = PCI_DEVICE_ID_VIRTIO_TEST;
    pcidev_k->revision = VIRTIO_PCI_ABI_VERSION;
    pcidev_k->class_id = PCI_CLASS_OTHERS;
}

static void virtio_test_pci_instance_init(Object *obj)
{
    VirtIOTestPCI *dev = VIRTIO_TEST_PCI(obj);

    printf("virtio_test_pci_instance_init function call\n");

    virtio_instance_init_common(obj, &dev->vdev, sizeof(dev->vdev),
                                TYPE_VIRTIO_TEST);
}

static const VirtioPCIDeviceTypeInfo virtio_test_pci_info = {
    .base_name              = TYPE_VIRTIO_TEST_PCI,
    .generic_name           = "virtio-test-pci",
    .transitional_name      = "virtio-test-pci-transitional",
    .non_transitional_name  = "virtio-test-pci-non-transitional",
    .instance_size = sizeof(VirtIOTestPCI),
    .instance_init = virtio_test_pci_instance_init,
    .class_init    = virtio_test_pci_class_init,
};

static void virtio_pci_register_types(void)
{
    virtio_pci_types_register(&virtio_test_pci_info);
}

type_init(virtio_pci_register_types)
```

**srcs/qemu-7.2.0/hw/virtio/virtio-test.c**

```c
#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/iov.h"
#include "qemu/timer.h"
#include "hw/virtio/virtio.h"
#include "hw/virtio/virtio-test.h"
#include "sysemu/kvm.h"
#include "sysemu/hax.h"
#include "exec/address-spaces.h"
#include "qapi/error.h"
#include "qapi/qapi-events-misc.h"
#include "qapi/visitor.h"
#include "qemu/error-report.h"

#include "hw/virtio/virtio-bus.h"
#include "hw/virtio/virtio-access.h"
#include "migration/migration.h"

int test_queue_size = 128;

struct test_request {
	uint32_t type;
	uint32_t arg1;
	uint32_t arg2;
    uint32_t arg3;
    char arg4[32];
};

struct test_response {
	uint32_t ret;
    uint32_t arg1;
    uint32_t arg2;
};

static void print_req_and_build_resp_pack(struct test_request *req, struct test_response *resp)
{    
    qemu_log("QEMU_BACK_END: >>> get type [ %d ] form the front end <<<\n", req->type);
    qemu_log("QEMU_BACK_END: >>> get arg1 [ %d ] form the front end <<<\n", req->arg1);
    qemu_log("QEMU_BACK_END: >>> get arg2 [ %d ] form the front end <<<\n", req->arg2);
    qemu_log("QEMU_BACK_END: >>> get arg3 [ %d ] form the front end <<<\n", req->arg3);
    qemu_log("QEMU_BACK_END: >>> get arg3 [ %s ] form the front end <<<\n", req->arg4);

    resp->ret = req->arg1  + 100;
    resp->arg1 = req->arg2 + 100;
    resp->arg2 = req->arg3 + 100;
}

static void virtio_test_handle_output(VirtIODevice *vdev, VirtQueue *vq)
{
    VirtIOTest *s = VIRTIO_TEST(vdev);

    struct test_request req;
    struct test_response resp;
    VirtQueueElement *elem;
    size_t offset = 0;

    for (;;) {

        elem = virtqueue_pop(vq, sizeof(VirtQueueElement));
        if (!elem)
            return;

        if (!iov_to_buf(elem->out_sg, elem->out_num, offset, &req, sizeof(req))) {
            qemu_log("Qemu Error: iov_to_buf function failed.\n");
            virtqueue_detach_element(vq, elem, 0);
            continue;
        }

        print_req_and_build_resp_pack(&req, &resp);

        iov_from_buf(elem->in_sg, elem->in_num, offset, &resp, sizeof(resp));

        virtqueue_push(vq, elem, sizeof(resp));
        virtio_notify(vdev, vq);
        g_free(elem);
    }
}

static void virtio_test_get_config(VirtIODevice *vdev, uint8_t *config_data)
{
    VirtIOTest *dev = VIRTIO_TEST(vdev);
    struct virtio_test_config config;

    config.actual = cpu_to_le32(dev->actual);
    config.event = cpu_to_le32(dev->event);

    memcpy(config_data, &config, sizeof(struct virtio_test_config));
}

static void virtio_test_set_config(VirtIODevice *vdev,
                                      const uint8_t *config_data)
{
    VirtIOTest *dev = VIRTIO_TEST(vdev);
    struct virtio_test_config config;

    memcpy(&config, config_data, sizeof(struct virtio_test_config));
    dev->actual = le32_to_cpu(config.actual);
    dev->event = le32_to_cpu(config.event);
}

static uint64_t virtio_test_get_features(VirtIODevice *vdev, uint64_t f,
                                            Error **errp)
{
    VirtIOTest *dev = VIRTIO_TEST(vdev);
    f |= dev->host_features;
    virtio_add_feature(&f, VIRTIO_TEST_F_CAN_PRINT);

    return f;
}

static int virtio_test_post_load_device(void *opaque, int version_id)
{
    VirtIOTest *s = VIRTIO_TEST(opaque);
    return 0;
}

static const VMStateDescription vmstate_virtio_test_device = {
    .name = "virtio-test-device",
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = virtio_test_post_load_device,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(actual, VirtIOTest),
        VMSTATE_END_OF_LIST()
    },
};

static void virtio_test_device_realize(DeviceState *dev, Error **errp)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VirtIOTest *s = VIRTIO_TEST(dev);
    int ret;

    virtio_init(vdev, VIRTIO_ID_TEST, sizeof(struct virtio_test_config));

    s->ivq = virtio_add_queue(vdev, test_queue_size, virtio_test_handle_output);
}

static void virtio_test_device_unrealize(DeviceState *dev, Error **errp)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VirtIOTest *s = VIRTIO_TEST(dev);

    virtio_cleanup(vdev);
}

static void virtio_test_device_reset(VirtIODevice *vdev)
{
    VirtIOTest *s = VIRTIO_TEST(vdev);
}

static void virtio_test_set_status(VirtIODevice *vdev, uint8_t status)
{
    VirtIOTest *s = VIRTIO_TEST(vdev);
    return;
}

static void virtio_test_instance_init(Object *obj)
{
    VirtIOTest *s = VIRTIO_TEST(obj);
    return;
}

static const VMStateDescription vmstate_virtio_test = {
    .name = "virtio-test",
    .minimum_version_id = 1,
    .version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_VIRTIO_DEVICE,
        VMSTATE_END_OF_LIST()
    },
};

static Property virtio_test_properties[] = {
    DEFINE_PROP_END_OF_LIST(),
};

static void virtio_test_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtioDeviceClass *vdc = VIRTIO_DEVICE_CLASS(klass);

    dc->props_ = virtio_test_properties;
    dc->vmsd = &vmstate_virtio_test;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    vdc->realize = virtio_test_device_realize;
    vdc->unrealize = virtio_test_device_unrealize;
    vdc->reset = virtio_test_device_reset;
    vdc->get_config = virtio_test_get_config;
    vdc->set_config = virtio_test_set_config;
    vdc->get_features = virtio_test_get_features;
    vdc->set_status = virtio_test_set_status;
    vdc->vmsd = &vmstate_virtio_test_device;
}

static const TypeInfo virtio_test_info = {
    .name = TYPE_VIRTIO_TEST,
    .parent = TYPE_VIRTIO_DEVICE,
    .instance_size = sizeof(VirtIOTest),
    .instance_init = virtio_test_instance_init,
    .class_init = virtio_test_class_init,
};

static void virtio_register_types(void)
{
    type_register_static(&virtio_test_info);
}

type_init(virtio_register_types)
```

**srcs/qemu-7.2.0/hw/virtio/virtio.c**

```
    [VIRTIO_ID_TEST] = "virtio-test" 
```

**srcs/qemu-7.2.0/include/hw/pci/pci.h**

```
#define PCI_DEVICE_ID_VIRTIO_TEST        0x1013 
```

**srcs/qemu-7.2.0/include/hw/virtio/virtio-test-pci.h**

```c
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
 * virtio-test-pci-base: This extends VirtioPCIProxy.
*/

#define TYPE_VIRTIO_TEST_PCI "virtio-test-pci-base"
DECLARE_INSTANCE_CHECKER(VirtIOTestPCI, VIRTIO_TEST_PCI,
                         TYPE_VIRTIO_TEST_PCI)

struct VirtIOTestPCI {
    VirtIOPCIProxy parent_obj;
    VirtIOTest vdev;
};

#endif /* HW_VIRTIO_TEST_PCI_H */
```

**srcs/qemu-7.2.0/include/hw/virtio/virtio-test.h**

```c
#ifndef QEMU_VIRTIO_TEST_H
#define QEMU_VIRTIO_TEST_H

#include "standard-headers/linux/virtio_test.h"
#include "hw/virtio/virtio.h"
#include "hw/pci/pci.h"

#define TYPE_VIRTIO_TEST "virtio-test-device"
#define VIRTIO_TEST(obj) \
        OBJECT_CHECK(VirtIOTest, (obj), TYPE_VIRTIO_TEST)

typedef struct VirtIOTest {
    VirtIODevice parent_obj;
    VirtQueue *ivq;
    uint32_t set_config;
    uint32_t actual;
    VirtQueueElement *stats_vq_elem;
    size_t stats_vq_offset;
    QEMUTimer *stats_timer;
    uint32_t host_features;
    uint32_t event;
} VirtIOTest;

#endif
```

**srcs/qemu-7.2.0/include/standard-headers/linux/virtio_ids.h**

```c
#define VIRTIO_ID_TEST          45 /* virtio test */ 
```

**srcs/qemu-7.2.0/include/standard-headers/linux/virtio_test.h**

```c
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
```

**srcs/qemu-7.2.0/roms/u-boot/configs/qemu-ppce500_defconfig**

```
CONFIG_VIRTIO_TEST=y
```

**运行结果：**

```c
# ./virtio 10
QEMU_BACK_END: >>> get type [ 10 ] form the front end <<<
QEMU_BACK_END: >>> get arg1 [ 11 ] form the front end <<<
QEMU_BACK_END: >>> get arg2 [ 12 ] form the front end <<<
QEMU_BACK_END: >>> get arg3 [ 13 ] form the front end <<<
QEMU_BACK_END: >>> get arg3 [ hello back end! ] form the front end <<<
[    7.627890] virtio test_request_done get ack
[    7.628088] virtio response ret is 111
[    7.628222] virtio response arg1 is 112
[    7.628304] virtio response arg2 is 113
# 
```

​	下面的代码是通过设置 timer 来进行触发，当定时时间到了之后就会通知前端，前端处理完数据又会通知qemu后端，然后将数据打印出来，通知是通过 .config_changed = virttest_changed, 来进行的，当后端有数据改变，这个函数就会被调用。

**附 Linux：**

```c
#include <linux/virtio.h>
#include <linux/virtio_test.h>
#include <linux/swap.h>
#include <linux/workqueue.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/oom.h>
#include <linux/wait.h>
#include <linux/mm.h>
#include <linux/mount.h>
#include <linux/magic.h>

struct virtio_test {
    struct virtio_device *vdev;
    struct virtqueue *print_vq;

    struct work_struct print_val_work;
    bool stop_update;
    atomic_t stop_once;

    /* Waiting for host to ack the pages we released. */
    wait_queue_head_t acked;

    __virtio32 num[256];
};

static struct virtio_device_id id_table[] = {
    { VIRTIO_ID_TEST, VIRTIO_DEV_ANY_ID },
    { 0 },
};

static struct virtio_test *vb_dev;

static void test_ack(struct virtqueue *vq)
{
    struct virtio_test *vb = vq->vdev->priv;
    printk("virttest get ack\n");
    unsigned int len;
    virtqueue_get_buf(vq, &len);
}

static int init_vqs(struct virtio_test *vb)
{
    struct virtqueue *vqs[1];
    vq_callback_t *callbacks[] = { test_ack };  //当队列上的描述符被处理时，这个回调函数会被调用
    static const char * const names[] = { "print"}; //为队列命名
    int err, nvqs;

    nvqs = virtio_has_feature(vb->vdev, VIRTIO_TEST_F_CAN_PRINT) ? 1 : 0;
    err = virtio_find_vqs(vb->vdev, nvqs, vqs, callbacks, names, NULL);
    if (err)
        return err;

    vb->print_vq = vqs[0];

    return 0;
}

static void remove_common(struct virtio_test *vb)
{
    /* Now we reset the device so we can clean up the queues. */
    vb->vdev->config->reset(vb->vdev);

    vb->vdev->config->del_vqs(vb->vdev);
}

static void virttest_remove(struct virtio_device *vdev)
{
    struct virtio_test *vb = vdev->priv;

    remove_common(vb);
    cancel_work_sync(&vb->print_val_work);
    kfree(vb);
    vb_dev = NULL;
}

static int virttest_validate(struct virtio_device *vdev)
{
    return 0;
}

static void print_val_func(struct work_struct *work)
{
    struct virtio_test *vb;
    struct scatterlist sg;

    vb = container_of(work, struct virtio_test, print_val_work);
    printk("virttest get config change\n");

    struct virtqueue *vq = vb->print_vq;
    vb->num[0]++;
    sg_init_one(&sg, &vb->num[0], sizeof(vb->num[0]));

    /* We should always be able to add one buffer to an empty queue. */
    virtqueue_add_outbuf(vq, &sg, 1, vb, GFP_KERNEL);
    virtqueue_kick(vq);
}

static void virttest_changed(struct virtio_device *vdev)
{
    struct virtio_test *vb = vdev->priv;
    printk("virttest virttest_changed\n");
    if (!vb->stop_update) {
        //atomic_set(&vb->stop_once, 0);
        queue_work(system_freezable_wq, &vb->print_val_work);
    }
}

static int virttest_probe(struct virtio_device *vdev)
{
    struct virtio_test *vb;
    int err;

    printk("******create virttest\n");
    if (!vdev->config->get) {
        return -EINVAL;
    }

    vdev->priv = vb = kmalloc(sizeof(*vb), GFP_KERNEL);
    if (!vb) {
        err = -ENOMEM;
        goto out;
    }
    vb->num[0] = 0;
    vb->vdev = vdev;
    INIT_WORK(&vb->print_val_work, print_val_func);

    vb->stop_update = false;

    init_waitqueue_head(&vb->acked);
    err = init_vqs(vb);
    if (err)
        goto out_free_vb;

    virtio_device_ready(vdev);

    atomic_set(&vb->stop_once, 0);
    vb_dev = vb;

    return 0;

out_free_vb:
    kfree(vb);
out:
    return err;
}

static unsigned int features[] = {
    VIRTIO_TEST_F_CAN_PRINT,
};

static struct virtio_driver virtio_test_driver = {
    .feature_table = features,
    .feature_table_size = ARRAY_SIZE(features),
    .driver.name =  KBUILD_MODNAME,
    .driver.owner = THIS_MODULE,
    .id_table = id_table,
    .validate = virttest_validate,
    .probe =    virttest_probe,
    .remove =   virttest_remove,
    .config_changed = virttest_changed,
};

module_virtio_driver(virtio_test_driver);
MODULE_DEVICE_TABLE(virtio, id_table);
MODULE_DESCRIPTION("Virtio test driver");
MODULE_LICENSE("GPL");
```

**附qemu：**

```c
#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/iov.h"
#include "qemu/timer.h"
#include "hw/virtio/virtio.h"
#include "hw/virtio/virtio-test.h"
#include "sysemu/kvm.h"
#include "sysemu/hax.h"
#include "exec/address-spaces.h"
#include "qapi/error.h"
#include "qapi/qapi-events-misc.h"
#include "qapi/visitor.h"
#include "qemu/error-report.h"

#include "hw/virtio/virtio-bus.h"
#include "hw/virtio/virtio-access.h"
#include "migration/migration.h"

static void virtio_test_handle_output(VirtIODevice *vdev, VirtQueue *vq)
{
    VirtIOTest *s = VIRTIO_TEST(vdev);
    VirtQueueElement *elem;
    MemoryRegionSection section;

    for (;;) {
        size_t offset = 0;
        uint32_t pfn;
        elem = virtqueue_pop(vq, sizeof(VirtQueueElement));
        if (!elem) {
            return;
        }

        while (iov_to_buf(elem->out_sg, elem->out_num, offset, &pfn, 4) == 4) {
            int p = virtio_ldl_p(vdev, &pfn);

            offset += 4;
            qemu_log("=========get virtio num:%d\n", p);
        }

        virtqueue_push(vq, elem, offset);
        virtio_notify(vdev, vq);
        g_free(elem);
    }
}

static void virtio_test_get_config(VirtIODevice *vdev, uint8_t *config_data)
{
    VirtIOTest *dev = VIRTIO_TEST(vdev);
    struct virtio_test_config config;

    config.actual = cpu_to_le32(dev->actual);
    config.event = cpu_to_le32(dev->event);

    memcpy(config_data, &config, sizeof(struct virtio_test_config));

}

static void virtio_test_set_config(VirtIODevice *vdev,
                                      const uint8_t *config_data)
{
    VirtIOTest *dev = VIRTIO_TEST(vdev);
    struct virtio_test_config config;

    memcpy(&config, config_data, sizeof(struct virtio_test_config));
    dev->actual = le32_to_cpu(config.actual);
    dev->event = le32_to_cpu(config.event);
}

static uint64_t virtio_test_get_features(VirtIODevice *vdev, uint64_t f,
                                            Error **errp)
{
    VirtIOTest *dev = VIRTIO_TEST(vdev);
    f |= dev->host_features;
    virtio_add_feature(&f, VIRTIO_TEST_F_CAN_PRINT);

    return f;
}

static int virtio_test_post_load_device(void *opaque, int version_id)
{
    VirtIOTest *s = VIRTIO_TEST(opaque);

    return 0;
}

static const VMStateDescription vmstate_virtio_test_device = {
    .name = "virtio-test-device",
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = virtio_test_post_load_device,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(actual, VirtIOTest),
        VMSTATE_END_OF_LIST()
    },
};

static void test_stats_change_timer(VirtIOTest *s, int64_t secs)
{
    timer_mod(s->stats_timer, qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + secs * 1000);
}

static void test_stats_poll_cb(void *opaque)
{
    VirtIOTest *s = opaque;
    VirtIODevice *vdev = VIRTIO_DEVICE(s);

    qemu_log("==============set config:%d\n", s->set_config++);
    virtio_notify_config(vdev);
    test_stats_change_timer(s, 1);
}

static void virtio_test_device_realize(DeviceState *dev, Error **errp)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VirtIOTest *s = VIRTIO_TEST(dev);
    int ret;

    virtio_init(vdev, VIRTIO_ID_TEST, sizeof(struct virtio_test_config));

    s->ivq = virtio_add_queue(vdev, 128, virtio_test_handle_output);

    /* create a new timer */
    g_assert(s->stats_timer == NULL);
    s->stats_timer = timer_new_ms(QEMU_CLOCK_VIRTUAL, test_stats_poll_cb, s);
    test_stats_change_timer(s, 30);
}

static void virtio_test_device_unrealize(DeviceState *dev, Error **errp)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VirtIOTest *s = VIRTIO_TEST(dev);

    virtio_cleanup(vdev);
}

static void virtio_test_device_reset(VirtIODevice *vdev)
{
    VirtIOTest *s = VIRTIO_TEST(vdev);
}

static void virtio_test_set_status(VirtIODevice *vdev, uint8_t status)
{
    VirtIOTest *s = VIRTIO_TEST(vdev);
    return;
}

static void virtio_test_instance_init(Object *obj)
{
    VirtIOTest *s = VIRTIO_TEST(obj);

    return;
}

static const VMStateDescription vmstate_virtio_test = {
    .name = "virtio-test",
    .minimum_version_id = 1,
    .version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_VIRTIO_DEVICE,
        VMSTATE_END_OF_LIST()
    },
};

static Property virtio_test_properties[] = {
    DEFINE_PROP_END_OF_LIST(),
};

static void virtio_test_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtioDeviceClass *vdc = VIRTIO_DEVICE_CLASS(klass);

    dc->props_ = virtio_test_properties;
    dc->vmsd = &vmstate_virtio_test;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    vdc->realize = virtio_test_device_realize;
    vdc->unrealize = virtio_test_device_unrealize;
    vdc->reset = virtio_test_device_reset;
    vdc->get_config = virtio_test_get_config;
    vdc->set_config = virtio_test_set_config;
    vdc->get_features = virtio_test_get_features;
    vdc->set_status = virtio_test_set_status;
    vdc->vmsd = &vmstate_virtio_test_device;
}

static const TypeInfo virtio_test_info = {
    .name = TYPE_VIRTIO_TEST,
    .parent = TYPE_VIRTIO_DEVICE,
    .instance_size = sizeof(VirtIOTest),
    .instance_init = virtio_test_instance_init,
    .class_init = virtio_test_class_init,
};

static void virtio_register_types(void)
{
    type_register_static(&virtio_test_info);
}

type_init(virtio_register_types)
```

**运行结果：**

```c
# 
# ==============set config:0
[   29.872211] virttest virttest_changed
[   29.872661] virttest get config change
=========get virtio num:1
[   29.872808] virttest get ack
==============set config:1
[   30.872490] virttest virttest_changed
[   30.872675] virttest get config change
=========get virtio num:2
[   30.872775] virttest get ack
==============set config:2
[   31.872211] virttest virttest_changed
[   31.872508] virttest get config change
=========get virtio num:3
[   31.872609] virttest get ack
==============set config:3
[   32.872568] virttest virttest_changed
[   32.872813] virttest get config change
=========get virtio num:4
[   32.872959] virttest get ack
==============set config:4
[   33.872574] virttest virttest_changed
[   33.872915] virttest get config change
=========get virtio num:5
[   33.873098] virttest get ack
==============set config:5
[   34.872579] virttest virttest_changed
[   34.872909] virttest get config change
=========get virtio num:6
[   34.873165] virttest get ack
==============set config:6
[   35.872644] virttest virttest_changed
[   35.872980] virttest get config change
=========get virtio num:7
[   35.873131] virttest get ack
==============set config:7
[   36.872088] virttest virttest_changed
[   36.872392] virttest get config change
=========get virtio num:8
[   36.872586] virttest get ack
==============set config:8
[   37.872583] virttest virttest_changed
[   37.872925] virttest get config change
=========get virtio num:9
[   37.873042] virttest get ack
==============set config:9
```