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

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
/* Конфігурація циклічного буфера UART */
#define BUFFER_SIZE         20

/* Налаштування періодів відправки по UART (у мілісекундах) */
#define TX_PERIOD_NORMAL    1000  // Нормальний режим: 1 раз на секунду
#define TX_PERIOD_FAST      500   // Прискорений режим: 2 рази на секунду

/* Конфігурація адрес Flash-пам'яті */
#define FLASH_READ_ADDR_1   0x0800FC00
#define FLASH_READ_ADDR_2   0x0800FC01
#define FLASH_ERASE_PAGE    0x0800FC00  // Для F1 це початок 63-ї сторінки
#define FLASH_WRITE_ADDR    0x0800FC02
#define FLASH_DATA_TO_WRITE 0xEEAA      // Об'єднані байти 0xAA та 0xEE
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
ADC_HandleTypeDef hadc1;

TIM_HandleTypeDef htim1;

UART_HandleTypeDef huart2;

/* USER CODE BEGIN PV */
// Оголошуємо глобальні змінні для зберігання зчитаних байтів
uint8_t flash_val1 = 0;
uint8_t flash_val2 = 0;

volatile uint8_t rx_buffer[BUFFER_SIZE] = {0}; // Циклічний буфер на 20 байт
volatile int8_t rx_index = -1;  // Індекс останньої заповненої комірки (-1 — буфер порожній)
volatile uint8_t rx_byte=0; // Прийнятий байт

uint32_t last_tx_time = 0; // Для зберігання часу останньої відправки
uint32_t tx_period = TX_PERIOD_NORMAL; // Період відправки (1 секунда за замовчуванням)

volatile uint16_t adc_result = 0;   // 12-бітний замір adc
volatile uint8_t adc_ready_flag = 0; // Прапорець готовності даних
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_ADC1_Init(void);
static void MX_TIM1_Init(void);
static void MX_USART2_UART_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

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
  MX_ADC1_Init();
  MX_TIM1_Init();
  MX_USART2_UART_Init();
  /* USER CODE BEGIN 2 */

  // При старті мікроконтролера зчитуємо два байти:
  flash_val1 = *(__IO uint8_t*)FLASH_READ_ADDR_1;
  flash_val2 = *(__IO uint8_t*)FLASH_READ_ADDR_2;

  //Запис двох байтів
  //(об'єднали 0xAA та 0xEE в одне 16-бітне число 0xEEAA, адреса 0x0800FC02)
  // Оголошуємо статус виконання операцій
  HAL_StatusTypeDef flash_status = HAL_OK;

  // 1. Розблокуємо можливість запису у Flash-пам'ять
  HAL_FLASH_Unlock();

  // 2. Налаштовуємо структуру для стирання сторінки
  FLASH_EraseInitTypeDef eraseConfig;
  eraseConfig.TypeErase = FLASH_TYPEERASE_PAGES; // Вказуємо, що перемо сторінками
  eraseConfig.PageAddress = FLASH_ERASE_PAGE;  // Адреса сторінки, яку треба стерти
  eraseConfig.NbPages = 1;                       // Кількість сторінок для стирання (всього 1)

  uint32_t pageError = 0; // Змінна для адреса помилки, якщо стирання не відбудеться

  // Викликаємо функцію стирання сторінки
  if (HAL_FLASHEx_Erase(&eraseConfig, &pageError) == HAL_OK)
  {
      // Якщо сторінка успішно стерлася можна записувати дані
      flash_status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD, FLASH_WRITE_ADDR, FLASH_DATA_TO_WRITE);
  }

  // Обробка помилок Flash за ТЗ
  if (flash_status != HAL_OK) {
      uint32_t flash_error = HAL_FLASH_GetError();
      (void)flash_error; // Заглушка для компілятора, щоб не було Warning
  }

  HAL_FLASH_Lock(); // Разблокуємо Flash

  // Запускаємо перший прийом по UART перед циклом
  HAL_UART_Receive_IT(&huart2, (uint8_t*)&rx_byte, 1);

  // Start PWM signal on timer 1, channel 1
  HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1); // Фізично запускаємо генерацію ШІМ на PA8

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {

		// 1. Перевірка входу PB2 (зміна частоти відправки)
		if (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_2) == GPIO_PIN_RESET)
		{
		    tx_period = TX_PERIOD_FAST;  // Якщо натиснуто (0) — відправляємо двічі на секунду (500 мс)
		}
		else
		{
		    tx_period = TX_PERIOD_NORMAL; // Якщо відпущено (1) — відправляємо раз на секунду (1000 мс)
		}

		// 2. Неблокуючий програмний таймер відправки в UART
		if ((HAL_GetTick() - last_tx_time) >= tx_period)
		{
		    last_tx_time = HAL_GetTick(); // Оновлюємо час останньої події

		    // Формуємо масив із двох сирих байтів, які ми зчитали з Flash при старті
		    uint8_t msg_data[2] = {flash_val1, flash_val2};

		    // Відправляємо сирі байти без ASCII-кодування
		    HAL_UART_Transmit(&huart2, msg_data, 2, 10);
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
  RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;
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
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
  PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_ADC;
  PeriphClkInit.AdcClockSelection = RCC_ADCPCLK2_DIV6;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief ADC1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_ADC1_Init(void)
{

  /* USER CODE BEGIN ADC1_Init 0 */

  /* USER CODE END ADC1_Init 0 */

  ADC_ChannelConfTypeDef sConfig = {0};

  /* USER CODE BEGIN ADC1_Init 1 */

  /* USER CODE END ADC1_Init 1 */

  /** Common config
  */
  hadc1.Instance = ADC1;
  hadc1.Init.ScanConvMode = ADC_SCAN_DISABLE;
  hadc1.Init.ContinuousConvMode = DISABLE;
  hadc1.Init.DiscontinuousConvMode = DISABLE;
  hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
  hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
  hadc1.Init.NbrOfConversion = 1;
  if (HAL_ADC_Init(&hadc1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Regular Channel
  */
  sConfig.Channel = ADC_CHANNEL_0;
  sConfig.Rank = ADC_REGULAR_RANK_1;
  sConfig.SamplingTime = ADC_SAMPLETIME_1CYCLE_5;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN ADC1_Init 2 */

  /* USER CODE END ADC1_Init 2 */

}

/**
  * @brief TIM1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM1_Init(void)
{

  /* USER CODE BEGIN TIM1_Init 0 */

  /* USER CODE END TIM1_Init 0 */

  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};
  TIM_OC_InitTypeDef sConfigOC = {0};
  TIM_BreakDeadTimeConfigTypeDef sBreakDeadTimeConfig = {0};

  /* USER CODE BEGIN TIM1_Init 1 */

  /* USER CODE END TIM1_Init 1 */
  htim1.Instance = TIM1;
  htim1.Init.Prescaler = 55;
  htim1.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim1.Init.Period = 255;
  htim1.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim1.Init.RepetitionCounter = 0;
  htim1.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim1) != HAL_OK)
  {
    Error_Handler();
  }
  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim1, &sClockSourceConfig) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_PWM_Init(&htim1) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim1, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sConfigOC.OCMode = TIM_OCMODE_PWM1;
  sConfigOC.Pulse = 128;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCNPolarity = TIM_OCNPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
  sConfigOC.OCIdleState = TIM_OCIDLESTATE_RESET;
  sConfigOC.OCNIdleState = TIM_OCNIDLESTATE_RESET;
  if (HAL_TIM_PWM_ConfigChannel(&htim1, &sConfigOC, TIM_CHANNEL_1) != HAL_OK)
  {
    Error_Handler();
  }
  sBreakDeadTimeConfig.OffStateRunMode = TIM_OSSR_DISABLE;
  sBreakDeadTimeConfig.OffStateIDLEMode = TIM_OSSI_DISABLE;
  sBreakDeadTimeConfig.LockLevel = TIM_LOCKLEVEL_OFF;
  sBreakDeadTimeConfig.DeadTime = 0;
  sBreakDeadTimeConfig.BreakState = TIM_BREAK_DISABLE;
  sBreakDeadTimeConfig.BreakPolarity = TIM_BREAKPOLARITY_HIGH;
  sBreakDeadTimeConfig.AutomaticOutput = TIM_AUTOMATICOUTPUT_DISABLE;
  if (HAL_TIMEx_ConfigBreakDeadTime(&htim1, &sBreakDeadTimeConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM1_Init 2 */

  /* USER CODE END TIM1_Init 2 */
  HAL_TIM_MspPostInit(&htim1);

}

/**
  * @brief USART2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART2_UART_Init(void)
{

  /* USER CODE BEGIN USART2_Init 0 */

  /* USER CODE END USART2_Init 0 */

  /* USER CODE BEGIN USART2_Init 1 */

  /* USER CODE END USART2_Init 1 */
  huart2.Instance = USART2;
  huart2.Init.BaudRate = 9600;
  huart2.Init.WordLength = UART_WORDLENGTH_8B;
  huart2.Init.StopBits = UART_STOPBITS_1;
  huart2.Init.Parity = UART_PARITY_NONE;
  huart2.Init.Mode = UART_MODE_TX_RX;
  huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart2.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART2_Init 2 */

  /* USER CODE END USART2_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin : PB0 */
  GPIO_InitStruct.Pin = GPIO_PIN_0;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /*Configure GPIO pin : PB2 */
  GPIO_InitStruct.Pin = GPIO_PIN_2;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

// Обробник переривання UART - запис у кільцевий буфер
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART2)
    {
        // 1. Вираховуємо наступний індекс у кільці (перехід від 0 до 19)
        rx_index = (rx_index + 1) % BUFFER_SIZE;
        // 2. Записуємо прийнятий байт у буфер
        rx_buffer[rx_index] = rx_byte;
        // 3. Одразу оновлюємо ШІМ
        __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, rx_byte);
        // 4. Перезапускаємо прийом наступного байта
        HAL_UART_Receive_IT(&huart2, (uint8_t*)&rx_byte, 1);
    }
}

// Обробка помилок UART за вимогами ТЗ
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART2)
    {
          uint32_t error_code = HAL_UART_GetError(huart);
          (void)error_code;
          HAL_UART_Receive_IT(&huart2, (uint8_t*)&rx_byte, 1); // Перезапуск після збою
   }
}

//Обробник переривання кнопки (EXTI0)
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    // Перевіряємо, що переривання саме від піна PB0
    if (GPIO_Pin == GPIO_PIN_0)
    {
        HAL_ADC_Start_IT(&hadc1); // Неблокуючий запуск adc (канал IN0 на PA0 за ТЗ)
    }
}

//Обробник завершення заміру adc (Conversion Complete Callback)
void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef* hadc)
{
    if (hadc->Instance == ADC1)
    {
        // Зчитуємо готове 12-бітне значення з регістру даних АЦП
        adc_result = HAL_ADC_GetValue(hadc);

        // Піднімаємо прапорець можливості використання даних
        adc_ready_flag = 1;
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
