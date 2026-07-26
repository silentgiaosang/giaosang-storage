/********************************** (C) COPYRIGHT *******************************
* File Name          : usb_osc_it.c
* Description        : USB interrupt handlers for oscilloscope
*                     USBSS + USBHS, EP1 IN (waveform data), EP1 OUT (commands)
*******************************************************************************/
#include "ch32h417.h"
#include "ch32h417_usb.h"
#include "usb_desc.h"
#include "usbss_device.h"
#include "usbhs_device.h"
#include "usb_osc_it.h"
#include "string.h"

/* ================================================================
 *  External variables (defined in usbss_device.c / usbhs_device.c)
 * ================================================================ */
extern USBSS_Dev_Info_t USBSS_Dev_Info;
extern volatile uint8_t  USB_Enum_Status;
extern volatile uint8_t  USBSS_DevEnumStatus;
extern volatile uint8_t  USBHS_DevEnumStatus;
extern volatile uint8_t  USBSS_DevConfig;
extern volatile uint8_t  USBHS_DevConfig;

/* ================================================================
 *  Global variables for endpoint data transfer
 * ================================================================ */
const uint8_t    *pUSBSS_Descr;
const uint8_t    *pUSBHS_Descr;

/* Setup Request */
volatile uint8_t  USBSS_SetupReqCode;
volatile uint8_t  USBSS_SetupReqType;
volatile uint16_t USBSS_SetupReqValue;
volatile uint16_t USBSS_SetupReqIndex;
volatile uint16_t USBSS_SetupReqLen;

/* USB Device Status */
volatile uint8_t  USBSS_DevAddr;
volatile uint8_t  USBSS_DevSleepStatus;
volatile uint16_t USBSS_DevMaxPackLen;
volatile uint8_t  USBSS_DevSpeed;

/* USBHS EP0 buffer (shared with USBHS driver) */
__attribute__ ((aligned(4))) uint8_t USBHS_EP0_Buf[64];

/* ================================================================
 *  USBSS EP buffers (referenced by usbss_device.c endpoint init)
 * ================================================================ */
__attribute__ ((aligned(4))) uint8_t USBSS_EP0_Buf[512];

/* Minimal buffers for EP2/EP3 (not used by oscilloscope, but required
 * because usbss_device.c endpoint init references them) */
__attribute__ ((aligned(4))) uint8_t USBSS_EP1_Rx_Buf[1024 * 16 * 2];
__attribute__ ((aligned(4))) uint8_t USBSS_EP2_Rx_Buf[1024 * 16 * 2];
__attribute__ ((aligned(4))) uint8_t USBSS_EP3_Rx_Buf[1024 * 16 * 2];

/* ================================================================
 *  Oscilloscope data transfer buffers
 *  DMA-capable, aligned to 4 bytes
 * ================================================================ */

/* EP1 IN double-buffer for waveform data (1024 * 16 * 2 = 32768 bytes each) */
__attribute__ ((aligned(4))) uint8_t g_usb_ep1_tx_buf0[DEF_USB_EP1_SS_SIZE * DEF_ENDP1_IN_BURST_LEVEL];
__attribute__ ((aligned(4))) uint8_t g_usb_ep1_tx_buf1[DEF_USB_EP1_SS_SIZE * DEF_ENDP1_IN_BURST_LEVEL];

/* EP1 OUT buffer for PC commands (1024 * 16 = 16384 bytes) */
__attribute__ ((aligned(4))) uint8_t g_usb_ep1_rx_buf[DEF_USB_EP1_SS_SIZE * DEF_ENDP1_OUT_BURST_LEVEL];

/* Current TX buffer index (0 or 1) */
volatile uint8_t  g_usb_tx_buf_sel = 0;
/* Flags */
volatile uint8_t  g_usb_ep1_tx_ready = 0;   /* 1 = EP1 IN ready to accept new data */
volatile uint8_t  g_usb_cmd_ready = 0;       /* 1 = command received on EP1 OUT */
volatile uint16_t g_usb_cmd_len = 0;         /* Length of received command data */

/* ================================================================
 *  Helper: get endpoint register pointer
 * ================================================================ */
static USBSS_EP_TX_TypeDef* USBSS_GetTxEp(uint8_t ep_num)
{
    if (ep_num == 0) return NULL;
    return (USBSS_EP_TX_TypeDef*)(&USBSSD->EP1_TX + ((ep_num - 1) & 0x7F) * 2);
}

static USBSS_EP_RX_TypeDef* USBSS_GetRxEp(uint8_t ep_num)
{
    if (ep_num == 0) return NULL;
    return (USBSS_EP_RX_TypeDef*)(&USBSSD->EP1_RX + (ep_num - 1) * 2);
}

/* ================================================================
 *  USBSS_Endp_Clear_Feature / USBSS_Endp_Set_Feature
 * ================================================================ */
static uint8_t USBSS_Endp_Clear_Feature(uint8_t dir_endp)
{
    if ((dir_endp & 0x7F) > 15) return 0xFF;
    if (dir_endp & 0x80) {
        USBSS_EP_TX_TypeDef* ep = USBSS_GetTxEp(dir_endp & 0x7F);
        if (ep) ep->UEP_TX_CR = 0x02 | 0x04;  /* CLR | CHAIN_CLR */
    } else {
        USBSS_EP_RX_TypeDef* ep = USBSS_GetRxEp(dir_endp & 0x7F);
        if (ep) {
            ep->UEP_RX_CR |= 0x02 | 0x04;
            ep->UEP_RX_CHAIN_MAX_NUMP = DEF_ENDP1_OUT_BURST_LEVEL;
        }
    }
    return 0;
}

static uint8_t USBSS_Endp_Set_Feature(uint8_t dir_endp)
{
    if ((dir_endp & 0x7F) > 15) return 0xFF;
    if (dir_endp & 0x80) {
        USBSS_EP_TX_TypeDef* ep = USBSS_GetTxEp(dir_endp & 0x7F);
        if (ep) ep->UEP_TX_CR |= 0x08;  /* HALT */
    } else {
        USBSS_EP_RX_TypeDef* ep = USBSS_GetRxEp(dir_endp & 0x7F);
        if (ep) ep->UEP_RX_CR |= 0x08;
    }
    return 0;
}

/* ================================================================
 *  USBSS_HandleSetup — handle standard USB requests on EP0
 * ================================================================ */
static uint8_t USBSS_HandleSetup(void)
{
    uint8_t errflag = 0;
    uint16_t len = 0;

    USBSS_Dev_Info.set_devaddr = 0;
    USBSS_Dev_Info.set_isoch_delay = 0;

    USBSS_SetupReqType  = ((PUSB_SETUP_REQ)USBSS_EP0_Buf)->bRequestType;
    USBSS_SetupReqCode  = ((PUSB_SETUP_REQ)USBSS_EP0_Buf)->bRequest;
    USBSS_SetupReqLen   = ((PUSB_SETUP_REQ)USBSS_EP0_Buf)->wLength;
    USBSS_SetupReqValue = ((PUSB_SETUP_REQ)USBSS_EP0_Buf)->wValue;
    USBSS_SetupReqIndex = ((PUSB_SETUP_REQ)USBSS_EP0_Buf)->wIndex;

    if ((USBSS_SetupReqType & 0x60) != 0x00) {
        return 0xFF;  /* Non-standard request, stall */
    }

    switch (USBSS_SetupReqCode) {
    case USB_GET_DESCRIPTOR:
        switch ((uint8_t)(USBSS_SetupReqValue >> 8)) {
        case USB_DESCR_TYP_DEVICE:
            pUSBSS_Descr = SS_DeviceDescriptor;
            len = DEF_USBSSD_DEVICE_DESC_LEN;
            break;
        case USB_DESCR_TYP_CONFIG:
            pUSBSS_Descr = SS_ConfigDescriptor;
            len = DEF_USBSSD_CONFIG_DESC_LEN;
            break;
        case USB_DESCR_TYP_STRING:
            switch ((uint8_t)(USBSS_SetupReqValue & 0xFF)) {
            case 0: pUSBSS_Descr = MyLangDescr;  len = DEF_USBSSD_LANG_DESC_LEN; break;
            case 1: pUSBSS_Descr = MyManuInfo;   len = DEF_USBSSD_MANU_DESC_LEN; break;
            case 2: pUSBSS_Descr = MyProdInfo;   len = DEF_USBSSD_PROD_DESC_LEN; break;
            case 3: pUSBSS_Descr = MySerNumInfo; len = DEF_USBSSD_SN_DESC_LEN; break;
            default: errflag = 0xFF; break;
            }
            break;
        case USB_DESCR_TYP_BOS:
            pUSBSS_Descr = MyBOSDesc_SS;
            len = DEF_USBSSD_BOS_DESC_LEN;
            break;
        default:
            errflag = 0xFF;
            break;
        }
        if (USBSS_SetupReqLen > len) USBSS_SetupReqLen = len;
        len = (USBSS_SetupReqLen >= 512) ? 512 : USBSS_SetupReqLen;
        memcpy(USBSS_EP0_Buf, pUSBSS_Descr, len);
        pUSBSS_Descr += len;
        break;

    case USB_SET_ADDRESS:
        USBSS_Dev_Info.set_devaddr = 1;
        USBSS_Dev_Info.devaddr = (uint16_t)(USBSS_SetupReqValue & 0xFF);
        break;

    case USB_GET_CONFIGURATION:
        USBSS_EP0_Buf[0] = USBSS_DevConfig;
        if (USBSS_SetupReqLen > 1) USBSS_SetupReqLen = 1;
        break;

    case USB_SET_CONFIGURATION:
        USBSS_DevConfig = (uint8_t)(USBSS_SetupReqValue & 0xFF);
        if (USBSS_DevConfig && (USBSS_DevConfig != SS_ConfigDescriptor[5])) {
            USBSS_DevConfig = SS_ConfigDescriptor[5];
            errflag = 0xFF;
        } else if (USBSS_DevConfig == 0) {
            USBSS_DevEnumStatus = 0x00;
        } else {
            USBSS_DevEnumStatus = 0x01;
        }
        break;

    case USB_CLEAR_FEATURE:
        if ((USBSS_SetupReqType & 0x1F) == 0x02) {  /* Endpoint */
            if ((uint8_t)(USBSS_SetupReqValue & 0xFF) == 0x00) { /* ENDPOINT_HALT */
                USBSS_Endp_Clear_Feature((uint8_t)(USBSS_SetupReqIndex & 0xFF));
            }
        } else {
            errflag = 0xFF;
        }
        break;

    case USB_SET_FEATURE:
        if ((USBSS_SetupReqType & 0x1F) == 0x02) {
            if ((uint8_t)(USBSS_SetupReqValue & 0xFF) == 0x00) {
                USBSS_Endp_Set_Feature((uint8_t)(USBSS_SetupReqIndex & 0xFF));
            }
        } else {
            errflag = 0xFF;
        }
        break;

    case USB_GET_INTERFACE:
        USBSS_EP0_Buf[0] = 0x00;
        if (USBSS_SetupReqLen > 1) USBSS_SetupReqLen = 1;
        break;

    case USB_SET_INTERFACE:
        break;

    case USB_GET_STATUS:
        USBSS_EP0_Buf[0] = 0x00;
        USBSS_EP0_Buf[1] = 0x00;
        if (USBSS_SetupReqLen > 2) USBSS_SetupReqLen = 2;
        break;

    default:
        errflag = 0xFF;
        break;
    }

    return errflag;
}

/* ================================================================
 *  USBSS_IRQHandler
 * ================================================================ */
void USBSS_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void USBSS_IRQHandler(void)
{
    uint8_t  errflag;
    uint16_t len;
    uint32_t status = USBSSD->USB_STATUS;

    if ((status & 0x00020000) && !(status & 0x00040000)) {  /* UDIF_SETUP */
        errflag = USBSS_HandleSetup();

        if (errflag == 0xFF) {
            USBSSD->UEP0_TX_CTRL = 0x04;  /* STALL */
            USBSSD->UEP0_RX_CTRL = 0x04 | 0x08;  /* STALL | ERDY */
        } else {
            if (USBSS_SetupReqType & 0x80) {
                /* IN */
                len = (USBSS_SetupReqLen > 512) ? 512 : USBSS_SetupReqLen;
                USBSS_SetupReqLen -= len;
                USBSSD->UEP0_TX_CTRL = (0 << 16) | len;  /* DPH | len */
                USBSSD->UEP0_TX_CTRL |= 0x01;  /* ERDY */
            } else {
                /* OUT */
                USBSSD->UEP0_TX_CTRL = (0 << 16);  /* DPH */
                USBSSD->UEP0_TX_CTRL |= 0x01;
                USBSSD->UEP0_RX_CTRL = 0x01 | 0x02;  /* ERDY | ACK */
            }
        }
        USBSSD->USB_STATUS = 0x00020000;
    }
    else if (status & 0x00040000) {  /* UDIF_STATUS */
        USBSSD->USB_STATUS = 0x00040000;

        if (USBSS_Dev_Info.set_devaddr) {
            SET_Device_Address(USBSS_Dev_Info.devaddr, USBSSH);
            USBSS_Dev_Info.set_devaddr = 0;
        }
        USBSSD->UEP0_TX_CTRL = 0;
        USBSSD->UEP0_RX_CTRL = 0;
    }
    else if (status & 0x00010000) {  /* UIF_TRANSFER */
        uint8_t ep_num = (status & 0x00000700) >> 8;
        uint8_t ep_dir = (status & 0x00001000) ? 1 : 0;

        if (ep_dir) {
            /* =========== IN transfers =========== */
            switch (ep_num) {
            case 0:
                /* EP0 IN: send remaining descriptor data */
                if (USBSS_SetupReqLen > 0) {
                    len = (USBSS_SetupReqLen > 512) ? 512 : USBSS_SetupReqLen;
                    memcpy(USBSS_EP0_Buf, pUSBSS_Descr, len);
                    USBSS_SetupReqLen -= len;
                    pUSBSS_Descr += len;
                    USBSSD->UEP0_TX_CTRL = (0 << 16) | len;
                    USBSSD->UEP0_TX_CTRL |= 0x01;
                } else {
                    USBSSD->UEP0_TX_CTRL = (0 << 16);  /* ZLP */
                }
                break;

            case 1:
                /* EP1 IN: waveform data sent, toggle buffer */
                USBSSD->EP1_TX.UEP_TX_CHAIN_ST |= 0x02;  /* CHAIN_IF */
                g_usb_tx_buf_sel ^= 0x01;
                g_usb_ep1_tx_ready = 1;
                /* Prepare next buffer for DMA */
                if (g_usb_tx_buf_sel == 0) {
                    USBSSD->EP1_TX.UEP_TX_DMA = (uint32_t)g_usb_ep1_tx_buf0;
                } else {
                    USBSSD->EP1_TX.UEP_TX_DMA = (uint32_t)g_usb_ep1_tx_buf1;
                }
                USBSSD->EP1_TX.UEP_TX_CHAIN_LEN = DEF_USB_EP1_SS_SIZE;
                USBSSD->EP1_TX.UEP_TX_CHAIN_EXP_NUMP = DEF_ENDP1_IN_BURST_LEVEL;
                break;

            default:
                break;
            }
        } else {
            /* =========== OUT transfers =========== */
            switch (ep_num) {
            case 0:
                USBSSD->UEP0_RX_CTRL = 0x01 | 0x02;  /* ERDY | ACK */
                break;

            case 1:
                /* EP1 OUT: command received from PC */
                USBSSD->EP1_RX.UEP_RX_CHAIN_ST |= 0x02;  /* CHAIN_IF */
                g_usb_cmd_len = USBSSD->EP1_RX.UEP_RX_DMA_OFS;
                g_usb_cmd_ready = 1;
                /* Re-arm RX */
                USBSSD->EP1_RX.UEP_RX_DMA = (uint32_t)g_usb_ep1_rx_buf;
                USBSSD->EP1_RX.UEP_RX_CHAIN_MAX_NUMP = DEF_ENDP1_OUT_BURST_LEVEL;
                break;

            default:
                break;
            }
        }
    }
}

/* ================================================================
 *  USBSS_LINK_IRQHandler — delegates to USBSS driver
 * ================================================================ */
void USBSS_LINK_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void USBSS_LINK_IRQHandler(void)
{
    USBSS_LINK_Handle(USBSSH);
}

/* ================================================================
 *  USB Osc Init — initialize USB and endpoints
 * ================================================================ */
void USB_Osc_Init(void)
{
    /* Initialize USB link timer */
    USB_Timer_Init();

    /* Initialize USBSS device (handles USB 3.0 link layer) */
    USBSS_Device_Init(ENABLE);

    /* After USBSS_Device_Init, endpoints are configured.
     * EP1 IN: set up initial DMA buffer for waveform data */
    g_usb_tx_buf_sel = 0;
    g_usb_ep1_tx_ready = 1;
    g_usb_cmd_ready = 0;

    USBSSD->EP1_TX.UEP_TX_DMA = (uint32_t)g_usb_ep1_tx_buf0;
    USBSSD->EP1_TX.UEP_TX_CHAIN_LEN = DEF_USB_EP1_SS_SIZE;
    USBSSD->EP1_TX.UEP_TX_CHAIN_EXP_NUMP = DEF_ENDP1_IN_BURST_LEVEL;

    USBSSD->EP1_RX.UEP_RX_DMA = (uint32_t)g_usb_ep1_rx_buf;
    USBSSD->EP1_RX.UEP_RX_CHAIN_MAX_NUMP = DEF_ENDP1_OUT_BURST_LEVEL;
}

/* ================================================================
 *  USB Osc Send Data — queue waveform data for EP1 IN transfer
 *  Returns: 1 = success, 0 = busy
 * ================================================================ */
uint8_t USB_Osc_SendData(uint8_t* data, uint16_t len)
{
    if (!g_usb_ep1_tx_ready) return 0;
    if (!USBSS_DevEnumStatus) return 0;

    g_usb_ep1_tx_ready = 0;

    uint8_t* dst = (g_usb_tx_buf_sel == 0) ? g_usb_ep1_tx_buf0 : g_usb_ep1_tx_buf1;
    uint16_t copy_len = (len > DEF_USB_EP1_SS_SIZE) ? DEF_USB_EP1_SS_SIZE : len;
    memcpy(dst, data, copy_len);

    USBSSD->EP1_TX.UEP_TX_DMA = (uint32_t)dst;
    USBSSD->EP1_TX.UEP_TX_CHAIN_LEN = copy_len;
    USBSSD->EP1_TX.UEP_TX_CHAIN_EXP_NUMP = DEF_ENDP1_IN_BURST_LEVEL;
    USBSSD->EP1_TX.UEP_TX_CR = 0x01;  /* ERDY */

    return 1;
}

/* ================================================================
 *  USB Osc Cmd Available — check if PC sent a command
 * ================================================================ */
uint8_t USB_Osc_CmdAvailable(void)
{
    return g_usb_cmd_ready;
}

/* ================================================================
 *  USB Osc Get Cmd — get received command data
 * ================================================================ */
uint8_t* USB_Osc_GetCmdBuf(void)
{
    return g_usb_ep1_rx_buf;
}

uint16_t USB_Osc_GetCmdLen(void)
{
    return g_usb_cmd_len;
}

void USB_Osc_CmdProcessed(void)
{
    g_usb_cmd_ready = 0;
}
