#ifndef CONFIG_PROTOCOL_H
#define CONFIG_PROTOCOL_H

#include <stdint.h>

// 接收来自 PC 上位机的配置数据流 (仅做缓冲，无耗时操作)
void ConfigProtocol_ReceiveCDC(uint8_t* data, uint16_t len);

// 主循环执行的重负载任务 (发送大包与擦写Flash)
void ConfigProtocol_Task(void);

// 当 Flash 为空时的安全保底配置
void Load_Safe_Default_Config(void);

#endif // CONFIG_PROTOCOL_H
