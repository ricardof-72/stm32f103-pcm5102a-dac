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
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
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
/*
 * O STM32F103C8 nao tem I2S. O protocolo (BCK, LRCK, DIN) e gerado por um timer
 * que dispara um DMA escrevendo em GPIOB->BSRR: PB13=BCK, PB12=LRCK, PB15=DIN.
 * O buffer contem exatamente UM periodo da onda quadrada e o DMA circular o
 * repete sozinho: a CPU nao participa (sem interrupcoes).
 */
#define PIN_LRCK            GPIO_PIN_12   // -> LCK  do PCM5102A
#define PIN_BCK             GPIO_PIN_13   // -> BCK  do PCM5102A
#define PIN_DIN             GPIO_PIN_15   // -> DIN  do PCM5102A

#define VOLUME_SHIFT        6             // 0 = maximo; cada unidade a mais divide por 2
#define TONE_HALF_PERIOD    12u           // quadros por semiciclo: tom = 11029 / (2 x 12) = ~460 Hz

#define FRAMES_TOTAL        (2u * TONE_HALF_PERIOD)   // 1 periodo completo
#define WORDS_PER_FRAME     64u           // 32 ciclos de BCK x 2 escritas
#define DMA_BUF_WORDS       (FRAMES_TOTAL * WORDS_PER_FRAME)

#define TIM_RELOAD          68u           // 48 MHz / 68 / 64 = ~11029 Hz de sample rate

static uint32_t i2s_buf[DMA_BUF_WORDS];   // 6 KB
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* Sobe o clock para 48 MHz (HSI/2 x12) para o sample rate ficar certo. */
static void Clock_48MHz(void)
{
  RCC_OscInitTypeDef osc = {0};
  RCC_ClkInitTypeDef clk = {0};

  osc.OscillatorType      = RCC_OSCILLATORTYPE_HSI;
  osc.HSIState            = RCC_HSI_ON;
  osc.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  osc.PLL.PLLState        = RCC_PLL_ON;
  osc.PLL.PLLSource       = RCC_PLLSOURCE_HSI_DIV2;
  osc.PLL.PLLMUL          = RCC_PLL_MUL12;
  if (HAL_RCC_OscConfig(&osc) != HAL_OK) Error_Handler();

  clk.ClockType      = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
                       RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
  clk.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
  clk.AHBCLKDivider  = RCC_SYSCLK_DIV1;
  clk.APB1CLKDivider = RCC_HCLK_DIV2;   // PCLK1 = 24 MHz -> clock do TIM2 = 48 MHz
  clk.APB2CLKDivider = RCC_HCLK_DIV1;
  if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_1) != HAL_OK) Error_Handler();
}

/*
 * Monta UM periodo da onda quadrada ja no formato de pinos (valores de BSRR).
 * Cada quadro = 32 ciclos de BCK (16 bits L + 16 bits R, mesma amostra nos dois).
 * Cada ciclo = 2 palavras: BCK baixo (dado/LRCK mudam) e BCK alto.
 * Formato left-justified (FMT do modulo em 3V3): o MSB sai junto com a troca do LRCK.
 */
/* I2S_FILL_BEGIN */
static void I2S_BuildSquare(void)
{
  uint32_t *dst = i2s_buf;
  for (uint32_t f = 0; f < FRAMES_TOTAL; f++) {
    int16_t  s = (f < TONE_HALF_PERIOD) ? 30000 : -30000;
    uint16_t u = (uint16_t)(s >> VOLUME_SHIFT);

    for (uint32_t c = 0; c < 32u; c++) {
      uint32_t lr  = (c >= 16u);
      uint32_t bit = (u >> (15u - (c & 15u))) & 1u;

      uint32_t set = (lr  ? PIN_LRCK : 0u) | (bit ? PIN_DIN : 0u);
      uint32_t rst = (lr  ? 0u : PIN_LRCK) | (bit ? 0u : PIN_DIN);

      *dst++ = set              | ((rst | PIN_BCK) << 16);   // BCK = 0
      *dst++ = (set | PIN_BCK)  | (rst << 16);               // BCK = 1
    }
  }
}
/* I2S_FILL_END */

static void I2S_Start(void)
{
  /* PB12/PB13/PB15 como saida push-pull rapida (PB13/PB15 eram SPI2, viram GPIO) */
  GPIO_InitTypeDef g = {0};
  g.Pin   = PIN_LRCK | PIN_BCK | PIN_DIN;
  g.Mode  = GPIO_MODE_OUTPUT_PP;
  g.Pull  = GPIO_NOPULL;
  g.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(GPIOB, &g);
  GPIOB->BSRR = ((uint32_t)(PIN_LRCK | PIN_BCK | PIN_DIN)) << 16;   // tudo em 0

  I2S_BuildSquare();

  /* DMA1 canal 2 = TIM2_UP : memoria -> GPIOB->BSRR, 32 bits, circular, sem interrupcao */
  __HAL_RCC_DMA1_CLK_ENABLE();
  DMA1_Channel2->CCR   = 0;
  DMA1_Channel2->CPAR  = (uint32_t)&GPIOB->BSRR;
  DMA1_Channel2->CMAR  = (uint32_t)i2s_buf;
  DMA1_Channel2->CNDTR = DMA_BUF_WORDS;
  DMA1_Channel2->CCR   = (1u << 4)      /* DIR: memoria -> periferico */
                       | (1u << 5)      /* CIRC  */
                       | (1u << 7)      /* MINC  */
                       | (2u << 8)      /* PSIZE = 32 bits */
                       | (2u << 10)     /* MSIZE = 32 bits */
                       | (2u << 12)     /* prioridade alta */
                       | 1u;            /* EN */

  /* TIM2: um update a cada TIM_RELOAD ciclos de 48 MHz -> pede um DMA */
  __HAL_RCC_TIM2_CLK_ENABLE();
  TIM2->PSC  = 0;
  TIM2->ARR  = TIM_RELOAD - 1u;
  TIM2->CNT  = 0;
  TIM2->DIER = (1u << 8);               /* UDE: DMA request no update */
  TIM2->CR1  = 1u;                      /* CEN */
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
  /* USER CODE BEGIN 2 */
  Clock_48MHz();
  I2S_Start();
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
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

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK)
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
