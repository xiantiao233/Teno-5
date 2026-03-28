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
#include "usbd_composite.h" // 确保包含了复合设备的头文件

// 引入 USB 句柄 (根据你的实际工程可能在 usb_device.h 或是 usbd_core.c 中定义)
extern USBD_HandleTypeDef hUsbDevice;

const uint8_t KEYCODE_MAP[12] = {
    0x1A, 0x08, 0x07, 0x06, 0x1B, 0x1D, 0x04, 0x14, // 前8个
    0x15, 0x17, 0x1c, 0x18                          // 新增 4 个示例键码
};

void Keyboard_Scan_Task(void)
{
    static uint16_t last_key_state = 0; // 改为 uint16_t 以支持 12 位

    // 读取 PA0-PA11 的状态 (掩码 0x0FFF)
    uint16_t current_key_state = (uint16_t)(GPIOA->IDR & 0x0FFF);

    if (current_key_state != last_key_state)
    {
        //HAL_Delay(5); // 缩短消抖时间以提高灵敏度
        current_key_state = (uint16_t)(GPIOA->IDR & 0x0FFF);

        if (current_key_state != last_key_state)
        {
            // 2. 报文数组必须是 19 字节！
            uint8_t hid_report[19] = {0};
            hid_report[0] = 0x01;  // Report ID

            uint8_t report_idx = 3; // 键码从第 3 字节开始

            for (int i = 0; i < 12; i++) // 遍历 12 个引脚
            {
                if (current_key_state & (1 << i) && i)
                {
                    if (report_idx < 19)
                    {
                        hid_report[report_idx] = KEYCODE_MAP[i];
                        report_idx++;
                    }
                }
            }

            // 3. 发送长度必须明确指定为 19！
            USBD_HID_Keybaord_SendReport(&hUsbDevice, hid_report, 19);
            last_key_state = current_key_state;
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
  * @param  huart: 串口句柄
  * @param  Size:  本次 DMA 接收到的数据总长度
  */
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size)
{
    if (huart->Instance == UART4)
    {
        // 1. 停止当前 DMA（为了强制复位 DMA 写入指针到 buffer 起始地址，应对循环模式带来的影响）
        HAL_UART_DMAStop(&huart4);

        // 2. 将收到的不定长数据通过 USB CDC 转发
        if (Size > 0)
        {
            CDC_Transmit(0, uart_rx_buf, Size);
        }

        // 3. 重新开启基于空闲中断的 DMA 接收，等待下一帧数据
        HAL_UARTEx_ReceiveToIdle_DMA(&huart4, uart_rx_buf, UART_RX_BUF_SIZE);
        __HAL_DMA_DISABLE_IT(huart->hdmarx, DMA_IT_HT); // 每次重新开启后再次禁用 HT 中断
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
