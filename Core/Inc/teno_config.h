#ifndef TENO_CONFIG_H
#define TENO_CONFIG_H

#include <stdint.h>
#include <stdbool.h>

// 强制 1 字节对齐，防止上位机下发时内存错位
#pragma pack(push, 1)

// ================= 通道数据结构 =================
typedef struct {
    char block;             // 区域区块 (A/B/C/D/E)
    uint64_t mask;          // 对应的 64-bit 按键掩码
    int32_t default_thresh; // 灵敏度阈值 (已应用 Override)
    int32_t var_thresh;     // 单独方差阈值
    int32_t var001_thresh;  // 单独Delta阈值
} ChannelConfig_t;

// ================= 完整全局配置结构体 =================
typedef struct {
    uint32_t magic;         // 防呆魔数: 0x4F4E4554 ("TENO")
    uint16_t version;       // 版本号: 0x0100 代表 v1.0

    // --- 全局开关 ---
    uint8_t  enable_fixed_trigger_mode; // 1: true, 0: false

    // --- 基础固定阈值 ---
    int32_t fixed_trigger_default_a;
    int32_t fixed_trigger_default_b;
    int32_t fixed_trigger_default_c;
    int32_t fixed_trigger_default_d;
    int32_t fixed_trigger_default_e;

    // --- 方差触发阈值 ---
    int32_t variance_thresh_bcde;
    int32_t variance_thresh_bcde_down;
    int32_t var_thresh_b;
    int32_t var_thresh_c;
    int32_t var_thresh_d;
    int32_t var_thresh_e;
    int32_t variance_thresh_a_default;
    int32_t var001_default;

    // --- A区状态机参数 ---
    int32_t area_a_release_drop_thresh;
    int32_t area_a_press_rise_thresh;
    int32_t area_a_press_break_thresh;
    int32_t area_a_fast_slide_fps_limit;

    float rea_a_down_tr_up;
    float rea_a_down_tr_down;

    // --- 物理通道映射 (34个通道) ---
    ChannelConfig_t channels[34];

} TenoConfig_t;

#pragma pack(pop)

// 暴露给外部调用的全局配置实例
extern TenoConfig_t g_TenoConfig;

// 协议层与 Flash 操作接口
void Config_Init(void);
void Config_ProcessByte(uint8_t byte);
void Config_SendToPC(void);

extern volatile uint8_t g_ConfigReplyCmd;

#endif // TENO_CONFIG_H
