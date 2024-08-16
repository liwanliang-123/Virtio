# **Virtio 代码分析**

## 一、Virtio Front End 代码初始化流程

### 1、virtio_find_vqs

​        虚拟机在启动过程中，当 virtio bus 上的 driver 和 device 匹配时，virttest_probe函数被调用，（前端创建的virtio设备都是PCI设备，因此，在对应的virtio设备的probe函数调用之前，都会调用virtio-pci设备的probe函数，在系统中先插入一个virtio-pci设备）。

```c
virttest_probe()
	--->init_vqs(vb)
    	---> virtio_find_vqs(vb->vdev, nvqs, vqs, callbacks, names, NULL)
			---> return vdev->config->find_vqs(vdev, nvqs, vqs, callbacks, names, NULL, desc)
```

在 virtio_find_vqs 函数中调用了vdev->config中的find_vqs函数，并且将初始化的一些参数传递到find_vqs，该函数定义在 /driver/virtio/virtio_ pci_legacy.c 文件中，定义如下：

```c
static const struct virtio_config_ops virtio_pci_config_ops = {
    .get        = vp_get,
    .set        = vp_set,
    .get_status = vp_get_status,
    .set_status = vp_set_status,
    .reset      = vp_reset,
    .find_vqs   = vp_find_vqs,
    .del_vqs    = vp_del_vqs,
    .get_features   = vp_get_features,
    .finalize_features = vp_finalize_features,
    .bus_name   = vp_bus_name,
    .set_vq_affinity = vp_set_vq_affinity,
    .get_vq_affinity = vp_get_vq_affinity,
};
```

​	find_vqs 函数首先会为给定的 virtio 设备查找和初始化虚拟队列，并先尝试使用 MSI-X（尽可能为每个队列提供一个中断向量），如果不成功则回退到传统的 INTx 中断。

```C
/* the config->find_vqs() implementation */
int vp_find_vqs(struct virtio_device *vdev, unsigned nvqs,
        struct virtqueue *vqs[], vq_callback_t *callbacks[],
        const char * const names[], const bool *ctx,
        struct irq_affinity *desc)
{
    int err;
    /* Try MSI-X with one vector per queue. */
    err = vp_find_vqs_msix(vdev, nvqs, vqs, callbacks, names, true, ctx, desc);
    if (!err)
        return 0;
    /* Fallback: MSI-X with one vector for config, one shared for queues. */
    err = vp_find_vqs_msix(vdev, nvqs, vqs, callbacks, names, false, ctx, desc);
    if (!err)
        return 0;
    /* Finally fall back to regular interrupts. */
    return vp_find_vqs_intx(vdev, nvqs, vqs, callbacks, names, ctx);
}
```

​	可以看到vp_find_vqs是依次尝试不同的中断模式，这里就以传统中断为例，进入到 vp_find_vqs_intx 函数，首先会根据传递进来的参数进行中断的申请和回调函数的注册，关于这里为什么要注册中断，后面在调用的流程中说明。

```c
err = request_irq(vp_dev->pci_dev->irq, vp_interrupt, IRQF_SHARED,dev_name(&vdev->dev), vp_dev);
```

注册完中断又调用了 vp_setup_vq 函数，该函数主要是用来创建和初始化 vring_virtqueue 的，函数的详细分析过程请看 2 小节，代码如下：

```c
for (i = 0; i < nvqs; ++i) {
......
        vqs[i] = vp_setup_vq(vdev, queue_idx++, callbacks[i], names[i],ctx ? ctx[i] : false, VIRTIO_MSI_NO_VECTOR);
......
}
static struct virtqueue *vp_setup_vq(......)
{
......
    vq = vp_dev->setup_vq(vp_dev, info, index, callback, name, ctx,msix_vec);
......
}
```

### 2、setup_vq

接上一小节，setup_vq主要是为virtio设备创建vring_virtqueue队列，函数的初始化在 virtio_pci_ legacy_probe 函数中，如下：

```c
int virtio_pci_legacy_probe(struct virtio_pci_device *vp_dev)
{
......
    vp_dev->setup_vq = setup_vq;
......
}
```

函数原型如下：

```c
static struct virtqueue *setup_vq(struct virtio_pci_device *vp_dev,
                  struct virtio_pci_vq_info *info,
                  unsigned index,
                  void (*callback)(struct virtqueue *vq),
                  const char *name,
                  bool ctx,
                  u16 msix_vec)；
```

​        因为在前端虚拟机内创建的virtio设备都是基于PCI的，所以也是一个PCI设备，因此可以利用PCI设备的配置空间来完成前后端之间的消息通知，vp_dev->ioaddr就指向了PCI配置空间寄存器集的首地址。有关PCI的知识大家可以点击这里：

```c
/* Select the queue we're interested in */
iowrite16(index, vp_dev->ioaddr + VIRTIO_PCI_QUEUE_SEL);
/* Check if queue is either not available or already active. */
num = ioread16(vp_dev->ioaddr + VIRTIO_PCI_QUEUE_NUM);
```

　    如上代码所示，首先通过iowrite写寄存器VIRTIO_PCI_QUEUE_SEL来通知QEMU后端，当前初始化的是第index号vring_virtqueue；而ioread则是读取QEMU端的vring_desc表，共有多少项，关于这个num的大小后面在分析QEMU后端代码的时候再分析，这里先暂时跳过。

```c
static void *vring_alloc_queue(struct virtio_device *vdev, size_t size,
			      dma_addr_t *dma_handle, gfp_t flag)
{
	if (vring_use_dma_api(vdev)) {
		return dma_alloc_coherent(vdev->dev.parent, size,
					  dma_handle, flag);
	} else {
		void *queue = alloc_pages_exact(PAGE_ALIGN(size), flag);

		if (queue) {
			phys_addr_t phys_addr = virt_to_phys(queue);
			*dma_handle = (dma_addr_t)phys_addr;

			/*
			 * Sanity check: make sure we dind't truncate
			 * the address.  The only arches I can find that
			 * have 64-bit phys_addr_t but 32-bit dma_addr_t
			 * are certain non-highmem MIPS and x86
			 * configurations, but these configurations
			 * should never allocate physical pages above 32
			 * bits, so this is fine.  Just in case, throw a
			 * warning and abort if we end up with an
			 * unrepresentable address.
			 */
			if (WARN_ON_ONCE(*dma_handle != phys_addr)) {
				free_pages_exact(queue, PAGE_ALIGN(size));
				return NULL;
			}
		}
		return queue;
	}
}

/* create the vring */
vq = vring_create_virtqueue(......)
   ---> static struct virtqueue *vring_create_virtqueue_split(......)
        {
            /* TODO: allocate each queue chunk individually */
            for (; num && vring_size(num, vring_align) > PAGE_SIZE; num /= 2) {
                queue = vring_alloc_queue(vdev, vring_size(num, vring_align),
                              &dma_addr,
                              GFP_KERNEL|__GFP_NOWARN|__GFP_ZERO);
            }
        }
```

根据之前ioread获得的表项数num来确定vring共享区域的大小，然后调用vring_create_virtqueue函数，并调用到vring_create_virtqu eue_split 函数，在该函数里面首先会调用vring_alloc_queue函数中的alloc_pages_exact函数在虚拟机里为vring_virtqueue分配内存空间，virt_to_phys(info->queue) 是将虚拟机的虚拟机地址转换成物理地址。

分配完内存空间之后就需要调用 vring_init 函数完成guest内部vring的初始化，代码如下：

```c
vring_init(&vring, num, queue, vring_align);
```

```c
static __inline__ void vring_init(struct vring *vr, unsigned int num, void *p,unsigned long align)
{
    vr->num = num;
    vr->desc = p;
    vr->avail = (struct vring_avail *)((char *)p + num * sizeof(struct vring_desc));
    vr->used = (void *)(((uintptr_t)&vr->avail->ring[num] + sizeof(__virtio16)
        \+ align-1) & ~(align - 1));
}
```

​	调用__vring_new_virtqueue函数会对vring_virtqueue结构体和vring_virtqueue->virtqueue结构体进行设置，比如callback函数就是在这里初始化的，后面调用就是通过vq->vq.callback进行调用，notify也是同理。

```c
vq = __vring_new_virtqueue(index, vring, vdev, weak_barriers, context,
                   notify, callback, name);
```

```c
/* Only available for split ring */
struct virtqueue *__vring_new_virtqueue(unsigned int index,
                    struct vring vring,
                    struct virtio_device *vdev,
                    bool weak_barriers,
                    bool context,
                    bool (*notify)(struct virtqueue *),
                    void (*callback)(struct virtqueue *),
                    const char *name)
{
    unsigned int i;
    struct vring_virtqueue *vq;
    if (virtio_has_feature(vdev, VIRTIO_F_RING_PACKED))
        return NULL;
    vq = kmalloc(sizeof(*vq), GFP_KERNEL);
    if (!vq)
        return NULL;
    vq->packed_ring = false;
    vq->vq.callback = callback;
    vq->vq.vdev = vdev;
    vq->vq.name = name;
    vq->vq.num_free = vring.num;
    vq->vq.index = index;
    vq->we_own_ring = false;
    vq->notify = notify;
    vq->weak_barriers = weak_barriers;
    vq->broken = false;
    vq->last_used_idx = 0;
    vq->num_added = 0;
	......
}
```

回到setup_vq函数中

```c
q_pfn = virtqueue_get_desc_addr(vq) >> VIRTIO_PCI_QUEUE_ADDR_SHIFT;
```

```c
dma_addr_t virtqueue_get_desc_addr(struct virtqueue *_vq)
{
	struct vring_virtqueue *vq = to_vvq(_vq);
	BUG_ON(!vq->we_own_ring);
	if (vq->packed_ring)
		return vq->packed.ring_dma_addr;
	return vq->split.queue_dma_addr;
}
```

上面代码段首先调用virtqueue_get_desc_addr用来获取vring_virtqueue的物理地址的页号, 根据不同的配置返回不同的值 。

```c
/* activate the queue */
iowrite32(q_pfn, vp_dev->ioaddr + VIRTIO_PCI_QUEUE_PFN);
```

​	这里的iowrite会将vring_virtqueue在虚拟机内的物理页号写到PCI配置空间中的VIRTIO_PCI_QUEUE_PFN寄存器中，然后会产生一个kvm_exit，QEMU端会捕获这个exit，并根据寄存器的地址将这个物理页号赋值给QEMU端维护的virtqueue，然后QEMU会根据这个物理页号初始化vring， 这样前后端就相当于是共享了一块物理内存，这样前后端就关联起来了。

继续分析setup_vq

```c
vq->priv = (void __force *)vp_dev->ioaddr + VIRTIO_PCI_QUEUE_NOTIFY;
```

该代码段是用于guest notify host 时，通过写PCI配置空间中的VIRTIO_PCI_QUEUE_NOTIFY寄存器来实现前端guest通知后端QEMU的功能，该代码段在vp_notify函数中被使用，vp_notify函数在__vring_new_virtqueue函数中被设置，函数实现如下：

```c
/* the notify function used when creating a virt queue */
bool vp_notify(struct virtqueue *vq)
{
    /* we write the queue's selector into the notification register to
     \* signal the other end */
    iowrite16(vq->index, (void __iomem *)vq->priv);
    return true;
}
```

​	可以看到，实际上前端通知后端仅仅是把队列的index写入到VIRTIO_PCI_QUEUE_NOTIFY寄存器中,这样在后端qemu就会知道是哪个队列发生了add buffer，然后就从对应队列的buffer取出数据。

好了，virtio前端初始化代码已经分析得差不多了，总结下来主要做了这几件事情：

1. 注册一个中断函数，为 QEMU host 通知 guest 而服务
2. 根据读取QEMU寄存器的值，注册并且初始化一个num大小的vring为前端服务
3. 将前端注册的vring_virtqueue的物理页号通过写PCI  VIRTIO_PCI_QUEUE_PFN 寄存器传递到QEMU
4. 将驱动传递的callback、name等相关参数注册到virtqueue中去

## 二、Virtio Back End 代码初始化流程

首先进入到virtio_test_device_realize函数，该函数是在 QEMU 虚拟机中初始化设备时，由 QEMU 的设备初始化流程调用的，直接进入到该函数。

```c
static void virtio_test_device_realize(DeviceState *dev, Error **errp)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VirtIOTest *s = VIRTIO_TEST(dev);
    int ret;

    virtio_init(vdev, VIRTIO_ID_TEST, sizeof(struct virtio_test_config));
    s->ivq = virtio_add_queue(vdev, test_queue_size, virtio_test_handle_output);
}
```

virtio_test_device_realize函数主要是对virtio进行相关的设置和初始化

virtio_init函数主要是对VirtIODevice结构体进行相关的初始化，比如设置 vdev->device_id 或者 vdev->config_len 等

下面对 virtio_add_queue 函数进行分析：

```c
VirtQueue *virtio_add_queue(VirtIODevice *vdev, int queue_size,
                            VirtIOHandleOutput handle_output)
{
    int i;
    for (i = 0; i < VIRTIO_QUEUE_MAX; i++) {
        if (vdev->vq[i].vring.num == 0)
            break;
    }
    if (i == VIRTIO_QUEUE_MAX || queue_size > VIRTQUEUE_MAX_SIZE)
        abort();
    vdev->vq[i].vring.num = queue_size;
    vdev->vq[i].vring.num_default = queue_size;
    vdev->vq[i].vring.align = VIRTIO_PCI_VRING_ALIGN;
    vdev->vq[i].handle_output = handle_output;
    vdev->vq[i].used_elems = g_new0(VirtQueueElement, queue_size);
    return &vdev->vq[i];
}
```

virtio_add_queue函数主要是对qemu端的vring进行相关的初始化，VIRTIO_QUEUE_MAX是Queue的最大值，即1024，queue_size是用户自定义的，也及是 vring->desc的表项数，（其实在上面的分析中前端读取的num就是这里设置的这个值，具体为什么是这个值后面再分析），vdev->vq[i].handle_output是设置QEMU的回调函数，当前端有消息通知后端时，这个回调函数就会被调用，调用流程也在后面分析。

后端qemu关于virtio相关的初始化大概就是这么多，关于qemu的一些初始化这里没有展开分析。

## 三、ioread/iowrite 函数

​	根据前面分析，当前端调用ioread16 (vp_dev->ioaddr + VIRTIO_PCI_QUEUE_NUM)时，返回的为什么是qemu中的vdev->vq[i] .vring. num，那为什么是这个值呢？以及前端是通过什么样的方式进行通知后端的、怎么完成前后端共享内存的建立的？

​	在前节就有提到，当在虚拟机内调用ioread/iowrite时，就会产生kvm_exit，然后kvm会根据exit的原因将退出的数据进行分发，IO请求会被发送到相应的监听函数，然后调用virtio_ioport_write/read来确定前端读/写了哪个寄存器，触发了何种动作。

virtio_ioport_write对应前端的iowrite操作，virtio_ioport_read对应前端的ioread操作，详细代码如下所示：

```c
static void virtio_ioport_write(void *opaque, uint32_t addr, uint32_t val)
{
    VirtIOPCIProxy *proxy = opaque;
    VirtIODevice *vdev = virtio_bus_get_device(&proxy->bus);
    uint16_t vector;
    hwaddr pa;
    switch (addr) {
    case VIRTIO_PCI_GUEST_FEATURES:
        /* Guest does not negotiate properly?  We have to assume nothing. */
        if (val & (1 << VIRTIO_F_BAD_FEATURE)) {
            val = virtio_bus_get_vdev_bad_features(&proxy->bus);
        }
        virtio_set_features(vdev, val);
        break;
    case VIRTIO_PCI_QUEUE_PFN:
        pa = (hwaddr)val << VIRTIO_PCI_QUEUE_ADDR_SHIFT;
        if (pa == 0) {
            virtio_pci_reset(DEVICE(proxy));
        }
        else
            virtio_queue_set_addr(vdev, vdev->queue_sel, pa);
        break;
    case VIRTIO_PCI_QUEUE_SEL:
        if (val < VIRTIO_QUEUE_MAX)
            vdev->queue_sel = val;
        break;
    case VIRTIO_PCI_QUEUE_NOTIFY:
        if (val < VIRTIO_QUEUE_MAX) {
            virtio_queue_notify(vdev, val);
        }
        break;
    case VIRTIO_PCI_STATUS:
        if (!(val & VIRTIO_CONFIG_S_DRIVER_OK)) {
            virtio_pci_stop_ioeventfd(proxy);
        }

        virtio_set_status(vdev, val & 0xFF);

        if (val & VIRTIO_CONFIG_S_DRIVER_OK) {
            virtio_pci_start_ioeventfd(proxy);
        }

        if (vdev->status == 0) {
            virtio_pci_reset(DEVICE(proxy));
        }

        /* Linux before 2.6.34 drives the device without enabling
           the PCI device bus master bit. Enable it automatically
           for the guest. This is a PCI spec violation but so is
           initiating DMA with bus master bit clear. */
        if (val == (VIRTIO_CONFIG_S_ACKNOWLEDGE | VIRTIO_CONFIG_S_DRIVER)) {
            pci_default_write_config(&proxy->pci_dev, PCI_COMMAND,
                                     proxy->pci_dev.config[PCI_COMMAND] |
                                     PCI_COMMAND_MASTER, 1);
        }
        break;
    case VIRTIO_MSI_CONFIG_VECTOR:
        if (vdev->config_vector != VIRTIO_NO_VECTOR) {
            msix_vector_unuse(&proxy->pci_dev, vdev->config_vector);
        }
        /* Make it possible for guest to discover an error took place. */
        if (val < proxy->nvectors) {
            msix_vector_use(&proxy->pci_dev, val);
        } else {
            val = VIRTIO_NO_VECTOR;
        }
        vdev->config_vector = val;
        break;
    case VIRTIO_MSI_QUEUE_VECTOR:
        vector = virtio_queue_vector(vdev, vdev->queue_sel);
        if (vector != VIRTIO_NO_VECTOR) {
            msix_vector_unuse(&proxy->pci_dev, vector);
        }
        /* Make it possible for guest to discover an error took place. */
        if (val < proxy->nvectors) {
            msix_vector_use(&proxy->pci_dev, val);
        } else {
            val = VIRTIO_NO_VECTOR;
        }
        virtio_queue_set_vector(vdev, vdev->queue_sel, val);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: unexpected address 0x%x value 0x%x\n",
                      __func__, addr, val);
        break;
    }
}
static uint32_t virtio_ioport_read(VirtIOPCIProxy *proxy, uint32_t addr)
{
    VirtIODevice *vdev = virtio_bus_get_device(&proxy->bus);
    uint32_t ret = 0xFFFFFFFF;

    switch (addr) {
    case VIRTIO_PCI_HOST_FEATURES:
        ret = vdev->host_features;
        break;
    case VIRTIO_PCI_GUEST_FEATURES:
        ret = vdev->guest_features;
        break;
    case VIRTIO_PCI_QUEUE_PFN:
        ret = virtio_queue_get_addr(vdev, vdev->queue_sel)
              >> VIRTIO_PCI_QUEUE_ADDR_SHIFT;
        break;
    case VIRTIO_PCI_QUEUE_NUM:
        ret = virtio_queue_get_num(vdev, vdev->queue_sel);
        break;
    case VIRTIO_PCI_QUEUE_SEL:
        ret = vdev->queue_sel;
        break;
    case VIRTIO_PCI_STATUS:
        ret = vdev->status;
        break;
    case VIRTIO_PCI_ISR:
        /* reading from the ISR also clears it. */
        ret = qatomic_xchg(&vdev->isr, 0);
        pci_irq_deassert(&proxy->pci_dev);
        break;
    case VIRTIO_MSI_CONFIG_VECTOR:
        ret = vdev->config_vector;
        break;
    case VIRTIO_MSI_QUEUE_VECTOR:
        ret = virtio_queue_vector(vdev, vdev->queue_sel);
        break;
    default:
        break;
    }

    return ret;
}
```

​	简单的说就是当前端调用iowrite/ioread操作时，会产生kvm_exit，然后就会对应到后端的这两个函数，根据不同的操作走不同的case，比如当前端调用ioread16 (vp_dev->ioaddr + VIRTIO_PCI_QUEUE_NUM) 时，就会调用到后端virtio_ioport_read函数中的virtio_queue_get_num(vdev, vdev->queue_sel)函数，而vdev->queue_sel则是通过前端调用iowrite16(index, vp_dev->ioaddr + VIRTIO_PCI_QUEUE_SEL) 进行设置的。

设置vdev->queue_sel：

```c
static void virtio_ioport_write(void *opaque, uint32_t addr, uint32_t val)
{
    case VIRTIO_PCI_QUEUE_SEL:
        if (val < VIRTIO_QUEUE_MAX)
            vdev->queue_sel = val;
        break;
}
```

 virtio_queue_get_num函数实现如下：

```c
static uint32_t virtio_ioport_read(VirtIOPCIProxy *proxy, uint32_t addr)
{
    case VIRTIO_PCI_QUEUE_NUM:
        ret = virtio_queue_get_num(vdev, vdev->queue_sel);
        break;
}
int virtio_queue_get_num(VirtIODevice *vdev, int n)
{
    return vdev->vq[n].vring.num;
}
```

可以看到virtio_queue_get_num函数就是将virtio_add_queue函数中初始化好的 vdev->vq[i].vring.num值返回给前端，然后前端根据返回的num值进行相关的初始化，n 表示第几项，由VIRTIO_PCI_QUEUE_SEL进行设置。

同理可得，当前端调用 iowrite32(q_pfn, vp_dev->ioaddr + VIRTIO_PCI_QUEUE_PFN) 时，也会产生kvm_exit，后端会进入下面的VIRTIO_PCI_QUEUE_PFN 分支

```c
static void virtio_ioport_write(void *opaque, uint32_t addr, uint32_t val)
{
    case VIRTIO_PCI_QUEUE_PFN:
        pa = (hwaddr)val << VIRTIO_PCI_QUEUE_ADDR_SHIFT;
        if (pa == 0) {
            virtio_pci_reset(DEVICE(proxy));
        }
        else
            virtio_queue_set_addr(vdev, vdev->queue_sel, pa);
        break;
}
void virtio_queue_set_addr(VirtIODevice *vdev, int n, hwaddr addr)
{
    if (!vdev->vq[n].vring.num) {
        return;
    }
    vdev->vq[n].vring.desc = addr;
    virtio_queue_update_rings(vdev, n);
}
/* virt queue functions */
void virtio_queue_update_rings(VirtIODevice *vdev, int n)
{
    VRing *vring = &vdev->vq[n].vring;

    if (!vring->num || !vring->desc || !vring->align) {
        /* not yet setup -> nothing to do */
        return;
    }
    vring->avail = vring->desc + vring->num * sizeof(VRingDesc);
    vring->used = vring_align(vring->avail +
                              offsetof(VRingAvail, ring[vring->num]),
                              vring->align);
    virtio_init_region_cache(vdev, n);
}
```

​	VIRTIO_PCI_QUEUE_PFN 分支的作用就是从PCI配置空间的对应位置获取前端写入的物理页号（guest的物理页），然后根据获取的页号对后端的vdev->vq[n].vring进行相关的设置，virtio_queue_update_rings 函数用来初始化vring->avail和vring->used 。

​	virtio_queue_set_addr函数将QEMU进程维护的virtio设备的virtqueue地址初始化，使得前端和后端指向同一片地址空间，完成virtio前后端共享内存的建立。（因为在KVM下的虚拟机是QEMU的一个进程，所以虚拟机的内存是通过QEMU进程来分配的，当QEMU进程知道了虚拟机创建的VRING的GPA就可以通过简单的转换在自己的HVA地址中找到VRING的内存地址，也是根据这样的特性，才能完成共享内存地址空间的建立）。

​	同样的，当前端调用notify函数时，也是调用iowrite对VIRTIO_PCI_QUEUE_NOTIFY寄存器进行写操作，（参数vq->priv在前端初始化时已经提及到，这里不在说明。）

```c
/* the notify function used when creating a virt queue */
bool vp_notify(struct virtqueue *vq)
{
    /* we write the queue's selector into the notification register to
     \* signal the other end */
    iowrite16(vq->index, (void __iomem *)vq->priv);
    return true;
}
```

对应QEMU 后端：

```c
static void virtio_ioport_write(void *opaque, uint32_t addr, uint32_t val)
{
    case VIRTIO_PCI_QUEUE_NOTIFY:
        if (val < VIRTIO_QUEUE_MAX) {
            virtio_queue_notify(vdev, val);
        }
        break;
}
void virtio_queue_notify(VirtIODevice *vdev, int n)
{
    VirtQueue *vq = &vdev->vq[n];

    if (unlikely(!vq->vring.desc || vdev->broken)) {
        return;
    }
    trace_virtio_queue_notify(vdev, vq - vdev->vq, vq);
    if (vq->host_notifier_enabled) {
        event_notifier_set(&vq->host_notifier);
    } else if (vq->handle_output) {
        vq->handle_output(vdev, vq);    //在这里最终调用了 host 端的回调函数
        if (unlikely(vdev->start_on_kick)) {
            virtio_set_started(vdev, true);
        }
    }
}
```

​	在上面代码 virtio_queue_notify 函数中vq->handle_output(vdev, vq)其实就是在上面virtio_add_queue函数中初始化的vdev->vq[i].handle_ output函数，也就是用户自定义的函数，这样也就能够说明为什么前端notify之后，后端的handle_output会被调用的原因了。

## 四、Virtio Front End 数据传输流程

​	在前面几节介绍了virtio 前后端之间通信需要做的一些初始化流程，相当于是为后面的数据传输做服务，这样才能让数据能够正确的传输，在本小节将介绍前端数据传输的流程，分析前端是怎样添加数据的，怎样通知后端的。

注意：以下分析的代码是笔者自己编写的一个简单的前后端数据传输代码，可做参考，但没有实际意义。

```c
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
static void build_test_request(struct virtio_test *vb, unsigned char num)
{
	vb->req.type = num++;
    vb->req.arg1 = num++;
    vb->req.arg2 = num++;
    vb->req.arg3 = num;
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
```

​	在上面代码段中 virtio_test_submit_request 就是用来提交前端数据到vring，并通知后端读取数据的函数，参数num是用户传递的一个参数，进入到函数里面， build_test_request 函数是用来构建请求包的，也就是初始化virtio_test vb->req中的相关参数，（这里是根据传入的num进行加一）。

```c
sg_init_one(&out_sg, &vb->req, sizeof(vb->req));
sgs[num_out++] = &out_sg;
sg_init_one(&in_sg, &vb->resp, sizeof(vb->resp));
sgs[num_out + num_in++] = &in_sg;
```

​	virtio前后端数据传输需要通过Linux内核的scatter-gather（SG）列表来进行管理。（scatter-gather列表是一种数据结构，用于将多个不连续的内存块组合成一个逻辑上的连续块，以便进行数据传输）。

​	上面代码段使用`sg_init_one`函数初始化两个SG条目`out_sg`和`in_sg`，分别指向`vb->req`和`vb->resp`，并设置其大小为`sizeof(vb->req)`和`sizeof(vb->resp)`。`vb->req`的内容即为一个请求数据包，用于写入到后端PCI设备，而`vb->resp`则是用于存放从设备接收到的响应数据。

​	`sgs[num_out++] = &out_sg`这行代码是将`out_sg`的地址添加到`sgs`数组中。`num_out`是一个索引，表示添加到列表中的输出SG条目的数量。通过`num_out++`，确保下一个输出SG条目将被添加到数组的下一个位置。

​	`sgs[num_out + num_in++] = &in_sg`代码段则是将`in_sg`添加到`sgs`数组中，添加的位置是基于已添加的`num_out`和`num_in`之和，这里num_in 初始化为 0，所以 `in_sg` 被添加到了`out_sg`的后面，在这里`sgs`数组的前半部分也就是sgs[0]用于存储输出SG条目，而后半部分sgs[1]用于存储输入SG条目。通过`num_in++`，我们确保下一个输入SG条目被添加到数组的适当位置。

```c
virtqueue_add_sgs(vq, sgs, num_out, num_in, vb, GFP_ATOMIC);
```

- `vq`: 指向一个`virtqueue`结构体的指针，这个结构体就是host和guest之间通信的一个虚拟队列。
- `sgs`: 指向一个`scatterlist`结构体数组的指针，表示`scatterlist`元素指向内存中的一个物理地址非连续区域，也就是上面填充的sgs[2]数组。
- `num_out`: 指定了`sgs`数组中用于输出的`scatterlist`的数量。
- `num_in`: 指定了`sgs`数组中用于输入的`scatterlist`的数量。
- `vb`: struct virtio_test 类型的一个结构体。
- `GFP_ATOMIC`: 表示这个操作应该在原子上下文中进行，不能睡眠（即不能等待I/O操作或内存分配）。

virtqueue_add_sgs 函数主要将一组散列列表添加到虚拟队列`vq`中。

在virtqueue_add_sgs函数中又调用了virtqueue_add函数，该函数主要是完成将新的数据更新到 vring_virtqueue->vring的具体实现。

​	到目前为止request数据已经添加到了vring_virtqueue->vring 中，下一步就需要通知QEMU后端到vring中读取数据了，具体的通知流程如下所示：

```c
virtqueue_kick(vq);
	--->if (virtqueue_kick_prepare(vq))
		return virtqueue_notify(vq);
            --->struct vring_virtqueue *vq
            --->vq->notify(_vq) //最后调用了 vring_virtqueue 结构体中的 notify 函数   
```

那 vq->notify 函数又在哪里被设置了呢？ 回到前端初始化流程的小节，在setup_vq函数中能看到如下代码段

```c
vq = vring_create_virtqueue(index, num,
			    VIRTIO_PCI_VRING_ALIGN, &vp_dev->vdev,
			    true, false, ctx,
			    vp_notify, callback, name);
	---> struct vring_virtqueue *vq;
	---> vq->notify = notify;
```

可以看到vring_virtqueue vq->notify函数被设置成了vp_notify函数，该函数实现如下：

```c
/* the notify function used when creating a virt queue */
bool vp_notify(struct virtqueue *vq)
{
	/* we write the queue's selector into the notification register to
	 * signal the other end */
	iowrite16(vq->index, (void __iomem *)vq->priv);
	return true;
}
```

​	调用iowrite将vq的index编号写入到PCI配置空间中的VIRTIO_PCI_QUEUE_NOTIFY寄存器中，（当把index写入到vq中后，qemu后端就会根据index知道是哪个队列发生了add buffer，然后从相应队列的buffer中取数据）。

​	根据上面几小节的分析，iowrite会产生kvm_exit，kvm会根据exit的原因执行不同的操作，如果是IO操作的exit，则会退出到用户空间，由QEMU进程来完成具体的IO操作。在这里QEMU后端最终会调用到virtio_queue_notify函数，再调用到handle_output函数，然后后将request数据包从vring中取出即可，以上就是virtio request 的大概流程。 

## 五、Virtio Back End 数据传输流程

在前面小节分析了 virtio 前端调用notify之后，QEMU后端会调用到根据virtio_add_queue函数初始化的handle_output函数，代码如下所示：

```c
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
```

​	首先是结构体定义，因为前后端请求数据包和发送数据包要相同，所以QEMU后端的struct test_request和 struct test_response结构体的结构体成员是和前端是一样的。

```c
typedef struct VirtQueueElement
{
    unsigned int index;
    unsigned int len;
    unsigned int ndescs;
    unsigned int out_num;
    unsigned int in_num;
    hwaddr *in_addr;
    hwaddr *out_addr;
    struct iovec *in_sg;
    struct iovec *out_sg;
} VirtQueueElement;

struct iovec {
    void *iov_base;
    size_t iov_len;
};
```

VirtQueueElement *elem 结构体的定义如上所示，in_addr和 out_addr保存的是guest的物理地址，而in_sg和out_sg中的地址是host的虚拟地址，物理地址和虚拟地址之间需要进行映射。

`index：`记录该buffer的首个物理内存块对应的描述符在描述符表中的下标，因为一个buffer数据可能由多个物理内存保存。

`out_num/in_num：`表示输出和输入块的数量。一个buffer可能包含可读区和可写区，因为一个buffer由多个物理块组成，有的物理块是可读而有的物理块是可写，out_num表示可读块的数量，而in_num表示可写块的数量。

`in_addr/out_addr：`记录的是可读块和可写块的物理地址（客户机的物理地址）。

因为in_addr/out_addr是客户机的物理地址，如果host要访问这些地址，则需要将这些guest物理地址映射成host的虚拟地址。

`in_sg/out_sg：`根据上面的分析，in_sg和out_sg就是保存的对应guest物理块在host的虚拟地址和长度，struct iovec结构体如上所示。

继续往下分析：

```c
elem = virtqueue_pop(vq, sizeof(VirtQueueElement));
```

virtqueue_pop函数主要功能为：

​	1、以 vq->last_avail_idx为索引从VRingAvail的ring数组中获取一个buffer head索引，并赋值到elem.index，然后获取各个guest物理buffer的相关信息。

​	2、将可写的物理buffer地址（客户机物理地址）记录到in_addr数组中，而可读的记录到out_addr数组中，并记录in_num和out_num，直到最后一个desc记录完毕。

​	3、获取完成后再将in_addr和out_addr映射成虚拟地址，并记录到in_sg和out_sg数组中，这样才可以在host中访问到。

（virtqueue_pop函数的具体分析请参考其它文章，这里只是简要说明功能。）

这样经过virtqueue_pop函数，QEMU后端就已经获取了buffer的相关信息，继续往下分析代码：

```c
iov_to_buf(elem->out_sg, elem->out_num, offset, &req, sizeof(req))；
print_req_and_build_resp_pack(&req, &resp);
iov_from_buf(elem->in_sg, elem->in_num, offset, &resp, sizeof(resp));
```

`iov_to_buf` 函数用于将 `iovec` 结构体数组中的数据复制到用户提供的缓冲区中。

- `elem->out_sg`：指向 `iovec` 结构体数组的指针。
- `elem->out_num`：指定了 `elem->out_sg` 数组中 `iovec` 结构体的数量。
- `offset`：指定了从哪个位置开始复制数据。
- `&req`：存放`guest`前端`request`的缓冲区指针，把从 `iovec` 数组中读取的数据复制到这个缓冲区中。
- `sizeof(req)`：这个参数指定了目标缓冲区 `req` 的大小，即函数最多可以复制多少字节到 `req` 中。

​	函数的大概作用为：从 `elem->out_sg` 指向的 `iovec` 数组开始，跳过 `offset` 指定的字节数，然后将数据复制到 `req` 指向的缓冲区中，直到达到 `req` 的大小限制或所有 `iovec` 中的数据都被复制完毕为止。

​	经过上面的分析，输出项`out_sg`指向的地址的内容就读取到了`req`结构体中，然后读取req结构体中的内容即可读取前端的数据，比如下面的函数，就是将req中的数据打印出来，并初始化好`struct test_response`为回应前端数据做准备。

```c
print_req_and_build_resp_pack(&req, &resp);
```

继续往下分析:

```c
iov_from_buf(elem->in_sg, elem->in_num, offset, &resp, sizeof(resp));
```

- `elem->in_sg`：指向一个`iovec`数组的指针，用于存储数据的分段信息。
- `elem->in_num`：表示`elem->in_sg`数组中可以使用的iovec的数量。
- `offset`：从缓冲区开始复制的偏移量。
- `&resp`：将数据从resp复制到iov向量列表中去。
- `sizeof(resp)`：这表示`resp`的大小。

​	和iov_to_buf函数的操作相反，`iov_from_buf`函数是将一段数据buf（resp）的内容复制到由 `iovec` 数组描述的内存区域中去，也就是`elem->in_sg`中。

​	到目前为止就完成了接收前端传递的数据，并将response包放入了in_sg中，继续往下分析

继续往下走就会调用到`virtqueue_push`函数，该函数的实现如下所示：

```c
void virtqueue_push(VirtQueue *vq, const VirtQueueElement *elem,
                    unsigned int len)
{
    RCU_READ_LOCK_GUARD();
    virtqueue_fill(vq, elem, len, 0);
    virtqueue_flush(vq, 1);
}
```

virtqueue_push函数主要是通过调用virtqueue_fill和virtqueue_flush对vring_used表的更新。

在virtqueue_fill函数中首先调用`virtqueue_unmap_sg(vq, elem, len)`，取消之前物理内存映射到虚拟内存的操作。然后更新vring_ used表，如下代码段所示。

```c
static void virtqueue_split_fill(VirtQueue *vq, const VirtQueueElement *elem,
                    unsigned int len, unsigned int idx)
{
    VRingUsedElem uelem;
    if (unlikely(!vq->vring.used)) {
        return;
    }
    idx = (idx + vq->used_idx) % vq->vring.num;
    uelem.id = elem->index;
    uelem.len = len;
    vring_used_write(vq, &uelem, idx);
}
```

然后调用virtqueue_flush->virtqueue_split_flush函数，更新vring_used表中的idx，使它指向表中下一个空闲位置。

```c
static void virtqueue_split_flush(VirtQueue *vq, unsigned int count)
{
    uint16_t old, new;
    if (unlikely(!vq->vring.used)) {
        return;
    }
    /* Make sure buffer is written before we update index. */
    smp_wmb();
    trace_virtqueue_flush(vq, count);
    old = vq->used_idx;
    new = old + count;
    vring_used_idx_set(vq, new);
    vq->inuse -= count;
    if (unlikely((int16_t)(new - vq->signalled_used) < (uint16_t)(new - old)))
        vq->signalled_used_valid = false;
}
```

更新完相关的vring之后，就需要通知前端到Virtquue中去取数据了，下面分析QEMU后端通知前端的方式。

QEMU 后端通知前端采用的是中断注入的方式，具体实现在virtio_notify函数，代码如下：

```c
virtio_notify(vdev, vq);
```

```c
static void virtio_notify_vector(VirtIODevice *vdev, uint16_t vector)
{
	......
    VirtioBusClass *k = VIRTIO_BUS_GET_CLASS(qbus);
	......
    if (k->notify) {
        k->notify(qbus->parent, vector);
    }
}
static void virtio_irq(VirtQueue *vq)
{
    virtio_set_isr(vq->vdev, 0x1);
    virtio_notify_vector(vq->vdev, vq->vector);
}
void virtio_notify(VirtIODevice *vdev, VirtQueue *vq)
{
    WITH_RCU_READ_LOCK_GUARD() {
        if (!virtio_should_notify(vdev, vq)) {
            return;
        }
    }
    trace_virtio_notify(vdev, vq);
    virtio_irq(vq);
}
```

​	virtio_set_isr用来设置ISR寄存器，virtio_notify_vector是调用了VirtioBusClass结构体中的notify函数来给guest注入中断，k->notify的实现如下代码段

```c
static void virtio_pci_notify(DeviceState *d, uint16_t vector)
{
    VirtIOPCIProxy *proxy = to_virtio_pci_proxy_fast(d);

    if (msix_enabled(&proxy->pci_dev)) {
        if (vector != VIRTIO_NO_VECTOR) {
            msix_notify(&proxy->pci_dev, vector);
        }
    } else {
        VirtIODevice *vdev = virtio_bus_get_device(&proxy->bus);
        pci_set_irq(&proxy->pci_dev, qatomic_read(&vdev->isr) & 1);
    }
}
static void virtio_pci_bus_class_init(ObjectClass *klass, void *data)
{
	......
    VirtioBusClass *k = VIRTIO_BUS_CLASS(klass);
	......
    k->notify = virtio_pci_notify;
	......
}
```

​	可以看到最后是通过virtio_pci_notify函数来对guest注入中断的，在这里会先判断，如果前端的virtio pci设备打开了MSIX中断机制，就采用msix_notify，否则就用普通的中断，关于具体是怎么给guest注入中断的这里不再说明，如果感兴趣可以去了解中断虚拟化和查看文档：https://michael2012z.medium.com/managing-interrupt-in-virtio-pci-bfe585117b49 和 https://docs.oasis-open.org/virtio/virtio/v1.1/csprd01/virtio-v1.1-csprd01.html#x1-1000001

## 六、Virtio Front End 中断流程

​	经过上一节的分析最后得出的结果是QEMU后端最后会给guest前端注入一个中断通知前端数据已经处理完毕，本小节就是分析后端注入中断之后，前端的函数调用流程。

​	根据前面对前端代码的分析可以知道中断函数其实早就被注册了，如下代码段：

```c
static int vp_find_vqs_intx(...)
{
......
err = request_irq(vp_dev->pci_dev->irq, vp_interrupt, IRQF_SHARED,
		dev_name(&vdev->dev), vp_dev);
......
}
```

​	可以看到在该函数注册了一个中断，并且中断回调函数为vp_interrupt，而vp_interrupt又会调用vp_vring_interrupt，再调用vring_ interrupt函数，函数实现如下：

```c
irqreturn_t vring_interrupt(int irq, void *_vq)
{
	struct vring_virtqueue *vq = to_vvq(_vq);
	......
	pr_debug("virtqueue callback for %p (%p)\n", vq, vq->vq.callback);
	if (vq->vq.callback)
		vq->vq.callback(&vq->vq);
	......
}
```

​	可以看到在中断回调函数中最终调用到了vring_virtqueue中的vq.callback函数，那这个vq.callback函数又是在哪里设置的呢？ 根据前面的分析，这个函数是在guest 前端初始化的时候就被初始化过了，如下代码段：

```c
/* Only available for split ring */
struct virtqueue *__vring_new_virtqueue(...)
{
    ......
    struct vring_virtqueue *vq;
	......
    vq->vq.callback = callback;
	......
}
```

​	因为前面小节已经分析过该流程，这里就不再详细分析，这个callback函数最终调用到的就是通过virtio_find_vqs(vb->vdev, nvqs, vqs, callbacks, names, NULL)函数中设置的callback。

​	所以就会调用到test_request_done函数，代码段如下：

```c
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
	......
	while ((test = virtqueue_get_buf(vq, &len)) != NULL) {
		// request packet will be completed by response packet
        print_response_data(test);
	}
	......
}
```

​	在test_request_done函数中会调用virtqueue_get_buf函数来读取QEMU后端response的数据，函数的调用流程如下：

```c
virtqueue_get_buf
	->virtqueue_get_buf_ctx
		->virtqueue_get_buf_ctx_packed/virtqueue_get_buf_ctx_split
```

以virtqueue_get_buf_ctx_split函数为例，virtqueue_get_buf_ctx_split函数会根据vring_used表在vring_desc中查找已经被后端QEMU进程处理过的数据请求项，并将这些表项重新设置为可用。

```c
static void *virtqueue_get_buf_ctx_split(struct virtqueue *_vq,
					 unsigned int *len,
					 void **ctx)
{
	struct vring_virtqueue *vq = to_vvq(_vq);
	......
	last_used = (vq->last_used_idx & (vq->split.vring.num - 1));
	i = virtio32_to_cpu(_vq->vdev,
			vq->split.vring.used->ring[last_used].id);
	*len = virtio32_to_cpu(_vq->vdev,
			vq->split.vring.used->ring[last_used].len);
	......
	/* detach_buf_split clears data, so grab it now. */
	ret = vq->split.desc_state[i].data;
	detach_buf_split(vq, i, ctx);
	vq->last_used_idx++;
	......
}
```

`virtqueue_get_buf_ctx_split`函数用于从一个virtqueue中获取一个`vring_used`缓冲区的相关信息。

- 参数：
  - `_vq`：指向`virtqueue`结构的指针。
  - `len`：用于返回获取到的缓冲区长度的指针。
  - `ctx`：用于返回与缓冲区关联的上下文信息的指针（如果有的话）。
- 返回值：如果成功则返回指向获取到的缓冲区的指针，否则返回`NULL`。

​	使用`virtio_rmb(vq->weak_barriers)`确保在读取`used`环之前，QEMU后端已经更新了它，`last_used_idx`指向的是QEMU后端在这次IO处理中所更新的`vring_used`表的第一项，从`vring_used`表中取出一个被qemu已处理完成的IO请求链表，将该链表的表头结点在`vring_desc`中的位置赋值给i，同时获得这个链表的长度len。

​	ret为void指针，如果上面流程都一切正常，使用`detach_buf_split`函数从队列中读出缓冲区，更新队列的状态。并将缓冲区的地址记录到`ret`指针变量中，这样就获取到了QEMU后端写入数据的地址信息，然后`last_used_idx`递增，指向`vring_used`表的下一项。

通过`struct virtio_test` 类型的结构体去接收`virtqueue_get_buf`函数的返回值，再将接收的数据通过print_response_data函数打印出来，这样就完成了一次IO请求所需要的操作。

附test_request_done函数调用流程：

```c
[ 7299.020188] CPU: 0 PID: 0 Comm: swapper/0 Not tainted 5.4.239 #19
[ 7299.020304] Hardware name: linux,dummy-virt (DT)
[ 7299.020433] Call trace:
[ 7299.020490]  dump_backtrace+0x0/0x190
[ 7299.020567]  show_stack+0x14/0x20
[ 7299.020670]  dump_stack+0xcc/0x108
[ 7299.020736]  test_request_done+0x4c/0xe0
[ 7299.020846]  vring_interrupt+0x60/0xa0
[ 7299.020943]  vm_interrupt+0x84/0xd0
[ 7299.021024]  __handle_irq_event_percpu+0x54/0x160
[ 7299.021132]  handle_irq_event+0x60/0xf0
[ 7299.021249]  handle_fasteoi_irq+0xa0/0x190
[ 7299.021346]  __handle_domain_irq+0x70/0xcc
[ 7299.021446]  gic_handle_irq+0xc0/0x158
[ 7299.021518]  el1_irq+0xb8/0x180
[ 7299.021589]  arch_cpu_idle+0x10/0x20
[ 7299.021706]  cpu_startup_entry+0x24/0x70
[ 7299.021821]  rest_init+0xd4/0xe0
[ 7299.021908]  arch_call_rest_init+0xc/0x14
[ 7299.021987]  start_kernel+0x3f0/0x424
```

## 七、总结

​	本篇文章主要是通过QEMU平台分析了Virtio前后端通信的一些实现方法，分别分析了Virtio前后端的初始化流程和前后端数据传输的流程，前后端初始化流程主要是做中断回调函数的注册，初始化VQ为前后端之间数据的传输做准备，以及PCI相关的初始化，而数据传输流程主要是分析从前端传输一个request数据包到QEMU后端，然后后端再通知到前端的流程。

大概流程如下：

​	1、在前端将一个request数据包放入到sgs中

​	2、调用virtqueue_add_sgs函数将sgs数据放入到vring中的desc中

​	3、调用virtqueue_kick函数通知QEMU后端有数据需要处理，该函数会产生 VM_EXIT，然后KVM拦截该操作，然后根据不同的退出原因做不同操作。

​	4、virtqueue_kick的退出会传递到用户空间QEMU中，然后会调用到virtio_ioport_write函数，在该函数中会调用到virtio_queue _notify中的handle_output函数，而handle_output函数的初始化是通过virtio_add_queue进行初始化的。

​	5、然后就到了自定义的handle_output函数，在该函数中先处理前端传递的数据，处理完成之后会构建response的数据包，并将包添加到VQ中去。

​	6、添加完数据就调用virtqueue_push函数更新vring_used表以及调用virtio_notify函数给前端虚拟机注入一个中断，以告诉前端后端的数据已经处理完毕。

​	7、当后端给前端注入一个中断之后，前端的test_request_done函数就会被调用，在该函数中会读取QEMU后端写入的response数据包，这样就完成一次IO调用流程的操作（test_request_done函数是通过virtio_find_vqs函数进行注册的）。

​	分析到这里本篇文章基本上就结束了，分析完virtio的代码才知道自己的专业知识还是非常匮乏的，里面涉及到的很多知识之前都没有了解过，也是阅读了非常多的文章才有了一点了解，但感觉还是有很多地方理解得不是很明白，比如PCI和QEMU方面的知识，以及一些比较细节的知识点，这方面只能够后面有时间再深入分析了。

#### References

https://www.redhat.com/en/blog/virtqueues-and-virtio-ring-how-data-travels

https://www.redhat.com/en/blog/virtqueues-and-virtio-ring-how-data-travels?source=searchresultlisting

https://www.cnblogs.com/ck1020/p/6066007.html

https://blog.csdn.net/majieyue/article/details/51084492

https://download.csdn.net/blog/column/10188630/128058297

https://lekensteyn.nl/files/docs/PCI_SPEV_V3_0.pdf

https://docs.oasis-open.org/virtio/virtio/v1.1/csprd01/virtio-v1.1-csprd01.html#x1-1000001

https://blog.csdn.net/qq_37756660/article/details/135849395

https://www.cnblogs.com/edver/p/16255243.html#_label1

https://www.cnblogs.com/edver/p/14684104.html

https://juniorprincewang.github.io/2018/03/01/virtio-docs/#qemu%E6%A8%A1%E6%8B%9Fio%E8%AE%BE%E5%A4%87%E7%9A%84%E5%9F%BA%E6%9C%AC%E5%8E%9F%E7%90%86

https://arttnba3.cn/2022/07/15/VIRTUALIZATION-0X00-QEMU-PART-I/#0x04-%E7%AE%80%E6%98%93-QEMU-%E8%AE%BE%E5%A4%87%E7%BC%96%E5%86%99

https://arttnba3.cn/2022/08/30/HARDWARE-0X00-PCI_DEVICE/#%E5%9B%9B%E3%80%81PCI-%E8%AE%BE%E5%A4%87%E9%85%8D%E7%BD%AE%E7%A9%BA%E9%97%B4

https://blog.csdn.net/leoufung/article/details/120552530?utm_medium=distribute.pc_relevant.none-task-blog-2~default~baidujs_baidulandingword~default-0-120552530-blog-135723055.235

https://juniorprincewang.github.io/2018/03/01/virtio-docs/

https://www.cnblogs.com/ck1020/p/5939777.html

