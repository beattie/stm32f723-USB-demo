#include "usb_host.h"
#include "usbh_core.h"
#include "usbh_hid.h"
#include "SEGGER_RTT.h"

USBH_HandleTypeDef hUsbHostFS;
ApplicationTypeDef Appli_state = APPLICATION_IDLE;

static void USBH_UserProcess(USBH_HandleTypeDef *phost, uint8_t id);

void MX_USB_HOST_Init(void)
{
    if (USBH_Init(&hUsbHostFS, USBH_UserProcess, HOST_FS) != USBH_OK)
        Error_Handler();
    if (USBH_RegisterClass(&hUsbHostFS, USBH_HID_CLASS) != USBH_OK)
        Error_Handler();
    if (USBH_Start(&hUsbHostFS) != USBH_OK)
        Error_Handler();
}

void MX_USB_HOST_Process(void)
{
    USBH_Process(&hUsbHostFS);
}

static void USBH_UserProcess(USBH_HandleTypeDef *phost, uint8_t id)
{
    switch (id) {
    case HOST_USER_CONNECTION:
        Appli_state = APPLICATION_START;
        SEGGER_RTT_printf(0, "USB-A: device connected\r\n");
        break;
    case HOST_USER_DISCONNECTION:
        Appli_state = APPLICATION_DISCONNECT;
        SEGGER_RTT_printf(0, "USB-A: device disconnected\r\n");
        break;
    case HOST_USER_CLASS_ACTIVE:
        Appli_state = APPLICATION_READY;
        SEGGER_RTT_printf(0, "USB-A: HID class active — keyboard ready\r\n");
        break;
    case HOST_USER_SELECT_CONFIGURATION:
    default:
        break;
    }
}
