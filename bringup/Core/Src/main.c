#include "main.h"
#include "sdmmc.h"
#include "spi.h"
#include "usb_device.h"
#include "usb_host.h"
#include "gpio.h"
#include "SEGGER_RTT.h"
#include "usbh_core.h"
#include "usbh_hid.h"
#include "usbh_hid_keybd.h"

void SystemClock_Config(void);
static void MPU_Config(void);
static void test_pin(GPIO_TypeDef *port, uint16_t pin, const char *name);

int main(void)
{
    SEGGER_RTT_printf(0, "Hello World\r\n");

    MPU_Config();
    HAL_Init();
    SystemClock_Config();

    MX_GPIO_Init();       SEGGER_RTT_printf(0, "GPIO ok\r\n");
    MX_SDMMC1_MMC_Init(); SEGGER_RTT_printf(0, "SDMMC ok\r\n");
    MX_SPI1_Init();       SEGGER_RTT_printf(0, "SPI ok\r\n");
    MX_USB_DEVICE_Init(); SEGGER_RTT_printf(0, "USB_DEVICE ok\r\n");
    MX_USB_HOST_Init();   SEGGER_RTT_printf(0, "USB_HOST ok\r\n");

    /* OTG_FS host register dump — verify host mode init */
    SEGGER_RTT_printf(0, "FS GUSBCFG=%08lX (FHMOD bit29 should=1)\r\n",  (unsigned long)USB_OTG_FS->GUSBCFG);
    SEGGER_RTT_printf(0, "FS GINTSTS=%08lX (CMOD bit0: 1=host 0=device)\r\n", (unsigned long)USB_OTG_FS->GINTSTS);
    SEGGER_RTT_printf(0, "FS HPRT   =%08lX (PCSTS bit0=port connect)\r\n",
        (unsigned long)*(volatile uint32_t *)(USB_OTG_FS_PERIPH_BASE + USB_OTG_HOST_PORT_BASE));

    /* OTG_HS device register dump — verify FS PHY + VBUS bypass init */
    SEGGER_RTT_printf(0, "HS GUSBCFG=%08lX (PHYSEL bit6 should=1)\r\n",  (unsigned long)USB_OTG_HS->GUSBCFG);
    SEGGER_RTT_printf(0, "HS GCCFG  =%08lX (PWRDWN bit16=1, VBDEN bit21=0)\r\n", (unsigned long)USB_OTG_HS->GCCFG);
    SEGGER_RTT_printf(0, "HS GOTGCTL=%08lX (BVALOEN bit6, BVALOVAL bit7 should=1)\r\n", (unsigned long)USB_OTG_HS->GOTGCTL);
    {
        USB_OTG_DeviceTypeDef *dev = (USB_OTG_DeviceTypeDef *)(USB_OTG_HS_PERIPH_BASE + USB_OTG_DEVICE_BASE);
        SEGGER_RTT_printf(0, "HS DCTL   =%08lX (SDIS bit1 should=0)\r\n",  (unsigned long)dev->DCTL);
        SEGGER_RTT_printf(0, "HS DCFG   =%08lX (DSPD bits1:0 should=11)\r\n", (unsigned long)dev->DCFG);
    }

    /* PB4 = TIM3_CH1 AF2: 1 MHz square wave to verify HSE/PLL on scope
     * APB1 = 48 MHz, TIM3 = APB1×2 = 96 MHz; PSC=0 ARR=95 → 1 MHz */
    __HAL_RCC_TIM3_CLK_ENABLE();
    {
        GPIO_InitTypeDef g = {0};
        g.Pin = GPIO_PIN_4; g.Mode = GPIO_MODE_AF_PP;
        g.Pull = GPIO_NOPULL; g.Speed = GPIO_SPEED_FREQ_HIGH;
        g.Alternate = GPIO_AF2_TIM3;
        HAL_GPIO_Init(GPIOB, &g);
    }
    TIM3->PSC = 0; TIM3->ARR = 95; TIM3->CCR1 = 47;
    TIM3->CCMR1 = (6 << TIM_CCMR1_OC1M_Pos);  /* PWM mode 1 */
    TIM3->CCER  = TIM_CCER_CC1E;
    TIM3->EGR   = TIM_EGR_UG;
    TIM3->CR1   = TIM_CR1_CEN;
    SEGGER_RTT_printf(0, "TIM3 CH1 1MHz on PB4\r\n");

    uint32_t led_tick = 0, led_phase = 0, log_tick = 0;
    uint32_t cnt_done = 0, cnt_notready = 0, cnt_idle = 0, cnt_other = 0;

    while (1)
    {
        MX_USB_HOST_Process();

        if (Appli_state == APPLICATION_READY &&
            hUsbHostFS.pActiveClass != NULL && hUsbHostFS.pActiveClass->pData != NULL)
        {
            HID_HandleTypeDef *hid = (HID_HandleTypeDef *)hUsbHostFS.pActiveClass->pData;
            switch (USBH_LL_GetURBState(&hUsbHostFS, hid->InPipe)) {
                case USBH_URB_DONE:     cnt_done++;     break;
                case USBH_URB_NOTREADY: cnt_notready++; break;
                case USBH_URB_IDLE:     cnt_idle++;     break;
                default:                cnt_other++;    break;
            }

            HID_KEYBD_Info_TypeDef *kb = USBH_HID_GetKeybdInfo(&hUsbHostFS);
            if (kb != NULL && kb->keys[0] != 0) {
                uint8_t ascii = USBH_HID_GetASCIICode(kb);
                if (ascii != 0)
                    SEGGER_RTT_printf(0, "KEY: '%c' (0x%02X)\r\n", ascii, kb->keys[0]);
                else
                    SEGGER_RTT_printf(0, "KEY: 0x%02X\r\n", kb->keys[0]);
            }
        }

        /* LED chase: PE0/PE1/PE2 at 250 ms per phase */
        if (HAL_GetTick() - led_tick >= 250) {
            led_tick = HAL_GetTick();
            HAL_GPIO_WritePin(GPIOE, GPIO_PIN_0|GPIO_PIN_1|GPIO_PIN_2, GPIO_PIN_RESET);
            if      (led_phase == 0) HAL_GPIO_WritePin(GPIOE, GPIO_PIN_0, GPIO_PIN_SET);
            else if (led_phase == 1) HAL_GPIO_WritePin(GPIOE, GPIO_PIN_1, GPIO_PIN_SET);
            else                     HAL_GPIO_WritePin(GPIOE, GPIO_PIN_2, GPIO_PIN_SET);
            led_phase = (led_phase + 1) % 3;
        }

        /* USB host status every 2 s */
        if (HAL_GetTick() - log_tick >= 2000) {
            log_tick = HAL_GetTick();
            if (hUsbHostFS.pActiveClass != NULL && hUsbHostFS.pActiveClass->pData != NULL) {
                HID_HandleTypeDef *hid = (HID_HandleTypeDef *)hUsbHostFS.pActiveClass->pData;
                SEGGER_RTT_printf(0, "USBH gState=%d Appli=%d | HID state=%d timer=%lu poll=%u | URB done=%lu notready=%lu idle=%lu other=%lu\r\n",
                    (int)hUsbHostFS.gState, (int)Appli_state,
                    (int)hid->state, (unsigned long)hUsbHostFS.Timer, (unsigned)hid->poll,
                    cnt_done, cnt_notready, cnt_idle, cnt_other);
            } else {
                SEGGER_RTT_printf(0, "USBH gState=%d Appli=%d | no class\r\n",
                    (int)hUsbHostFS.gState, (int)Appli_state);
            }
            cnt_done = cnt_notready = cnt_idle = cnt_other = 0;
        }
    }
}

/* 8 MHz HSE → PLL → SYSCLK=192 MHz, APB1=48 MHz, APB2=96 MHz, PLLQ=48 MHz (USB) */
void SystemClock_Config(void)
{
    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

    RCC_OscInitTypeDef osc = {0};
    osc.OscillatorType      = RCC_OSCILLATORTYPE_HSE;
    osc.HSEState            = RCC_HSE_ON;
    osc.PLL.PLLState        = RCC_PLL_ON;
    osc.PLL.PLLSource       = RCC_PLLSOURCE_HSE;
    osc.PLL.PLLM            = 4;    /* 8 MHz / 4 = 2 MHz VCO input */
    osc.PLL.PLLN            = 192;  /* × 192 = 384 MHz VCO */
    osc.PLL.PLLP            = RCC_PLLP_DIV2;  /* 192 MHz SYSCLK */
    osc.PLL.PLLQ            = 8;    /* 48 MHz USB */
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
    if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_6) != HAL_OK)
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

/* SD/MMC connector solder joint test: float each pin and check pull-up/pull-down response.
 * A healthy floating pin follows the pull; a stuck or shorted pin does not. */
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
    SEGGER_RTT_printf(0, "  %-12s hi=%d lo=%d  %s\r\n", name, hi, lo, result);
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

void Error_Handler(void)
{
    __disable_irq();
    while (1) { }
}
