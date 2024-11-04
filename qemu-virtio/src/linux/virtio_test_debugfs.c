#include <linux/virtio.h>
#include <linux/virtio_test.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/miscdevice.h>
#if defined(CONFIG_DEBUG_FS)
#include <linux/debugfs.h>
#endif

#define MISC_NAME "virtio_misc"
#define MISC_MINOR  144

struct test_request {
	__virtio32 arg1;
    char arg2[32];
};

struct  test_response {
	__virtio32 ret;
};

struct virtio_test {
    struct test_request req;
    struct test_response res;
    struct virtio_device *vdev;
    struct virtqueue *factorial_vq;
    #if defined(CONFIG_DEBUG_FS)
    struct dentry *virtio_test_debugfs;
    #endif
};

static struct virtio_test *vt_dev;

static void print_response_data(struct virtio_test *vt)
{
    printk("virtio response ret is %d\n",vt->res.ret);
}

/* Called from virtio device, in IRQ context */
static void test_request_done(struct virtqueue *vq)
{
    uint32_t len;
    struct virtio_test *vt;
    printk(" %s called, line: %d \n", __func__, __LINE__);

	do {
		virtqueue_disable_cb(vq);
		while ((vt = virtqueue_get_buf(vq, &len)) != NULL) {
			// request packet will be completed by response packet
            print_response_data(vt);
		}
		if (unlikely(virtqueue_is_broken(vq)))
			break;
	} while (!virtqueue_enable_cb(vq));
}

static void build_test_request(struct virtio_test *vt, uint32_t num)
{
    vt->req.arg1 = num;
    strncpy(vt->req.arg2, "hello back end!",
                            sizeof(vt->req.arg2));
}

static void virtio_test_submit_request(uint32_t num)
{
    struct virtqueue *vq;
    struct virtio_test *vt;
    struct scatterlist out_sg, in_sg, *sgs[2];

	int num_out = 0, num_in = 0;

    vt = vt_dev;
    vq = vt->factorial_vq;

    build_test_request(vt, num);

    sg_init_one(&out_sg, &vt->req, sizeof(vt->req));
    sgs[num_out++] = &out_sg;
	sg_init_one(&in_sg, &vt->res, sizeof(vt->res));
	sgs[num_out + num_in++] = &in_sg;

    /* We should always be able to add one buffer to an empty queue. */
	virtqueue_add_sgs(vq, sgs, num_out, num_in, vt, GFP_ATOMIC);
    virtqueue_kick(vq);
}

static int init_vqs(struct virtio_test *vt)
{
    int err, nvqs;
    struct virtqueue *vqs[1];
    vq_callback_t *callbacks[] = { test_request_done };
    const char * const names[] = { "virtio_test"};

    nvqs = virtio_has_feature(vt->vdev, VIRTIO_TEST_F_CAN_PRINT) ? 1 : 0;
    err = virtio_find_vqs(vt->vdev, nvqs, vqs, callbacks, names, NULL);
    if (err)
        return err;

    vt->factorial_vq = vqs[0];

    return 0;
}

static void remove_common(struct virtio_test *vt)
{
    vt->vdev->config->reset(vt->vdev);
    vt->vdev->config->del_vqs(vt->vdev);
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
    uint32_t factorial[1];

    ret = copy_from_user(factorial, buf, count);
    if(ret < 0)
        return -EINVAL;

    virtio_test_submit_request(factorial[0]);

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

#if defined(CONFIG_DEBUG_FS)
static int _hang_show(struct seq_file *m, void *v)
{
	return 0;
}

static int virtio_test_open(struct inode *inode, struct file *filp)
{
	return single_open(filp, _hang_show, inode->i_private);
}

static ssize_t virtio_test_write(struct file *filp, const char __user *buf,
			      size_t count, loff_t *ppos)
{
    int ret;
    uint32_t factorial[1];

	ret = copy_from_user(factorial, buf, count);
    if (ret < 0) {
		printk("virtio-test:failed to get data from user\n");
		return -EFAULT;
	}

    printk("virtio-test: factorial = %d\n", factorial[0]);

    // virtio_test_submit_request(factorial[0]);
	return 0;
}

static const struct file_operations virtio_test_fops = {
    .owner = THIS_MODULE,
    .open = virtio_test_open,
    .write = virtio_test_write,
};
#endif

static int virttest_probe(struct virtio_device *vdev)
{
    int err;
    struct virtio_test *vt;

    if (!vdev->config->get) {
        return -EINVAL;
    }

    vdev->priv = vt = kmalloc(sizeof(*vt), GFP_KERNEL);
    if (!vt) {
        err = -ENOMEM;
        goto out;
    }

    vt->vdev = vdev;

    err = init_vqs(vt);
    if (err)
        goto out_free_vt;

    virtio_device_ready(vdev);

    vt_dev = vt;

    /* misc driver registered */
    err = misc_register(&virtio_miscdev);
    if(err < 0) {
        printk( "misc register is failed\n");
        goto out_free_misc;
    }
    printk( "misc register has succeeded\n");

#if defined(CONFIG_DEBUG_FS)
	const char *dir_name = "virtio_test";
	vt->virtio_test_debugfs = debugfs_create_dir(dir_name, NULL);
	if (IS_ERR(vt->virtio_test_debugfs)) {
		printk("debugfs_create_dir failed: %ld\n",
		       PTR_ERR(vt->virtio_test_debugfs));
		err = PTR_ERR(vt->virtio_test_debugfs);
		goto out_free_misc;
	}

	debugfs_create_file("test", 0600, vt->virtio_test_debugfs, NULL,
			    &virtio_test_fops);
#endif

	printk("virtio test initialized\n");
    return 0;

out_free_misc:
    misc_deregister(&virtio_miscdev);
out_free_vt:
    kfree(vt);
out:
    return err;
}

static void virttest_remove(struct virtio_device *vdev)
{
    struct virtio_test *vt = vdev->priv;

    remove_common(vt);
    kfree(vt);
    vt_dev = NULL;
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
    .probe =    virttest_probe,
    .remove =   virttest_remove,
};

module_virtio_driver(virtio_test_driver);
MODULE_DEVICE_TABLE(virtio, id_table);
MODULE_DESCRIPTION("Virtio test driver");
MODULE_LICENSE("GPL");