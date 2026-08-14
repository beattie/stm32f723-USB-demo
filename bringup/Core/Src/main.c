#include "main.h"
#include "gpio.h"
#include "sdmmc.h"
#include "spi.h"
#include "st7735.h"
#include "usb_host.h"
#include "usbh_hid.h"
#include "usb_device.h"
#include "usbd_hid.h"
#include <stdio.h>
#include <string.h>

void SystemClock_Config(void);
static void MPU_Config(void);
static void USART2_Init(void);
static void sd_pin_test(void);

UART_HandleTypeDef huart2;

/* v0.2 LED chase: PE9=BLUE, PE11=GREEN, PE13=YELLOW, PB4=RGB_R, PB5=RGB_G, PB6=RGB_B */
static const struct { GPIO_TypeDef *port; uint16_t pin; const char *name; } leds[] = {
    { GPIOE, GPIO_PIN_9,  "PE9  BLUE"  },
    { GPIOE, GPIO_PIN_11, "PE11 GREEN" },
    { GPIOE, GPIO_PIN_13, "PE13 YELLOW"},
    { GPIOB, GPIO_PIN_4,  "PB4  RGB_R" },
    { GPIOB, GPIO_PIN_5,  "PB5  RGB_G" },
    { GPIOB, GPIO_PIN_6,  "PB6  RGB_B" },
};
#define NUM_LEDS (sizeof(leds)/sizeof(leds[0]))

int main(void)
{
    MPU_Config();
    HAL_Init();
    SystemClock_Config();

    MX_GPIO_Init();
    USART2_Init();
    setvbuf(stdout, NULL, _IONBF, 0);  /* disable stdio buffering — flush every printf immediately */
    printf("GPIO ok — SYSCLK=%lu PCLK1=%lu PCLK2=%lu Hz\r\n",
        HAL_RCC_GetSysClockFreq(), HAL_RCC_GetPCLK1Freq(), HAL_RCC_GetPCLK2Freq());
    MX_SPI1_Init();
    ST7735_Unselect();
    ST7735_Init();
    ST7735_FillScreen(ST7735_BLACK);
    ST7735_WriteString(0, 0, "STM32F723", Font_11x18, ST7735_WHITE, ST7735_BLACK);
    ST7735_WriteString(0, 20, "v0.2 OK", Font_7x10, ST7735_GREEN, ST7735_BLACK);
    printf("Display ok\r\n");

    MX_USB_HOST_Init();
    HAL_Delay(500);  /* let TPS2065C/device settle before first enumeration attempt */
    printf("USB-A host started — plug keyboard\r\n");

    MX_USB_DEVICE_Init();
    printf("USB-C device started (OTG_HS OTGPHYC) — plug to PC\r\n");

    sd_pin_test();

    MX_SDMMC1_SD_Init();
    {
        HAL_SD_CardInfoTypeDef info;
        HAL_SD_GetCardInfo(&hsd1, &info);
        HAL_SD_CardStateTypeDef cstate = HAL_SD_GetCardState(&hsd1);
        printf("SD state=%u err=%08lX cardstate=%u type=%u ver=%u blocks=%lu bsize=%lu\r\n",
            (unsigned)hsd1.State, hsd1.ErrorCode, (unsigned)cstate,
            (unsigned)info.CardType, (unsigned)info.CardVersion,
            info.BlockNbr, info.BlockSize);
        /* dump raw CSD from handle */
        printf("  RCA=%04lX CSD: %08lX %08lX %08lX %08lX\r\n",
            hsd1.SdCard.RelCardAdd,
            hsd1.CSD[0], hsd1.CSD[1], hsd1.CSD[2], hsd1.CSD[3]);
    }

    uint32_t disp_tick = 0;
    uint32_t disp_phase = 0;
    static const uint16_t colors[] = { ST7735_RED, ST7735_GREEN, ST7735_BLUE, ST7735_WHITE, ST7735_BLACK };
    uint32_t led_tick = 0;
    uint32_t led_phase = 0;
    uint32_t uart_tick = 0;
    uint32_t uart_count = 0;
    uint8_t rx_byte;

    while (1)
    {
        /* Display color cycle: new color every 2s */
        if (HAL_GetTick() - disp_tick >= 2000)
        {
            disp_tick = HAL_GetTick();
            ST7735_FillScreen(colors[disp_phase]);
            disp_phase = (disp_phase + 1) % 5;
        }

        /* LED chase at 250ms per phase */
        if (HAL_GetTick() - led_tick >= 250)
        {
            led_tick = HAL_GetTick();

            HAL_GPIO_WritePin(GPIOE, GPIO_PIN_9|GPIO_PIN_11|GPIO_PIN_13, GPIO_PIN_RESET);
            HAL_GPIO_WritePin(GPIOB, GPIO_PIN_4|GPIO_PIN_5|GPIO_PIN_6,   GPIO_PIN_RESET);

            HAL_GPIO_WritePin(leds[led_phase].port, leds[led_phase].pin, GPIO_PIN_SET);
            led_phase = (led_phase + 1) % NUM_LEDS;
        }

#ifdef UART_TICKS
        /* UART TX: send tick every second */
        if (HAL_GetTick() - uart_tick >= 1000)
        {
            uart_tick = HAL_GetTick();
            char buf[32];
            int len = snprintf(buf, sizeof(buf), "tick %lu\r\n", uart_count++);
            HAL_UART_Transmit(&huart2, (uint8_t *)buf, len, 10);
        }
#endif

        /* UART RX: echo any received byte */
        if (HAL_UART_Receive(&huart2, &rx_byte, 1, 0) == HAL_OK)
        {
            HAL_UART_Transmit(&huart2, &rx_byte, 1, 10);
        }

        /* USB-A host */
        MX_USB_HOST_Process();

        if (kbd_report_new)
        {
            kbd_report_new = 0;
            HID_KEYBD_Info_TypeDef *k = USBH_HID_GetKeybdInfo(&hUsbHostFS);
            if (k)
            {
                /* Build standard 8-byte boot keyboard HID report */
                uint8_t report[8];
                report[0] = (k->lctrl  << 0) | (k->lshift << 1) | (k->lalt  << 2) | (k->lgui  << 3) |
                            (k->rctrl  << 4) | (k->rshift << 5) | (k->ralt  << 6) | (k->rgui  << 7);
                report[1] = 0x00;
                memcpy(&report[2], k->keys, 6);

                USBD_HID_SendReport(&hUsbDeviceHS, report, sizeof(report));

                uint8_t ascii = USBH_HID_GetASCIICode(k);
                if (ascii)
                    printf("KEY: '%c' (0x%02X)\r\n", ascii, ascii);
                else if (k->keys[0])
                    printf("KEY: keycode=0x%02X\r\n", k->keys[0]);
            }
        }
    }
}

/* Step 2: HSE 25 MHz → PLL → SYSCLK=216 MHz, APB1=54 MHz, APB2=108 MHz, PLLQ=48 MHz */
void SystemClock_Config(void)
{
    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

    RCC_OscInitTypeDef osc = {0};
    osc.OscillatorType      = RCC_OSCILLATORTYPE_HSE;
    osc.HSEState            = RCC_HSE_ON;
    osc.PLL.PLLState        = RCC_PLL_ON;
    osc.PLL.PLLSource       = RCC_PLLSOURCE_HSE;
    osc.PLL.PLLM            = 25;   /* 25 MHz / 25 = 1 MHz VCO input */
    osc.PLL.PLLN            = 432;  /* × 432 = 432 MHz VCO */
    osc.PLL.PLLP            = RCC_PLLP_DIV2;  /* 216 MHz SYSCLK */
    osc.PLL.PLLQ            = 9;    /* 48 MHz USB */
    if (HAL_RCC_OscConfig(&osc) != HAL_OK)
        Error_Handler();

    if (HAL_PWREx_EnableOverDrive() != HAL_OK)
        Error_Handler();

    RCC_ClkInitTypeDef clk = {0};
    clk.ClockType      = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
                         RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
    clk.AHBCLKDivider  = RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider = RCC_HCLK_DIV4;
    clk.APB2CLKDivider = RCC_HCLK_DIV2;
    if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_7) != HAL_OK)
        Error_Handler();
}

/* MPU: mark 0x00000000–0xFFFFFFFF no-access with sub-region exceptions for
 * SRAM and Flash, preventing speculative accesses to unmapped regions. */
static void MPU_Config(void)
{
    MPU_Region_InitTypeDef r = {0};
    HAL_MPU_Disable();
    r.Enable            = MPU_REGION_ENABLE;
    r.Number            = MPU_REGION_NUMBER0;
    r.BaseAddress       = 0x0;
    r.Size              = MPU_REGION_SIZE_4GB;
    r.SubRegionDisable  = 0x87;
    r.TypeExtField      = MPU_TEX_LEVEL0;
    r.AccessPermission  = MPU_REGION_NO_ACCESS;
    r.DisableExec       = MPU_INSTRUCTION_ACCESS_DISABLE;
    r.IsShareable       = MPU_ACCESS_SHAREABLE;
    r.IsCacheable       = MPU_ACCESS_NOT_CACHEABLE;
    r.IsBufferable      = MPU_ACCESS_NOT_BUFFERABLE;
    HAL_MPU_ConfigRegion(&r);
    HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);
}


static void test_pin(GPIO_TypeDef *port, uint16_t pin, const char *name)
{
    GPIO_InitTypeDef g = {0};
    g.Pin = pin; g.Mode = GPIO_MODE_INPUT; g.Speed = GPIO_SPEED_FREQ_LOW;

    g.Pull = GPIO_PULLUP;   HAL_GPIO_Init(port, &g); HAL_Delay(1);
    int hi = HAL_GPIO_ReadPin(port, pin);
    g.Pull = GPIO_PULLDOWN; HAL_GPIO_Init(port, &g); HAL_Delay(1);
    int lo = HAL_GPIO_ReadPin(port, pin);

    const char *result = (hi == 1 && lo == 0) ? "OK (floating)" :
                         (hi == 1 && lo == 1) ? "STUCK HIGH"    :
                         (hi == 0 && lo == 0) ? "STUCK LOW"     : "?";
    printf("  %-10s hi=%d lo=%d  %s\r\n", name, hi, lo, result);
}

static void sd_pin_test(void)
{
    __HAL_RCC_GPIOC_CLK_ENABLE();
    printf("SD pin test (no card):\r\n");
    test_pin(GPIOC, GPIO_PIN_8,  "PC8  D0");
    test_pin(GPIOC, GPIO_PIN_9,  "PC9  D1");
    test_pin(GPIOC, GPIO_PIN_10, "PC10 D2");
    test_pin(GPIOC, GPIO_PIN_11, "PC11 D3");
    test_pin(GPIOC, GPIO_PIN_12, "PC12 CLK");
    test_pin(GPIOD, GPIO_PIN_2,  "PD2  CMD");
    test_pin(GPIOE, GPIO_PIN_3,  "PE3  CDET");
    printf("SD pin test done\r\n");
}

/* USART2: PA2=TX (AF7), PA3=RX (AF7), 115200 8N1 — v0.2 debug header */
static void USART2_Init(void)
{
    __HAL_RCC_USART2_CLK_ENABLE();

    GPIO_InitTypeDef g = {0};
    g.Pin       = GPIO_PIN_2 | GPIO_PIN_3;
    g.Mode      = GPIO_MODE_AF_PP;
    g.Pull      = GPIO_NOPULL;
    g.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
    g.Alternate = GPIO_AF7_USART2;
    HAL_GPIO_Init(GPIOA, &g);

    huart2.Instance          = USART2;
    huart2.Init.BaudRate     = 115200;
    huart2.Init.WordLength   = UART_WORDLENGTH_8B;
    huart2.Init.StopBits     = UART_STOPBITS_1;
    huart2.Init.Parity       = UART_PARITY_NONE;
    huart2.Init.Mode         = UART_MODE_TX_RX;
    huart2.Init.HwFlowCtl   = UART_HWCONTROL_NONE;
    huart2.Init.OverSampling = UART_OVERSAMPLING_16;
    if (HAL_UART_Init(&huart2) != HAL_OK)
        Error_Handler();

    /* Direct register smoke test — confirms PA2 TX path before stdio init */
    const char *hw = "\r\nUART2 HW OK\r\n";
    for (const char *p = hw; *p; p++) {
        while (!(USART2->ISR & USART_ISR_TXE));
        USART2->TDR = (uint8_t)*p;
    }
}

void Error_Handler(void)
{
    __disable_irq();
    while (1) { }
}
