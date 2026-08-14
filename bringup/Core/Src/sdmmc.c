/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    sdmmc.c
  * @brief   This file provides code for the configuration
  *          of the SDMMC instances.
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
#include "sdmmc.h"

/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

SD_HandleTypeDef hsd1;

/* SDMMC1 SD card init — 4-bit mode, CLK48 source */
void MX_SDMMC1_SD_Init(void)
{
    hsd1.Instance                 = SDMMC1;
    hsd1.Init.ClockEdge           = SDMMC_CLOCK_EDGE_RISING;
    hsd1.Init.ClockBypass         = SDMMC_CLOCK_BYPASS_DISABLE;
    hsd1.Init.ClockPowerSave      = SDMMC_CLOCK_POWER_SAVE_DISABLE;
    hsd1.Init.BusWide             = SDMMC_BUS_WIDE_1B;
    hsd1.Init.HardwareFlowControl = SDMMC_HARDWARE_FLOW_CONTROL_DISABLE;
    hsd1.Init.ClockDiv            = 118; /* 48 MHz / (118+2) = 400 kHz init clock */
    if (HAL_SD_Init(&hsd1) != HAL_OK)
        return;  /* no card or init failed — non-fatal */

    HAL_SD_ConfigWideBusOperation(&hsd1, SDMMC_BUS_WIDE_4B);  /* non-fatal if fails */
}

void HAL_SD_MspInit(SD_HandleTypeDef *hsd)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};

    if (hsd->Instance != SDMMC1) return;

    PeriphClkInitStruct.PeriphClockSelection  = RCC_PERIPHCLK_SDMMC1 | RCC_PERIPHCLK_CLK48;
    PeriphClkInitStruct.Clk48ClockSelection   = RCC_CLK48SOURCE_PLL;
    PeriphClkInitStruct.Sdmmc1ClockSelection  = RCC_SDMMC1CLKSOURCE_CLK48;
    if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK)
        Error_Handler();

    __HAL_RCC_SDMMC1_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();

    /* PC8=D0, PC9=D1, PC10=D2, PC11=D3, PC12=CLK */
    GPIO_InitStruct.Pin       = GPIO_PIN_8|GPIO_PIN_9|GPIO_PIN_10|GPIO_PIN_11|GPIO_PIN_12;
    GPIO_InitStruct.Mode      = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull      = GPIO_NOPULL;
    GPIO_InitStruct.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF12_SDMMC1;
    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

    /* PD2=CMD */
    GPIO_InitStruct.Pin = GPIO_PIN_2;
    HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);
}

void HAL_SD_MspDeInit(SD_HandleTypeDef *hsd)
{
    if (hsd->Instance != SDMMC1) return;
    __HAL_RCC_SDMMC1_CLK_DISABLE();
    HAL_GPIO_DeInit(GPIOC, GPIO_PIN_8|GPIO_PIN_9|GPIO_PIN_10|GPIO_PIN_11|GPIO_PIN_12);
    HAL_GPIO_DeInit(GPIOD, GPIO_PIN_2);
}

/* USER CODE BEGIN 1 */

/* USER CODE END 1 */

