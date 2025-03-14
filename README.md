Allows accessing the physical address of a continguous DMA BUF object while
the returned file handle is kept open. Used on info-beamer OS to share a
dma_buf with the VPU. Usage:

```
#define IOCTL_GET_PHYS_ADDR _IOR('d', 1, uint64_t)

struct phys_addr_data {
    int32_t fd;
    uint64_t phys_addr;
};

static int driver_import(int dma_fd, uint32_t offset, uint32_t *dma_addr) {
    struct phys_addr_data data = {.fd = dma_fd};
    if (ioctl(dma_buf_phys_fd, IOCTL_GET_PHYS_ADDR, &data) != 0)
        die("cannot get physical addr");
    *dma_addr = VPU_UNCACHED((uint32_t)((data.phys_addr + offset) & 0xffffffff));
    return data.fd;
}
```
