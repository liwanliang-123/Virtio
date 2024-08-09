Q1、为什么说qemu中的virtio是基于mmio而不是pci或者Channel I/O ？

​	通过目前对代码的调试分析，发现qemu并不是绝对的基于一种方法实现的virtio，而是mmio和PCI都有使用到，下面对 linux kernel中的 virtio_blk 和 virtio_net 的函数调用栈进行分析。

（1）virtio_blk

​	目前我们是不知道 virtio_blk 到底是基于mmio还是pci的，但是我们可以假设是其中一种，然后再通过实验验证我们的猜想是否正确。

​	那这里要怎么验证呢？ 我们都知道在 virtio 设备和 virtio 驱动匹配的时候就会调用 probe 函数，然后在 probe 函数中调用init_vq函数去初始化VQ，基于不同的协议就会有不同的VQ函数，那只需要知道 virtio_blk 最终是调用的那一个函数就可以知道是基于什么协议了。初始化VQ的函数实现是通过`virtio_mmio_config_ops`结构体进行初始化的，该结构体为 Virtio 的驱动程序提供了一系列标准化的函数指针操作集来与设备进行交互。

基于`virtio PCI`协议的`virtio_config_ops`结构体如下：

```c
static const struct virtio_config_ops virtio_pci_config_ops = {
	.get		= vp_get,
	.set		= vp_set,
	.get_status	= vp_get_status,
	.set_status	= vp_set_status,
	.reset		= vp_reset,
	.find_vqs	= vp_find_vqs,
	.del_vqs	= vp_del_vqs,
	.get_features	= vp_get_features,
	.finalize_features = vp_finalize_features,
	.bus_name	= vp_bus_name,
	.set_vq_affinity = vp_set_vq_affinity,
	.get_vq_affinity = vp_get_vq_affinity,
};
```

基于`virtio mmio` 协议的`virtio_config_ops`结构体如下：

```c
static const struct virtio_config_ops virtio_mmio_config_ops = {
	.get		= vm_get,
	.set		= vm_set,
	.generation	= vm_generation,
	.get_status	= vm_get_status,
	.set_status	= vm_set_status,
	.reset		= vm_reset,
	.find_vqs	= vm_find_vqs,
	.del_vqs	= vm_del_vqs,
	.get_features	= vm_get_features,
	.finalize_features = vm_finalize_features,
	.bus_name	= vm_bus_name,
};
```

​	打开 driver/block/virtio_blk.c 文件，进入到 virtio_probe 函数，找到 init_vq 函数并进入，能够看到 virtio_find_vqs 函数，函数实现如下：

```c
static inline
int virtio_find_vqs(struct virtio_device *vdev, unsigned nvqs,
			struct virtqueue *vqs[], vq_callback_t *callbacks[],
			const char * const names[],
			struct irq_affinity *desc)
{
	return vdev->config->find_vqs(vdev, nvqs, vqs, callbacks, names, NULL, desc);
}
```

​	这个函数其实最终就会调用到 `virtio_config_ops` 结构体中设置的这些回调函数了，这里调用的是 `find_vqs` 函数。如果是基于pci协议则会调用到 `vp_find_vqs` 函数，同理，如果是基于 mmio实现则会调用到 `vm_find_vqs` 函数，这样的话我们只需要在这两个函数中调用 dump_stack 函数打印出 find_vqs 函数的调用流程就可以知道 virtio_blk 到底是基于什么协议了。

vm_find_vqs 函数中的 back trace 结果如下：

```c
[    0.282469] CPU: 2 PID: 1 Comm: swapper/0 Not tainted 5.4.239 #21
[    0.282567] Hardware name: linux,dummy-virt (DT)
[    0.282635] Call trace:
[    0.282678]  dump_backtrace+0x0/0x190
[    0.282736]  show_stack+0x14/0x20
[    0.282788]  dump_stack+0xcc/0x108
[    0.282842]  vm_find_vqs+0xe8/0x490
[    0.282903]  init_vq+0x148/0x280
[    0.282959]  virtblk_probe+0xe4/0x720
[    0.283025]  virtio_dev_probe+0x198/0x23c
[    0.283097]  really_probe+0xd8/0x440
[    0.283155]  driver_probe_device+0x54/0xe4
[    0.283226]  device_driver_attach+0xb4/0xc0
[    0.283293]  __driver_attach+0x60/0x120
[    0.283356]  bus_for_each_dev+0x6c/0xc0
[    0.283420]  driver_attach+0x20/0x30
[    0.283478]  bus_add_driver+0x100/0x1f0
[    0.283540]  driver_register+0x74/0x120
[    0.283606]  register_virtio_driver+0x24/0x3c
[    0.283674]  init+0x60/0x98
[    0.283717]  do_one_initcall+0x4c/0x1c0
[    0.283776]  kernel_init_freeable+0x1ec/0x294
[    0.283846]  kernel_init+0x10/0x100
[    0.283903]  ret_from_fork+0x10/0x24
[    0.283964] >>>>>func: vm_setup_vq ## line: 364 <<<<<
```

​	可以看到 virtblk_probe 函数最终是调用到了 virtio_mmio_config_ops 结构体中的 vm_find_vqs 函数，所以 virtio_blk 是基于mmio实现的。

（2）virtio_net

​        virtio_net也可以通过相同的方法进行验证，打开 driver/net/virtio_net.c 文件，进入virtnet_probe -> init_vqs -> virtnet_find_vqs函数可以看到如下代码段：

```c
ret = vi->vdev->config->find_vqs(vi->vdev, total_vqs, vqs, callbacks,
					 names, ctx, NULL);
```

vp_find_vqs 函数中的 back trace 结果如下：

```c
[    0.278931] CPU: 2 PID: 1 Comm: swapper/0 Not tainted 5.4.239 #24
[    0.279036] Hardware name: linux,dummy-virt (DT)
[    0.279112] Call trace:
[    0.279156]  dump_backtrace+0x0/0x190
[    0.279216]  show_stack+0x14/0x20
[    0.279272]  dump_stack+0xcc/0x108
[    0.279326]  vp_find_vqs+0x3c/0x19c
[    0.279384]  vp_modern_find_vqs+0x14/0x70
[    0.279449]  virtnet_find_vqs+0x1d8/0x320
[    0.279514]  virtnet_probe+0x358/0x840
[    0.279573]  virtio_dev_probe+0x198/0x23c
[    0.279637]  really_probe+0xd8/0x440
[    0.279694]  driver_probe_device+0x54/0xe4
[    0.279762]  device_driver_attach+0xb4/0xc0
[    0.279828]  __driver_attach+0x60/0x120
[    0.279887]  bus_for_each_dev+0x6c/0xc0
[    0.279947]  driver_attach+0x20/0x30
[    0.280004]  bus_add_driver+0x100/0x1f0
[    0.280096]  driver_register+0x74/0x120
[    0.280188]  register_virtio_driver+0x24/0x3c
[    0.280293]  virtio_net_driver_init+0x7c/0xac
[    0.280373]  do_one_initcall+0x4c/0x1c0
[    0.280453]  kernel_init_freeable+0x1ec/0x294
[    0.280522]  kernel_init+0x10/0x100
[    0.280582]  ret_from_fork+0x10/0x24
```

​	所以 virtnet_probe 函数最终是调用了virtio_pci_config_ops结构体中的 vp_find_vqs函数来查找并初始化 virtqueue的，这也能够看出  virtio_net 是基于 pci 实现的。



QEMU 中基于mmio的virtio实现：

​	通过MMIO接口的virtio设备通常是通过在虚拟机的内存空间中映射一块物理内存区域来实现的，虚拟机可以直接通过读写这块内存区域与设备进行交互。

Q2、virtio driver和virtio device 的初始化是怎么样的？

​	(1) driver init

​	driver 主要就是初始化一个 virtio_driver 结构体，在该结构体中 virtio_device_id成员是用来匹配driver 和 device 的，当 driver和device匹配上了之后 virtio_driver 结构体中的 probe函数将被调用。

​	然后调用 module_virtio_driver函数将初始化好的virtio_driver结构体注册到virtio bus 上，函数最终会调用到register_virtio_driver函数，实现如下：

```c
int register_virtio_driver(struct virtio_driver *driver)
{
    /* Catch this early. */
    BUG_ON(driver->feature_table_size && !driver->feature_table);
    driver->driver.bus = &virtio_bus;
    return driver_register(&driver->driver);
}
```

`driver->driver.bus = &virtio_bus;` 用来指定该driver的bus为virtio bus ,然后调用 driver_register 函数将该驱动注册到该bus 上面，至此基于mmio的 virtio driver 部分的初始化流程就已经完成。

​	（2）device init

​	device 的初始化在/driver/virtio/virtio_mmio.c 文件中实现，在virtio_mmio.c文件的开头就介绍了基于mmio的virtio device的三种实现方法，因为qemu是以 device tree node 实现的，所以这里以 device tree 进行分析。Device Tree Node 代码如下所示：

```c
virtio_mmio@a003a00 {
	dma-coherent;
	interrupts = <0x00 0x2d 0x01>;
	reg = <0x00 0xa003a00 0x00 0x200>;
	compatible = "virtio,mmio";
};
```

`virtio_mmio@a003a00：`virtio设备的节点名称和地址。@a003a00表示该设备在物理内存中的基地址。

`dma-coherent：`表示该设备支持DMA操作。

`interrupts：`设备的中断号。（关于中断号的计算在分析qemu时再分析）

`reg：`设备在物理内存中的寄存器区域。基地址为0xa003a00，长度为0x200（512字节）。

`compatible`：用来和driver做匹配。

​	因为这里是基于 device tree 实现，所以这里会初始化一个platform_driver结构体用来和 device tree 进行匹配，代码如下：

```c
/* Platform driver */
static const struct of_device_id virtio_mmio_match[] = {
	{ .compatible = "virtio,mmio", },
	{},
};
MODULE_DEVICE_TABLE(of, virtio_mmio_match);
static struct platform_driver virtio_mmio_driver = {
	.probe		= virtio_mmio_probe,
	.remove		= virtio_mmio_remove,
	.driver		= {
		.name	= "virtio-mmio",
		.of_match_table	= virtio_mmio_match,
		.acpi_match_table = ACPI_PTR(virtio_mmio_acpi_match),
	},
};
static int __init virtio_mmio_init(void)
{
	return platform_driver_register(&virtio_mmio_driver);
}
static void __exit virtio_mmio_exit(void)
{
	platform_driver_unregister(&virtio_mmio_driver);
	vm_unregister_cmdline_devices();
}
module_init(virtio_mmio_init);
module_exit(virtio_mmio_exit);
```

​	因为 virtio_mmio_match 中的 compatible = "virtio,mmio" 和 设备树中的 compatible 相同，当 virtio_mmio_driver被注册到 platform bus 上时，bus 会遍历 platform device ,然后virtio_mmio_driver 中的probe函数就会被调用，然后进入下一阶段的初始化。

​	（3）virtio device 的初始化

​	上一小节分析的是关于mmio的相关初始化，并且device  tree 和 virtio_mmio_driver已经匹配上，调用了 virtio_mmio_driver 中的probe函数，但好像和 virtio device 还没有关系？ 那就继续分析 probe 函数，代码如下：

```c
static int virtio_mmio_probe(struct platform_device *pdev)
{
	struct virtio_mmio_device *vm_dev;
	struct resource *mem;
	unsigned long magic;
	int rc;

	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!mem)
		return -EINVAL;
	if (!devm_request_mem_region(&pdev->dev, mem->start,
			resource_size(mem), pdev->name))
		return -EBUSY;

	vm_dev = devm_kzalloc(&pdev->dev, sizeof(*vm_dev), GFP_KERNEL);
	if (!vm_dev)
		return -ENOMEM;

	vm_dev->vdev.dev.parent = &pdev->dev;
	vm_dev->vdev.dev.release = virtio_mmio_release_dev;
	vm_dev->vdev.config = &virtio_mmio_config_ops;
	vm_dev->pdev = pdev;
	INIT_LIST_HEAD(&vm_dev->virtqueues);
	spin_lock_init(&vm_dev->lock);

	vm_dev->base = devm_ioremap(&pdev->dev, mem->start, resource_size(mem));
	if (vm_dev->base == NULL)
		return -EFAULT;
	/* Check magic value */
	magic = readl(vm_dev->base + VIRTIO_MMIO_MAGIC_VALUE);
	if (magic != ('v' | 'i' << 8 | 'r' << 16 | 't' << 24)) {
		dev_warn(&pdev->dev, "Wrong magic value 0x%08lx!\n", magic);
		return -ENODEV;
	}
	/* Check device version */
	vm_dev->version = readl(vm_dev->base + VIRTIO_MMIO_VERSION);
	if (vm_dev->version < 1 || vm_dev->version > 2) {
		dev_err(&pdev->dev, "Version %ld not supported!\n",
				vm_dev->version);
		return -ENXIO;
	}
	vm_dev->vdev.id.device = readl(vm_dev->base + VIRTIO_MMIO_DEVICE_ID);
	if (vm_dev->vdev.id.device == 0) {
		/*
		 * virtio-mmio device with an ID 0 is a (dummy) placeholder
		 * with no function. End probing now with no error reported.
		 */
		return -ENODEV;
	}
	vm_dev->vdev.id.vendor = readl(vm_dev->base + VIRTIO_MMIO_VENDOR_ID);
	if (vm_dev->version == 1) {
		writel(PAGE_SIZE, vm_dev->base + VIRTIO_MMIO_GUEST_PAGE_SIZE);

		rc = dma_set_mask(&pdev->dev, DMA_BIT_MASK(64));
		/*
		 * In the legacy case, ensure our coherently-allocated virtio
		 * ring will be at an address expressable as a 32-bit PFN.
		 */
		if (!rc)
			dma_set_coherent_mask(&pdev->dev,
					      DMA_BIT_MASK(32 + PAGE_SHIFT));
	} else {
		rc = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64));
	}
	if (rc)
		rc = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));
	if (rc)
		dev_warn(&pdev->dev, "Failed to enable 64-bit or 32-bit DMA.  Trying to continue, but this might not work.\n");

	platform_set_drvdata(pdev, vm_dev);
/*
	register_virtio_device 函数会将设备注册到virtio总线,
	触发virtio总线上的match操作，然后进行virtio设备的探测
*/
	rc = register_virtio_device(&vm_dev->vdev);
	if (rc)
		put_device(&vm_dev->vdev.dev);
	return rc;
}
```

下面对virtio_mmio_probe函数中的主要内容进行分析

(1) 获取 device resource info

```c
mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
```

​        virtio_mmio_probe 函数首先通过 platform_get_resource 函数从platform_device获取设备树中的mem资源，一旦获得了device 的mem资源，就可以使用resource结构体中的信息来访问硬件了。resource结构体如下：

```c
struct resource {
	resource_size_t start;
	resource_size_t end;
	const char *name;
	unsigned long flags;
	unsigned long desc;
	struct resource *parent, *sibling, *child;
};
```

- `resource_size_t start;`：对于内存资源来说，这是内存区域的起始物理地址。
- `resource_size_t end;`：资源的结束地址。
- `const char *name;`：资源的名称。
- `unsigned long flags;`：资源的标志。
- `unsigned long desc;`：资源的描述符。
- `struct resource *parent, *sibling, *child;`：这些字段用于将资源组织成链表结构。

（2）请求内存资源

```c
if (!devm_request_mem_region(&pdev->dev, mem->start,
		resource_size(mem), pdev->name))
	return -EBUSY;
```

​        在前面通过platform_get_resource函数获取了device resource 的一些基本信息，接下来就需要调用devm_request_mem_region函数来请求这些资源，devm_request_mem_region函数尝试请求从`mem->start`开始，大小为`resource_size(mem)`的内存区域，并将这段区域与`pdev`表示的设备相关联，`pdev->name`是设备名称。

（3）virtio_device结构体相关初始化

​        前面已经获取了device tree相关的mem资源，也就是和mmio相关的内存段kernel已经获取到了，从这里开始将会对virtio_device部分进行相关初始化，首先看一下virtio_mmio_device结构体，如下：

```c
struct virtio_mmio_device {
	struct virtio_device vdev;
	struct platform_device *pdev;

	void __iomem *base;
	unsigned long version;

	/* a list of queues so we can dispatch IRQs */
	spinlock_t lock;
	struct list_head virtqueues;
};
```

​        virtio_mmio_device结构体是在Linux kernel中用来表示通过内存映射I/O（MMIO）接口通信的virtio设备的结构体，它包含了设备的基本信息、内存映射区域的访问指针、版本信息、以及用来同步访问的锁和虚拟队列的链表。

- `struct virtio_device vdev;`：包含了virtio设备的核心信息，如设备ID、配置空间、设备状态等。

- `void __iomem *base;`：一个指向内存映射区域的指针，用于访问virtio设备的寄存器。`__iomem`宏用来告诉编译器这个指针指向的是内存映射的I/O空间，而不是普通的内存空间。

- `unsigned long version;`：virtio设备的版本信息。

然后通过devm_kzalloc函数对virtio_mmio_device结构体指针开辟空间并初始化相关参数，代码如下所示：

```c
vm_dev = devm_kzalloc(&pdev->dev, sizeof(*vm_dev), GFP_KERNEL);
if (!vm_dev)
	return -ENOMEM;
vm_dev->vdev.dev.parent = &pdev->dev;
vm_dev->vdev.dev.release = virtio_mmio_release_dev;
vm_dev->vdev.config = &virtio_mmio_config_ops;
vm_dev->pdev = pdev;
INIT_LIST_HEAD(&vm_dev->virtqueues);
```

由virtio_mmio_device结构体可以得出vm_dev->vdev就是对virtio_device结构体进行初始化，这里有一个重要的参数设置就是vm_dev->vdev.config，这个config参数是一个`virtio_config_ops` 的结构体，定义了一系列用于配置 Virtio 设备的操作，结构体如下：

```c
struct virtio_config_ops {
	void (*get)(struct virtio_device *vdev, unsigned offset,
		    void *buf, unsigned len);
	void (*set)(struct virtio_device *vdev, unsigned offset,
		    const void *buf, unsigned len);
	u32 (*generation)(struct virtio_device *vdev);
	u8 (*get_status)(struct virtio_device *vdev);
	void (*set_status)(struct virtio_device *vdev, u8 status);
	void (*reset)(struct virtio_device *vdev);
	int (*find_vqs)(struct virtio_device *, unsigned nvqs,
			struct virtqueue *vqs[], vq_callback_t *callbacks[],
			const char * const names[], const bool *ctx,
			struct irq_affinity *desc);
	void (*del_vqs)(struct virtio_device *);
	u64 (*get_features)(struct virtio_device *vdev);
	int (*finalize_features)(struct virtio_device *vdev);
	const char *(*bus_name)(struct virtio_device *vdev);
	int (*set_vq_affinity)(struct virtqueue *vq,
			       const struct cpumask *cpu_mask);
	const struct cpumask *(*get_vq_affinity)(struct virtio_device *vdev,
			int index);
};
```

 `virtio_config_ops` 结构体中定义了一系列的函数指针，下面关键成员进行解释：

- `get`：用于从 Virtio 设备读取配置数据。
- `set`：用于向 Virtio 设备写入配置数据。
- `get_status`：读取设备的状态字节。
- `set_status`：设置设备的状态字节。
- `find_vqs`：查找并初始化 Virtio 队列（virtqueues）。这个函数负责根据给定参数（如队列数量、回调函数、队列名称等）找到并设置虚拟队列。

- `reset`：重置设备。
- `del_vqs`：释放之前通过 `find_vqs` 找到的 Virtio 队列。
- `get_features`：获取设备的特性位数组。
- `finalize_features`：设备特性集。
- `bus_name`：设备所在的总线名称。

virtio_mmio_config_ops结构体实现如下：

```c
static const struct virtio_config_ops virtio_mmio_config_ops = {
	.get		= vm_get,
	.set		= vm_set,
	.generation	= vm_generation,
	.get_status	= vm_get_status,
	.set_status	= vm_set_status,
	.reset		= vm_reset,
	.find_vqs	= vm_find_vqs,
	.del_vqs	= vm_del_vqs,
	.get_features	= vm_get_features,
	.finalize_features = vm_finalize_features,
	.bus_name	= vm_bus_name,
};
```

​        virtio_mmio_config_ops结构体为 Virtio 设备的驱动程序提供了一系列标准化的函数指针操作集来与设备进行交互，使得virtio驱动程序可以更加灵活简单地处理不同的 Virtio 设备。

（4）初始化Virtio设备的内存映射

​        关于 virtio_device 结构体成员的初始化后面再接着分析，现在先看一下 virtio device 的 memery 是怎么初始化的。

```c
vm_dev->base = devm_ioremap(&pdev->dev, mem->start, resource_size(mem));
if (vm_dev->base == NULL)
	return -EFAULT;
```

​        这里使用`devm_ioremap`函数将平台设备`pdev->dev`的`mem->start`（设备内存资源的起始物理地址）映射到内核的虚拟地址空间，映射的大小为`resource_size(mem)`，映射的虚拟地址存储在`vm_dev->base`中，供后续访问设备内存使用，完成这样的映射之后，就能在 linux kernel 中通过读写内存的方式进行访问 mmio device 了，这也是 mmio 的特点。

​        所以如果有 driver 想在kernel 中访问一个 mmio device 的相关寄存器，就需要以 vm_dev->base 为基地址再加上相关的offset就能够访问到该设备的相关寄存器。

（5） Check mmio device information

​        通过前面的内存映射目前已经能够通过 vm_dev->base访问到该 device 了，但在访问之前还需要检查一下设备是否合规，代码如下：

```c
/* Check magic value */
magic = readl(vm_dev->base + VIRTIO_MMIO_MAGIC_VALUE);
if (magic != ('v' | 'i' << 8 | 'r' << 16 | 't' << 24)) {
	dev_warn(&pdev->dev, "Wrong magic value 0x%08lx!\n", magic);
	return -ENODEV;
}
/* Check device version */
vm_dev->version = readl(vm_dev->base + VIRTIO_MMIO_VERSION);
if (vm_dev->version < 1 || vm_dev->version > 2) {
	dev_err(&pdev->dev, "Version %ld not supported!\n",
			vm_dev->version);
	return -ENXIO;
}
```

检查魔术值（Magic Value）

- 首先通过`readl`函数从`vm_dev->base + VIRTIO_MMIO_MAGIC_VALUE`地址读取设备的魔术值。`VIRTIO_MMIO_MAGIC_VALUE`是一个偏移量，用于定位到设备内存映射中存储魔术值的位置。
- 然后将读取到的魔术值'v' | 'i' << 8 | 'r' << 16 | 't' << 24`进行比较，如果不相等，说明这不是一个virtio设备或者设备初始化失败。
- `mmio device` 的`Magic Value`必须为`0x74726976`。

检查设备版本 （Version）

- 通过`readl`函数从`vm_dev->base + VIRTIO_MMIO_VERSION`地址读取设备的版本信息。
- 然后，检查这个版本号是否在支持的范围内（`Version >= 1 && Version <= 2`）。

​        在这里`VIRTIO_MMIO_VERSION` 和 `VIRTIO_MMIO_MAGIC_VALUE`都是一个 offset ,也就是前面所提到的，通过`vm_dev->base`基地址加上一段 offset 就能够访问到 device 的相关信息了。

​        那这里为什么要检查并规定device的这两个参数呢？ 这其实是在 [Virtual I/O Device (VIRTIO) Version 1.0]() 手册中就有相关说明，引用手册中的两句话：

> The device MUST return 0x74726976 in MagicValue.   
>
> The device MUST return value 0x2 in Version. (Legacy device returns value 0x1.)

（6）初始化virtio_device结构体的其它成员

前面对 virtio_device 的部分成员进行了初始化，下面对其它成员进行初始化。代码如下：

```c
vm_dev->vdev.id.device = readl(vm_dev->base + VIRTIO_MMIO_DEVICE_ID);
if (vm_dev->vdev.id.device == 0) {
	/*
	 * virtio-mmio device with an ID 0 is a (dummy) placeholder
	 * with no function. End probing now with no error reported.
	 */
	return -ENODEV;
}
vm_dev->vdev.id.vendor = readl(vm_dev->base + VIRTIO_MMIO_VENDOR_ID);
if (vm_dev->version == 1) {
	writel(PAGE_SIZE, vm_dev->base + VIRTIO_MMIO_GUEST_PAGE_SIZE);
	rc = dma_set_mask(&pdev->dev, DMA_BIT_MASK(64));
	/*
	 * In the legacy case, ensure our coherently-allocated virtio
	 * ring will be at an address expressable as a 32-bit PFN.
	 */
	if (!rc)
		dma_set_coherent_mask(&pdev->dev,
				      DMA_BIT_MASK(32 + PAGE_SHIFT));
} else {
	rc = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64));
}
if (rc)
	rc = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));
if (rc)
	dev_warn(&pdev->dev, "Failed to enable 64-bit or 32-bit DMA.  Trying to continue, but this might not work.\n");
```

这里主要是通过readl函数读取 virtio_device 的设备ID和厂商ID，设备ID的主要作用是用来和 virtio driver 匹配，mmio 的厂商ID固定为0x554D4551。

```c
vm_dev->vdev.id.device = readl(vm_dev->base + VIRTIO_MMIO_DEVICE_ID);
vm_dev->vdev.id.vendor = readl(vm_dev->base + VIRTIO_MMIO_VENDOR_ID);
```

如果前面读取的virsion id 等于1 ，还需要通过`VIRTIO_MMIO_GUEST_PAGE_SIZE`设置guest页的大小和DMA掩码。

（7）platform_set_drvdata函数

```c
static inline void platform_set_drvdata(struct platform_device *pdev, void *data);
static inline void *platform_get_drvdata(const struct platform_device *pdev)
```

- `pdev`：指向要设置私有数据的`platform device`的指针。
- `data`：要设置为私有数据的指针，可以指向任何数据结构。

​        `platform_set_drvdata` 函数是 Linux 内核中用于将私有数据指针与`platform device`相关联的函数。简单来说就是存储用户在probe函数中主动申请的内存区域的指针以防止丢失。在上述代码中是将初始化之后的 `virtio_mmio_device` 结构体和 platform  device相关联。相反的 `platform_get_drvdata` 函数则是将其取出，一般在remove函数中使用。

（8）注册 virtio device 到 virtio bus

​        分析到这里关于 virtio device 的前期初始化已经完成，包括 mmio 设备树和 driver 的probe，再到物理地址到虚拟地址的映射，再到从设备中读取 virtio device id / vender id 并初始化到 virtio device 等，后面的步骤就只是将前面初始化好的 virtio device 注册到 virtio bus 即可，如下：

```c
rc = register_virtio_device(&vm_dev->vdev);
if (rc)
	put_device(&vm_dev->vdev.dev);
```

调用 `register_virtio_device` 函数就可以将 virtio device 注册到 virtio bus 上，（注意到这里传递的参数是 `vm_dev->vdev` ），函数实现如下：

```c
int register_virtio_device(struct virtio_device *dev)
{
	int err;

	dev->dev.bus = &virtio_bus;  //这里设置了 virtio bus
	device_initialize(&dev->dev);

	/* Assign a unique device index and hence name. */
	err = ida_simple_get(&virtio_index_ida, 0, 0, GFP_KERNEL);
	if (err < 0)
		goto out;

	dev->index = err;
	dev_set_name(&dev->dev, "virtio%u", dev->index);

	spin_lock_init(&dev->config_lock);
	dev->config_enabled = false;
	dev->config_change_pending = false;

	/* We always start by resetting the device, in case a previous
	 * driver messed it up.  This also tests that code path a little. */
	dev->config->reset(dev);

	/* Acknowledge that we've seen the device. */
	virtio_add_status(dev, VIRTIO_CONFIG_S_ACKNOWLEDGE);

	INIT_LIST_HEAD(&dev->vqs);
	spin_lock_init(&dev->vqs_list_lock);

	/*
	 * device_add() causes the bus infrastructure to look for a matching
	 * driver.
	 */
	err = device_add(&dev->dev);
	if (err)
		ida_simple_remove(&virtio_index_ida, dev->index);
out:
	if (err)
		virtio_add_status(dev, VIRTIO_CONFIG_S_FAILED);
	return err;
}
```

在 register_virtio_device 函数的开头有一句代码：

```c
dev->dev.bus = &virtio_bus;
```

​        这一句代码就是将 virtio device 注册到 virtio bus 上面，如果device注册成功，bus就会去遍历注册在 virtio bus 上的所有 virtio driver，在前面分析 virtio driver 的时候就有分析到，virtio driver 最后也是将驱动注册到了virtio bus上面，所以如果当 virtio device和virtio driver 中的 device id 相同的话，driver 中的probe就会被调用，然后继续后面virtio的初始化，后面的代码将不再赘述。

Q3 :为什么只需要device id这一项来匹配virtio driver 和 virtio device ？

​        根据 linux 设备模型的知识可以知道，当driver注册到bus上时，kernel就会遍历bus上的所有device，当device被注册到kernel的时候kernel也会遍历bus上的所有driver进行匹配，如果匹配成功就会调用 driver 中的probe函数。所以 virtio bus 也适用这一套机制，当调用 register_virtio_driver 函数注册 virtio driver 的时候最终会调用到 virtio bus 中的 match函数进行驱动和设备的匹配。

virtio_bus 如下：

```c
static struct bus_type virtio_bus = {
	.name  = "virtio",
	.match = virtio_dev_match,
	.dev_groups = virtio_dev_groups,
	.uevent = virtio_uevent,
	.probe = virtio_dev_probe,
	.remove = virtio_dev_remove,
};
```

virtio_dev_match 函数的调用流程如下：

register_virtio_driver

​	-> driver_register

​		->bus_add_driver

​			->driver_attach

​				->bus_for_each_dev

​					->driver_attach

​						->__driver_attach

​							->driver_match_device

```c
static inline int driver_match_device(struct device_driver *drv,
				      struct device *dev)
{
	return drv->bus->match ? drv->bus->match(dev, drv) : 1;
}
```

 最终在`driver_match_device`函数中调用了 virtio bus 中的 match 函数进行驱动和设备的匹配，下面对virtio bus 中的 match 函数进行分析，也就是 `virtio_dev_match` 函数。代码如下：

```c
static int virtio_dev_match(struct device *_dv, struct device_driver *_dr)
{
	unsigned int i;
	struct virtio_device *dev = dev_to_virtio(_dv);
	const struct virtio_device_id *ids;
	dump_stack();
	ids = drv_to_virtio(_dr)->id_table;
	for (i = 0; ids[i].device; i++)
		if (virtio_id_match(dev, &ids[i]))
			return 1;
	return 0;
}
```

`virtio_dev_match`函数首先调用`drv_to_virtio`函数获取驱动中的 ID表，然后再遍历 ID 表并调用virtio_id_match 函数，如下：

```c
static inline int virtio_id_match(const struct virtio_device *dev,
				  const struct virtio_device_id *id)
{
	if (id->device != dev->id.device && id->device != VIRTIO_DEV_ANY_ID)
		return 0;

	return id->vendor == VIRTIO_DEV_ANY_ID || id->vendor == dev->id.vendor;
}
```

`virtio_id_match` 函数就是最终 driver 和device 进行匹配的函数，可以看到在该函数中主要判断了驱动中的设备 ID（`id->device`）是否与设备的设备 ID（`dev->id.device`）是否相同，如果相同则driver和device就匹配成功。

Q4 : 基于mmio的virtqueue需要怎么初始化？

前面介绍了mmio virtio_device 和 virtio_driver 的注册和probe过程，下面开始介绍在进入到 probe 函数之后初始化vq的流程。

​        这里以 virtio_blk 驱动进行简单分析，首先打开 driver/block/virtio_blk.c 文件，进入到 virtblk_probe -> init_vq 函数，该函数就是用来初始化虚拟队列 VQ 的，部分代码如下：

```c
static int init_vq(struct virtio_blk *vblk)
{
	vq_callback_t **callbacks;
	const char **names;
	struct virtqueue **vqs;
	unsigned short num_vqs;
	struct virtio_device *vdev = vblk->vdev;
	......
	for (i = 0; i < num_vqs; i++) {
		callbacks[i] = virtblk_done;
		snprintf(vblk->vqs[i].name, VQ_NAME_LEN, "req.%d", i);
		names[i] = vblk->vqs[i].name;
	}
	/* Discover virtqueues and write information to configuration.  */
	err = virtio_find_vqs(vdev, num_vqs, vqs, callbacks, names, &desc);
	......
	for (i = 0; i < num_vqs; i++) {
		spin_lock_init(&vblk->vqs[i].lock);
		vblk->vqs[i].vq = vqs[i];
	}
	vblk->num_vqs = num_vqs;
    ......
}
```

​	在这里主要进行了一些简单的初始化，比如设置 callbacks 回调函数、name 等，然后调用 virtio_find_vqs 函数，该函数最终会调用到     virtio_mmio_config_ops 结构体中的 vm_find_vqs 函数，函数如下：

```c
static int vm_find_vqs(struct virtio_device *vdev, unsigned nvqs,
		       struct virtqueue *vqs[],
		       vq_callback_t *callbacks[],
		       const char * const names[],
		       const bool *ctx,
		       struct irq_affinity *desc)
{
	struct virtio_mmio_device *vm_dev = to_virtio_mmio_device(vdev);
	int irq = platform_get_irq(vm_dev->pdev, 0);
	int i, err, queue_idx = 0;
	if (irq < 0) {
		dev_err(&vdev->dev, "Cannot get IRQ resource\n");
		return irq;
	}

	err = request_irq(irq, vm_interrupt, IRQF_SHARED,
			dev_name(&vdev->dev), vm_dev);
	if (err)
		return err;

	for (i = 0; i < nvqs; ++i) {
		if (!names[i]) {
			vqs[i] = NULL;
			continue;
		}

		vqs[i] = vm_setup_vq(vdev, queue_idx++, callbacks[i], names[i],
				     ctx ? ctx[i] : false);
		if (IS_ERR(vqs[i])) {
			vm_del_vqs(vdev);
			return PTR_ERR(vqs[i]);
		}
	}
	return 0;
}
```

**函数主要功能为：**

​	**获取IRQ资源：**通过`platform_get_irq`函数获取virtio设备的IRQ号。

​	**注册中断处理函数：**通过获取的IRQ号，使用`request_irq`函数注册中断处理函数`vm_interrupt`。

​	**设置虚拟队列：**遍历`nvqs`虚拟队列的数量，并调用`vm_setup_vq`函数来设置虚拟队列。

vm_setup_vq 函数如下：

```c
static struct virtqueue *vm_setup_vq(struct virtio_device *vdev, unsigned index,
				  void (*callback)(struct virtqueue *vq),
				  const char *name, bool ctx)
{
	struct virtio_mmio_device *vm_dev = to_virtio_mmio_device(vdev);
	struct virtio_mmio_vq_info *info;
	struct virtqueue *vq;
	unsigned long flags;
	unsigned int num;
	int err;

	if (!name)
		return NULL;
	/* Select the queue we're interested in */
	writel(index, vm_dev->base + VIRTIO_MMIO_QUEUE_SEL);

	/* Queue shouldn't already be set up. */
	if (readl(vm_dev->base + (vm_dev->version == 1 ?
			VIRTIO_MMIO_QUEUE_PFN : VIRTIO_MMIO_QUEUE_READY))) {
		err = -ENOENT;
		goto error_available;
	}

	/* Allocate and fill out our active queue description */
	info = kmalloc(sizeof(*info), GFP_KERNEL);
	if (!info) {
		err = -ENOMEM;
		goto error_kmalloc;
	}

	num = readl(vm_dev->base + VIRTIO_MMIO_QUEUE_NUM_MAX);
	if (num == 0) {
		err = -ENOENT;
		goto error_new_virtqueue;
	}
	/* Create the vring */
	vq = vring_create_virtqueue(index, num, VIRTIO_MMIO_VRING_ALIGN, vdev,
				 true, true, ctx, vm_notify, callback, name);
	if (!vq) {
		err = -ENOMEM;
		goto error_new_virtqueue;
	}

	/* Activate the queue */
	writel(virtqueue_get_vring_size(vq), vm_dev->base + VIRTIO_MMIO_QUEUE_NUM);
	if (vm_dev->version == 1) {
		u64 q_pfn = virtqueue_get_desc_addr(vq) >> PAGE_SHIFT;

		/*
		 * virtio-mmio v1 uses a 32bit QUEUE PFN. If we have something
		 * that doesn't fit in 32bit, fail the setup rather than
		 * pretending to be successful.
		 */
		if (q_pfn >> 32) {
			dev_err(&vdev->dev,
				"platform bug: legacy virtio-mmio must not be used with RAM above 0x%llxGB\n",
				0x1ULL << (32 + PAGE_SHIFT - 30));
			err = -E2BIG;
			goto error_bad_pfn;
		}
		writel(PAGE_SIZE, vm_dev->base + VIRTIO_MMIO_QUEUE_ALIGN);
		writel(q_pfn, vm_dev->base + VIRTIO_MMIO_QUEUE_PFN);
	} else {
		u64 addr;

		addr = virtqueue_get_desc_addr(vq);
		writel((u32)addr, vm_dev->base + VIRTIO_MMIO_QUEUE_DESC_LOW);
		writel((u32)(addr >> 32),
				vm_dev->base + VIRTIO_MMIO_QUEUE_DESC_HIGH);

		addr = virtqueue_get_avail_addr(vq);
		writel((u32)addr, vm_dev->base + VIRTIO_MMIO_QUEUE_AVAIL_LOW);
		writel((u32)(addr >> 32),
				vm_dev->base + VIRTIO_MMIO_QUEUE_AVAIL_HIGH);

		addr = virtqueue_get_used_addr(vq);
		writel((u32)addr, vm_dev->base + VIRTIO_MMIO_QUEUE_USED_LOW);
		writel((u32)(addr >> 32),
				vm_dev->base + VIRTIO_MMIO_QUEUE_USED_HIGH);

		writel(1, vm_dev->base + VIRTIO_MMIO_QUEUE_READY);
	}

	vq->priv = info;
	info->vq = vq;

	spin_lock_irqsave(&vm_dev->lock, flags);
	list_add(&info->node, &vm_dev->virtqueues);
	spin_unlock_irqrestore(&vm_dev->lock, flags);

	return vq;

error_bad_pfn:
	vring_del_virtqueue(vq);
error_new_virtqueue:
	if (vm_dev->version == 1) {
		writel(0, vm_dev->base + VIRTIO_MMIO_QUEUE_PFN);
	} else {
		writel(0, vm_dev->base + VIRTIO_MMIO_QUEUE_READY);
		WARN_ON(readl(vm_dev->base + VIRTIO_MMIO_QUEUE_READY));
	}
	kfree(info);
error_kmalloc:
error_available:
	return ERR_PTR(err);
}
```

vm_setup_vq 函数就是Linux内核中用于为 virtio-mmio 设备设置一个新的虚拟队列（virtqueue），是比较重要的一个函数，下面将对这个函数进行解释说明。

​	（1）通过写入`VIRTIO_MMIO_QUEUE_SEL`寄存器来选择要操作的队列。

```c
/* Select the queue we're interested in */
writel(index, vm_dev->base + VIRTIO_MMIO_QUEUE_SEL);
```

​	（2）通过读取`VIRTIO_MMIO_QUEUE_PFN`或`VIRTIO_MMIO_QUEUE_READY`寄存器来检查队列是否已经被设置，（具体使用那一个宏由 virtio - mmio 的版本决定）如果已设置，则返回错误。

```c
/* Queue shouldn't already be set up. */
if (readl(vm_dev->base + (vm_dev->version == 1 ?
		VIRTIO_MMIO_QUEUE_PFN : VIRTIO_MMIO_QUEUE_READY))) {
	err = -ENOENT;
}
```

​        （3）使用`kmalloc`为队列信息结构体`virtio_mmio_vq_info`分配内存。

```c
/* Allocate and fill out our active queue description */
info = kmalloc(sizeof(*info), GFP_KERNEL);
```

​	（4）通过读取`VIRTIO_MMIO_QUEUE_NUM_MAX`寄存器来获取设备所支持的最大队列数。

```c
num = readl(vm_dev->base + VIRTIO_MMIO_QUEUE_NUM_MAX);
if (num == 0) {
	err = -ENOENT;
}
```

​	（5）调用`vring_create_virtqueue`函数来创建虚拟环形缓冲区（vring）和对应的虚拟队列（virtqueue）。

```c
/* Create the vring */
vq = vring_create_virtqueue(index, num, VIRTIO_MMIO_VRING_ALIGN, vdev,
			 true, true, ctx, vm_notify, callback, name);
if (!vq) {
	err = -ENOMEM;
}
```

​	（6）将队列的vring大小写入`VIRTIO_MMIO_QUEUE_NUM`寄存器来激活队列。

```c
writel(virtqueue_get_vring_size(vq), vm_dev->base + VIRTIO_MMIO_QUEUE_NUM);
```

​	（7）如果`vm_dev->version`为1，则还需要处理32位物理地址的限制。

1. **计算队列描述符的物理页帧号（PFN）**：通过`virtqueue_get_desc_addr(vq) >> PAGE_SHIFT`计算vring描述符的物理页帧号。

```c
u64 q_pfn = virtqueue_get_desc_addr(vq) >> PAGE_SHIFT;
```

1. **检查32位PFN限制**：如果`q_pfn`的高32位不为0（即`q_pfn >> 32`），说明vring描述符的物理地址超出了32位能表示的范围。(virtio-mmio v1只支持32位物理地址)，则跳转到错误处理代码。

```c
if (q_pfn >> 32) {
	dev_err(&vdev->dev,
		"platform bug: legacy virtio-mmio must not be used with RAM above 0x%llxGB\n",
		0x1ULL << (32 + PAGE_SHIFT - 30));
	err = -E2BIG;
	goto error_bad_pfn;
}
writel(PAGE_SIZE, vm_dev->base + VIRTIO_MMIO_QUEUE_ALIGN);
writel(q_pfn, vm_dev->base + VIRTIO_MMIO_QUEUE_PFN);
```

2. **设置队列对齐和物理页帧号**：如果`q_pfn`在32位范围内，则设置队列的对齐大小（通常为页面大小）和物理页帧号到`VIRTIO_MMIO_QUEUE_ALIGN` 和 `VIRTIO_MMIO_QUEUE_PFN` 寄存器中（因为笔者的virtio-mmio是V1版本，所以不在介绍另外一种情况）。

最后返回 virtqueue 即可完成对虚拟队列的设置。关于为什么需要这些流程？这在手册中就有相关的规定，大家可以阅读 [Virtual I/O Device (VIRTIO) Version 1.0]() 手册的 [4.2 Virtio Over MMIO 章节]()， 下面是手册中的原文。

> The virtual queue is configured as follows:
> 1. Select the queue writing its index (first queue is 0) to QueueSel.
> 2. Check if the queue is not already in use: read QueuePFN, expecting a returned value of zero (0x0).
> 3. Read maximum queue size (number of elements) from QueueNumMax. If the returned value is zero
>   (0x0) the queue is not available.
> 4. Allocate and zero the queue pages in contiguous virtual memory, aligning the Used Ring to an opti-
>   mal boundary (usually page size). The driver should choose a queue size smaller than or equal to
>   QueueNumMax.
> 5. Notify the device about the queue size by writing the size to QueueNum.
> 6. Notify the device about the used alignment by writing its value in bytes to QueueAlign.
> 7. Write the physical number of the first page of the queue to the QueuePFN register.
>   Notification mechanisms did not change.
>
> Notification mechanisms did not change.

​        关于mmio virtqueue的初始化流程大概就是这些内容，这里只对框架进行了简单分析，对设置的一些参数的细节并没有说明，后面有时间再分享，比如写到 `VIRTIO_MMIO_QUEUE_PFN` 寄存器中的 q_pfn 值对 virtio 共享内存的创建就起到了关键的作用。

Q5 : 怎么在 qemu 中创建关于 virtio mmio 的设备树？

前面对 virtio 前端已经分析完毕，下面对 qemu 后端进行简单的分析，先看一下 qemu 的设备树文件内容（已删除部分节点）：

qemu-virt.dts

```dtd
/dts-v1/;

/ {
	interrupt-parent = <0x8005>;
	model = "linux,dummy-virt";
	#size-cells = <0x02>;
	#address-cells = <0x02>;
	compatible = "linux,dummy-virt";

	psci {
		migrate = <0xc4000005>;
		cpu_on = <0xc4000003>;
		cpu_off = <0x84000002>;
		cpu_suspend = <0xc4000001>;
		method = "hvc";
		compatible = "arm,psci-1.0", "arm,psci-0.2", "arm,psci";
	};

	memory@40000000 {
		reg = <0x00 0x40000000 0x00 0x40000000>;
		device_type = "memory";
	};

	platform-bus@c000000 {
		interrupt-parent = <0x8005>;
		ranges = <0x00 0x00 0xc000000 0x2000000>;
		#address-cells = <0x01>;
		#size-cells = <0x01>;
		compatible = "qemu,platform", "simple-bus";
	};

	fw-cfg@9020000 {
		dma-coherent;
		reg = <0x00 0x9020000 0x00 0x18>;
		compatible = "qemu,fw-cfg-mmio";
	};

	virtio_mmio@a000000 {
		dma-coherent;
		interrupts = <0x00 0x10 0x01>;
		reg = <0x00 0xa000000 0x00 0x200>;
		compatible = "virtio,mmio";
	};

	virtio_mmio@a000200 {
		dma-coherent;
		interrupts = <0x00 0x11 0x01>;
		reg = <0x00 0xa000200 0x00 0x200>;
		compatible = "virtio,mmio";
	};

	virtio_mmio@a000400 {
		dma-coherent;
		interrupts = <0x00 0x12 0x01>;
		reg = <0x00 0xa000400 0x00 0x200>;
		compatible = "virtio,mmio";
	};

	virtio_mmio@a000600 {
		dma-coherent;
		interrupts = <0x00 0x13 0x01>;
		reg = <0x00 0xa000600 0x00 0x200>;
		compatible = "virtio,mmio";
	};

	......

	virtio_mmio@a003e00 {
		dma-coherent;
		interrupts = <0x00 0x2f 0x01>;
		reg = <0x00 0xa003e00 0x00 0x200>;
		compatible = "virtio,mmio";
	};

	gpio-keys {
		compatible = "gpio-keys";

		poweroff {
			gpios = <0x8007 0x03 0x00>;
			linux,code = <0x74>;
			label = "GPIO Key Poweroff";
		};
	};

	pl061@9030000 {
		phandle = <0x8007>;
		clock-names = "apb_pclk";
		clocks = <0x8000>;
		interrupts = <0x00 0x07 0x04>;
		gpio-controller;
		#gpio-cells = <0x02>;
		compatible = "arm,pl061", "arm,primecell";
		reg = <0x00 0x9030000 0x00 0x1000>;
	};

	pl011@9000000 {
		clock-names = "uartclk", "apb_pclk";
		clocks = <0x8000 0x8000>;
		interrupts = <0x00 0x01 0x04>;
		reg = <0x00 0x9000000 0x00 0x1000>;
		compatible = "arm,pl011", "arm,primecell";
	};

	pmu {
		interrupts = <0x01 0x07 0x04>;
		compatible = "arm,armv8-pmuv3";
	};

	intc@8000000 {
		phandle = <0x8005>;
		reg = <0x00 0x8000000 0x00 0x10000 0x00 0x80a0000 0x00 0xf60000>;
		#redistributor-regions = <0x01>;
		compatible = "arm,gic-v3";
		ranges;
		#size-cells = <0x02>;
		#address-cells = <0x02>;
		interrupt-controller;
		#interrupt-cells = <0x03>;

		its@8080000 {
			phandle = <0x8006>;
			reg = <0x00 0x8080000 0x00 0x20000>;
			#msi-cells = <0x01>;
			msi-controller;
			compatible = "arm,gic-v3-its";
		};
	};

	flash@0 {
		bank-width = <0x04>;
		reg = <0x00 0x00 0x00 0x4000000 0x00 0x4000000 0x00 0x4000000>;
		compatible = "cfi-flash";
	};

	cpus {
		#size-cells = <0x00>;
		#address-cells = <0x01>;

		cpu-map {

			socket0 {

				cluster0 {

					core0 {
						cpu = <0x8004>;
					};

					core1 {
						cpu = <0x8003>;
					};

					core2 {
						cpu = <0x8002>;
					};

					core3 {
						cpu = <0x8001>;
					};
				};
			};
		};

		cpu@0 {
			phandle = <0x8004>;
			reg = <0x00>;
			enable-method = "psci";
			compatible = "arm,cortex-a53";
			device_type = "cpu";
		};

		cpu@1 {
			phandle = <0x8003>;
			reg = <0x01>;
			enable-method = "psci";
			compatible = "arm,cortex-a53";
			device_type = "cpu";
		};

		cpu@2 {
			phandle = <0x8002>;
			reg = <0x02>;
			enable-method = "psci";
			compatible = "arm,cortex-a53";
			device_type = "cpu";
		};

		cpu@3 {
			phandle = <0x8001>;
			reg = <0x03>;
			enable-method = "psci";
			compatible = "arm,cortex-a53";
			device_type = "cpu";
		};
	};

	timer {
		interrupts = <0x01 0x0d 0x04 0x01 0x0e 0x04 0x01 0x0b 0x04 0x01 0x0a 0x04>;
		always-on;
		compatible = "arm,armv8-timer", "arm,armv7-timer";
	};

	apb-pclk {
		phandle = <0x8000>;
		clock-output-names = "clk24mhz";
		clock-frequency = <0x16e3600>;
		#clock-cells = <0x00>;
		compatible = "fixed-clock";
	};

	chosen {
		bootargs = "console=ttyAMA0,38400 keep_bootcon root=/dev/vda nokaslr";
		stdout-path = "/pl011@9000000";
		rng-seed = <0x5ebd88ef 0x10a34b2b 0x7d840589 0x5d3e3fd0 0x61005889 0x3fd22dbd 0xe172d108 0xbeca534b>;
		kaslr-seed = <0x4d083b1f 0x518adc6f>;
	};
};
```

qemu-virt.dts 文件中的节点都是通过 qemu 进行创建的，这里以 virtio_mmio 节点进行分析，首先打开 qemu/hw/arm/virt.c 文件，进入到 `machvirt_init` 函数，部分代码如下：

```c
static void machvirt_init(MachineState *machine)
{
	......
	
    virt_set_memmap(vms, pa_bits);
    
    ......
    
    create_fdt(vms);

	......
   
    fdt_add_timer_nodes(vms);
    fdt_add_cpu_nodes(vms);

    memory_region_add_subregion(sysmem, vms->memmap[VIRT_MEM].base,
                                machine->ram);
    if (machine->device_memory) {
        memory_region_add_subregion(sysmem, machine->device_memory->base,
                                    &machine->device_memory->mr);
    }

    virt_flash_fdt(vms, sysmem, secure_sysmem ?: sysmem);

    create_gic(vms, sysmem);

    virt_cpu_post_init(vms, sysmem);

    fdt_add_pmu_nodes(vms);

    create_uart(vms, VIRT_UART, sysmem, serial_hd(0));

	......

    create_rtc(vms);

    create_pcie(vms);

	......

    /* Create mmio transports, so the user can create virtio backends
     * (which will be automatically plugged in to the transports). If
     * no backend is created the transport will just sit harmlessly idle.
     */
    create_virtio_devices(vms);

	......

    create_platform_bus(vms);

	......
}
```

如上述代码所示，`machvirt_init`  函数首先会调用 `create_fdt` 函数创建并配置一个 `Flattened Device Tree（FDT）`，（关于是怎么创建 FDT 的大家有兴趣可以自行查看，这里不再赘述），然后会创建各种 device node，比如 timer、cpu、 pmu、uart、pcie 等 node，而 virtio mmio 节点的创建还需要调用 `create_virtio_devices` 函数，如下：

```c
static void create_virtio_devices(const VirtMachineState *vms)
{
    int i;
    hwaddr size = vms->memmap[VIRT_MMIO].size;
    MachineState *ms = MACHINE(vms);

    /* We create the transports in forwards order. Since qbus_realize()
     * prepends (not appends) new child buses, the incrementing loop below will
     * create a list of virtio-mmio buses with decreasing base addresses.
     *
     * When a -device option is processed from the command line,
     * qbus_find_recursive() picks the next free virtio-mmio bus in forwards
     * order. The upshot is that -device options in increasing command line
     * order are mapped to virtio-mmio buses with decreasing base addresses.
     *
     * When this code was originally written, that arrangement ensured that the
     * guest Linux kernel would give the lowest "name" (/dev/vda, eth0, etc) to
     * the first -device on the command line. (The end-to-end order is a
     * function of this loop, qbus_realize(), qbus_find_recursive(), and the
     * guest kernel's name-to-address assignment strategy.)
     *
     * Meanwhile, the kernel's traversal seems to have been reversed; see eg.
     * the message, if not necessarily the code, of commit 70161ff336.
     * Therefore the loop now establishes the inverse of the original intent.
     *
     * Unfortunately, we can't counteract the kernel change by reversing the
     * loop; it would break existing command lines.
     *
     * In any case, the kernel makes no guarantee about the stability of
     * enumeration order of virtio devices (as demonstrated by it changing
     * between kernel versions). For reliable and stable identification
     * of disks users must use UUIDs or similar mechanisms.
     */
    for (i = 0; i < NUM_VIRTIO_TRANSPORTS; i++) {
        int irq = vms->irqmap[VIRT_MMIO] + i;
        hwaddr base = vms->memmap[VIRT_MMIO].base + i * size;

        sysbus_create_simple("virtio-mmio", base,
                             qdev_get_gpio_in(vms->gic, irq));
    }

    /* We add dtb nodes in reverse order so that they appear in the finished
     * device tree lowest address first.
     *
     * Note that this mapping is independent of the loop above. The previous
     * loop influences virtio device to virtio transport assignment, whereas
     * this loop controls how virtio transports are laid out in the dtb.
     */
    for (i = NUM_VIRTIO_TRANSPORTS - 1; i >= 0; i--) {
        char *nodename;
        int irq = vms->irqmap[VIRT_MMIO] + i;
        hwaddr base = vms->memmap[VIRT_MMIO].base + i * size;
        nodename = g_strdup_printf("/virtio_mmio@%" PRIx64, base);
        qemu_fdt_add_subnode(ms->fdt, nodename);
        qemu_fdt_setprop_string(ms->fdt, nodename,
                                "compatible", "virtio,mmio");
        qemu_fdt_setprop_sized_cells(ms->fdt, nodename, "reg",
                                     2, base, 2, size);
        qemu_fdt_setprop_cells(ms->fdt, nodename, "interrupts",
                               GIC_FDT_IRQ_TYPE_SPI, irq,
                               GIC_FDT_IRQ_FLAGS_EDGE_LO_HI);
        qemu_fdt_setprop(ms->fdt, nodename, "dma-coherent", NULL, 0);
        g_free(nodename);
    }
}
```

`create_virtio_devices` 函数就是用来为 linux kernel 创建 mmio 设备树节点的，里面的注释已经非常详细，感兴趣的可以进行详细分析，这里只对一些参数做一些分析。

1、自定义mmio device node 的个数

​	在上述代码 for  循环中有一个宏 `NUM_VIRTIO_TRANSPORTS` （#define NUM_VIRTIO_TRANSPORTS 32），这个宏就可以控制 mmio device node 的个数，默认是32，所以会在设备树中创建 32 个关于 mmio 的 device node。

2、mmio 的地址范围

先看一下 `MemMapEntry` 结构体

```c
static const MemMapEntry base_memmap[] = {
    /* Space up to 0x8000000 is reserved for a boot ROM */
    [VIRT_FLASH] =              {          0, 0x08000000 },
    [VIRT_CPUPERIPHS] =         { 0x08000000, 0x00020000 },
    /* GIC distributor and CPU interfaces sit inside the CPU peripheral space */
    [VIRT_GIC_DIST] =           { 0x08000000, 0x00010000 },
    [VIRT_GIC_CPU] =            { 0x08010000, 0x00010000 },
    [VIRT_GIC_V2M] =            { 0x08020000, 0x00001000 },
    [VIRT_GIC_HYP] =            { 0x08030000, 0x00010000 },
    [VIRT_GIC_VCPU] =           { 0x08040000, 0x00010000 },
    /* The space in between here is reserved for GICv3 CPU/vCPU/HYP */
    [VIRT_GIC_ITS] =            { 0x08080000, 0x00020000 },
    /* This redistributor space allows up to 2*64kB*123 CPUs */
    [VIRT_GIC_REDIST] =         { 0x080A0000, 0x00F60000 },
    [VIRT_UART] =               { 0x09000000, 0x00001000 },
    [VIRT_RTC] =                { 0x09010000, 0x00001000 },
    [VIRT_FW_CFG] =             { 0x09020000, 0x00000018 },
    [VIRT_GPIO] =               { 0x09030000, 0x00001000 },
    [VIRT_SECURE_UART] =        { 0x09040000, 0x00001000 },
    [VIRT_SMMU] =               { 0x09050000, 0x00020000 },
    [VIRT_PCDIMM_ACPI] =        { 0x09070000, MEMORY_HOTPLUG_IO_LEN },
    [VIRT_ACPI_GED] =           { 0x09080000, ACPI_GED_EVT_SEL_LEN },
    [VIRT_NVDIMM_ACPI] =        { 0x09090000, NVDIMM_ACPI_IO_LEN},
    [VIRT_PVTIME] =             { 0x090a0000, 0x00010000 },
    [VIRT_SECURE_GPIO] =        { 0x090b0000, 0x00001000 },
    [VIRT_MMIO] =               { 0x0a000000, 0x00000200 },
    /* ...repeating for a total of NUM_VIRTIO_TRANSPORTS, each of that size */
    [VIRT_PLATFORM_BUS] =       { 0x0c000000, 0x02000000 },
    [VIRT_SECURE_MEM] =         { 0x0e000000, 0x01000000 },
    [VIRT_PCIE_MMIO] =          { 0x10000000, 0x2eff0000 },
    [VIRT_PCIE_PIO] =           { 0x3eff0000, 0x00010000 },
    [VIRT_PCIE_ECAM] =          { 0x3f000000, 0x01000000 },
    /* Actual RAM size depends on initial RAM and device memory settings */
    [VIRT_MEM] =                { GiB, LEGACY_RAMLIMIT_BYTES },
};
```

​        MemMapEntry 结构体定义了每个设备的addr和size，`[VIRT_MMIO] = { 0x0a000000, 0x00000200 },`表示为MMIO（内存映射I/O）区域分配了空间，以 0x0a000000 为基地址，size为 0x00000200 ，也就是 512kb 的空间，而VIRT_MMIO 是一个枚举值，如下：

```c
enum {
    VIRT_FLASH,
    VIRT_MEM,
    VIRT_CPUPERIPHS,
    VIRT_GIC_DIST,
    VIRT_GIC_CPU,
    VIRT_GIC_V2M,
    VIRT_GIC_HYP,
    VIRT_GIC_VCPU,
    VIRT_GIC_ITS,
    VIRT_GIC_REDIST,
    VIRT_SMMU,
    VIRT_UART,
    VIRT_MMIO,
    VIRT_RTC,
    VIRT_FW_CFG,
    VIRT_PCIE,
    VIRT_PCIE_MMIO,
    VIRT_PCIE_PIO,
    VIRT_PCIE_ECAM,
    VIRT_PLATFORM_BUS,
    VIRT_GPIO,
    VIRT_SECURE_UART,
    VIRT_SECURE_MEM,
    VIRT_SECURE_GPIO,
    VIRT_PCDIMM_ACPI,
    VIRT_ACPI_GED,
    VIRT_NVDIMM_ACPI,
    VIRT_PVTIME,
    VIRT_LOWMEMMAP_LAST,
};
```

在 machvirt_init函数中有一个 virt_set_memmap 函数，进入这个函数能看到如下代码段：

```c
static void virt_set_memmap(VirtMachineState *vms, int pa_bits)
{
	......

    vms->memmap = extended_memmap;

    for (i = 0; i < ARRAY_SIZE(base_memmap); i++) {
        vms->memmap[i] = base_memmap[i];
    }

	......

    for (i = VIRT_LOWMEMMAP_LAST; i < ARRAY_SIZE(extended_memmap); i++) {
        hwaddr size = extended_memmap[i].size;
        bool fits;

        base = ROUND_UP(base, size);
        vms->memmap[i].base = base;
        vms->memmap[i].size = size;
	......
}
```

​        在这个函数中将 base_memmap[i] 赋值给了vms -> memmap[i]，结合前面的分析，也可以得出 vms->memmap[VIRT_MMIO] = base_memmap[VIRT_MMIO] ，并在后面对 base 和 size 都进行了设置。

​	按照前面的分析，在 `create_virtio_devices` 函数中 `hwaddr base = vms->memmap[VIRT_MMIO].base + i * size` 是用来设置 mmio 的基地址，即：

> i = 0   => base =  vms->memmap[VIRT_MMIO].base + i * size 即 base = 0x0a000000 + 0 = 0x0a000000;
>
> i = 1   => base =  vms->memmap[VIRT_MMIO].base + i * size 即 base = 0x0a000000 + 1 * 0x00000200 = 0x0a000200;
>
> i = 2   => base =  vms->memmap[VIRT_MMIO].base + i * size 即 base = 0x0a000000 + 2 * 0x00000200 = 0x0a000400;
>
> ......
>
> i = 31   => base =  vms->memmap[VIRT_MMIO].base + i * size 即 base = 0x0a000000 + 31 * 0x00000200 = 0xa003e00;

​        所以当 NUM_VIRTIO_TRANSPORTS = 32 时，mmio device node 的 base address 如上所示，即地址范围为 0x0a000000 ~ 

0xa003e00，这也就解释了在 qemu-virt.dts 文件中，为什么 virtio mmio node 的地址为什么是从 virtio_mmio@a000000 开始，而到 virtio_mmio@a003e00 结束，共32个 node 。

3、怎么计算 IRQ？

​	首先看一下 `create_virtio_devices` 函数中获取 irq 的方法：

```c
int irq = vms->irqmap[VIRT_MMIO] + i;
```

可以看出 virtio mmio irq 和 `vms->irqmap[VIRT_MMIO]` 和 for 循环变量 i 有关，那 `vms->irqmap[VIRT_MMIO]` 等于什么呢？打开 hw/arm/virt.c 文件，找到 `virt_instance_init` 函数，能看到如下代码段：

```c
static void virt_instance_init(Object *obj)
{
	......

    vms->irqmap = a15irqmap;

	......
}
```

a15irqmap 是一个数组，如下：

```c
static const int a15irqmap[] = {
    [VIRT_UART] = 1,
    [VIRT_RTC] = 2,
    [VIRT_PCIE] = 3, /* ... to 6 */
    [VIRT_GPIO] = 7,
    [VIRT_SECURE_UART] = 8,
    [VIRT_ACPI_GED] = 9,
    [VIRT_MMIO] = 16, /* ...to 16 + NUM_VIRTIO_TRANSPORTS - 1 */
    [VIRT_GIC_V2M] = 48, /* ...to 48 + NUM_GICV2M_SPIS - 1 */
    [VIRT_SMMU] = 74,    /* ...to 74 + NUM_SMMU_IRQS - 1 */
    [VIRT_PLATFORM_BUS] = 112, /* ...to 112 + PLATFORM_BUS_NUM_IRQS -1 */
};
```

可以看到数组 a15irqmap 的 a15irqmap[VIRT_MMIO] = 16，所以 virtio mmio 的 irq  =  16 + i （i < NUM_VIRTIO_TRANSPORTS）。

即：

> irq = vms->irqmap[VIRT_MMIO] + i
>
> 当 i = 0  时  irq = 16 + 0 = 16;
>
> 当 i = 1   时  irq = 16 + 1 = 17;
>
> ......
>
> 当 i = 31  时  irq = 16 + 31 = 47;

​	目前只是简单分析了在 qemu 设备虚拟化中如何计算和设置的 virtio_mmio 设备节点中的 interrupts 和 reg 参数，其它参数的设置和初始化大家感兴趣可以自行阅读 qemu 的代码，这里暂且跳过。

Q6 : virtio_mmio 前后端notify机制流程是怎样的？

1. host notify guest

​	host 通知 guest 采用的是给 guest 注入中断的方式进行，而在代码中只需要在 qemu 代码中调用 virtio_notify 函数即可，下面对virtio_notify 函数进行简单的分析，函数如下：

```c
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

这里首先会判断此时是否应该通知 guest ,如果可以通知则继续往下运行

```c
static void virtio_irq(VirtQueue *vq)
{
    virtio_set_isr(vq->vdev, 0x1);
    virtio_notify_vector(vq->vdev, vq->vector);
}
```

```c
/* virtio device */
static void virtio_notify_vector(VirtIODevice *vdev, uint16_t vector)
{
    BusState *qbus = qdev_get_parent_bus(DEVICE(vdev));
    VirtioBusClass *k = VIRTIO_BUS_GET_CLASS(qbus);

    if (virtio_device_disabled(vdev)) {
        return;
    }

    if (k->notify) {
        k->notify(qbus->parent, vector);
    }
}
```

可以看到 最后是调用了 VirtioBusClass *k -> notify 函数，那该函数在哪里呢？因为该 virtio 是基于 mmio ，所以打开  hw/virtio/virtio-mmio.c 文件并进入到 `virtio_mmio_bus_class_init` 函数，如下：

```c
static void virtio_mmio_bus_class_init(ObjectClass *klass, void *data)
{
    ......
    VirtioBusClass *k = VIRTIO_BUS_CLASS(klass);

    k->notify = virtio_mmio_update_irq;
	......
}
```

在 virtio_mmio_bus_class_init 函数中就对 notify 函数进行了设置，进入到 virtio_mmio_update_irq 函数，如下：

```c
static void virtio_mmio_update_irq(DeviceState *opaque, uint16_t vector)
{
	......
    trace_virtio_mmio_setting_irq(level);
    qemu_set_irq(proxy->irq, level);
}
```

qemu_set_irq :

```c
void qemu_set_irq(qemu_irq irq, int level)
{
    if (!irq)
        return;

    irq->handler(irq->opaque, irq->n, level);
}
```

​	可以看到最后是调用了 qemu_irq irq->handler 函数进行中断的注入，告诉 guest 前端数据已经处理完毕，那这个 handler 函数指针又在哪里设置的呢？ 感兴趣的话可以参考 [这位](https://luohao-brian.gitbooks.io/interrupt-virtualization/content/qemu-kvm-zhong-duan-xu-ni-hua-kuang-jia-fen-679028-4e2d29.html) 老哥的文章，里面会涉及到中断虚拟化的知识，稍微有点复杂，这里将不在赘述，后面有机会再分析。

​	假设目前 host 已经给 guest 注入了一个中断，那 guest 端会出现什么状态呢？还记得我们在分析 virtio guest driver 的时候注册的一个中断函数吗？如下：

```c
static int vm_find_vqs(struct virtio_device *vdev, unsigned nvqs,
		       struct virtqueue *vqs[],
		       vq_callback_t *callbacks[],
		       const char * const names[],
		       const bool *ctx,
		       struct irq_affinity *desc)
{
	......

	err = request_irq(irq, vm_interrupt, IRQF_SHARED,
			dev_name(&vdev->dev), vm_dev);
    ......
    	/* Create the vring */
	vq = vring_create_virtqueue(index, num, VIRTIO_MMIO_VRING_ALIGN, vdev,
				 true, true, ctx, vm_notify, callback, name);
	......
}
```

vring_create_virtqueue：

```c
vring_create_virtqueue
-> vring_create_virtqueue_split
	-> __vring_new_virtqueue(
				bool (*notify)(struct virtqueue *),
				void (*callback)(struct virtqueue *)...)
	{
		struct vring_virtqueue *vq;
		......
		vq->vq.callback = callback;
		......
		vq->notify = notify;
		......
	}
```

vm_interrupt：

```c
/* Notify all virtqueues on an interrupt. */
static irqreturn_t vm_interrupt(int irq, void *opaque)
{
	......
	ret |= vring_interrupt(irq, info->vq);
	......
}
```

```c
irqreturn_t vring_interrupt(int irq, void *_vq)
{
	struct vring_virtqueue *vq = to_vvq(_vq);
	......
	if (vq->vq.callback)
		vq->vq.callback(&vq->vq);
	......
}
```

​	在 vm_find_vqs 函数中通过 request_irq 函数注册了一个irq中断，并将 callback 赋给 vring_virtqueue vq->vq.callback，（`vq->notify = notify 用于 guest 向 host kick`）当 host 给前端 guest 注入一个中断时，中断回调函数 vm_interrupt 就会被调用，最后会调用到自定义的 callbacks 函数，从而完成 host 通知 guest 的流程。 

host notify guest 的流程到这里就分析结束了，目前只分析了大概流程，而中断注入到底是怎么注入的？以及 guest 端是怎么收到这个中断的？这些问题等后面有时间了再继续分析。

附 irq callback 调用栈：

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

2. guest notify host

​	在普通的 OS 中，用户空间发起一个IO请求时，会经过一系列调用流程，最后到达 blk device driver 层，由 driver 层处理请求，而读写磁盘数据。而在虚拟机 OS 中，IO请求的过程大致相同，不同的地方在于最后是将IO请求交给了 virtio driver，而不是直接读写磁盘。

比如 GuestOS 来了一个关于 blk 的 IO请求后，会调用 virtio_blk.c 文件中的处理函数 virtio_queue_rq ，流程如下：

```c
static blk_status_t virtio_queue_rq(struct blk_mq_hw_ctx *hctx,
			   const struct blk_mq_queue_data *bd)
{
	......
	err = virtblk_add_req(vblk->vqs[qid].vq, vbr, vbr->sg, num);
	if (err) {
		virtqueue_kick(vblk->vqs[qid].vq);
	......
}
```

​	这里只保留了关键的代码段，virtblk_add_req 函数用来将请求数据包添加到 VQ中，添加完成后就调用 virtqueue_kick 函数来通知 host 后端有数据需要处理了，virtqueue_kick 函数如下：

```c
bool virtqueue_kick(struct virtqueue *vq)
{
	if (virtqueue_kick_prepare(vq))
		return virtqueue_notify(vq);
	return true;
}
```

```c
static __inline__ int vring_need_event(__u16 event_idx, __u16 new_idx, __u16 old)
{
	/* Note: Xen has similar logic for notification hold-off
	 * in include/xen/interface/io/ring.h with req_event and req_prod
	 * corresponding to event_idx + 1 and new_idx respectively.
	 * Note also that req_event and req_prod in Xen start at 1,
	 * event indexes in virtio start at 0. */
	return (__u16)(new_idx - event_idx - 1) < (__u16)(new_idx - old);
}
```

首先调用 virtqueue_kick_prepare 函数看是否需要kick host，kick的两个条件为：

（1）如果 vring.used->flags 设置为VRING_USED_F_NO_NOTIFY，表示不通知，反之通知。

（2）看 vring_virtqueue vq->event 是否大于 0，如果大于0 还需要根据 vring_need_event 函数的返回值进行再一次的判断。

如果需要kick则调用virtqueue_notify，这个函数直接调用vq->notify(_vq)来通知 host 端，如下：

```c
/**
 * virtqueue_notify - second half of split virtqueue_kick call.
 * @_vq: the struct virtqueue
 *
 * This does not need to be serialized.
 *
 * Returns false if host notify failed or queue is broken, otherwise true.
 */
bool virtqueue_notify(struct virtqueue *_vq)
{
	......
	if (!vq->notify(_vq)) {
.	.....
}
```

那 vq->notify 函数在哪里设置的呢？ 大家可以阅读 " host notify guest" ,就能够知道 notify 函数最后是在 __vring_new_virtqueue 函数中设置的，notify 函数如下：

```c
/* the notify function used when creating a virt queue */
static bool vm_notify(struct virtqueue *vq)
{
	struct virtio_mmio_device *vm_dev = to_virtio_mmio_device(vq->vdev);

	/* We write the queue's selector into the notification register to
	 * signal the other end */
	writel(vq->index, vm_dev->base + VIRTIO_MMIO_QUEUE_NOTIFY);
	return true;
}
```

​	writel 函数用来将 vq 的 index 写入到 virtio mmio 空间中的 VIRTIO_MMIO_QUEUE_NOTIFY 寄存器，这个操作会立即产生 VMExit   ，会被 KVM 拦截，KVM 直接与 guest OS 进行交互，所以 guest OS 的 IO 操作会先到 KVM，从而 KVM 对 IO 操作进行拦截，VMExit后会回到 vcpu_enter_guest 函数接着后面的执行。

guest OS 发生 VMExit 之后会先返回到 vcpu_enter_guest 函数，这个函数会接着调用 vmx_handle_exit 对VMExit进行处理，流程如下：

arch/x86/kvm/x86.c

```c
kvm_arch_vcpu_ioctl_run()
->
    static int vcpu_run(struct kvm_vcpu *vcpu)
    {
        ......
        for (;;) {
            if (kvm_vcpu_running(vcpu)) {
                r = vcpu_enter_guest(vcpu);
            } else {
                r = vcpu_block(kvm, vcpu);
            }
            if (r <= 0)
                break;
        ......
        }
    }
```

vcpu_enter_guest：

```c
/*
 * Returns 1 to let vcpu_run() continue the guest execution loop without
 * exiting to the userspace.  Otherwise, the value will be returned to the
 * userspace.
 */
static int vcpu_enter_guest(struct kvm_vcpu *vcpu)
{
	......
	kvm_x86_ops->prepare_guest_switch(vcpu);
    ......
	kvm_x86_ops->run(vcpu);
	......
	/*
	 * Profile KVM exit RIPs:
	 */
	......
	r = kvm_x86_ops->handle_exit(vcpu);
	......
}
```

handle_exit 的初始化：

```c
static struct kvm_x86_ops vmx_x86_ops __ro_after_init = {
	......
	.run = vmx_vcpu_run,
	.handle_exit = vmx_handle_exit,
	......
};
```

vmx_handle_exit：

```c
/*
 * The guest has exited.  See if we can fix it or if we need userspace
 * assistance.
 */
static int vmx_handle_exit(struct kvm_vcpu *vcpu)
{
	struct vcpu_vmx *vmx = to_vmx(vcpu);
	u32 exit_reason = vmx->exit_reason;
	......
	return kvm_vmx_exit_handlers[exit_reason](vcpu);
	......
}
```

kvm_vmx_exit_handlers：

```c
/*
 * The exit handlers return 1 if the exit was handled fully and guest execution
 * may resume.  Otherwise they set the kvm_run parameter to indicate what needs
 * to be done to userspace and return 0.
 */
static int (*kvm_vmx_exit_handlers[])(struct kvm_vcpu *vcpu) = {
	......
	[EXIT_REASON_TRIPLE_FAULT]            = handle_triple_fault,
	[EXIT_REASON_NMI_WINDOW]	      = handle_nmi_window,
	[EXIT_REASON_IO_INSTRUCTION]          = handle_io,
    [EXIT_REASON_CR_ACCESS]               = handle_cr,
	[EXIT_REASON_DR_ACCESS]               = handle_dr,
	[EXIT_REASON_EPT_MISCONFIG]           = handle_ept_misconfig,
	......
};
```

​	根据 VM 的退出原因执行对应的handler，因为是读写 MMIO 内存，所以这里是 ept misconfiguration ，（ Guest OS 写MMIO内存时每次都会缺页异常而退出，然后交给后端来处理，和敏感指令差不多）即handle_ept_misconfig函数，如下：

```c
static int handle_ept_misconfig(struct kvm_vcpu *vcpu)
{
	gpa_t gpa;

	/*
	 * A nested guest cannot optimize MMIO vmexits, because we have an
	 * nGPA here instead of the required GPA.
	 */
	gpa = vmcs_read64(GUEST_PHYSICAL_ADDRESS);
	if (!is_guest_mode(vcpu) &&
	    !kvm_io_bus_write(vcpu, KVM_FAST_MMIO_BUS, gpa, 0, NULL)) {
		trace_kvm_fast_mmio(gpa);
		return kvm_skip_emulated_instruction(vcpu);
	}

	return kvm_mmu_page_fault(vcpu, gpa, PFERR_RSVD_MASK, NULL, 0);
}
```

​	handle_ept_misconfig 是处理KVM中EPT配置出错的回调函数，（EPT是Intel处理器中用于支持虚拟化技术的页表机制，允许虚拟机直接访问物理内存，同时由 Host 控制和管理这些访问）。

读取Guest OS物理地址：

```c
gpa = vmcs_read64(GUEST_PHYSICAL_ADDRESS);
```

​	通过调用`vmcs_read64`函数并传入`GUEST_PHYSICAL_ADDRESS`，即从 VMCS 中读取并存储当前触发EPT错误的 Guest OS 的 GPA。

处理MMIO：

```c
if (!is_guest_mode(vcpu) &&
    !kvm_io_bus_write(vcpu, KVM_FAST_MMIO_BUS, gpa, 0, NULL)) {
	trace_kvm_fast_mmio(gpa);
	return kvm_skip_emulated_instruction(vcpu);
}
```

​	`is_guest_mode` 函数首先检查是否处于嵌套虚拟化模式，（嵌套虚拟化指在一个VM内部再运行一个VM）如果不在嵌套模式下，会尝试通过`kvm_io_bus_write`函数来快速处理MMIO访问。

​	`kvm_io_bus_write`函数会尝试在不退出到 host OS 的情况下，直接在KVM内部处理MMIO访问。如果成功，则会进入 if 语句记录一个trace，并调用 `kvm_skip_emulated_instruction` 函数跳过当前指令的模拟执行，因为MMIO访问已经被处理。

处理EPT页错误：

```c
return kvm_mmu_page_fault(vcpu, gpa, PFERR_RSVD_MASK, NULL, 0);
```

​	如果不在嵌套模式下且MMIO的访问没有被快速处理，则调用`kvm_mmu_page_fault`函数来处理EPT页错误，函数如下：

```c
int kvm_mmu_page_fault(struct kvm_vcpu *vcpu, gpa_t cr2_or_gpa, u64 error_code,
		       void *insn, int insn_len)
{
	......
	if (unlikely(error_code & PFERR_RSVD_MASK)) {
		r = handle_mmio_page_fault(vcpu, cr2_or_gpa, direct);
		if (r == RET_PF_EMULATE)
			goto emulate;
	}
	......
emulate:
	/*
	 * On AMD platforms, under certain conditions insn_len may be zero on #NPF.
	 * This can happen if a guest gets a page-fault on data access but the HW
	 * table walker is not able to read the instruction page (e.g instruction
	 * page is not present in memory). In those cases we simply restart the
	 * guest, with the exception of AMD Erratum 1096 which is unrecoverable.
	 */
	if (unlikely(insn && !insn_len)) {
		if (!kvm_x86_ops->need_emulation_on_page_fault(vcpu))
			return 1;
	}

	return x86_emulate_instruction(vcpu, cr2_or_gpa, emulation_type, insn,
				       insn_len);
}
```

handle_mmio_page_fault：

```c
static int handle_mmio_page_fault(struct kvm_vcpu *vcpu, u64 addr, bool direct)
{
	u64 spte;
	bool reserved;

	if (mmio_info_in_cache(vcpu, addr, direct))
		return RET_PF_EMULATE;

	reserved = walk_shadow_page_get_mmio_spte(vcpu, addr, &spte);
	if (WARN_ON(reserved))
		return -EINVAL;

	if (is_mmio_spte(spte)) {
		gfn_t gfn = get_mmio_spte_gfn(spte);
		unsigned access = get_mmio_spte_access(spte);

		if (!check_mmio_spte(vcpu, spte))
			return RET_PF_INVALID;

		if (direct)
			addr = 0;

		trace_handle_mmio_page_fault(addr, gfn, access);
		vcpu_cache_mmio_info(vcpu, addr, gfn, access);
		return RET_PF_EMULATE;
	}

	/*
	 * If the page table is zapped by other cpus, let CPU fault again on
	 * the address.
	 */
	return RET_PF_RETRY;
}
```

​	`handle_mmio_page_fault` 函数用来处理由 MMIO 访问引起的页面错误。

检查 MMIO 信息缓存：

```c
if (mmio_info_in_cache(vcpu, addr, direct))
```

如果引起MMIO的GPA在之前就保存在缓存中，则跳过页表遍历，直接返回 `RET_PF_EMULATE`，表示应该通过模拟来处理这个页面错误。

处理 MMIO SPTE：

​	通过 `is_mmio_spte`判断取出的页表项SPTE是否为MMIO SPTE类型的，如果是，则提取该 SPTE 的页框号 GFN（Guest Frame Number），在执行指令模拟前，调用 `vcpu_cache_mmio_info` 函数将 MMIO 信息缓存起来，如果是相同GPA地址的MMIO缺页就可以直接从缓存中去spte，以便将来快速访问。

最后返回 `RET_PF_EMULATE`，表示需要KVM通过模拟MMIO内存的读写指令来处理这个页面错误。

回到 kvm_mmu_page_fault 函数，因为 handle_mmio_page_fault 函数返回的是 RET_PF_EMULATE，所以会 goto emulate，即：

```c
emulate:
......
	return x86_emulate_instruction(vcpu, cr2_or_gpa, emulation_type, insn,
				       insn_len);
```

x86_emulate_instruction：

```c
int x86_emulate_instruction(struct kvm_vcpu *vcpu, gpa_t cr2_or_gpa,
			    int emulation_type, void *insn, int insn_len)
{
	int r;
	......
	} else if (vcpu->mmio_needed) {
		++vcpu->stat.mmio_exits;

		if (!vcpu->mmio_is_write)
			writeback = false;
		r = 0;
		vcpu->arch.complete_userspace_io = complete_emulated_mmio;
	} else if (r == EMULATION_RESTART)
		goto restart;
	else
		r = 1;
	......
	return r;
}
```

​	x86_emulate_instruction 函数是KVM中用来执行指令模拟的，而上面代码段是处理x86指令模拟执行后的后续操作。主要根据当前虚拟vcpu的状态来决定是否需要退出模拟、以及如何处理这些状态，并设置相应的回调函数在需要时从用户空间获取数据。

处理MMIO操作：

- 如果 mmio_needed 为 true时进入 if，并增加统计信息中MMIO退出的次数。
- 将返回值`r`设置为0，会返回到kvm_arch_vcpu_ioctl_run循环中，即vcpu_enter_guest返回值为0，KVM会跳出循环，返回到用户空间。

```c
vcpu->arch.complete_userspace_io = complete_emulated_mmio;
```

​	在返回用户空间之前还需要准备和MMIO相关的一些信息传递到用户空间，这里会通过设置vcpu->arch.complete_userspace_io函数来体现，即 complete_emulated_mmio 函数。

 complete_emulated_mmio 函数如下：

```c
/*
 * Implements the following, as a state machine:
 *
 * read:
 *   for each fragment
 *     for each mmio piece in the fragment
 *       write gpa, len
 *       exit
 *       copy data
 *   execute insn
 *
 * write:
 *   for each fragment
 *     for each mmio piece in the fragment
 *       write gpa, len
 *       copy data
 *       exit
 */
static int complete_emulated_mmio(struct kvm_vcpu *vcpu)
{
	struct kvm_run *run = vcpu->run;
	......

	run->exit_reason = KVM_EXIT_MMIO;
	run->mmio.phys_addr = frag->gpa;
	if (vcpu->mmio_is_write)
		memcpy(run->mmio.data, frag->data, min(8u, frag->len));
	run->mmio.len = min(8u, frag->len);
	run->mmio.is_write = vcpu->mmio_is_write;
	vcpu->arch.complete_userspace_io = complete_emulated_mmio;
	return 0;
}
```

> run->exit_reason = KVM_EXIT_MMIO：设置KVM退回到Qemu的原因。
>
> run->mmio.phys_addr = frag->gpa：设置MMIO PF时访问的物理地址。
>
> if (vcpu->mmio_is_write)  ：如果是对MMIO内存的写操作，还需要拷贝要写入的数据，再传递给Qemu用户空间。 
>
> ​    memcpy(run->mmio.data, frag->data, min(8u, frag->len));

​	complete_emulated_mmio 函数就分析到这里，那该函数是在哪里被调用的呢？也就是在哪里运行的 vcpu->arch.complete_ userspace_io？

进入到 kvm_vcpu_ioctl 函数：

```c
static long kvm_vcpu_ioctl(struct file *filp,
			   unsigned int ioctl, unsigned long arg)
{
	......
	switch (ioctl) {
	case KVM_RUN: {
	......
		r = kvm_arch_vcpu_ioctl_run(vcpu, vcpu->run);
		trace_kvm_userspace_exit(vcpu->run->exit_reason, r);
		break;
	}
	......
}
static struct file_operations kvm_vcpu_fops = {
	......
	.unlocked_ioctl = kvm_vcpu_ioctl,
	......
};
```

kvm_arch_vcpu_ioctl_run 函数：(linux/arch/x86/kvm/x86.c)

```c
int kvm_arch_vcpu_ioctl_run(struct kvm_vcpu *vcpu, struct kvm_run *kvm_run)
{
	......
	if (unlikely(vcpu->arch.complete_userspace_io)) {
		int (*cui)(struct kvm_vcpu *) = vcpu->arch.complete_userspace_io;
		vcpu->arch.complete_userspace_io = NULL;
		r = cui(vcpu);
		if (r <= 0)
			goto out;
	} else
		WARN_ON(vcpu->arch.pio.count || vcpu->mmio_needed);

	if (kvm_run->immediate_exit)
		r = -EINTR;
	else
		r = vcpu_run(vcpu);
out:
	kvm_put_guest_fpu(vcpu);
	if (vcpu->run->kvm_valid_regs)
		store_regs(vcpu);
	post_kvm_run_save(vcpu);
	kvm_sigset_deactivate(vcpu);

	vcpu_put(vcpu);
	return r;
}
```

​	vcpu_run：实际执行vCPU的指令。其中vCPU会一直运行，直到出现需要退出到host的条件。

​	kvm_arch_vcpu_ioctl_run 函数主要是让 vCPU 运行起来，并进入Guest态，但在进入Guest态前，需要调用 unlikely(vcpu->arch. complete_userspace_io) 函数检查是否有需要在用户空间完成的IO 操作，可以看到在这里调用了之前在x86_emulate_instruction函数中注册在complete_userspace_io的函数，最后调用了 complete_emulated_mmio 函数。

在执行完complete_emulated_mmio函数后，vCPU并不会继续执行vcpu_run函数进入Guest态，而是会返回用户态的Qemu。

​	至此，KVM就完成了对MMIO PF的所有处理，并将后续的处理交给了qemu，接下来的流程就是返回qemu之后qemu再对 mmio 进行相关操作，qemu部分下一小节在分析了。

QEMU 端：

​	经过前面的分析在调用vm_notify函数时，其实是在对mmio内存进行写操作，会产生 VMexit ，退出到 KVM，在KVM中经过一系列的处理之后会返回QEMU用户空间继续执行mmio的回调函数，所以首先介绍一下 qemu 用户空间和 mmio 操作相关的回调函数。

在Qemu中用 MemoryRegionOps 结构体描述MMIO内存读写的回调，如下：

/include/exec/memory.h

```c
/*
 * Memory region callbacks
 */
struct MemoryRegionOps {
    /* Read from the memory region. @addr is relative to @mr; @size is
     * in bytes. */
    uint64_t (*read)(void *opaque,
                     hwaddr addr,
                     unsigned size);
    /* Write to the memory region. @addr is relative to @mr; @size is
     * in bytes. */
    void (*write)(void *opaque,
                  hwaddr addr,
                  uint64_t data,
                  unsigned size);
};
```

qemu中mmio 配置空间的读写函数如下所示：（/hw/virtio/virtio-mmio.c）

```c
static uint64_t virtio_mmio_read(void *opaque, hwaddr offset, unsigned size)
{
	......
    case VIRTIO_MMIO_QUEUE_NOTIFY: //因为 notify 是 write 配置空间，所以 read 为空
	......
}
static void virtio_mmio_write(void *opaque, hwaddr offset, uint64_t value,
                              unsigned size)
{
    VirtIOMMIOProxy *proxy = (VirtIOMMIOProxy *)opaque;
    VirtIODevice *vdev = virtio_bus_get_device(&proxy->bus);
	......
    case VIRTIO_MMIO_QUEUE_NOTIFY:
        if (value < VIRTIO_QUEUE_MAX) {
            virtio_queue_notify(vdev, value);
        }
        break;
	......
}
static const MemoryRegionOps virtio_mem_ops = {
    .read = virtio_mmio_read,
    .write = virtio_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};
```

在这里就初始化了和 mmio 配置空间相关的 read/write 回调函数，那这些函数被初始化到了哪里呢？又在哪里被调用呢？ 打开 virtio_ mmio_realizefn 函数，如下：

```c
static void virtio_mmio_realizefn(DeviceState *d, Error **errp)
{
	......
    } else {
        memory_region_init_io(&proxy->iomem, OBJECT(d),
                              &virtio_mem_ops, proxy,
                              TYPE_VIRTIO_MMIO, 0x200);
    }
    sysbus_init_mmio(sbd, &proxy->iomem);
}
```

内存区域初始化：

```c
memory_region_init_io
```

这个函数用于初始化一个内存区域，用于virtio-mmio设备的通信。

​	函数第三个参数就是上面初始化的内存操作结构体virtio_mem_ops，定义了内存区域的读写回调，TYPE_VIRTIO_MMIO表示内存区域的类型，0x200是内存区域的大小，即512字节，这也是是virtio-mmio设备标准配置空间的大小。

将内存区域映射到系统总线：

```c
sysbus_init_mmio(sbd, &proxy->iomem);
```

​	该函数将上面初始化的内存区域映射到系统总线上，使得虚拟机中的设备可以通过内存访问到这个区域，从而与virtio-mmio设备进行通信。

memory_region_init_io：

```c
void memory_region_init_io(MemoryRegion *mr,
                           Object *owner,
                           const MemoryRegionOps *ops,
                           void *opaque,
                           const char *name,
                           uint64_t size)
{
    memory_region_init(mr, owner, name, size);
    mr->ops = ops ? ops : &unassigned_mem_ops;
    mr->opaque = opaque;
    mr->terminates = true;
}
```

​	`memory_region_init_io` 函数就是初始化一个内存区域的具体实现。这个函数为内存区域设置了基本的属性，包括操作函数（ops）、名称（name）和大小（size）。

​	这里在设置操作函数时是将 `MemoryRegion mr->ops` 设置为传入的 `ops` 参数，即初始化的mmio内存回调函数。

​	介绍完 mmio 回调函数的初始化流程之后，那read/write回调是什么时候被调用的呢？回到之前分析 KVM的代码，KVM在完成指令模拟后，会返回到用户空间的Qemu运行，那我们就从Qemu的vCPU线程开始分析，代码如下：

```c
static void *kvm_vcpu_thread_fn(void *arg)
{
	......
    do {
        if (cpu_can_run(cpu)) {
            r = kvm_cpu_exec(cpu);
            if (r == EXCP_DEBUG) {
                cpu_handle_guest_debug(cpu);
            }
        }
        qemu_wait_io_event(cpu);
    } while (!cpu->unplug || cpu_can_run(cpu));
	......
}
```

kvm_cpu_exec：

```c
int kvm_cpu_exec(CPUState *cpu)
{
    struct kvm_run *run = cpu->kvm_run;
    int ret, run_ret;

    DPRINTF("kvm_cpu_exec()\n");
	......
    do {
        MemTxAttrs attrs;
	......
        /* Read cpu->exit_request before KVM_RUN reads run->immediate_exit.
         * Matching barrier in kvm_eat_signals.
         */
        smp_rmb();

        run_ret = kvm_vcpu_ioctl(cpu, KVM_RUN, 0);
		......
        trace_kvm_run_exit(cpu->cpu_index, run->exit_reason);
        switch (run->exit_reason) {
        case KVM_EXIT_IO:
            DPRINTF("handle_io\n");
            /* Called outside BQL */
            kvm_handle_io(run->io.port, attrs,
                          (uint8_t *)run + run->io.data_offset,
                          run->io.direction,
                          run->io.size,
                          run->io.count);
            ret = 0;
            break;
        case KVM_EXIT_MMIO:
            DPRINTF("handle_mmio\n");
            /* Called outside BQL */
            address_space_rw(&address_space_memory,
                             run->mmio.phys_addr, attrs,
                             run->mmio.data,
                             run->mmio.len,
                             run->mmio.is_write);
            ret = 0;
            break;
        default:
            DPRINTF("kvm_arch_handle_exit\n");
            ret = kvm_arch_handle_exit(cpu, run);
            break;
        }
    } while (ret == 0);
	......
    return ret;
}
```

​	在 kvm_cpu_exec 函数中 vCPU 会调用 kvm_vcpu_ioctl 函数陷入到 KVM 中，并在 KVM 中调用 vcpu_enter_guest 函数进入到 guest OS 中运行，当在 guest OS 中运行一些敏感指令时，vCPU就会出现 VMexit 从 guest OS 退出到 KVM中，如果KVM处理不了本次 VMexit 还会再返回到qemu 用户空间，前面分析的操作 mmio 内存空间就是 vCPU 退出的条件之一。

​	 当 vCPU 从 kvm_vcpu_ioctl 返回时，也就是退出 KVM 时，会先通过 run->exit_reason 判断 vCPU 返回的原因进行分情况处理，比如返回的原因是MMIO读写引起的，就会运行到 case KVM_EXIT_MMIO，而 run->exit_reason 参数在 KVM 中通过 complete_emulated_ mmio 函数初始化。

```c
case KVM_EXIT_MMIO:
   DPRINTF("handle_mmio\n");
   /* Called outside BQL */
   address_space_rw(&address_space_memory,
                     run->mmio.phys_addr, attrs,
                     run->mmio.data,
                     run->mmio.len,
                     run->mmio.is_write);
   ret = 0;
   break;
```

在qemu中是调用 address_space_rw 函数进行处理因为读写MMIO引起的 VMexit，如下：

address_space_rw：

```c
MemTxResult address_space_rw(AddressSpace *as, hwaddr addr, MemTxAttrs attrs,
                             void *buf, hwaddr len, bool is_write)
{
    if (is_write) {
        return address_space_write(as, addr, attrs, buf, len);
    } else {
        return address_space_read_full(as, addr, attrs, buf, len);
    }
}
```

这里首先会通过 is_write 区分是 read 还是 write，因为 notify 是写操作，所以这里就以 write 为例：

> address_space_write
>
> ​	-> flatview_write
>
> ​		-> flatview_write_continue

flatview_write_continue :

```c
/* Called within RCU critical section.  */
static MemTxResult flatview_write_continue(FlatView *fv, hwaddr addr,
                                           MemTxAttrs attrs,
                                           const void *ptr,
                                           hwaddr len, hwaddr addr1,
                                           hwaddr l, MemoryRegion *mr)
{
	......
    for (;;) {
		......
        } else if (!memory_access_is_direct(mr, true)) {
            release_lock |= prepare_mmio_access(mr);
            l = memory_access_size(mr, l, addr1);
            /* XXX: could force current_cpu to NULL to avoid
               potential bugs */
            val = ldn_he_p(buf, l);
            result |= memory_region_dispatch_write(mr, addr1, val,
                                                   size_memop(l), attrs);
        } else {
		......
    	mr = flatview_translate(fv, addr, &addr1, &l, true, attrs);
    }
    return result;
}
```

​	通过 flatview_write_continue 函数遍历地址空间，如果当前内存区域是MMIO区域，则调用`memory_region_dispatch_write`函数写入到相应的内存区域，即执行在qemu中初始化好的回调函数。

memory_region_dispatch_write：

 

```c
MemTxResult memory_region_dispatch_write(MemoryRegion *mr,
                                         hwaddr addr,
                                         uint64_t data,
                                         MemOp op,
                                         MemTxAttrs attrs)
{
	......
    if (mr->ops->write) {
        return access_with_adjusted_size(addr, &data, size,
                                         mr->ops->impl.min_access_size,
                                         mr->ops->impl.max_access_size,
                                         memory_region_write_accessor, mr,
                                         attrs);
    } else {
	......
}
```

​	这里会先判断该内存区域是否自定义了写入操作函数 mr->ops->write ，也就是前面初始化的 MemoryRegionOps 结构体，因为已经初始化过，所以会调用 access_with_adjusted_size 函数，如下：

```c
static MemTxResult access_with_adjusted_size(hwaddr addr,
                                      uint64_t *value,
                                      unsigned size,
                                      unsigned access_size_min,
                                      unsigned access_size_max,
                                      MemTxResult (*access_fn)
                                                  (MemoryRegion *mr,
                                                   hwaddr addr,
                                                   uint64_t *value,
                                                   unsigned size,
                                                   signed shift,
                                                   uint64_t mask,
                                                   MemTxAttrs attrs),
                                      MemoryRegion *mr,
                                      MemTxAttrs attrs)
{
	......
    if (memory_region_big_endian(mr)) {
        for (i = 0; i < size; i += access_size) {
            r |= access_fn(mr, addr + i, value, access_size,
                        (size - access_size - i) * 8, access_mask, attrs);
        }
    } else {
        for (i = 0; i < size; i += access_size) {
            r |= access_fn(mr, addr + i, value, access_size, i * 8,
                        access_mask, attrs);
        }
    }
    return r;
}
```

​	函数会根据内存区域是大端（big-endian）还是小端（little-endian）来调整数据访问的偏移量。但最后都是通过函数指针 access_fn  执行访问操作的函数，即函数指针 access_fn 最后调用的是 memory_region_write_accessor 函数，如下：

```c
static MemTxResult memory_region_write_accessor(MemoryRegion *mr,
                                                hwaddr addr,
                                                uint64_t *value,
                                                unsigned size,
                                                signed shift,
                                                uint64_t mask,
                                                MemTxAttrs attrs)
{
	......
    mr->ops->write(mr->opaque, addr, tmp, size);
    return MEMTX_OK;
}
```

​	可以看到，在 memory_region_write_accessor 函数中就调用了在 virtio-mmio.c 文件中通过 MemoryRegionOps 结构体初始化的 write 函数，因为是 notify ，所以会执行到 VIRTIO_MMIO_QUEUE_NOTIFY 分支，即再调用 virtio_queue_notify 函数继续下面的流程，virtio_queue_notify 函数如下：

```c
void virtio_queue_notify(VirtIODevice *vdev, int n)
{
	......
    } else if (vq->handle_output) {
        vq->handle_output(vdev, vq);
	......
    }
}
```

​	在virtio_queue_notify函数中其实是调用的vq->handle_output函数，而该函数是qemu后端设备在初始化时调用 virtio_add_queue 函数进行初始化的，这也就解释了为什么当guest OS调用 notify 函数且满足notify的条件时，host端初始化的handle_output函数就会被调用的原因了。

打开 qemu/hw/block/virtio-blk.c 文件，可以看到 virtio-blk 设备对 vq->handle_output 函数的初始化流程：

```c
static void virtio_blk_handle_output(VirtIODevice *vdev, VirtQueue *vq)
{
	......
    virtio_blk_handle_vq(s, vq);
}
static void virtio_blk_device_realize(DeviceState *dev, Error **errp)
{
	......
    for (i = 0; i < conf->num_queues; i++) {
        virtio_add_queue(vdev, conf->queue_size, virtio_blk_handle_output);
    }
	......
}
```

至此，guest notify host 的流程就分析到这里。
