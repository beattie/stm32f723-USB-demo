# OTG_HS Full-Speed Mode with 8 MHz Crystal

Yes, you can absolutely configure the OTG_HS peripheral to operate strictly in
Full-Speed (FS) mode using your 8 MHz crystal.

This is a highly effective hardware strategy for your v0.1 bring-up. By forcing
the OTG_HS controller into Full-Speed mode (12 Mbps), you completely bypass the
integrated High-Speed PHY and its strict 12/16/24 MHz crystal requirements.
Instead, the controller shifts its tracking and logic to use standard, stable
48 MHz clock routing supplied by your Main PLL, which can easily be generated
from your 8 MHz hardware.

Here is how to configure it to successfully pass CubeMX validation and run on
your board:

## 1. Fix the CubeMX Clocks First (The 12 MHz Bypass)

Because CubeMX enforces strict software checks, it will automatically throw a
clock configuration error if you select the OTG_HS peripheral while the input
frequency box says 8 MHz.

In the **Clock Configuration** tab, set the **Input Frequency (HSE value)** to
**12 MHz** just to clear the user interface restriction.

Set your Main PLL parameters explicitly to handle your actual physical 8 MHz
crystal:

| Parameter | Value |
|-----------|-------|
| M | 8 |
| N | 384 |
| Q | 8 |

Q=8 yields exactly 48 MHz on your real board, satisfying the target FS USB core
clock requirements.

## 2. Configure the OTG_HS Peripheral in Full-Speed Mode

1. Go to **Pinout & Configuration** → **Connectivity** → **USB_OTG_HS**.
2. Set the **Mode** dropdown to **Device_Only** or **Host_Only** depending on
   your design.
3. In the **Parameter Settings** block, locate the **Speed** parameter and
   change it from *High Speed* to **Full Speed 12 MBit/s**.
4. Ensure **Physical Interface (PHY)** is set to **Internal FS PHY**. Do not
   select "Internal HS PHY" — that triggers the broken 8 MHz initialization
   logic loop inside the transceiver.

## 3. Verify Physical Pins

When you configure OTG_HS in internal Full-Speed mode, the USB differential
pair routes through:

- **PB14** — USB_OTG_HS_DM
- **PB15** — USB_OTG_HS_DP

Make sure your USB-C connector is wired to PB14/PB15, not PA11/PA12 (which are
reserved for the standalone OTG_FS block).

Once you generate code under this configuration, the ST HAL drivers
(`stm32f7xx_hal_pcd.c`) will initialize the engine using `USB_OTG_SPEED_FULL`,
letting your host enumerate the board at 12 Mbps using your 8 MHz crystal.

## Caveats

The "set HSE to 12 MHz in CubeMX to bypass the check" trick is a hack — the
generated `SystemClock_Config` will have the wrong PLLM value for your actual
8 MHz crystal. Hand-correct it after code generation (or set HSE to 8 MHz and
fix CubeMX's complaints manually).

Also note: bypassing VBUS detection (GCCFG VBDEN=0) may be required on v0.1
where PB13 is NC, to prevent BSVLD=0 from blocking the D+ pull-up even when
using the internal FS PHY path (PHYSEL=1).

---

# Configuring OTG_HS as a USB HID Keyboard

To configure the OTG_HS interface (acting in Full-Speed mode on pins PB14/PB15)
as a USB HID Keyboard, set up the middleware parameters in STM32CubeMX. Once
generated, you only need to modify a small buffer array payload in your code to
start sending keystrokes to your computer.

## 1. CubeMX Middleware Configuration

1. Go to the **Pinout & Configuration** tab.
2. In the left sidebar, expand **Middleware and Software Packs** and click
   **USB_DEVICE**.
3. Change the **Class For HS IP** dropdown to
   **Human Interface Device Class (HID)**.
4. In the **Parameter Settings** tab below, ensure **HID Boot Protocol** is set
   to **Keyboard**.

> **Note:** CubeMX names the parameter "Class For HS IP" because you are
> technically using the High-Speed peripheral block, even though it is forced to
> operate at Full-Speed.

## 2. Verify Generated Descriptor

Generate your code and open the project. The ST HAL automatically configures the
correct USB HID descriptors for a keyboard based on your selection. Verify this
in the generated file:

- **File:** `USB_DEVICE/App/usbd_hid_if.c`
- Look for `HID_MOUSE_ReportDesc` — ST uses the "Mouse" naming convention in
  some older template strings, but the internal arrays are mapped for keyboards
  when toggled in CubeMX. You will see the standard 63-byte HID keyboard report
  descriptor.

## 3. Writing the Keystroke Code

A standard USB HID Keyboard report requires an 8-byte buffer:

| Byte | Description |
|------|-------------|
| 0 | Modifier keys (Ctrl, Shift, Alt, GUI/Windows) |
| 1 | Reserved — always `0x00` |
| 2–7 | Up to 6 simultaneous key codes (e.g. `0x04` = 'A') |

To send a keystroke, send the key-press report immediately followed by an empty
report to simulate key release.

Add this helper function to `main.c` inside the `/* USER CODE BEGIN 4 */` block:

```c
#include "usbd_hid.h"
extern USBD_HandleTypeDef hUsbDeviceHS;

void USB_Send_Key(uint8_t modifier, uint8_t keycode)
{
    uint8_t report_buffer[8] = {0};

    /* Key press */
    report_buffer[0] = modifier;
    report_buffer[2] = keycode;
    USBD_HID_SendReport(&hUsbDeviceHS, report_buffer, 8);
    HAL_Delay(20);

    /* Key release — crucial to prevent stuck keys */
    report_buffer[0] = 0;
    report_buffer[2] = 0;
    USBD_HID_SendReport(&hUsbDeviceHS, report_buffer, 8);
    HAL_Delay(20);
}
```

## 4. Test Loop in Main

Inside the `while(1)` loop in `main.c`, add a test routine that presses a
character every 5 seconds:

```c
/* USER CODE BEGIN WHILE */
while (1)
{
    /* Sends lowercase 'a' (HID code 0x04) */
    USB_Send_Key(0, 0x04);

    /* Sends uppercase 'A' by holding Left Shift (modifier 0x02) */
    /* USB_Send_Key(0x02, 0x04); */

    HAL_Delay(5000);
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
}
/* USER CODE END 3 */
```

## HID Key Code Reference

| Key | Code |
|-----|------|
| 'A'–'Z' | `0x04`–`0x1D` |
| '1'–'0' | `0x1E`–`0x27` |
| Return/Enter | `0x28` |
| Left Shift (modifier) | `0x02` |
| Left Ctrl (modifier) | `0x01` |

When you plug the v0.1 board into a USB port via PB14/PB15, it will enumerate
as a hardware keyboard and type characters directly into the active text editor.
