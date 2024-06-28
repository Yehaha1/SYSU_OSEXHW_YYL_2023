#ifndef ADDRESS_POOL_H
#define ADDRESS_POOL_H

#include "bitmap.h"
#include "os_type.h"
const int MAX_PAGES = 200;

class AddressPool
{
public:
    BitMap resources;
    int startAddress;
    // lru队列
    int lrucnt[MAX_PAGES];
    // 用于记录对应页的索引
    int lruindex[MAX_PAGES];
    // 局部时钟
    int clock;
public:
    AddressPool();
    // 初始化地址池
    void initialize(char *bitmap, const int length, const int startAddress);
    // 从地址池中分配count个连续页，成功则返回第一个页的地址，失败则返回-1
    int allocate(const int count);
    // 释放若干页的空间
    void release(const int address, const int amount);
    // 更新LRU数组
    void LRU();
    int Out();
};

#endif