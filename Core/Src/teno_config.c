#include "teno_config.h"
#include "stm32g4xx_hal.h"
#include <string.h>

// STM32G431RBT6: 128KB Flash, 64 Pages (0-63), 2KB/Page
#define FLASH_CONFIG_PAGE  63
#define FLASH_CONFIG_ADDR  (0x08000000 + FLASH_CONFIG_PAGE * 2048)
#define TENO_MAGIC_NUM     0x4F4E4554 // "TENO" ASCII

TenoConfig_t g_TenoConfig;

// 外部声明你自定义的 CDC 发送函数
extern uint8_t CDC_Transmit(uint8_t ch, uint8_t *Buf, uint16_t Len);

volatile uint8_t g_ConfigReplyCmd = 0; // 0=不回复, 1=请求全量配置, 3=需发ACK, 4=需发NACK

// 出厂默认通道数据
static const ChannelConfig_t DEFAULT_CHANNELS[34] = {
    {'A', 1ULL << 6,  30, 1200, 600}, {'C', 1ULL << 17, 30, 0, 0},
    {'E', 1ULL << 32, 23, 0, 0},      {'D', 1ULL << 24, 22, 0, 0},
    {'B', 1ULL << 13, 46, 0, 0},      {'A', 1ULL << 5,  25, 1200, 600},
    {'E', 1ULL << 31, 28, 0, 0},      {'D', 1ULL << 23, 23, 0, 0},
    {'B', 1ULL << 12, 35, 0, 0},      {'A', 1ULL << 4,  32, 1200, 600},
    {'E', 1ULL << 30, 30, 0, 0},      {'D', 1ULL << 22, 38, 0, 0},
    {'B', 1ULL << 11, 39, 0, 0},      {'A', 1ULL << 3,  47, 1200, 600},
    {'E', 1ULL << 29, 30, 0, 0},      {'D', 1ULL << 21, 60, 0, 0},
    {'B', 1ULL << 10, 47, 0, 0},      {'A', 1ULL << 2,  72, 1200, 600},
    {'C', 1ULL << 16, 25, 0, 0},      {'E', 1ULL << 28, 46, 0, 0},
    {'D', 1ULL << 20, 60, 0, 0},      {'B', 1ULL << 9,  51, 0, 0},
    {'A', 1ULL << 1,  51, 1200, 600}, {'E', 1ULL << 27, 35, 0, 0},
    {'D', 1ULL << 19, 42, 0, 0},      {'B', 1ULL << 8,  56, 0, 0},
    {'E', 1ULL << 26, 24, 0, 0},      {'A', 1ULL << 0,  30, 1200, 600},
    {'D', 1ULL << 18, 37, 0, 0},      {'B', 1ULL << 15, 50, 0, 0},
    {'A', 1ULL << 7,  34, 1200, 600}, {'E', 1ULL << 33, 17, 0, 0},
    {'D', 1ULL << 25, 24, 0, 0},      {'B', 1ULL << 14, 42, 0, 0}
};

void Config_Init(void) {
    TenoConfig_t* flash_cfg = (TenoConfig_t*)FLASH_CONFIG_ADDR;

    // 若Flash中的魔数匹配，说明存有配置
    if (flash_cfg->magic == TENO_MAGIC_NUM) {
        memcpy(&g_TenoConfig, flash_cfg, sizeof(TenoConfig_t));
    } else {
        // 第一次烧录或数据损坏，加载默认设定 (你以前的 #define 参数)
        memset(&g_TenoConfig, 0, sizeof(TenoConfig_t));
        g_TenoConfig.magic = TENO_MAGIC_NUM;
        g_TenoConfig.version = 0x0100;

        g_TenoConfig.enable_fixed_trigger_mode = 1;
        g_TenoConfig.fixed_trigger_default_a = 49600;
        g_TenoConfig.fixed_trigger_default_b = 47500;
        g_TenoConfig.fixed_trigger_default_c = 47000;
        g_TenoConfig.fixed_trigger_default_d = 49500;
        g_TenoConfig.fixed_trigger_default_e = 49500;

        g_TenoConfig.variance_thresh_bcde = 600;
        g_TenoConfig.variance_thresh_bcde_down = 300;
        g_TenoConfig.var_thresh_b = 600;
        g_TenoConfig.var_thresh_c = 800;
        g_TenoConfig.var_thresh_d = 800;
        g_TenoConfig.var_thresh_e = 600;
        g_TenoConfig.variance_thresh_a_default = 1200;
        g_TenoConfig.var001_default = 600;

        g_TenoConfig.area_a_release_drop_thresh = 1500;
        g_TenoConfig.area_a_press_rise_thresh = 1000;
        g_TenoConfig.area_a_press_break_thresh = 800;
        g_TenoConfig.area_a_fast_slide_fps_limit = -1;
        g_TenoConfig.rea_a_down_tr_up = 0.9f;
        g_TenoConfig.rea_a_down_tr_down = 0.2f;

        memcpy(g_TenoConfig.channels, DEFAULT_CHANNELS, sizeof(DEFAULT_CHANNELS));
    }
}

static uint8_t Config_SaveToFlash(void) {
    HAL_FLASH_Unlock();

    // 擦除第 63 页
    FLASH_EraseInitTypeDef erase_init;
    erase_init.TypeErase = FLASH_TYPEERASE_PAGES;
    erase_init.Banks     = FLASH_BANK_1;
    erase_init.Page      = FLASH_CONFIG_PAGE;
    erase_init.NbPages   = 1;
    uint32_t pageError   = 0;

    if (HAL_FLASHEx_Erase(&erase_init, &pageError) != HAL_OK) {
        HAL_FLASH_Lock();
        return 0;
    }

    // G4 必须按 64-bit 写入
    int num_double_words = (sizeof(TenoConfig_t) + 7) / 8;
    uint64_t* data_ptr = (uint64_t*)&g_TenoConfig;

    for(int i = 0; i < num_double_words; i++) {
        uint64_t write_val = 0xFFFFFFFFFFFFFFFF;
        int remaining = sizeof(TenoConfig_t) - (i * 8);
        if (remaining > 8) remaining = 8;
        memcpy(&write_val, (uint8_t*)&g_TenoConfig + i * 8, remaining);

        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, FLASH_CONFIG_ADDR + i * 8, write_val) != HAL_OK) {
            HAL_FLASH_Lock();
            return 0;
        }
    }

    HAL_FLASH_Lock();
    return 1;
}

// ---------------- 协议解析状态机 ----------------
static uint16_t CalcCRC16(const uint8_t *data, uint16_t len) {
    uint16_t crc = 0xFFFF;
    for (uint16_t pos = 0; pos < len; pos++) {
        crc ^= (uint16_t)data[pos];
        for (int i = 8; i != 0; i--) {
            if ((crc & 0x0001) != 0) { crc >>= 1; crc ^= 0xA001; }
            else { crc >>= 1; }
        }
    }
    return crc;
}

void Config_SendToPC(void) {
    uint16_t len = sizeof(TenoConfig_t);
    uint16_t crc = CalcCRC16((uint8_t*)&g_TenoConfig, len);

    static uint8_t tx_buf[sizeof(TenoConfig_t) + 9];
    tx_buf[0] = 0x5A; tx_buf[1] = 0xA5;       // Header
    tx_buf[2] = 0x01;                         // Cmd (0x01 Read Reply)
    tx_buf[3] = len & 0xFF; tx_buf[4] = (len >> 8) & 0xFF; // Len
    memcpy(&tx_buf[5], &g_TenoConfig, len);   // Payload
    tx_buf[5+len] = crc & 0xFF; tx_buf[6+len] = (crc >> 8) & 0xFF; // CRC
    tx_buf[7+len] = 0xED; tx_buf[8+len] = 0xDE; // Tail

    CDC_Transmit(0, tx_buf, sizeof(tx_buf));
}

void Config_ProcessByte(uint8_t byte) {
    typedef enum { S_H1, S_H2, S_CMD, S_L_L, S_L_H, S_PAY, S_C_L, S_C_H, S_T1, S_T2 } RxState;
    static RxState state = S_H1;
    static uint8_t cmd;
    static uint16_t len, idx, crc;
    static uint8_t payload[sizeof(TenoConfig_t) + 16];

    switch (state) {
        case S_H1:  if (byte == 0x5A) state = S_H2; break;
        case S_H2:  state = (byte == 0xA5) ? S_CMD : S_H1; break;
        case S_CMD: cmd = byte; state = S_L_L; break;
        case S_L_L: len = byte; state = S_L_H; break;
        case S_L_H:
            len |= (byte << 8); idx = 0;
            if (len > sizeof(payload)) state = S_H1;
            else state = (len > 0) ? S_PAY : S_C_L;
            break;
        case S_PAY: payload[idx++] = byte; if (idx >= len) state = S_C_L; break;
        case S_C_L: crc = byte; state = S_C_H; break;
        case S_C_H: crc |= (byte << 8); state = S_T1; break;
        case S_T1:  state = (byte == 0xED) ? S_T2 : S_H1; break;
        case S_T2:
                    if (byte == 0xDE) {
                        if (CalcCRC16(payload, len) == crc) {
                            if (cmd == 0x01) {
                                // 收到读取请求，立Flag交给主循环处理
                                g_ConfigReplyCmd = 1;
                            } else if (cmd == 0x02 && len == sizeof(TenoConfig_t)) {
                                // 收到写入请求
                                memcpy(&g_TenoConfig, payload, sizeof(TenoConfig_t));
                                if (Config_SaveToFlash()) {
                                    g_ConfigReplyCmd = 3; // 标记需要发送成功 ACK
                                } else {
                                    g_ConfigReplyCmd = 4; // 标记需要发送失败 NACK
                                }
                            }
                        }
                    }
                    state = S_H1; break;
    }
}
