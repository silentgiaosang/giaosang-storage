/********************************** (C) COPYRIGHT *******************************
* File Name          : usb_osc_it.h
* Description        : USB interrupt handler API for oscilloscope
*******************************************************************************/
#ifndef __USB_OSC_IT_H
#define __USB_OSC_IT_H

#include "ch32h417.h"

#ifdef __cplusplus
extern "C" {
#endif

/* EP1 TX double buffers */
extern __attribute__((aligned(4))) uint8_t g_usb_ep1_tx_buf0[];
extern __attribute__((aligned(4))) uint8_t g_usb_ep1_tx_buf1[];

/* EP1 TX buffer selector (0 or 1) */
extern volatile uint8_t g_usb_tx_buf_sel;

/* Flags */
extern volatile uint8_t g_usb_ep1_tx_ready;
extern volatile uint8_t g_usb_cmd_ready;
extern volatile uint16_t g_usb_cmd_len;

/* Init */
void USB_Osc_Init(void);

/* Data send (EP1 IN) — call from main loop */
uint8_t USB_Osc_SendData(uint8_t* data, uint16_t len);

/* Command receive (EP1 OUT) — poll in main loop */
uint8_t  USB_Osc_CmdAvailable(void);
uint8_t* USB_Osc_GetCmdBuf(void);
uint16_t USB_Osc_GetCmdLen(void);
void     USB_Osc_CmdProcessed(void);

#ifdef __cplusplus
}
#endif

#endif /* __USB_OSC_IT_H */
