#include "config_protocol.h"
#include "touch_processor.h"
#include "teno_config.h"
#include "stm32g4xx_hal.h"
#include <string.h>

extern uint8_t CDC_Transmit(uint8_t ep, uint8_t* Buf, uint16_t Len);
extern TenoConfig_t g_TenoConfig;

#define CONFIG_FLASH_ADDR 0x0801F800

static uint16_t calc_crc16(uint8_t* data, uint32_t len) {
    uint16_t crc = 0xFFFF;
    for (uint32_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 0x0001) crc = (crc >> 1) ^ 0xA001;
            else crc >>= 1;
        }
    }
    return crc;
}

static uint8_t rx_buf[1024];
static uint16_t rx_idx = 0;
static uint8_t tx_buf[810];

// 异步任务标志位
volatile uint8_t pending_cmd = 0; // 1=请求发送配置, 2=请求写入Flash
volatile uint16_t pending_write_len = 0;

void ConfigProtocol_ReceiveCDC(uint8_t* data, uint16_t len) {
    for(uint16_t i=0; i<len; i++) {
        if(rx_idx < 1024) rx_buf[rx_idx++] = data[i];
    }

    if(rx_idx >= 9) {
        if(rx_buf[0] == 0x5A && rx_buf[1] == 0xA5) {
            uint8_t cmd = rx_buf[2];
            if (cmd == 0x01 && rx_idx >= 9) {
                if (rx_buf[7] == 0xED && rx_buf[8] == 0xDE) {
                    pending_cmd = 1; // 通知主循环异步发送
                }
            }
            else if (cmd == 0x02) {
                uint16_t p_len = rx_buf[3] | (rx_buf[4] << 8);
                if (rx_idx >= 5 + p_len + 4) {
                    pending_write_len = p_len;
                    pending_cmd = 2; // 通知主循环异步写入
                }
            }
        } else {
            // 包头错误，移位寻找
            for(int i=1; i<rx_idx; i++) rx_buf[i-1] = rx_buf[i];
            rx_idx--;
        }
    }
}

void ConfigProtocol_Task(void) {
    if (pending_cmd == 1) {
        pending_cmd = 0; // 清除标志
        tx_buf[0] = 0x5A; tx_buf[1] = 0xA5; tx_buf[2] = 0x01;
        uint16_t p_len = sizeof(TenoConfig_t);
        tx_buf[3] = p_len & 0xFF; tx_buf[4] = (p_len >> 8) & 0xFF;

        memcpy(&tx_buf[5], &g_TenoConfig, p_len);
        uint16_t crc = calc_crc16(&tx_buf[5], p_len);
        tx_buf[5+p_len] = crc & 0xFF; tx_buf[6+p_len] = (crc >> 8) & 0xFF;
        tx_buf[7+p_len] = 0xED; tx_buf[8+p_len] = 0xDE;

        CDC_Transmit(0, tx_buf, 5 + p_len + 4);
        rx_idx = 0;
    }
    else if (pending_cmd == 2) {
        pending_cmd = 0; // 清除标志
        uint16_t p_len = pending_write_len;

        if (p_len == sizeof(TenoConfig_t)) {
            uint16_t rx_crc = rx_buf[5+p_len] | (rx_buf[6+p_len] << 8);
            uint16_t calc_crc = calc_crc16(&rx_buf[5], p_len);

            if (rx_crc == calc_crc && rx_buf[7+p_len] == 0xED && rx_buf[8+p_len] == 0xDE) {
                // 安全地在主线程擦除 Flash，不会卡死 USB
                HAL_FLASH_Unlock();
                __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_ALL_ERRORS);

                FLASH_EraseInitTypeDef EraseInitStruct;
                uint32_t PageError = 0;
                EraseInitStruct.TypeErase = FLASH_TYPEERASE_PAGES;
                EraseInitStruct.Banks = FLASH_BANK_1;
                EraseInitStruct.Page = 63;
                EraseInitStruct.NbPages = 1;
                HAL_FLASHEx_Erase(&EraseInitStruct, &PageError);

                uint32_t Address = CONFIG_FLASH_ADDR;
                uint64_t data64 = 0;
                uint8_t* payload = &rx_buf[5];

                for (int i = 0; i < p_len; i += 8) {
                    data64 = 0;
                    memcpy(&data64, &payload[i], (p_len - i < 8) ? (p_len - i) : 8);
                    HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, Address, data64);
                    Address += 8;
                }
                HAL_FLASH_Lock();

                uint8_t ack[9] = {0x5A, 0xA5, 0x03, 0x00, 0x00, 0x00, 0x00, 0xED, 0xDE};
                CDC_Transmit(0, ack, 9);

                HAL_Delay(50);
                NVIC_SystemReset(); // 配置落盘，重启生效！
            }
        }
        rx_idx = 0;
    }
}

void Load_Safe_Default_Config(void) {
    memset(&g_TenoConfig, 0, sizeof(TenoConfig_t));
    g_TenoConfig.magic = 0x54454E4F;
    g_TenoConfig.enable_fixed_trigger_mode = 1;
    g_TenoConfig.fixed_trigger_default_a = 50300;
    g_TenoConfig.fixed_trigger_default_b = 48000;
    g_TenoConfig.fixed_trigger_default_c = 47000;
    g_TenoConfig.fixed_trigger_default_d = 50000;
    g_TenoConfig.fixed_trigger_default_e = 50000;
    g_TenoConfig.variance_thresh_a_default = 6000;

    // 【核心修复】赋予真实的防断连判定值，防止微小噪音引发疯狂闪烁
    g_TenoConfig.area_a_press_rise_thresh = 1000;
    g_TenoConfig.area_a_release_drop_thresh = 1500;
    g_TenoConfig.area_a_press_break_thresh = 700;
    g_TenoConfig.rea_a_down_tr_up = 0.8f;
    g_TenoConfig.rea_a_down_tr_down = 0.25f;

    for (int i = 0; i < 34; i++) {
        g_TenoConfig.channels[i].block = 'A';
        g_TenoConfig.channels[i].default_thresh = 30;
        // 绝不乱动你的通道物理映射！保底时保持纯静默，强制要求上位机下发。
        g_TenoConfig.channels[i].mask = 0;
    }
}
