/********************************** (C) COPYRIGHT *******************************
* File Name          : usb_desc.c
* Description        : USB descriptors for oscilloscope
*                     USB 3.0 SS + USB 2.0 HS fallback
*                     EP1 IN:  Bulk, waveform data (MCU → PC)
*                     EP1 OUT: Bulk, commands     (PC → MCU)
*******************************************************************************/
#include "ch32h417.h"
#include "usb_desc.h"

/* ================================================================
 *  USB 3.0 SuperSpeed Descriptors
 * ================================================================ */

/* USB 3.0 Device Descriptor */
const uint8_t SS_DeviceDescriptor[] =
{
    0x12,                   /* bLength */
    0x01,                   /* bDescriptorType: Device */
    0x00, 0x03,             /* bcdUSB 3.0 */
    0xFF,                   /* bDeviceClass: Vendor Specific */
    0x00,                   /* bDeviceSubClass */
    0x00,                   /* bDeviceProtocol */
    0x09,                   /* bMaxPacketSize0: 2^9 = 512 */
    (uint8_t)USB_OSC_VID,
    (uint8_t)(USB_OSC_VID >> 8),
    (uint8_t)USB_OSC30_PID,
    (uint8_t)(USB_OSC30_PID >> 8),
    0x01, 0x00,             /* bcdDevice 1.00 */
    0x01,                   /* iManufacturer */
    0x02,                   /* iProduct */
    0x03,                   /* iSerialNumber */
    0x01,                   /* bNumConfigurations */
};

/* USB 3.0 Configuration Descriptor + Interface + Endpoints */
const uint8_t SS_ConfigDescriptor[] =
{
    /* --- Configuration Descriptor --- */
    0x09,                   /* bLength */
    0x02,                   /* bDescriptorType: Configuration */
    0x32, 0x00,             /* wTotalLength = 50 */
    0x01,                   /* bNumInterfaces */
    0x01,                   /* bConfigurationValue */
    0x00,                   /* iConfiguration */
    0x80,                   /* bmAttributes: Bus Powered */
    0x64,                   /* bMaxPower: 200mA */

    /* --- Interface Descriptor --- */
    0x09,                   /* bLength */
    0x04,                   /* bDescriptorType: Interface */
    0x00,                   /* bInterfaceNumber */
    0x00,                   /* bAlternateSetting */
    0x02,                   /* bNumEndpoints */
    0xFF,                   /* bInterfaceClass: Vendor Specific */
    0xFF,                   /* bInterfaceSubClass */
    0xFF,                   /* bInterfaceProtocol */
    0x00,                   /* iInterface */

    /* --- EP1 IN (Bulk) Descriptor --- */
    0x07,                   /* bLength */
    0x05,                   /* bDescriptorType: Endpoint */
    0x81,                   /* bEndpointAddress: IN, EP1 */
    0x02,                   /* bmAttributes: Bulk */
    0x00, 0x04,             /* wMaxPacketSize = 1024 */
    0x00,                   /* bInterval */

    /* --- EP1 IN SuperSpeed Companion --- */
    0x06,                   /* bLength */
    0x30,                   /* bDescriptorType: SS Endpoint Companion */
    DEF_ENDP1_IN_BURST_LEVEL - 1,   /* bMaxBurst: 15 */
    0x00,                   /* bmAttributes */
    0x00, 0x00,             /* wBytesPerInterval */

    /* --- EP1 OUT (Bulk) Descriptor --- */
    0x07,                   /* bLength */
    0x05,                   /* bDescriptorType: Endpoint */
    0x01,                   /* bEndpointAddress: OUT, EP1 */
    0x02,                   /* bmAttributes: Bulk */
    0x00, 0x04,             /* wMaxPacketSize = 1024 */
    0x00,                   /* bInterval */

    /* --- EP1 OUT SuperSpeed Companion --- */
    0x06,                   /* bLength */
    0x30,                   /* bDescriptorType: SS Endpoint Companion */
    DEF_ENDP1_OUT_BURST_LEVEL - 1,  /* bMaxBurst: 15 */
    0x00,                   /* bmAttributes */
    0x00, 0x00,             /* wBytesPerInterval */
};

/* ================================================================
 *  USB 2.0 High Speed Descriptors (fallback)
 * ================================================================ */

/* USB 2.0 Device Descriptor */
const uint8_t MyDevDescr[] =
{
    0x12,                   /* bLength */
    0x01,                   /* bDescriptorType */
    0x00, 0x02,             /* bcdUSB 2.0 */
    0xFF,                   /* bDeviceClass */
    0x00,                   /* bDeviceSubClass */
    0x00,                   /* bDeviceProtocol */
    DEF_USBD_UEP0_SIZE,     /* bMaxPacketSize0 */
    (uint8_t)USB_OSC_VID,
    (uint8_t)(USB_OSC_VID >> 8),
    (uint8_t)USB_OSC_PID,
    (uint8_t)(USB_OSC_PID >> 8),
    0x01, 0x00,             /* bcdDevice */
    0x01,                   /* iManufacturer */
    0x02,                   /* iProduct */
    0x03,                   /* iSerialNumber */
    0x01,                   /* bNumConfigurations */
};

/* USB 2.0 HS Configuration Descriptor */
const uint8_t MyCfgDescr_HS[] =
{
    0x09,                   /* bLength */
    0x02,                   /* bDescriptorType */
    0x29, 0x00,             /* wTotalLength = 41 */
    0x01,                   /* bNumInterfaces */
    0x01,                   /* bConfigurationValue */
    0x00,                   /* iConfiguration */
    0x80,                   /* bmAttributes */
    0x64,                   /* bMaxPower: 200mA */

    /* Interface */
    0x09,                   /* bLength */
    0x04,                   /* bDescriptorType */
    0x00,                   /* bInterfaceNumber */
    0x00,                   /* bAlternateSetting */
    0x02,                   /* bNumEndpoints */
    0xFF,                   /* bInterfaceClass */
    0xFF,                   /* bInterfaceSubClass */
    0xFF,                   /* bInterfaceProtocol */
    0x00,                   /* iInterface */

    /* EP1 IN */
    0x07,                   /* bLength */
    0x05,                   /* bDescriptorType */
    0x81,                   /* bEndpointAddress */
    0x02,                   /* bmAttributes: Bulk */
    (uint8_t)DEF_USB_EP1_HS_SIZE,
    (uint8_t)(DEF_USB_EP1_HS_SIZE >> 8),  /* 512 */
    0x00,

    /* EP1 OUT */
    0x07,                   /* bLength */
    0x05,                   /* bDescriptorType */
    0x01,                   /* bEndpointAddress */
    0x02,                   /* bmAttributes: Bulk */
    (uint8_t)DEF_USB_EP1_HS_SIZE,
    (uint8_t)(DEF_USB_EP1_HS_SIZE >> 8),  /* 512 */
    0x00,
};

/* USB 2.0 FS Configuration Descriptor */
const uint8_t MyCfgDescr_FS[] =
{
    0x09,                   /* bLength */
    0x02,                   /* bDescriptorType */
    0x29, 0x00,             /* wTotalLength = 41 */
    0x01,                   /* bNumInterfaces */
    0x01,                   /* bConfigurationValue */
    0x00,                   /* iConfiguration */
    0x80,                   /* bmAttributes */
    0x64,                   /* bMaxPower: 200mA */

    /* Interface */
    0x09,                   /* bLength */
    0x04,                   /* bDescriptorType */
    0x00,                   /* bInterfaceNumber */
    0x00,                   /* bAlternateSetting */
    0x02,                   /* bNumEndpoints */
    0xFF,                   /* bInterfaceClass */
    0xFF,                   /* bInterfaceSubClass */
    0xFF,                   /* bInterfaceProtocol */
    0x00,                   /* iInterface */

    /* EP1 IN */
    0x07,                   /* bLength */
    0x05,                   /* bDescriptorType */
    0x81,                   /* bEndpointAddress */
    0x02,                   /* bmAttributes: Bulk */
    (uint8_t)DEF_USB_EP1_FS_SIZE,
    (uint8_t)(DEF_USB_EP1_FS_SIZE >> 8),  /* 64 */
    0x00,

    /* EP1 OUT */
    0x07,                   /* bLength */
    0x05,                   /* bDescriptorType */
    0x01,                   /* bEndpointAddress */
    0x02,                   /* bmAttributes: Bulk */
    (uint8_t)DEF_USB_EP1_FS_SIZE,
    (uint8_t)(DEF_USB_EP1_FS_SIZE >> 8),  /* 64 */
    0x00,
};

/* ================================================================
 *  BOS Descriptor (USB 3.0 required)
 * ================================================================ */
const uint8_t MyBOSDesc_SS[] =
{
    0x05,                   /* bLength */
    0x0F,                   /* bDescriptorType: BOS */
    0x16, 0x00,             /* wTotalLength = 22 */
    0x02,                   /* bNumDeviceCaps */

    /* USB 2.0 Extension */
    0x07,                   /* bLength */
    0x10,                   /* bDescriptorType: Device Capability */
    0x02,                   /* bDevCapabilityType: USB 2.0 Extension */
    0x02, 0x00, 0x00, 0x00, /* bmAttributes: LPM supported */

    /* SuperSpeed USB Device Capability */
    0x0A,                   /* bLength */
    0x10,                   /* bDescriptorType: Device Capability */
    0x03,                   /* bDevCapabilityType: SuperSpeed */
    0x00,                   /* bmAttributes */
    0x00, 0x0E,             /* wSpeedsSupported: FS+HS+SS */
    0x03,                   /* bFunctionalitySupport: SS */
    0x0A,                   /* bU1DevExitLat: 10us */
    0x07, 0x00,             /* wU2DevExitLat: ~700us */
};

/* ================================================================
 *  String Descriptors
 * ================================================================ */

const uint8_t MyLangDescr[] = { 0x04, 0x03, 0x09, 0x04 };

const uint8_t MyManuInfo[] =
{
    32, 0x03,
    'C',0,'H',0,'3',0,'2',0,'H',0,'4',0,'1',0,'7',0,' ',0,
    'O',0,'S',0,'C',0,' ',0,'2',0,'0',0,'2',0,'6',0,
};

const uint8_t MyProdInfo[] =
{
    32, 0x03,
    'U',0,'S',0,'B',0,'3',0,' ',0,
    'O',0,'s',0,'c',0,'i',0,'l',0,'l',0,'o',0,'s',0,'c',0,'o',0,'p',0,'e',0,
};

const uint8_t MySerNumInfo[] =
{
    12, 0x03,
    '0',0,'0',0,'0',0,'1',0,'A',0,
};
