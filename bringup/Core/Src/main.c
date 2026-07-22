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
#include "sdmmc.h"
#include "spi.h"
#include "usb_device.h"
#include "usb_host.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "SEGGER_RTT.h"
#include "usbh_core.h"
#include "usbh_hid.h"
#include "usbh_hid_keybd.h"
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
static void MPU_Config(void);
void MX_USB_HOST_Process(void);

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
  SEGGER_RTT_printf(0, "Hello World\r\n");
  /* USER CODE END 1 */

  /* MPU Configuration--------------------------------------------------------*/
  MPU_Config();

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
  SEGGER_RTT_printf(0, "GPIO ok\r\n");
  MX_SDMMC1_MMC_Init();
  SEGGER_RTT_printf(0, "SDMMC ok\r\n");
  MX_SPI1_Init();
  SEGGER_RTT_printf(0, "SPI ok\r\n");
  MX_USB_DEVICE_Init();
  SEGGER_RTT_printf(0, "USB_DEVICE ok\r\n");
  MX_USB_HOST_Init();
  SEGGER_RTT_printf(0, "USB_HOST ok\r\n");
  /* Dump OTG_FS host registers */
  SEGGER_RTT_printf(0, "FS GUSBCFG=%08lX (FHMOD bit29 should=1)\r\n", (unsigned long)USB_OTG_FS->GUSBCFG);
  SEGGER_RTT_printf(0, "FS GINTSTS=%08lX (CMOD bit0: 1=host 0=device)\r\n", (unsigned long)USB_OTG_FS->GINTSTS);
  {
    volatile uint32_t *hprt = (volatile uint32_t *)(USB_OTG_FS_PERIPH_BASE + USB_OTG_HOST_PORT_BASE);
    SEGGER_RTT_printf(0, "FS HPRT   =%08lX (PCSTS bit0=port connect)\r\n", (unsigned long)*hprt);
  }
  /* USER CODE BEGIN 2 */
  /* Dump USB OTG_HS registers to verify init state */
  SEGGER_RTT_printf(0, "GUSBCFG=%08lX  (PHYSEL bit7 should=1)\r\n", USB_OTG_HS->GUSBCFG);
  SEGGER_RTT_printf(0, "GCCFG  =%08lX  (PWRDWN bit16 should=1, VBDEN bit21 should=0)\r\n", USB_OTG_HS->GCCFG);
  SEGGER_RTT_printf(0, "GOTGCTL=%08lX  (BVALOEN bit6, BVALOVAL bit7 should=1)\r\n", USB_OTG_HS->GOTGCTL);
  {
    USB_OTG_DeviceTypeDef *dev = (USB_OTG_DeviceTypeDef *)(USB_OTG_HS_PERIPH_BASE + USB_OTG_DEVICE_BASE);
    SEGGER_RTT_printf(0, "DCTL   =%08lX  (SDIS bit1 should=0)\r\n", dev->DCTL);
    SEGGER_RTT_printf(0, "DCFG   =%08lX  (DSPD bits1:0 should=11 for FS)\r\n", dev->DCFG);
  }

  /* PB4 = TIM3_CH1 AF2: 1 MHz square wave for scope verification of HSE clock
   * TIM3 clock = APB1 x2 = (192/4)*2 = 96 MHz
   * PSC=0, ARR=95 -> 96 MHz / 96 = 1 MHz */
  __HAL_RCC_TIM3_CLK_ENABLE();
  {
      GPIO_InitTypeDef g = {0};
      g.Pin = GPIO_PIN_4;
      g.Mode = GPIO_MODE_AF_PP;
      g.Pull = GPIO_NOPULL;
      g.Speed = GPIO_SPEED_FREQ_HIGH;
      g.Alternate = GPIO_AF2_TIM3;
      HAL_GPIO_Init(GPIOB, &g);
  }
  TIM3->PSC   = 0;
  TIM3->ARR   = 95;
  TIM3->CCR1  = 47;  /* 50% duty cycle */
  TIM3->CCMR1 = (6 << TIM_CCMR1_OC1M_Pos);  /* PWM mode 1 */
  TIM3->CCER  = TIM_CCER_CC1E;
  TIM3->EGR   = TIM_EGR_UG;   /* load PSC/ARR */
  TIM3->CR1   = TIM_CR1_CEN;
  SEGGER_RTT_printf(0, "TIM3 CH1 1MHz on PB4 — probe here\r\n");
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  /* USER CODE END WHILE */
  uint32_t led_tick = 0, led_phase = 0, log_tick = 0;
  uint32_t cnt_done = 0, cnt_notready = 0, cnt_idle = 0, cnt_other = 0;
  extern ApplicationTypeDef Appli_state;
  extern USBH_HandleTypeDef hUsbHostFS;
  while (1)
  {
    MX_USB_HOST_Process();

    /* Count URB states every loop iteration to catch transient states */
    if (Appli_state == APPLICATION_READY &&
        hUsbHostFS.pActiveClass != NULL && hUsbHostFS.pActiveClass->pData != NULL)
    {
      HID_HandleTypeDef *hid = (HID_HandleTypeDef *)hUsbHostFS.pActiveClass->pData;
      switch (USBH_LL_GetURBState(&hUsbHostFS, hid->InPipe))
      {
        case USBH_URB_DONE:     cnt_done++;     break;
        case USBH_URB_NOTREADY: cnt_notready++; break;
        case USBH_URB_IDLE:     cnt_idle++;     break;
        default:                cnt_other++;    break;
      }

      HID_KEYBD_Info_TypeDef *kb = USBH_HID_GetKeybdInfo(&hUsbHostFS);
      if (kb != NULL && kb->keys[0] != 0)
      {
        uint8_t ascii = USBH_HID_GetASCIICode(kb);
        if (ascii != 0)
          SEGGER_RTT_printf(0, "KEY: '%c' (code=0x%02X)\r\n", ascii, kb->keys[0]);
        else
          SEGGER_RTT_printf(0, "KEY: code=0x%02X\r\n", kb->keys[0]);
      }
    }

    /* LED chase at 250 ms per phase */
    if (HAL_GetTick() - led_tick >= 250)
    {
      led_tick = HAL_GetTick();
      HAL_GPIO_WritePin(GPIOE, GPIO_PIN_0|GPIO_PIN_1|GPIO_PIN_2, GPIO_PIN_RESET);
      if      (led_phase == 0) HAL_GPIO_WritePin(GPIOE, GPIO_PIN_0, GPIO_PIN_SET);
      else if (led_phase == 1) HAL_GPIO_WritePin(GPIOE, GPIO_PIN_1, GPIO_PIN_SET);
      else                     HAL_GPIO_WritePin(GPIOE, GPIO_PIN_2, GPIO_PIN_SET);
      led_phase = (led_phase + 1) % 3;
    }

    /* Log USB host state every 2 s */
    if (HAL_GetTick() - log_tick >= 2000)
    {
      log_tick = HAL_GetTick();
      if (hUsbHostFS.pActiveClass != NULL && hUsbHostFS.pActiveClass->pData != NULL)
      {
        HID_HandleTypeDef *hid = (HID_HandleTypeDef *)hUsbHostFS.pActiveClass->pData;
        SEGGER_RTT_printf(0, "USBH gState=%d Appli=%d | HID state=%d timer=%lu poll=%u | URB done=%lu notready=%lu idle=%lu other=%lu\r\n",
          (int)hUsbHostFS.gState, (int)Appli_state,
          (int)hid->state, (unsigned long)hUsbHostFS.Timer,
          (unsigned)hid->poll,
          cnt_done, cnt_notready, cnt_idle, cnt_other);
      }
      else
      {
        SEGGER_RTT_printf(0, "USBH gState=%d Appli=%d | no class\r\n",
          (int)hUsbHostFS.gState, (int)Appli_state);
      }
      cnt_done = cnt_notready = cnt_idle = cnt_other = 0;
    }
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
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 4;   /* 8 MHz crystal / 4 = 2 MHz VCO input (CubeMX used 6 for fake 12 MHz) */
  RCC_OscInitStruct.PLL.PLLN = 192;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 8;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Activate the Over-Drive mode
  */
  if (HAL_PWREx_EnableOverDrive() != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_6) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

/* Test MMC connector solder joints by reading each pin with pull-up then
 * pull-down. A good floating pin follows the pull; a stuck pin does not.
 * Run before MX_SDMMC1_MMC_Init, then call mmc_pin_test() from USER CODE BEGIN 2.
 */
static void test_pin(GPIO_TypeDef *port, uint16_t pin, const char *name)
{
    GPIO_InitTypeDef g = {0};
    g.Pin   = pin;
    g.Mode  = GPIO_MODE_INPUT;
    g.Speed = GPIO_SPEED_FREQ_LOW;

    g.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(port, &g);
    HAL_Delay(1);
    int hi = HAL_GPIO_ReadPin(port, pin);

    g.Pull = GPIO_PULLDOWN;
    HAL_GPIO_Init(port, &g);
    HAL_Delay(1);
    int lo = HAL_GPIO_ReadPin(port, pin);

    /* A floating pin follows pull direction: hi=1, lo=0 — solder joint OK */
    const char *result = (hi == 1 && lo == 0) ? "OK (floating)" :
                         (hi == 1 && lo == 1) ? "STUCK HIGH"    :
                         (hi == 0 && lo == 0) ? "STUCK LOW"     : "?";
    SEGGER_RTT_printf(0, "  %-12s pull-up=%d pull-dn=%d  %s\r\n",
                      name, hi, lo, result);
}

void mmc_pin_test(void)
{
    SEGGER_RTT_printf(0, "MMC pin test:\r\n");
    test_pin(GPIOC, GPIO_PIN_8,  "PC8  D0");
    test_pin(GPIOC, GPIO_PIN_9,  "PC9  D1");
    test_pin(GPIOC, GPIO_PIN_10, "PC10 D2");
    test_pin(GPIOC, GPIO_PIN_11, "PC11 D3");
    test_pin(GPIOB, GPIO_PIN_8,  "PB8  D4");
    test_pin(GPIOB, GPIO_PIN_9,  "PB9  D5");
    test_pin(GPIOC, GPIO_PIN_6,  "PC6  D6");
    test_pin(GPIOC, GPIO_PIN_7,  "PC7  D7");
    test_pin(GPIOC, GPIO_PIN_12, "PC12 CLK");
    test_pin(GPIOD, GPIO_PIN_2,  "PD2  CMD");
    SEGGER_RTT_printf(0, "MMC pin test done\r\n");
}

/* USER CODE END 4 */

 /* MPU Configuration */

void MPU_Config(void)
{
  MPU_Region_InitTypeDef MPU_InitStruct = {0};

  /* Disables the MPU */
  HAL_MPU_Disable();

  /** Initializes and configures the Region and the memory to be protected
  */
  MPU_InitStruct.Enable = MPU_REGION_ENABLE;
  MPU_InitStruct.Number = MPU_REGION_NUMBER0;
  MPU_InitStruct.BaseAddress = 0x0;
  MPU_InitStruct.Size = MPU_REGION_SIZE_4GB;
  MPU_InitStruct.SubRegionDisable = 0x87;
  MPU_InitStruct.TypeExtField = MPU_TEX_LEVEL0;
  MPU_InitStruct.AccessPermission = MPU_REGION_NO_ACCESS;
  MPU_InitStruct.DisableExec = MPU_INSTRUCTION_ACCESS_DISABLE;
  MPU_InitStruct.IsShareable = MPU_ACCESS_SHAREABLE;
  MPU_InitStruct.IsCacheable = MPU_ACCESS_NOT_CACHEABLE;
  MPU_InitStruct.IsBufferable = MPU_ACCESS_NOT_BUFFERABLE;

  HAL_MPU_ConfigRegion(&MPU_InitStruct);
  /* Enables the MPU */
  HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);

}

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
