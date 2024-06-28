#include "address_pool.h"
#include "os_constant.h"
#include "stdio.h"
#include "asm_utils.h"

AddressPool::AddressPool()
{
}

// 设置地址池BitMap
void AddressPool::initialize(char *bitmap, const int length, const int startAddress)
{
    resources.initialize(bitmap, length);
    this->startAddress = startAddress;
    // 清空时间戳
    for(int i = 0; i < MAX_PAGES; ++i)
    {
        lrucnt[i] = 0;
        lruindex[i] = -1;
    }
    // 初始化时钟
    clock = 0;
}

// 从地址池中分配count个连续页
int AddressPool::allocate(const int count)
{
    int start = resources.allocate(count);
    if(start == -1)
        return -1;
    // 更新时钟
    for(int i = 0; i < count; ++i)
    {
        // if((unsigned int)((i + start) * PAGE_SIZE + startAddress) >= 0xffc00000 || (unsigned int)((i + start) * PAGE_SIZE + startAddress) <= 0x8048000)
        //     break;
        int j = 0;
        while (lruindex[j] != -1 && j < MAX_PAGES)
        {
            ++j;
        }
        if(j == MAX_PAGES)
        {
            printf("Exeed the memory queue. halt ...\n");
            asm_halt();
            return -1;
        }
        lruindex[j] = i + start;
        lrucnt[j] = clock;
    }
    return start * PAGE_SIZE + startAddress;
}

// 释放若干页的空间
void AddressPool::release(const int address, const int amount)
{
    resources.release((address - startAddress) / PAGE_SIZE, amount);
    int start = (address - startAddress) / PAGE_SIZE;
    // TODO
    for(int i = 0; i < amount; ++i)
    {
        int j = 0;
        while (lruindex[j] != i + start && j < MAX_PAGES)
        {
            ++j;
        }
        lruindex[j] = -1;
        lrucnt[j] = 0;
    }
}

void AddressPool::LRU()
{
    ++clock;
    for (int i = 0; i < MAX_PAGES; ++i)
    {
        if(lruindex[i] == -1)
        // 如果没有被分配，则跳过
            continue;
        int vaddr = startAddress + lruindex[i] * PAGE_SIZE;
        unsigned int* pte = (unsigned int*)(0xffc00000 + ((vaddr & 0xffc00000) >> 10) + (((vaddr & 0x003ff000) >> 12) * 4));
        if((*pte) & (1<<5))
        {
            // printf_error("Updating index %d\n", i);
            lrucnt[i] = clock;
            // 写入位和脏位置零
            (*pte) = (*pte) & (~( 3 << 5));
        }
    }
}

int AddressPool::Out()
{
    int Min = clock, index = 0;
    printf("lruCNT Array: ");
    for (int i = 0; i < MAX_PAGES; ++i)
    {
        if(lruindex[i] == -1)
        // 如果没有被分配，则跳过
            continue;
        printf("[%d, %d] ",lruindex[i], lrucnt[i]);
        if(lrucnt[i] < Min)
        {
            Min = lrucnt[i];
            index = lruindex[i];
        }
    }
    printf("\n[Swap out] index  = %d, vaddr = 0x%x\n",index, startAddress + index * PAGE_SIZE);
    return startAddress + index * PAGE_SIZE;

}