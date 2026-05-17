#include "mai2touch.h"
#include <string.h>

// 声明外部的 CDC 发送函数
extern uint8_t CDC_Transmit(uint8_t ep, uint8_t* Buf, uint16_t Len);

// 引入 touch_processor.c 中定义的调试开关
extern bool debug_raw_output;

static bool mai2touch_halted = true; // 默认 halt，等待收到 {STAT} 才开始发数据
static uint64_t last_touch_mask = 0; // 记录最后的触摸状态

// 串口命令解析缓冲
static uint8_t cmd_buf[16];
static uint8_t cmd_idx = 0;
static bool in_cmd = false;

void Mai2Touch_Init(void) {
    mai2touch_halted = true;
    last_touch_mask = 0;
    cmd_idx = 0;
    in_cmd = false;
    debug_raw_output = false;
}

// 格式化并发送 34 通道的 Touch 数据
void Mai2Touch_SendTouch(uint64_t mask) {
    last_touch_mask = mask; // 始终更新状态

    if (mai2touch_halted) return; // Halt 状态下不允许发包

    uint8_t packet[9];
    packet[0] = '(';

    // 【核心修复】：原生 mai2touch 协议为 Little Endian
    // 必须从低位 (A1) 开始切片，先发送低位，最后发送高位 (E8)
    packet[1] = (uint8_t)((mask)       & 0x1F); // bits 0~4   : A1, A2, A3, A4, A5
    packet[2] = (uint8_t)((mask >> 5)  & 0x1F); // bits 5~9   : A6, A7, A8, B1, B2
    packet[3] = (uint8_t)((mask >> 10) & 0x1F); // bits 10~14 : B3, B4, B5, B6, B7
    packet[4] = (uint8_t)((mask >> 15) & 0x1F); // bits 15~19 : B8, C1, C2, D1, D2
    packet[5] = (uint8_t)((mask >> 20) & 0x1F); // bits 20~24 : D3, D4, D5, D6, D7
    packet[6] = (uint8_t)((mask >> 25) & 0x1F); // bits 25~29 : D8, E1, E2, E3, E4
    packet[7] = (uint8_t)((mask >> 30) & 0x1F); // bits 30~33 : E5, E6, E7, E8

    packet[8] = ')';

    CDC_Transmit(0, packet, 9);
}

// 处理单独的一条命令（不包含 {}）
static void ProcessCommand(uint8_t* cmd, uint8_t len) {
    if (len == 4) {
        if (strncmp((char*)cmd, "HALT", 4) == 0) {
            mai2touch_halted = true;
            return;
        }
        else if (strncmp((char*)cmd, "STAT", 4) == 0) {
            mai2touch_halted = false;
            // 恢复发送时，立刻补发一次当前状态
            Mai2Touch_SendTouch(last_touch_mask);
            return;
        }
        else if (strncmp((char*)cmd, "RSET", 4) == 0) {
            mai2touch_halted = true;
            return;
        }else if (strncmp((char*)cmd, "DBON", 4) == 0) {
            debug_raw_output = true; // 开启透传
            return;
        }
        else if (strncmp((char*)cmd, "DBOF", 4) == 0) {
            debug_raw_output = false; // 关闭透传
            return;
        }
    }

    // 如果不是系统命令，说明是配置命令 (如 LAr2, LAk. 等)
    // 根据协议直接欺骗回传即可：把 {cmd} 变成 (cmd)
    uint8_t resp[16];
    resp[0] = '(';
    memcpy(&resp[1], cmd, len);
    resp[len + 1] = ')';
    CDC_Transmit(0, resp, len + 2);
}

// 喂入 CDC 接收到的 PC 数据流
void Mai2Touch_ReceiveCDC(uint8_t* data, uint16_t len) {
    for (uint16_t i = 0; i < len; i++) {
        uint8_t c = data[i];

        if (c == '{') {
            in_cmd = true;
            cmd_idx = 0;
        } else if (c == '}') {
            if (in_cmd) {
                ProcessCommand(cmd_buf, cmd_idx);
                in_cmd = false;
            }
        } else if (in_cmd) {
            if (cmd_idx < sizeof(cmd_buf)) {
                cmd_buf[cmd_idx++] = c;
            } else {
                in_cmd = false; // 超长溢出，丢弃错误包
            }
        }
    }
}
