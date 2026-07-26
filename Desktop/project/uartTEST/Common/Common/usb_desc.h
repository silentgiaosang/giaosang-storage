/********************************** (C) COPYRIGHT *******************************
* File Name          : usb_desc.h
* Description        : USB descriptors for oscilloscope (USB 3.0 SS + HS fallback)
*******************************************************************************/
#ifndef __USB_OSC_DESC_H
#define __USB_OSC_DESC_H

#ifdef __cplusplus
extern "C" {
#endif
#include "ch32h417.h"

/* USB Device Info */
#define USB_OSC_VID                  0x1A86    /* WCH */
#define USB_OSC_PID                  0x5539    /* Oscilloscope */
#define USB_OSC30_PID                0x5539

/* EP0 size */
#define DEF_USBD_UEP0_SIZE           64

/* HS (USB 2.0 High Speed) EP sizes */
#define DEF_USBD_HS_PACK_SIZE        512
#define DEF_USB_EP1_HS_SIZE          DEF_USBD_HS_PACK_SIZE

/* FS (USB 2.0 Full Speed) EP sizes */
#define DEF_USBD_FS_PACK_SIZE        64
#define DEF_USB_EP1_FS_SIZE          DEF_USBD_FS_PACK_SIZE

/* SS (USB 3.0 SuperSpeed) EP sizes */
#define DEF_USB_EP1_SS_SIZE          1024

/* SS burst / chain config */
#define DEF_ENDP1_OUT_BURST_LEVEL     16
#define DEF_ENDP1_IN_BURST_LEVEL      16
#define DEF_ENDP1_OUT_BUFF_SIZE       2
#define DEF_ENDP1_IN_BUFF_SIZE        2

/* USB3 BOS descriptor */
#define DEF_USBSSD_BOS_DESC_LEN      22

/* Descriptor length macros */
#define DEF_USBSSD_DEVICE_DESC_LEN   18
#define DEF_USBSSD_CONFIG_DESC_LEN   50
#define DEF_USBSSD_LANG_DESC_LEN     4
#define DEF_USBSSD_MANU_DESC_LEN     32
#define DEF_USBSSD_PROD_DESC_LEN     32
#define DEF_USBSSD_SN_DESC_LEN       12

#define DEF_USBD_DEVICE_DESC_LEN     18
#define DEF_USBD_CONFIG_HS_DESC_LEN  41
#define DEF_USBD_CONFIG_FS_DESC_LEN  41
#define DEF_USBD_LANG_DESC_LEN       4
#define DEF_USBD_MANU_DESC_LEN       32
#define DEF_USBD_PROD_DESC_LEN       32
#define DEF_USBD_SN_DESC_LEN         12

/* SS descriptors */
extern const uint8_t SS_DeviceDescriptor[];
extern const uint8_t SS_ConfigDescriptor[];
extern const uint8_t MyBOSDesc_SS[];

/* HS/FS descriptors */
extern const uint8_t MyDevDescr[];
extern const uint8_t MyCfgDescr_HS[];
extern const uint8_t MyCfgDescr_FS[];

/* String descriptors (shared) */
extern const uint8_t MyLangDescr[];
extern const uint8_t MyManuInfo[];
extern const uint8_t MyProdInfo[];
extern const uint8_t MySerNumInfo[];

#ifdef __cplusplus
}
#endif

#endif /* __USB_OSC_DESC_H */
