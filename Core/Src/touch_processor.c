#include "touch_processor.h"
#include "teno_config.h"
#include "mai2touch.h"
#include <string.h>

// 声明外部的 CDC 发送函数
extern uint8_t CDC_Transmit(uint8_t ep, uint8_t* Buf, uint16_t Len);

bool debug_raw_output = false;

// ================= 环形缓冲区 =================
typedef struct {
    int data[10];
    int head;
    int count;
    int limit;
} FastQueue_t;

static void FQ_Init(FastQueue_t *q, int limit) {
    q->head = 0; q->count = 0; q->limit = limit;
}

static void FQ_Add(FastQueue_t *q, int val) {
    if (q->count < q->limit) {
        q->data[q->count++] = val;
    } else {
        q->data[q->head] = val;
        q->head = (q->head + 1) % q->limit;
    }
}

static int FQ_Get(FastQueue_t *q, int index) {
    if (q->count < q->limit) return q->data[index];
    return q->data[(q->head + index) % q->limit];
}

static void FQ_Clear(FastQueue_t *q) {
    q->head = 0; q->count = 0;
}

// ================= 通道状态跟踪器 =================
typedef enum { TS_NONE = 0, TS_RAW10, TS_RAW01 } TouchSubState_t;
typedef enum { SIDE_NONE = 0, SIDE_ABOVE, SIDE_BELOW } TouchSide_t;

typedef struct {
    float baseline;
    float rawDefault;
    FastQueue_t history;
    FastQueue_t history2;
    FastQueue_t varHistory;

    int lastTouchFrames;
    TouchSubState_t subState;
    TouchSide_t lastSide;
    int currentStatus;
    int rawTouched;
    int rawTouchedLock;
} CapsenseState_t;

static CapsenseState_t trackers[34];
static uint64_t currentTouchMask = 0;

// 校准相关
static int startupRawBuffer[34] = {0};
static float startupRawFinal[34] = {0};
static int startupPacketsCount = 0;
static bool startupRawReady = false;

// 初始化
void TouchProcessor_Init(void) {
    for (int i = 0; i < 34; i++) {
        trackers[i].baseline = 0;
        trackers[i].rawDefault = 0;
        FQ_Init(&trackers[i].history, 4);
        FQ_Init(&trackers[i].history2, 10);
        FQ_Init(&trackers[i].varHistory, 10);
        trackers[i].subState = TS_NONE;
        trackers[i].lastSide = SIDE_NONE;
        trackers[i].currentStatus = 0;
        trackers[i].rawTouched = 0;
        trackers[i].rawTouchedLock = 0;
        trackers[i].lastTouchFrames = 0;
    }
    startupRawReady = false;
    startupPacketsCount = 0;
}

static int GetBlockDefault(char block) {
    switch (block) {
        case 'A': return g_TenoConfig.fixed_trigger_default_a;
        case 'B': return g_TenoConfig.fixed_trigger_default_b;
        case 'C': return g_TenoConfig.fixed_trigger_default_c;
        case 'D': return g_TenoConfig.fixed_trigger_default_d;
        case 'E': return g_TenoConfig.fixed_trigger_default_e;
        default: return 50000;
    }
}

static bool IsTriggered(int raw, int variance, int threshold, char block) {
    if (g_TenoConfig.enable_fixed_trigger_mode) {
        int effectiveThreshold = (threshold - 30) * 100 + GetBlockDefault(block);
        return (raw > effectiveThreshold) || (raw >= 0xFF00);
    } else {
        return (variance > threshold) || (raw >= 0xFF00);
    }
}

// 核心运算逻辑：单通道更新
static int ProcessChannel(int chIdx, uint16_t rawVal) {
    CapsenseState_t* tr = &trackers[chIdx];
    const ChannelConfig_t* cfg = &g_TenoConfig.channels[chIdx];
    int raw = (int)rawVal;

    if (tr->baseline == 0) tr->baseline = startupRawFinal[chIdx];
    if (cfg->block == 'A' && tr->rawDefault == 0) tr->rawDefault = startupRawFinal[chIdx];

    int variance = raw - (int)(cfg->block == 'A' ? tr->rawDefault : tr->baseline);

    if (cfg->block == 'A') {
        float effThreshold = g_TenoConfig.enable_fixed_trigger_mode ? ((cfg->default_thresh - 30) * 100 + GetBlockDefault('A')) : cfg->default_thresh;
        bool isTriggered = raw > effThreshold;
        TouchSide_t currentSide = isTriggered ? SIDE_ABOVE : SIDE_BELOW;

        FQ_Add(&tr->history2, raw);
        FQ_Add(&tr->varHistory, variance);

        if (tr->lastSide != SIDE_NONE && currentSide != tr->lastSide) {
            FQ_Clear(&tr->history);
            tr->subState = TS_NONE;
            tr->rawTouchedLock = 0;
            tr->rawTouched = 0;
            tr->currentStatus = (currentSide == SIDE_ABOVE) ? 1 : 0;
        }

        tr->lastSide = currentSide;
        FQ_Add(&tr->history, raw);

        if (tr->history.count >= 1) {
            int oldest = FQ_Get(&tr->history, 0);
            int current = raw;

            if (currentSide == SIDE_ABOVE) {
                if (tr->history2.count >= 8 && tr->rawTouchedLock == 0) {
                    tr->rawTouched = current;
                    tr->rawTouchedLock = 1;
                }

                if (tr->subState != TS_RAW10) {
                    // [替换] AREA_A_PRESS_BREAK_THRESH
                    if (current < tr->rawTouched - g_TenoConfig.area_a_press_break_thresh) {
                        tr->currentStatus = 0;
                        tr->subState = TS_RAW10;
                        FQ_Clear(&tr->history);
                        FQ_Add(&tr->history, current);
                    }
                } else {
                    // [替换] AREA_A_PRESS_RISE_THRESH
                    if (current - oldest >= g_TenoConfig.area_a_press_rise_thresh) {
                        tr->currentStatus = 1;
                        tr->subState = TS_NONE;
                        FQ_Clear(&tr->history);
                        FQ_Add(&tr->history, current);
                    }
                }
            } else {
                tr->rawTouched = 0;
                tr->rawTouchedLock = 0;
                int gap = (int)(effThreshold - tr->rawDefault);
                if (gap <= 0) gap = 100;

                // [替换] REA_A_DOWN_TR_UP / REA_A_DOWN_TR_DOWN
                if (current - oldest >= gap * g_TenoConfig.rea_a_down_tr_up) {
                    tr->currentStatus = 1;
                    tr->subState = TS_RAW01;
                    FQ_Clear(&tr->history);
                    FQ_Add(&tr->history, current);
                } else if (oldest - current >= gap * g_TenoConfig.rea_a_down_tr_down) {
                    tr->currentStatus = 0;
                    tr->subState = TS_NONE;
                    FQ_Clear(&tr->history);
                    FQ_Add(&tr->history, current);
                }

                int var1 = (tr->varHistory.count >= 2) ? FQ_Get(&tr->varHistory, tr->varHistory.count - 2) : variance;
                int delta = variance - var1;

                if (variance < 200) {
                    tr->currentStatus = 0;
                    if (tr->lastTouchFrames < 0) tr->lastTouchFrames = 0;
                }

                if (tr->lastTouchFrames > 0) {
                    tr->lastTouchFrames--;
                } else {
                    bool isFastSlide = (delta > cfg->var001_thresh || variance > cfg->var_thresh);
                    bool isRising = (delta > 0);
                    bool isNotPrePress = (raw < (effThreshold - 800));

                    if (isFastSlide && isRising && isNotPrePress) {
                        tr->currentStatus = 1;
                        // [替换] AREA_A_FAST_SLIDE_FPS_LIMIT
                        tr->lastTouchFrames = g_TenoConfig.area_a_fast_slide_fps_limit >= 0 ? g_TenoConfig.area_a_fast_slide_fps_limit : 10;
                    }
                }
            }
        }

        if (raw >= 0xFF00) tr->currentStatus = 1;
        tr->baseline = tr->rawDefault;
        return tr->currentStatus;
    } else {
        // BCDE 区域处理
        int varThresh = 0;
        // [替换] 副通道阈值配置
        switch (cfg->block) {
            case 'B': varThresh = g_TenoConfig.var_thresh_b; break;
            case 'C': varThresh = g_TenoConfig.var_thresh_c; break;
            case 'D': varThresh = g_TenoConfig.var_thresh_d; break;
            case 'E': varThresh = g_TenoConfig.var_thresh_e; break;
        }

        if (tr->baseline + varThresh > raw) {
            tr->baseline = (tr->baseline * 0.8f) + (raw * 0.2f);
        }
        variance = raw - (int)tr->baseline;

        // [替换] VARIANCE_THRESH_BCDE
        if (IsTriggered(raw, variance, cfg->default_thresh, cfg->block) || (g_TenoConfig.variance_thresh_bcde > 0 && variance > g_TenoConfig.variance_thresh_bcde)) {
            tr->currentStatus = 1;
        } else {
            tr->currentStatus = 0;
        }

        // [替换] VARIANCE_THRESH_BCDE_DOWN
        if (g_TenoConfig.variance_thresh_bcde_down > 0 && variance < g_TenoConfig.variance_thresh_bcde_down) {
            tr->currentStatus = 0;
        }
        return tr->currentStatus;
    }
}

// 单帧整体处理逻辑
static void ProcessFrame(uint16_t* channels) {
    if (!startupRawReady) {
        if (channels[0] < 10000) return;

        for (int i = 0; i < 34; i++) startupRawBuffer[i] += channels[i];
        startupPacketsCount++;
        if (startupPacketsCount >= 10) {
            for (int i = 0; i < 34; i++) startupRawFinal[i] = startupRawBuffer[i] / 10.0f;
            startupRawReady = true;
        }
        return;
    }

    uint64_t newMask = 0;
    for (int i = 0; i < 34; i++) {
        if (ProcessChannel(i, channels[i]) == 1) {
            // [替换] 旧的 TENO_CHANNELS 数组引用
            newMask |= g_TenoConfig.channels[i].mask;
        }
    }

    currentTouchMask = newMask;
    Mai2Touch_SendTouch(currentTouchMask);
}

// ================= UART RX 缓存与分包逻辑 =================
#define RX_BUFFER_SIZE 1024
static uint8_t rx_ring[RX_BUFFER_SIZE];
static int rx_head = 0;
static int rx_tail = 0;

void TouchProcessor_FeedData(uint8_t* data, uint16_t len) {
    for (uint16_t i = 0; i < len; i++) {
        rx_ring[rx_tail] = data[i];
        rx_tail = (rx_tail + 1) % RX_BUFFER_SIZE;
    }
}

static int Available(void) {
    return (rx_tail >= rx_head) ? (rx_tail - rx_head) : (RX_BUFFER_SIZE - rx_head + rx_tail);
}

static uint8_t ReadByte(int offset) {
    return rx_ring[(rx_head + offset) % RX_BUFFER_SIZE];
}

static void Consume(int n) {
    rx_head = (rx_head + n) % RX_BUFFER_SIZE;
}

void TouchProcessor_Task(void) {
    while (Available() >= 70) {
        if (ReadByte(0) == 0x00) {
            int checksum = 0;
            for (int i = 0; i < 69; i++) {
                checksum += ReadByte(i);
            }

            if ((checksum & 0xFF) == ReadByte(69)) {
                uint16_t channels[34];
                uint8_t raw_frame[70];

                // 将数据提取出来
                for (int i = 0; i < 70; i++) {
                    raw_frame[i] = ReadByte(i);
                }
                // 解析成 34 通道的 uint16_t 给核心处理器
                for (int i = 0; i < 34; i++) {
                    channels[i] = (uint16_t)(raw_frame[1 + i * 2] | (raw_frame[2 + i * 2] << 8));
                }

                for (int i = 0; i < 34; i++) {
                    channels[i] = (uint16_t)(ReadByte(1 + i * 2) | (ReadByte(2 + i * 2) << 8));
                }

                ProcessFrame(channels);

                // ===== 新增：透传调试数据到上位机 =====
                if (debug_raw_output) {
                   // 当开启调试模式时，将 70 字节的传感器原始数据原封不动丢给 USB 虚拟串口
                   CDC_Transmit(0, raw_frame, 70);
                }

                Consume(70);
            } else {
                Consume(1);
            }
        } else {
            Consume(1);
        }
    }
}
