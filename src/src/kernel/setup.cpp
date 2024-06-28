#include "asm_utils.h"
#include "interrupt.h"
#include "stdio.h"
#include "program.h"
#include "thread.h"
#include "sync.h"
#include "memory.h"
#include "syscall.h"
#include "tss.h"
#include "disk.h"
#include "stdlib.h"

// 屏幕IO处理器
STDIO stdio;
// 中断管理器
InterruptManager interruptManager;
// 程序管理器
ProgramManager programManager;
// 内存管理器
MemoryManager memoryManager;
// 系统调用
SystemService systemService;
// Task State Segment
TSS tss;

int syscall_0(int first, int second, int third, int forth, int fifth)
{
    printf("systerm call 0: %d, %d, %d, %d, %d\n",
           first, second, third, forth, fifth);
    return first + second + third + forth + fifth;
}

void first_process()
{
    printf("Testing my process\n");
    enum AddressPoolType type = AddressPoolType::USER;
    char* buffer[4];
    for (int i = 0; i < 4; i++)
    {
        buffer[i] = (char*)memoryManager.allocatePages(USER, 1);
    }
    
    printf("%x\n",*(int*)memoryManager.toPTE((int)&buffer[3][0]));
    printf("Reading unallocated page: %d\n",buffer[3][0]);

    // printf("Testing my swap\n");
    // enum AddressPoolType type = AddressPoolType::USER;
    // char buffer[512] = "hello world";
    // char newbuffer[512];
    // Disk::write(200,buffer);
    // Disk::read(200,newbuffer);
    // bool flag = 1;
    // for(int i = 0;i < 512;i++){
    //     if(buffer[i] != newbuffer[i]){
    //         flag = 0;
    //         break;
    //     }
    // }
    // if(flag){
    //     printf("%s,success!!\n",buffer);
    // }
}

void second_thread(void *arg)
{
    printf("thread exit\n");
    // exit(0);
}

void first_thread(void *arg)
{
    printf("start process\n");
    programManager.executeProcess((const char *)first_process, 1);
    printf("Kernel Process is halting...\n");
    asm_halt();
}

extern "C" void setup_kernel()
{

    // 中断管理器
    interruptManager.initialize();
    interruptManager.enableTimeInterrupt();
    interruptManager.setTimeInterrupt((void *)asm_time_interrupt_handler);
    // 设置页错误中断的中断描述符
    interruptManager.setInterruptDescriptor(14, (uint)asm_pageFault_handler, 0);
    // 输出管理器
    stdio.initialize();

    // 进程/线程管理器
    programManager.initialize();

    // 初始化系统调用
    systemService.initialize();
    // 设置0号系统调用
    systemService.setSystemCall(0, (int)syscall_0);
    // 设置1号系统调用
    systemService.setSystemCall(1, (int)syscall_write);
    // 设置2号系统调用
    systemService.setSystemCall(2, (int)syscall_fork);
    // 设置3号系统调用
    systemService.setSystemCall(3, (int)syscall_exit);
    // 设置4号系统调用
    systemService.setSystemCall(4, (int)syscall_wait);

    // 内存管理器
    memoryManager.initialize();

    // 创建第一个线程
    int pid = programManager.executeThread(first_thread, nullptr, "first thread", 1);
    if (pid == -1)
    {
        printf("can not execute thread\n");
        asm_halt();
    }

    ListItem *item = programManager.readyPrograms.front();
    PCB *firstThread = ListItem2PCB(item, tagInGeneralList);
    firstThread->status = ProgramStatus::RUNNING;
    programManager.readyPrograms.pop_front();
    programManager.running = firstThread;
    asm_switch_thread(0, firstThread);

    asm_halt();
}
