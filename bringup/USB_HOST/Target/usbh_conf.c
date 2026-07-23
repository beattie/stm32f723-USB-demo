#include "usbh_core.h"
#include "main.h"

HCD_HandleTypeDef hhcd_USB_OTG_FS;

/* Clock and GPIO init called by HAL_HCD_Init */
void HAL_HCD_MspInit(HCD_HandleTypeDef *hcd)
{
    GPIO_InitTypeDef gpio = {0};
    RCC_PeriphCLKInitTypeDef clk = {0};

    clk.PeriphClockSelection = RCC_PERIPHCLK_CLK48;
    clk.Clk48ClockSelection  = RCC_CLK48SOURCE_PLL;  /* PLLQ = 48 MHz */
    HAL_RCCEx_PeriphCLKConfig(&clk);

    /* PA11 = DM, PA12 = DP — OTG_FS AF10 */
    __HAL_RCC_GPIOA_CLK_ENABLE();
    gpio.Pin       = GPIO_PIN_11 | GPIO_PIN_12;
    gpio.Mode      = GPIO_MODE_AF_PP;
    gpio.Pull      = GPIO_NOPULL;
    gpio.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = GPIO_AF10_OTG_FS;
    HAL_GPIO_Init(GPIOA, &gpio);

    __HAL_RCC_USB_OTG_FS_CLK_ENABLE();
    HAL_NVIC_SetPriority(OTG_FS_IRQn, 6, 0);  /* <5 allows SWD/DAP to preempt for RTT debug */
    HAL_NVIC_EnableIRQ(OTG_FS_IRQn);
}

void HAL_HCD_MspDeInit(HCD_HandleTypeDef *hcd)
{
    __HAL_RCC_USB_OTG_FS_CLK_DISABLE();
    HAL_GPIO_DeInit(GPIOA, GPIO_PIN_11 | GPIO_PIN_12);
    HAL_NVIC_DisableIRQ(OTG_FS_IRQn);
}

/* IRQ → middleware callbacks */
void HAL_HCD_SOF_Callback(HCD_HandleTypeDef *hcd)          { USBH_LL_IncTimer(hcd->pData); }
void HAL_HCD_Connect_Callback(HCD_HandleTypeDef *hcd)       { USBH_LL_Connect(hcd->pData); }
void HAL_HCD_Disconnect_Callback(HCD_HandleTypeDef *hcd)    { USBH_LL_Disconnect(hcd->pData); }
void HAL_HCD_PortEnabled_Callback(HCD_HandleTypeDef *hcd)   { USBH_LL_PortEnabled(hcd->pData); }
void HAL_HCD_PortDisabled_Callback(HCD_HandleTypeDef *hcd)  { USBH_LL_PortDisabled(hcd->pData); }
void HAL_HCD_HC_NotifyURBChange_Callback(HCD_HandleTypeDef *hcd, uint8_t chnum, HCD_URBStateTypeDef urb_state) { }

/* Middleware → HAL bridge */

USBH_StatusTypeDef USBH_LL_Init(USBH_HandleTypeDef *phost)
{
    hhcd_USB_OTG_FS.pData    = phost;
    phost->pData             = &hhcd_USB_OTG_FS;
    hhcd_USB_OTG_FS.Instance = USB_OTG_FS;
    hhcd_USB_OTG_FS.Init.Host_channels       = 12;
    hhcd_USB_OTG_FS.Init.speed               = USB_OTG_SPEED_FULL;
    hhcd_USB_OTG_FS.Init.dma_enable          = DISABLE;
    hhcd_USB_OTG_FS.Init.phy_itface          = HCD_PHY_EMBEDDED;
    hhcd_USB_OTG_FS.Init.Sof_enable          = DISABLE;
    hhcd_USB_OTG_FS.Init.vbus_sensing_enable = DISABLE;
    if (HAL_HCD_Init(&hhcd_USB_OTG_FS) != HAL_OK)
        Error_Handler();
    /* HAL_HCD_Init → USB_HostInit sets FHMOD (force host mode).
     * Required on v0.1: PA10 (OTG_FS_ID) pad is damaged/floating. */
    USBH_LL_SetTimer(phost, HAL_HCD_GetCurrentFrame(&hhcd_USB_OTG_FS));
    return USBH_OK;
}

USBH_StatusTypeDef USBH_LL_DeInit(USBH_HandleTypeDef *phost)
{
    HAL_HCD_DeInit(phost->pData);
    return USBH_OK;
}

USBH_StatusTypeDef USBH_LL_Start(USBH_HandleTypeDef *phost)
{
    HAL_HCD_Start(phost->pData);
    return USBH_OK;
}

USBH_StatusTypeDef USBH_LL_Stop(USBH_HandleTypeDef *phost)
{
    HAL_HCD_Stop(phost->pData);
    return USBH_OK;
}

USBH_StatusTypeDef USBH_LL_ResetPort(USBH_HandleTypeDef *phost)
{
    HAL_HCD_ResetPort(phost->pData);
    return USBH_OK;
}

USBH_StatusTypeDef USBH_LL_ClosePipe(USBH_HandleTypeDef *phost, uint8_t pipe)    { return USBH_OK; }
USBH_StatusTypeDef USBH_LL_ActivatePipe(USBH_HandleTypeDef *phost, uint8_t pipe) { return USBH_OK; }

USBH_SpeedTypeDef USBH_LL_GetSpeed(USBH_HandleTypeDef *phost)
{
    switch (HAL_HCD_GetCurrentSpeed(phost->pData)) {
        case 0:  return USBH_SPEED_HIGH;
        case 1:  return USBH_SPEED_FULL;
        case 2:  return USBH_SPEED_LOW;
        default: return USBH_SPEED_FULL;
    }
}

uint32_t USBH_LL_GetLastXferSize(USBH_HandleTypeDef *phost, uint8_t pipe)
{
    return HAL_HCD_HC_GetXferCount(phost->pData, pipe);
}

USBH_StatusTypeDef USBH_LL_OpenPipe(USBH_HandleTypeDef *phost, uint8_t pipe,
    uint8_t epnum, uint8_t dev_addr, uint8_t speed, uint8_t ep_type, uint16_t mps)
{
    HAL_HCD_HC_Init(phost->pData, pipe, epnum, dev_addr, speed, ep_type, mps);
    return USBH_OK;
}

USBH_StatusTypeDef USBH_LL_SubmitURB(USBH_HandleTypeDef *phost, uint8_t pipe,
    uint8_t direction, uint8_t ep_type, uint8_t token,
    uint8_t *pbuff, uint16_t length, uint8_t do_ping)
{
    HAL_HCD_HC_SubmitRequest(phost->pData, pipe, direction, ep_type, token, pbuff, length, do_ping);
    return USBH_OK;
}

USBH_URBStateTypeDef USBH_LL_GetURBState(USBH_HandleTypeDef *phost, uint8_t pipe)
{
    return (USBH_URBStateTypeDef)HAL_HCD_HC_GetURBState(phost->pData, pipe);
}

USBH_StatusTypeDef USBH_LL_DriverVBUS(USBH_HandleTypeDef *phost, uint8_t state)
{
    /* PB0 = USBA_EN: TPS2065C active-high enable (v0.2 only; v0.1 has always-on polyfuse) */
    if (state)
        GPIOB->BSRR = GPIO_PIN_0;
    else
        GPIOB->BSRR = GPIO_PIN_0 << 16;
    return USBH_OK;
}

USBH_StatusTypeDef USBH_LL_SetToggle(USBH_HandleTypeDef *phost, uint8_t pipe, uint8_t toggle)
{
    HCD_HandleTypeDef *hcd = phost->pData;
    if (hcd->hc[pipe].ep_is_in)
        hcd->hc[pipe].toggle_in  = toggle;
    else
        hcd->hc[pipe].toggle_out = toggle;
    return USBH_OK;
}

uint8_t USBH_LL_GetToggle(USBH_HandleTypeDef *phost, uint8_t pipe)
{
    HCD_HandleTypeDef *hcd = phost->pData;
    return hcd->hc[pipe].ep_is_in ? hcd->hc[pipe].toggle_in : hcd->hc[pipe].toggle_out;
}

void USBH_Delay(uint32_t ms) { HAL_Delay(ms); }
