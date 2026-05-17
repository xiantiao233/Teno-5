#ifndef MAI2TOUCH_H
#define MAI2TOUCH_H

#include <stdint.h>
#include <stdbool.h>

// 初始化 mai2touch 协议状态
void Mai2Touch_Init(void);

// 接收来自 PC (USB CDC) 的数据流进行命令解析
void Mai2Touch_ReceiveCDC(uint8_t* data, uint16_t len);

// 发送格式化后的触摸数据包给 PC
void Mai2Touch_SendTouch(uint64_t mask);

#endif // MAI2TOUCH_H
