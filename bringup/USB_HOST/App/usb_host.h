#ifndef __USB_HOST_H__
#define __USB_HOST_H__

#include "stm32f7xx_hal.h"
#include "usbh_core.h"

typedef enum {
    APPLICATION_IDLE       = 0,
    APPLICATION_START,
    APPLICATION_READY,
    APPLICATION_DISCONNECT,
} ApplicationTypeDef;

extern USBH_HandleTypeDef hUsbHostFS;
extern ApplicationTypeDef Appli_state;

void MX_USB_HOST_Init(void);
void MX_USB_HOST_Process(void);

#endif /* __USB_HOST_H__ */
