#ifndef DISK_H
#define DISK_H

#include "asm_utils.h"
#include "os_type.h"
#include "stdio.h"

#define SECTOR_SIZE 512

class Disk
{
private:
    Disk();

public:
    // 以扇区为单位写入，每次写入一个扇区
    // 参数 start: 起始逻辑扇区号
    // 参数 buf: 待写入的数据的起始地址
    static void write(int start, void *buf)
    {
        byte *buffer = (byte *)buf;
        int temp = 0;
        int high, low;

        // 请求硬盘写入一个扇区，等待硬盘就绪
        bool flag = waitForDisk(start, 1, 0x30);
        if (!flag)
        {
            return;
        }

        for (int i = 0; i < SECTOR_SIZE; i += 2)
        {
            high = buffer[i+1];
            high = high & 0xff;
            high = high << 8;

            low = buffer[i];
            low = low & 0xff;

            temp = high | low;

            // 每次需要向0x1f0写入一个字（2个字节）            
            asm_outw_port(0x1f0, temp);
            // 硬盘的状态可以从0x1F7读入
            // 最低位是err位
            asm_in_port(0x1f7, (uint8 *)&temp);
            
            if (temp & 0x1)
            {
                asm_in_port(0x1f1, (uint8 *)&temp);
                printf("disk error, error code: %x\n", (temp & 0xff));
                return;
            }
        }

        busyWait();
    }

    // 以扇区为单位读出，每次读取一个扇区
    // 参数 start: 起始逻辑扇区号
    // 参数 buf: 读出的数据写入的起始地址
    static void read(int start, void *buf)
    {
        byte *buffer = (byte *)buf;
        int temp;

        // 请求硬盘读出一个扇区，等待硬盘就绪
        bool flag = waitForDisk(start, 1, 0x20);
        if (!flag)
        {
            return;
        }

        for (int i = 0; i < SECTOR_SIZE; i += 2)
        {
            // 从0x1f0读入一个字
            asm_inw_port(0x1f0, buffer + i);
            // 硬盘的状态可以从0x1F7读入
            // 最低位是err位
            asm_in_port(0x1f7, (uint8 *)&temp);
            if (temp & 0x1)
            {
                asm_in_port(0x1f1, (uint8 *)&temp);
                printf("disk error, error code: %x\n", (temp & 0xff));
                return;
            }
        }

        busyWait();
    }

private:
    // 请求硬盘读取或写入数据，等待硬盘就绪
    // 参数 start: 待读取或写入的起始扇区的地址
    // 参数 amount: 读取或写入的扇区数量
    // 参数 type: 读取或写入的标志，读取=0x20，写入=0x30
    static bool waitForDisk(int start, int amount, int type)
    {
        int temp;

        temp = start;

        // 将要读取的扇区数量写入0x1F2端口
        asm_out_port(0x1f2, amount);

        // LBA地址7~0
        asm_out_port(0x1f3, temp & 0xff);

        // LBA地址15~8
        temp = temp >> 8;
        asm_out_port(0x1f4, temp & 0xff);

        // LBA地址23~16
        temp = temp >> 8;
        asm_out_port(0x1f5, temp & 0xff);

        // LBA地址27~24
        temp = temp >> 8;
        asm_out_port(0x1f6, (temp & 0xf) | 0xe0);

        // 向0x1F7端口写入操作类型，读取=0x20，写入=0x30
        asm_out_port(0x1f7, type);

        asm_in_port(0x1f7, (uint8 *)&temp);
        while ((temp & 0x88) != 0x8)
        {
            // 读入硬盘状态
            if (temp & 0x1)
            {
                // 错误码
                asm_in_port(0x1f1, (uint8 *)&temp);
                printf("disk error, error code: %x\n", (temp & 0xff));
                return false;
            }
            asm_in_port(0x1f7, (uint8 *)&temp);
        }
        return true;
    }

    static void busyWait() {
        uint temp = 0xfffff;
        while(temp) --temp;
    }
};

#endif