/********************************** (C) COPYRIGHT *******************************
* File Name          : hardware_usb.c
* Description        : USB hardware initialization
*******************************************************************************/
#include "hardware_usb.h"
#include "usbss_device.h"
#include "usbhs_device.h"
#include "usb_osc_it.h"

/*********************************************************************
 * @fn      USB_Hardware_Init
 *
 * @brief   Initialize USB hardware: clocks, timer, USBSS device.
 *          Starts USB 3.0 link training. USBHS fallback is automatic
 *          if USB 3.0 negotiation fails.
 *
 * @return  none
 */
void USB_Hardware_Init(void)
{
    RCC_HB2PeriphClockCmd(RCC_HB2Periph_AFIO | RCC_HB2Periph_GPIOB, ENABLE);
    GPIO_PinRemapConfig(GPIO_Remap_SWJ_Disable, ENABLE);

    /* USB link timer (used for fallback timeout) */
    USB_Timer_Init();

    /* Start USB 3.0 device initialization */
    USBSS_Device_Init(ENABLE);
}
