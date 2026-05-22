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
// 这里的顺序对应了 C# 原版中的 TouchSheetMapping 物理通道映射顺序
// 包含区块(A-E)、64-bit掩码、基础灵敏度Threshold、独立方差阈值CustomVarianceOverrides、单独Delta阈值CustomVar001Overrides
// 出厂默认通道数据
// 已按每个区块名称 (A1~A8, B1~B8 等) 添加注释，方便区分和修改
// 出厂默认通道数据
// 出厂默认通道数据
// 已经严格按照你提供的高精度 0-33 硬件扫描顺序重新排序
// 独立方差与 Delta 阈值默认填 -1，代表禁用特殊化，直接回退并使用全局配置
// 出厂默认通道数据
// 已严格按照最新提供的 [TouchSheet] INI 0-33 硬件扫描顺序重新覆盖
// 独立方差与 Delta 阈值默认填 -1，代表禁用特殊化，直接回退并使用全局配置
static const ChannelConfig_t DEFAULT_CHANNELS[34] = {
    {'D', 1ULL << 24, 25, -1, -1}, // [0]  对应 Channel0=D7
    {'B', 1ULL << 13, 25, -1, -1}, // [1]  对应 Channel1=B6
    {'A', 1ULL << 5,  30, -1, -1}, // [2]  对应 Channel2=A6
    {'E', 1ULL << 31, 13, -1, -1}, // [3]  对应 Channel3=E6
    {'D', 1ULL << 23, 25, -1, -1}, // [4]  对应 Channel4=D6

    {'B', 1ULL << 12, 25, -1, -1}, // [5]  对应 Channel5=B5
    {'A', 1ULL << 4,  30, -1, -1}, // [6]  对应 Channel6=A5
    {'E', 1ULL << 30, 13, -1, -1}, // [7]  对应 Channel7=E5
    {'D', 1ULL << 22, 25, -1, -1}, // [8]  对应 Channel8=D5
    {'B', 1ULL << 11, 25, -1, -1}, // [9]  对应 Channel9=B4

    {'A', 1ULL << 3,  30, -1, -1}, // [10] 对应 Channel10=A4
    {'E', 1ULL << 29, 13, -1, -1}, // [11] 对应 Channel11=E4
    {'D', 1ULL << 21, 25, -1, -1}, // [12] 对应 Channel12=D4
    {'B', 1ULL << 10, 25, -1, -1}, // [13] 对应 Channel13=B3
    {'A', 1ULL << 2,  30, -1, -1}, // [14] 对应 Channel14=A3

    {'C', 1ULL << 16, 5,  -1, -1}, // [15] 对应 Channel15=C1
    {'E', 1ULL << 28, 13, -1, -1}, // [16] 对应 Channel16=E3
    {'D', 1ULL << 20, 25, -1, -1}, // [17] 对应 Channel17=D3
    {'B', 1ULL << 9,  25, -1, -1}, // [18] 对应 Channel18=B2
    {'A', 1ULL << 1,  30, -1, -1}, // [19] 对应 Channel19=A2

    {'E', 1ULL << 27, 13, -1, -1}, // [20] 对应 Channel20=E2
    {'D', 1ULL << 19, 25, -1, -1}, // [21] 对应 Channel21=D2
    {'B', 1ULL << 8,  25, -1, -1}, // [22] 对应 Channel22=B1
    {'A', 1ULL << 0,  30, -1, -1}, // [23] 对应 Channel23=A1
    {'E', 1ULL << 26, 13, -1, -1}, // [24] 对应 Channel24=E1

    {'D', 1ULL << 18, 25, -1, -1}, // [25] 对应 Channel25=D1
    {'B', 1ULL << 15, 25, -1, -1}, // [26] 对应 Channel26=B8
    {'A', 1ULL << 7,  30, -1, -1}, // [27] 对应 Channel27=A8
    {'E', 1ULL << 33, 13, -1, -1}, // [28] 对应 Channel28=E8
    {'D', 1ULL << 25, 25, -1, -1}, // [29] 对应 Channel29=D8

    {'B', 1ULL << 14, 25, -1, -1}, // [30] 对应 Channel30=B7
    {'A', 1ULL << 6,  30, -1, -1}, // [31] 对应 Channel31=A7
    {'C', 1ULL << 17, 5,  -1, -1}, // [32] 对应 Channel32=C2
    {'E', 1ULL << 32, 13, -1, -1}  // [33] 对应 Channel33=E7
};
void Config_Init(void) {
    TenoConfig_t* flash_cfg = (TenoConfig_t*)FLASH_CONFIG_ADDR;

    // 若Flash中的魔数匹配，说明存有配置
    if (flash_cfg->magic == TENO_MAGIC_NUM) {
        memcpy(&g_TenoConfig, flash_cfg, sizeof(TenoConfig_t));
    } else {
        // 第一次烧录或数据损坏，加载默认设定 (对应你以前的 C# ini 配置文件参数)
        memset(&g_TenoConfig, 0, sizeof(TenoConfig_t));
        g_TenoConfig.magic = TENO_MAGIC_NUM;
        g_TenoConfig.version = 0x0100;

        // --- 基础模式与固定触发基础设置 ---
        g_TenoConfig.enable_fixed_trigger_mode = 1;     // 启用固定触发模式 (EnableFixedTriggerMode)
        g_TenoConfig.fixed_trigger_default_a = 56000;   // A区固定触发基础 (FixedTriggerDefaultA)
        g_TenoConfig.fixed_trigger_default_b = 47500;   // B区固定触发基础 (FixedTriggerDefaultB)
        g_TenoConfig.fixed_trigger_default_c = 47000;   // C区固定触发基础 (FixedTriggerDefaultC)
        g_TenoConfig.fixed_trigger_default_d = 49500;   // D区固定触发基础 (FixedTriggerDefaultD)
        g_TenoConfig.fixed_trigger_default_e = 49500;   // E区固定触发基础 (FixedTriggerDefaultE)

        // --- 方差与突变阈值设置 ---
        g_TenoConfig.variance_thresh_bcde = 600;        // BCDE区方差突变触发阈值 (VarianceThresholdBCDE)
        g_TenoConfig.variance_thresh_bcde_down = 300;   // BCDE区方差突变触发阈值, 下降 (VarianceThresholdBCDEDown)
        g_TenoConfig.var_thresh_b = 600;                // B区方varThresh阈值 (VarThreshB)
        g_TenoConfig.var_thresh_c = 800;                // C区方varThresh阈值 (VarThreshC)
        g_TenoConfig.var_thresh_d = 800;                // D区方varThresh阈值 (VarThreshD)
        g_TenoConfig.var_thresh_e = 600;                // E区方varThresh阈值 (VarThreshE)

        g_TenoConfig.variance_thresh_a_default = 2500;  // A区默认方差突变阈值 (VarianceThresholdADefault)
        g_TenoConfig.var001_default = 0xFF00;              // A区Delta触发默认阈值(var001) (Var001Default)

        // --- A区专用状态机与基线参数 ---
        g_TenoConfig.area_a_release_drop_thresh = 1800; // A区松开判定下降阈值 (AreaAReleaseDropThreshold)
        g_TenoConfig.area_a_press_rise_thresh = 1500;   // A区按下判定上升阈值 (AreaAPressRiseThreshold)
        g_TenoConfig.area_a_press_break_thresh = 800;   // A区防断管子偏移值 (AreaAPressBreakThreshold)
        g_TenoConfig.area_a_fast_slide_fps_limit = -1;  // A区瞬发限制 (AreaAFastSlideFpsLimit)
        g_TenoConfig.rea_a_down_tr_up = 0.9f;           // A区未触发时的固定基线变化检测，上升 (ReaADonwTrUP)
        g_TenoConfig.rea_a_down_tr_down = 0.2f;         // A区未触发时的固定基线变化检测，下降 (ReaADonwTrDown)

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
