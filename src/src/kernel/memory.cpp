#include "memory.h"
#include "os_constant.h"
#include "stdlib.h"
#include "asm_utils.h"
#include "stdio.h"
#include "program.h"
#include "os_modules.h"
#include "disk.h"

MemoryManager::MemoryManager()
{
    initialize();
}

void MemoryManager::initialize()
{
    this->totalMemory = 0;
    this->totalMemory = getTotalMemory();

    // 预留的内存
    int usedMemory = 256 * PAGE_SIZE + 0x100000;
    if (this->totalMemory < usedMemory)
    {
        printf("memory is too small, halt.\n");
        asm_halt();
    }
    // 剩余的空闲的内存
    int freeMemory = this->totalMemory - usedMemory;

    int freePages = freeMemory / PAGE_SIZE;
    int kernelPages = freePages / 2;
    int userPages = freePages - kernelPages;
    
    int kernelPhysicalStartAddress = usedMemory;
    int userPhysicalStartAddress = usedMemory + kernelPages * PAGE_SIZE;

    int kernelPhysicalBitMapStart = BITMAP_START_ADDRESS;
    int userPhysicalBitMapStart = kernelPhysicalBitMapStart + ceil(kernelPages, 8);
    int kernelVirtualBitMapStart = userPhysicalBitMapStart + ceil(userPages, 8);
    int swapManagerBitMapStart = kernelVirtualBitMapStart + ceil(kernelPages, 8);

    // userpages 4 pages to test
    userPages = 4;
    kernelPhysical.initialize(
        (char *)kernelPhysicalBitMapStart,
        kernelPages,
        kernelPhysicalStartAddress);

    userPhysical.initialize(
        (char *)userPhysicalBitMapStart,
        userPages,
        userPhysicalStartAddress);

    kernelVirtual.initialize(
        (char *)kernelVirtualBitMapStart,
        kernelPages,
        KERNEL_VIRTUAL_START);

    swapResources.initialize((char *)swapManagerBitMapStart, 400);
    beginSector = 200;

    printf("total memory: %d bytes ( %d MB )\n",
           this->totalMemory,
           this->totalMemory / 1024 / 1024);

    printf("kernel pool\n"
           "    start address: 0x%x\n"
           "    total pages: %d ( %d MB )\n"
           "    bitmap start address: 0x%x\n",
           kernelPhysicalStartAddress,
           kernelPages, kernelPages * PAGE_SIZE / 1024 / 1024,
           kernelPhysicalBitMapStart);

    printf("user pool\n"
           "    start address: 0x%x\n"
           "    total pages: %d ( %d MB )\n"
           "    bit map start address: 0x%x\n",
           userPhysicalStartAddress,
           userPages, userPages * PAGE_SIZE / 1024 / 1024,
           userPhysicalBitMapStart);

    printf("kernel virtual pool\n"
           "    start address: 0x%x\n"
           "    total pages: %d  ( %d MB ) \n"
           "    bit map start address: 0x%x\n",
           KERNEL_VIRTUAL_START,
           kernelPages, kernelPages * PAGE_SIZE / 1024 / 1024,
           kernelVirtualBitMapStart);
}

int MemoryManager::allocatePhysicalPages(enum AddressPoolType type, const int count)
{
    int start = -1;

    if (type == AddressPoolType::KERNEL)
    {
        start = kernelPhysical.allocate(count);
    }
    else if (type == AddressPoolType::USER)
    {
        start = userPhysical.allocate(count);
    }

    return (start == -1) ? 0 : start;
}

void MemoryManager::releasePhysicalPages(enum AddressPoolType type, const int paddr, const int count)
{
    if (type == AddressPoolType::KERNEL)
    {
        kernelPhysical.release(paddr, count);
    }
    else if (type == AddressPoolType::USER)
    {

        userPhysical.release(paddr, count);
    }
}

int MemoryManager::getTotalMemory()
{

    if (!this->totalMemory)
    {
        int memory = *((int *)MEMORY_SIZE_ADDRESS);
        // ax寄存器保存的内容
        int low = memory & 0xffff;
        // bx寄存器保存的内容
        int high = (memory >> 16) & 0xffff;

        this->totalMemory = low * 1024 + high * 64 * 1024;
    }

    return this->totalMemory;
}

int MemoryManager::allocatePages(enum AddressPoolType type, const int count)
{
    // 第一步：从虚拟地址池中分配若干虚拟页
    int virtualAddress = allocateVirtualPages(type, count);
    if (!virtualAddress)
    {
        return 0;
    }

    bool flag;
    int physicalPageAddress;
    int vaddress = virtualAddress;

    // 依次为每一个虚拟页指定物理页
    for (int i = 0; i < count; ++i, vaddress += PAGE_SIZE)
    {
        flag = false;
        // 第二步：从物理地址池中分配一个物理页
        physicalPageAddress = allocatePhysicalPages(type, 1);
        if (physicalPageAddress)
        {
            // printf("allocate physical page 0x%x\n", physicalPageAddress);

            // 第三步：为虚拟页建立页目录项和页表项，使虚拟页内的地址经过分页机制变换到物理页内。
            flag = connectPhysicalVirtualPage(vaddress, physicalPageAddress);
        }
        else
        {   
            /*
            if not enough pages 
            */
            flag = false;
            int *pte = (int*)toPTE(vaddress);
            if (type == AddressPoolType::KERNEL)
            {
                int index = (vaddress - kernelVirtual.startAddress) / PAGE_SIZE;
                int i = 0;
                for (i = 0; i < MAX_PAGES && kernelVirtual.lruindex[i] != index; i++);
                kernelVirtual.lruindex[i] = -1;
            }
            else if (type == AddressPoolType::USER)
            { 
                int index = (vaddress - (programManager.running)->userVirtual.startAddress) / PAGE_SIZE;
                int i = 0;
                for (i = 0; i < MAX_PAGES && (programManager.running)->userVirtual.lruindex[i] != index; i++);
                (programManager.running)->userVirtual.lruindex[i] = -1;
            }
            printf("[%x, %x]Not enough phy-pages\n",vaddress, *pte);
        }
    }

    return virtualAddress;
}

int MemoryManager::allocateVirtualPages(enum AddressPoolType type, const int count)
{
    int start = -1;

    if (type == AddressPoolType::KERNEL)
    {
        start = kernelVirtual.allocate(count);
    }
    else if (type == AddressPoolType::USER)
    {
        start = programManager.running->userVirtual.allocate(count);
    }

    return (start == -1) ? 0 : start;
}

bool MemoryManager::connectPhysicalVirtualPage(const int virtualAddress, const int physicalPageAddress)
{
    // 计算虚拟地址对应的页目录项和页表项
    int *pde = (int *)toPDE(virtualAddress);
    int *pte = (int *)toPTE(virtualAddress);

    // 页目录项无对应的页表，先分配一个页表
    if (!(*pde & 0x00000001))
    {
        // 从内核物理地址空间中分配一个页表
        int page = allocatePhysicalPages(AddressPoolType::KERNEL, 1);
        if (!page)
            return false;

        // 使页目录项指向页表
        *pde = page | 0x7;
        // 初始化页表
        char *pagePtr = (char *)(((int)pte) & 0xfffff000);
        memset(pagePtr, 0, PAGE_SIZE);
    }

    // 使页表项指向物理页
    *pte = physicalPageAddress | 0x7;
    printf("Connecting VP: 0x%x with PP: 0x%x PTE%x\n",virtualAddress, physicalPageAddress, *pte);
    return true;
}

int MemoryManager::toPDE(const int virtualAddress)
{
    return (0xfffff000 + (((virtualAddress & 0xffc00000) >> 22) * 4));
}

int MemoryManager::toPTE(const int virtualAddress)
{
    return (0xffc00000 + ((virtualAddress & 0xffc00000) >> 10) + (((virtualAddress & 0x003ff000) >> 12) * 4));
}

void MemoryManager::releasePages(enum AddressPoolType type, const int virtualAddress, const int count)
{
    int vaddr = virtualAddress;
    int *pte;
    for (int i = 0; i < count; ++i, vaddr += PAGE_SIZE)
    {
        // 第一步，对每一个虚拟页，释放为其分配的物理页
        releasePhysicalPages(type, vaddr2paddr(vaddr), 1);

        // 设置页表项为不存在，防止释放后被再次使用
        pte = (int *)toPTE(vaddr);
        *pte = 0;
        // 刷新TLB
        asm_update_tlb();
    }

    // 第二步，释放虚拟页
    releaseVirtualPages(type, virtualAddress, count);
}

int MemoryManager::vaddr2paddr(int vaddr)
{
    int *pte = (int *)toPTE(vaddr);
    int page = (*pte) & 0xfffff000;
    int offset = vaddr & 0xfff;
    return (page + offset);
}

void MemoryManager::releaseVirtualPages(enum AddressPoolType type, const int vaddr, const int count)
{
    if (type == AddressPoolType::KERNEL)
    {
        kernelVirtual.release(vaddr, count);
    }
    else if (type == AddressPoolType::USER)
    {
        programManager.running->userVirtual.release(vaddr, count);
    }
}

int MemoryManager::swapOut(uint32 vaddr, int mod)
{
    enum AddressPoolType type = mod == 1 ? AddressPoolType::KERNEL : AddressPoolType:: USER;
    int *pte = (int *)toPTE(vaddr);
    int index = swapResources.allocate(8);//one page equal eight sections
    if (index == -1)
    {
        printf("Swapping Out Failed ,due to disk swap space is not enough\n");
        return -1;
    }
    if (mod == 1){
        printf("[Mod Kernel]Swapping out Page: 0x%x to Sector %d\n", vaddr, index + beginSector);
    }
    else{
        printf("[Mod User]Swapping out Page: 0x%x to Sector %d\n", vaddr, index + beginSector);
    }
    for (int i = 0; i < 8; i++)
    {
        char *ptr = (char *)vaddr + i * 512;
        Disk::write(index + i + beginSector, (void *)ptr);
    }
    releasePhysicalPages(type, vaddr2paddr(vaddr), 1);
    if (type == AddressPoolType::KERNEL)
    {
        int index = (vaddr - kernelVirtual.startAddress) / PAGE_SIZE;
        int i = 0;
        for (i = 0; i < MAX_PAGES && kernelVirtual.lruindex[i] != index; i++);
        kernelVirtual.lruindex[i] = -1;
    }
    else if (type == AddressPoolType::USER)
    { 
        int index = (vaddr - (programManager.running)->userVirtual.startAddress) / PAGE_SIZE;
        int i = 0;
        for (i = 0; i < MAX_PAGES && (programManager.running)->userVirtual.lruindex[i] != index; i++);
        printf("Realease index %d page\n",index);
        (programManager.running)->userVirtual.lruindex[i] = -1;
    }
    //store pte and significance bit
    *pte = (index << 20) + 2;
    // 刷新TLB
    asm_update_tlb();
    return 0;
}
int MemoryManager::swapIn(uint32 vaddr, int mod)
{
    enum AddressPoolType type = mod == 1 ? AddressPoolType::KERNEL : AddressPoolType:: USER;
    int *pte = (int *)toPTE(vaddr);
    int index = (*pte) >> 20;
    printf("[Mod %d]Swapping in Page: 0x%x from Sector %d\n",!mod, vaddr, index + beginSector);
    int physicalPageAddress = allocatePhysicalPages(type, 1);
    // int physicalPageAddress = allocatePhysicalPages(AddressPoolType::USER, 1);
    if (physicalPageAddress == 0)
    {
        /*
        not enough physical pages
        find one page swapout
        */
        int swapOutPage = 0;
        if(type == 1)
        {
            // kernel
            swapOutPage = memoryManager.kernelVirtual.Out();
        }
        else
        {
            // user 
            swapOutPage = programManager.running->userVirtual.Out();
        }
        swapOut(swapOutPage, type);
        physicalPageAddress = allocatePhysicalPages(type, 1);
    }
    connectPhysicalVirtualPage((int)vaddr, physicalPageAddress);
    for (int i = 0; i < 8; i++)
    {
        char *ptr = (char *)vaddr + i * 512;
        Disk::read(index + i + beginSector, (void *)ptr);
    }
    swapResources.release(index, 8);
    if (type == AddressPoolType::KERNEL)
    {
        int index = (vaddr - kernelVirtual.startAddress) / PAGE_SIZE;
        int i = 0;
        for (i = 0; i < MAX_PAGES && kernelVirtual.lruindex[i] != -1; i++);
        kernelVirtual.lruindex[i] = index;
    }
    else if (type == AddressPoolType::USER)
    { 
        int index = (vaddr - (programManager.running)->userVirtual.startAddress) / PAGE_SIZE;
        int i = 0;
        for (i = 0; i < MAX_PAGES && (programManager.running)->userVirtual.lruindex[i] != -1; i++);
        (programManager.running)->userVirtual.lruindex[i] = index;
    }
    // 刷新TLB
    asm_update_tlb();
}