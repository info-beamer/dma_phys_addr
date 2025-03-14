#include <linux/module.h>
#include <linux/fs.h>
#include <linux/dma-buf.h>
#include <linux/uaccess.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/anon_inodes.h>

#define DEVICE_NAME "dma_buf_phys"
#define CLASS_NAME "dma_buf_class"
#define IOCTL_GET_PHYS_ADDR _IOR('d', 1, __u64)

static int major;
static struct class *dma_buf_class;
static struct device *dma_buf_device;

struct phys_addr_data {
    __s32 fd;
    __u64 phys_addr;
};

struct dma_buf_fd {
    struct dma_buf *dma_buf;
    struct dma_buf_attachment *attach;
    struct sg_table *sgt;
};

static void dma_buf_fd_release(struct dma_buf_fd *dma_buf_fd) {
    struct dma_buf *dma_buf = dma_buf_fd->dma_buf;
    struct dma_buf_attachment *attach = dma_buf_fd->attach;
    struct sg_table *sgt = dma_buf_fd->sgt;

    dma_sync_sg_for_device(dma_buf_device, sgt->sgl, sgt->nents, DMA_BIDIRECTIONAL);
    dma_buf_unmap_attachment(attach, sgt, DMA_BIDIRECTIONAL);
    dma_buf_detach(dma_buf, attach);
    dma_buf_put(dma_buf);
    kfree(dma_buf_fd);
}

static int dma_buf_release(struct inode *inode, struct file *file) {
    struct dma_buf_fd *dma_buf_fd = file->private_data;
    dma_buf_fd_release(dma_buf_fd);
    return 0;
}

static const struct file_operations dma_buf_fops = {
    .release = dma_buf_release,
};

static long dma_buf_ioctl(struct file *file, unsigned int cmd, unsigned long arg) {
    struct phys_addr_data user_data;
    struct dma_buf_fd *dma_buf_fd = NULL;
    int fd = -1;
    int ret = 0;

    if (cmd != IOCTL_GET_PHYS_ADDR)
        return -EINVAL;

    if (copy_from_user(&user_data, (void __user *)arg, sizeof(user_data)))
        return -EFAULT;

    dma_buf_fd = kzalloc(sizeof(*dma_buf_fd), GFP_KERNEL);
    if (!dma_buf_fd)
        return -ENOMEM;

    dma_buf_fd->dma_buf = dma_buf_get(user_data.fd);
    if (!dma_buf_fd->dma_buf) {
        ret = -EINVAL;
        goto error;
    }

    dma_buf_fd->attach = dma_buf_attach(dma_buf_fd->dma_buf, dma_buf_device);
    if (IS_ERR(dma_buf_fd->attach)) {
        ret = PTR_ERR(dma_buf_fd->attach);
        goto error;
    }

    dma_buf_fd->sgt = dma_buf_map_attachment(dma_buf_fd->attach, DMA_BIDIRECTIONAL);
    if (IS_ERR(dma_buf_fd->sgt)) {
        ret = PTR_ERR(dma_buf_fd->sgt);
        goto error;
    }

    if (dma_buf_fd->sgt->nents != 1) {
        ret = -ENOMEM;
        goto error;
    }

    dma_sync_sg_for_cpu(dma_buf_device, dma_buf_fd->sgt->sgl, dma_buf_fd->sgt->nents, DMA_BIDIRECTIONAL);

    fd = anon_inode_getfd("dma_buf_fd", &dma_buf_fops, dma_buf_fd, O_CLOEXEC);
    if (fd < 0) {
        ret = fd;
        goto error;
    }

    user_data.fd = fd;
    user_data.phys_addr = sg_dma_address(dma_buf_fd->sgt->sgl);

    if (copy_to_user((void __user *)arg, &user_data, sizeof(user_data)))
        ret = -EFAULT;

    return 0;
error:
    dma_buf_fd_release(dma_buf_fd);
    return ret;
}

static struct file_operations fops = {
    .owner = THIS_MODULE,
    .unlocked_ioctl = dma_buf_ioctl,
    .compat_ioctl = dma_buf_ioctl, // For 32-bit user-space compatibility
};

static u64 dma_mask = DMA_BIT_MASK(32);

static int __init dma_buf_init(void) {
    major = register_chrdev(0, DEVICE_NAME, &fops);
    if (major < 0)
        return major;

    dma_buf_class = class_create(CLASS_NAME);
    if (IS_ERR(dma_buf_class)) {
        unregister_chrdev(major, DEVICE_NAME);
        return PTR_ERR(dma_buf_class);
    }

    dma_buf_device = device_create(dma_buf_class, NULL, MKDEV(major, 0), NULL, DEVICE_NAME);
    if (IS_ERR(dma_buf_device)) {
        class_destroy(dma_buf_class);
        unregister_chrdev(major, DEVICE_NAME);
        return PTR_ERR(dma_buf_device);
    }

    dma_buf_device->dma_mask = &dma_mask;

    int err = dma_set_mask_and_coherent(dma_buf_device, DMA_BIT_MASK(32));
    if (err) {
        dev_err(dma_buf_device, "dma_set_mask_and_coherent failed: %d\n", err);
        return err;
    }

    dma_buf_device->dma_parms = devm_kzalloc(dma_buf_device,
                                       sizeof(*dma_buf_device->dma_parms),
                                       GFP_KERNEL);
    dma_set_max_seg_size(dma_buf_device, 0x3FFFFFFF);
    pr_info("dma_buf_phys driver loaded\n");
    return 0;
}

static void __exit dma_buf_exit(void) {
    device_destroy(dma_buf_class, MKDEV(major, 0));
    class_destroy(dma_buf_class);
    unregister_chrdev(major, DEVICE_NAME);
    pr_info("dma_buf_phys driver unloaded\n");
}

module_init(dma_buf_init);
module_exit(dma_buf_exit);

MODULE_IMPORT_NS(DMA_BUF);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Florian Wesch <fw@info-beamer.com>");
MODULE_DESCRIPTION("A Linux kernel driver to get physical address of dma_buf");
