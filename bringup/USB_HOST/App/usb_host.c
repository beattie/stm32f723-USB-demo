#include <stdio.h>
#include "usb_host.h"
#include "usbh_core.h"
#include "usbh_hid.h"
#include <string.h>

USBH_HandleTypeDef hUsbHostFS;
ApplicationTypeDef Appli_state = APPLICATION_IDLE;

uint8_t kbd_report[64];   /* latest received report (sized for NKRO) */
uint8_t kbd_report_new;
uint8_t kbd_report_len;

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

/* Override the weak callback fired by the HID middleware after each
 * successful interrupt IN transfer (URB_DONE on InPipe / EP 0x82).
 * pData holds the raw bytes; length is wMaxPacketSize of that pipe. */
void USBH_HID_EventCallback(USBH_HandleTypeDef *phost)
{
    if (phost->pActiveClass == NULL) return;
    HID_HandleTypeDef *hid = (HID_HandleTypeDef *)phost->pActiveClass->pData;
    if (hid == NULL || hid->length == 0 || hid->length > sizeof(kbd_report)) return;
    memcpy(kbd_report, hid->pData, hid->length);
    kbd_report_len = (uint8_t)hid->length;
    kbd_report_new = 1;
}

static void USBH_UserProcess(USBH_HandleTypeDef *phost, uint8_t id)
{
    switch (id) {
    case HOST_USER_CONNECTION:
        Appli_state = APPLICATION_START;
        printf("USB-A: device connected\r\n");
        break;
    case HOST_USER_DISCONNECTION:
        Appli_state = APPLICATION_DISCONNECT;
        kbd_report_new = 0;
        printf("USB-A: device disconnected\r\n");
        break;
    case HOST_USER_CLASS_ACTIVE:
        Appli_state = APPLICATION_READY;
        printf("USB-A: class active\r\n");
        break;
    case HOST_USER_SELECT_CONFIGURATION:
    default:
        break;
    }
}
