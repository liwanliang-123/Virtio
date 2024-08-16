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


#if 0
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




#endif





