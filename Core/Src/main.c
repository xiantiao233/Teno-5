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

// 定义 PA0-PA7 对应的 HID 键码 (w, e, d, c, x, z, a, q)
const uint8_t KEYCODE_MAP[8] = {
    0x1A, // PA0 -> 'w'
    0x08, // PA1 -> 'e'
    0x07, // PA2 -> 'd'
    0x06, // PA3 -> 'c'
    0x1B, // PA4 -> 'x'
    0x1D, // PA5 -> 'z'
    0x04, // PA6 -> 'a'
    0x14  // PA7 -> 'q'
};

void Keyboard_Scan_Task(void)
{
    // 用于记录上一次的按键状态，只有状态改变时才发送 USB 数据
    static uint8_t last_key_state = 0;

    // 读取 PA0-PA7 的电平状态。
    // 按下时为高电平(1)，未按下时为低电平(0)
    uint8_t current_key_state = (uint8_t)(GPIOA->IDR & 0x00FF);

    // 如果按键状态发生了变化 (有按键按下或松开)
    if (current_key_state != last_key_state)
    {
        //HAL_Delay(10); // 简单的软件消抖延迟

        // 再次读取确认状态
        current_key_state = (uint8_t)(GPIOA->IDR & 0x00FF);

        if (current_key_state != last_key_state)
        {
            uint8_t hid_report[8] = {0}; // 初始化 8 字节空报文
            uint8_t report_idx = 2;      // 普通按键从第 2 字节开始存放

            // 遍历 8 个引脚的状态
            for (int i = 0; i < 8; i++)
            {
                // 如果当前引脚状态为 1 (触发)
                if (current_key_state & (1 << i))
                {
                    if (report_idx < 8) // 标准键盘一次最多发 6 个键 (无冲限制)
                    {
                        hid_report[report_idx] = KEYCODE_MAP[i];
                        report_idx++;
                    }
                }
            }

            // 发送 HID 键盘报文
            // 注意：下面这个函数名是 AL94 包提供的典型键盘发送 API。
            // 如果编译提示找不到该函数，请检查 AL94 源码目录中 HID_KEYBOARD 相关的 .h 文件确认准确函数名。
            USBD_HID_Keybaord_SendReport(&hUsbDevice, hid_report, 8); // 注意这里的 a 和 o 写反了

            // 更新状态
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
  MX_USB_PCD_Init();
  /* USER CODE BEGIN 2 */
  MX_USB_DEVICE_Init();
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
	  //Keyboard_Scan_Task(); // 调用键盘扫描与发送任务

	  // 2. 加入自动打字测试代码 (每 5 秒自动输入一个 'a')
	        static uint32_t last_test_tick = 0;
	        if (HAL_GetTick() - last_test_tick > 5000)
	        {
	            last_test_tick = HAL_GetTick();

	            uint8_t test_report[8] = {0, 0, 0x04, 0, 0, 0, 0, 0}; // 0x04 是字母 'a'
	            USBD_HID_Keybaord_SendReport(&hUsbDevice, test_report, 8); // 发送按下

	            HAL_Delay(20); // 停顿 20ms 模拟手指按压时间

	            uint8_t release_report[8] = {0};
	            USBD_HID_Keybaord_SendReport(&hUsbDevice, release_report, 8); // 发送松开
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
