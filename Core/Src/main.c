/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "dma.h"
#include "usart.h"
#include "usb.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "usb_device.h"
#include "usbd_hid_keyboard.h"
#include "touch_processor.h"
#include "teno_config.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
uint8_t uart_rx_buf[UART_RX_BUF_SIZE];
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
#include "usbd_composite.h"
#include <string.h>

extern USBD_HandleTypeDef hUsbDevice;

// 【修复2】：严格匹配描述符计算的 17 字节 (1 ID + 1 Modifiers + 1 Reserved + 14 Keys)
uint8_t hid_report[17] = {0};
uint8_t last_hid_report[17] = {0};

uint8_t pa_connected_mask = 0; // 仅记录 PA0-PA7 的连接状态

// PA0 - PA7 的主键码
const uint8_t PA_KEYCODE_MAP[8] = {
    0x1A, 0x08, 0x07, 0x06, 0x1B, 0x1D, 0x04, 0x14
};

void Keyboard_Init(void)
{
    // 延时 100ms 等待上电后电容充电及电平完全稳定
    HAL_Delay(100);

    // 启动检测：PA0-PA7。未接入悬空时因内部上拉会读到高电平
    // 接入按键后被外围硬件拉低，读到低电平 (RESET) 才算作已连接
    for(int i = 0; i < 8; i++)
    {
        if(HAL_GPIO_ReadPin(GPIOA, (uint16_t)(1 << i)) == GPIO_PIN_RESET)
        {
            pa_connected_mask |= (1 << i);
        }
    }

    hid_report[0] = 0x01; // Report ID 固定为 1
}

void Keyboard_Scan_Task(void)
{
    static uint32_t last_scan_tick = 0;

    // 如果 USB 尚未与电脑握手成功，直接返回，防死锁
    if (hUsbDevice.dev_state != USBD_STATE_CONFIGURED) {
        return;
    }

    uint32_t current_tick = HAL_GetTick();
    // 1ms 轮询限制 (真1000Hz)，绝不阻塞主循环和 CDC 串口解析
    if (current_tick - last_scan_tick < 1) {
        return;
    }
    last_scan_tick = current_tick;

    // 清空按键数据区 (从索引 3 开始的 14 个字节)
    memset(&hid_report[3], 0, 14);
    uint8_t key_index = 3;

    // --- 1. 扫描 PA0-PA7 (游玩时高电平触发) ---
    uint16_t pa_state = GPIOA->IDR & 0x00FF; // 只取前8位，绝不碰 USB 引脚
    for(int i = 0; i < 8; i++)
    {
        if(pa_connected_mask & (1 << i))
        {
            if(pa_state & (1 << i)) // 触发
            {
                if (key_index < 17) hid_report[key_index++] = PA_KEYCODE_MAP[i];
            }
        }
    }

    // --- 2. 扫描 GPIOB 副按键 (根据你的 gpio.c，这几个默认上拉，按下接地触发即低电平) ---
    // 副键码使用: 0x15(R), 0x17(T), 0x1C(Y), 0x18(U), 0x28(Enter)
    uint16_t pb_state = GPIOB->IDR;
    if(!(pb_state & GPIO_PIN_0))  { if (key_index < 17) hid_report[key_index++] = 0x15; }
    if(!(pb_state & GPIO_PIN_1))  { if (key_index < 17) hid_report[key_index++] = 0x17; }
    if(!(pb_state & GPIO_PIN_2))  { if (key_index < 17) hid_report[key_index++] = 0x1C; }
    if(!(pb_state & GPIO_PIN_10)) { if (key_index < 17) hid_report[key_index++] = 0x18; }
    if(!(pb_state & GPIO_PIN_11)) { if (key_index < 17) hid_report[key_index++] = 0x28; }

    // --- 3. 极速状态比对与发送 ---
    // 只有在按下或松开的瞬间发送一包，彻底释放带宽，12键无冲拉满
    if (memcmp(hid_report, last_hid_report, 17) != 0)
    {
        if (USBD_HID_Keybaord_SendReport(&hUsbDevice, hid_report, 17) == USBD_OK)
        {
            memcpy(last_hid_report, hid_report, 17);
        }
    }
}
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_USB_PCD_Init();
  MX_UART4_Init();


  /* USER CODE BEGIN 2 */

  // [新增] 1. 优先加载Flash配置
    Config_Init();
    TouchProcessor_Init();
    Keyboard_Init(); // <--- 放在这里

  MX_USB_DEVICE_Init();


  // 【修改这里】：使用扩展函数开启接收，它会自动开启空闲中断(IDLE IT)
    if (HAL_UARTEx_ReceiveToIdle_DMA(&huart4, uart_rx_buf, UART_RX_BUF_SIZE) != HAL_OK) {
        Error_Handler();
    }
    /* 建议关闭半传输中断(Half Transfer)，否则数据接收过半时会触发额外中断导致错乱 */
    __HAL_DMA_DISABLE_IT(huart4.hdmarx, DMA_IT_HT);
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
    while (1)
      {
          Keyboard_Scan_Task();
          TouchProcessor_Task();

          if (g_ConfigReplyCmd != 0) {
                    if (g_ConfigReplyCmd == 1) {
                        Config_SendToPC();
                    } else if (g_ConfigReplyCmd == 3) {
                        // 【修复点】：加上 static
                        static uint8_t ack[] = {0x5A, 0xA5, 0x03, 0x00, 0x00, 0xFF, 0xFF, 0xED, 0xDE};
                        CDC_Transmit(0, ack, 9);
                    } else if (g_ConfigReplyCmd == 4) {
                        // 【修复点】：加上 static
                        static uint8_t nack[] = {0x5A, 0xA5, 0x04, 0x00, 0x00, 0xFF, 0xFF, 0xED, 0xDE};
                        CDC_Transmit(0, nack, 9);
                    }
                    g_ConfigReplyCmd = 0; // 清除标志
                }
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1_BOOST);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI|RCC_OSCILLATORTYPE_HSI48;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.HSI48State = RCC_HSI48_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = RCC_PLLM_DIV4;
  RCC_OscInitStruct.PLL.PLLN = 85;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = RCC_PLLQ_DIV2;
  RCC_OscInitStruct.PLL.PLLR = RCC_PLLR_DIV2;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */
/**
  * @brief  串口空闲中断回调函数 (由 HAL_UART_IRQHandler 自动调用)
  */
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size)
{
    if (huart->Instance == UART4)
    {
        // 静态变量，用来记录上一次读取的绝对位置
        static uint16_t old_pos = 0;

        // Size 是 HAL 库算出的当前 DMA 写入指针在 buffer 中的绝对偏移量
        uint16_t curr_pos = Size;
        uint16_t len = 0;

        if (curr_pos > old_pos)
        {
            // DMA 指针正常向前推进，没有绕回
            len = curr_pos - old_pos;
            TouchProcessor_FeedData(&uart_rx_buf[old_pos], len);
        }
        else if (curr_pos < old_pos)
        {
            // 因为是 Circular 模式，DMA 指针到底部后绕回了头部
            // 1. 先把从老位置到 buffer 尾部的数据读完
            len = UART_RX_BUF_SIZE - old_pos;
            TouchProcessor_FeedData(&uart_rx_buf[old_pos], len);

            // 2. 再把绕回头部后产生的新数据读完
            if (curr_pos > 0) {
                TouchProcessor_FeedData(uart_rx_buf, curr_pos);
            }
        }
        // 如果 curr_pos == old_pos，说明没有新数据，不处理

        // 更新历史位置
        old_pos = curr_pos;

        // 【极其重要】：
        // 绝对不要调用 HAL_UART_DMAStop！
        // 绝对不要再次调用 HAL_UARTEx_ReceiveToIdle_DMA！
        // 因为 DMA 在 Circular 模式下一直在后台默默工作，你只需要去“追”它的写指针即可。
    }
}
/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
