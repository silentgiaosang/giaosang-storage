/********************************** (C) COPYRIGHT *******************************
 * HSADC 40Msps + USBSS Oscilloscope — PC0=HSADC_IN0
 * USB 3.0 (USBSS) Bulk transfer to PC
 *******************************************************************************/

#include "debug.h"
#include "hsadc_fft.h"
#include "usb_osc_it.h"
#include "usbss_device.h"
#include "hardware_usb.h"

/* ================================================================
 *  10-bit Packing: 4 samples → 5 bytes
 *  Little-endian: sample A (earliest) occupies bits [9:2] of byte0
 *  and bits [1:0] in byte4
 * ================================================================ */
static void pack_10bit(const uint16_t* samples, uint8_t* packed, int count)
{
    int di = 0;
    for (int i = 0; i < count; i += 4) {
        uint16_t a = samples[i]     & 0x3FF;
        uint16_t b = samples[i + 1] & 0x3FF;
        uint16_t c = samples[i + 2] & 0x3FF;
        uint16_t d = samples[i + 3] & 0x3FF;

        packed[di++] = (a >> 2) & 0xFF;           /* A[9:2] */
        packed[di++] = (b >> 2) & 0xFF;           /* B[9:2] */
        packed[di++] = (c >> 2) & 0xFF;           /* C[9:2] */
        packed[di++] = (d >> 2) & 0xFF;           /* D[9:2] */
        packed[di++] = ((a & 0x03) << 6) |
                       ((b & 0x03) << 4) |
                       ((c & 0x03) << 2) |
                        (d & 0x03);               /* A[1:0]|B[1:0]|C[1:0]|D[1:0] */
    }
}

/* ================================================================
 *  CRC16-CCITT (table-driven)
 * ================================================================ */
static uint16_t crc16_ccitt(const uint8_t* data, uint16_t len)
{
    uint16_t crc = 0xFFFF;
    for (uint16_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (uint8_t j = 0; j < 8; j++) {
            if (crc & 0x8000)
                crc = (crc << 1) ^ 0x1021;
            else
                crc <<= 1;
        }
    }
    return crc;
}

/* ================================================================
 *  Build protocol packet
 *  [Sync:2B][Seq:4B][Mode:1B][Len:2B][Payload:N][CRC16:2B]
 * ================================================================ */
static uint16_t build_packet(uint8_t* buf, uint32_t seq, uint8_t mode,
                              const uint8_t* payload, uint16_t payload_len)
{
    buf[0] = 0xAA;                    /* Sync */
    buf[1] = 0x55;
    buf[2] = (seq >> 0)  & 0xFF;     /* Seq (LE) */
    buf[3] = (seq >> 8)  & 0xFF;
    buf[4] = (seq >> 16) & 0xFF;
    buf[5] = (seq >> 24) & 0xFF;
    buf[6] = mode;                     /* 0=continuous, 1=triggered */
    buf[7] = (payload_len >> 0) & 0xFF;
    buf[8] = (payload_len >> 8) & 0xFF;

    uint16_t total = 9 + payload_len + 2;  /* header + payload + CRC */

    if (payload && payload_len > 0) {
        for (uint16_t i = 0; i < payload_len; i++)
            buf[9 + i] = payload[i];
    }

    uint16_t crc = crc16_ccitt(buf + 9, payload_len);
    buf[9 + payload_len]     = (crc >> 0) & 0xFF;
    buf[9 + payload_len + 1] = (crc >> 8) & 0xFF;

    return total;
}

/* ================================================================
 *  Command handling
 * ================================================================ */
static volatile uint8_t  g_op_mode = 0;        /* 0=continuous, 1=triggered */
static volatile uint8_t  g_trig_edge = 0;       /* 0=rising, 1=falling */
static volatile uint16_t g_trig_level = 512;    /* 10-bit ADC trigger level */

static void process_command(const uint8_t* cmd, uint16_t len)
{
    if (len < 5) return;

    uint8_t  type  = cmd[0];
    uint32_t value = cmd[1] | ((uint32_t)cmd[2] << 8) |
                     ((uint32_t)cmd[3] << 16) | ((uint32_t)cmd[4] << 24);

    switch (type) {
    case 0x01: g_op_mode = 0;  break;                /* continuous */
    case 0x02: g_op_mode = 1;  break;                /* triggered */
    case 0x03: g_trig_edge = (uint8_t)value;  break; /* edge */
    case 0x04: g_trig_level = (uint16_t)value; break; /* level */
    case 0x05: /* sample rate divider — TODO */ break;
    default: break;
    }
}

/* ================================================================
 *  main
 * ================================================================ */
int main(void)
{
    SystemAndCoreClockUpdate();
    Delay_Init();
    USART_Printf_Init(115200);
    printf("CH32H417 Oscilloscope USB3\r\n");
    Delay_Ms(100);

    /* Init USB hardware (starts USB 3.0 link training) */
    USB_Hardware_Init();
    printf("USBSS init started, waiting for enumeration...\r\n");

    /* Init HSADC at 40Msps (existing init uses FFT module) */
    HSADC_FFT_Init();
    printf("HSADC init done (CLK=%d Hz, %d Msps)\r\n",
           (int)(SystemCoreClock), (int)(HSADC_FFT_SAMPLE_RATE / 1000000));

    /* Wait for USB enumeration to complete */
    printf("Waiting for USB enumeration...\r\n");
    while (!USBSS_DevEnumStatus) {
        /* USB link training runs in ISR context */
        Delay_Ms(10);
    }
    printf("USB enumeration complete! Starting data stream...\r\n");

    /* Start HSADC continuous capture */
    HSADC_FFT_Start();

    /* Data transfer loop */
    uint32_t packet_seq = 0;
    uint32_t loop_cnt = 0;

    while (1)
    {
        /* ================================================================
         *  Poll HSADC DMA completion
         * ================================================================ */
        if (HSADC_GetFlagStatus(HSADC_FLAG_DMAEnd) != RESET)
        {
            HSADC_ClearFlag(HSADC_FLAG_DMAEnd);
            HSADC_ClearFlag(HSADC_FLAG_EOC);

            /* Pack 10-bit samples → 1280 bytes for 1024 samples */
            static uint8_t packed[1280];
            pack_10bit(g_hsadc_buf0, packed, HSADC_BUF_SIZE);

            /* Build protocol packet in EP1 TX buffer */
            uint8_t* tx_buf = (g_usb_tx_buf_sel == 0) ?
                              g_usb_ep1_tx_buf0 : g_usb_ep1_tx_buf1;

            uint16_t pkt_len = build_packet(tx_buf, packet_seq++,
                                            g_op_mode, packed, 1280);

            /* Send via EP1 IN */
            USB_Osc_SendData(tx_buf, pkt_len);

            /* Restart HSADC DMA */
            HSADC->ADDR0 = (uint32_t)g_hsadc_buf0;
            HSADC_ClearFlag(HSADC_FLAG_EOC);
            HSADC_ClearFlag(HSADC_FLAG_DMAEnd);
            HSADC_SoftwareStartConvCmd(ENABLE);
        }

        /* ================================================================
         *  Check for PC commands
         * ================================================================ */
        if (USB_Osc_CmdAvailable())
        {
            process_command(USB_Osc_GetCmdBuf(), USB_Osc_GetCmdLen());
            USB_Osc_CmdProcessed();
        }

        /* ================================================================
         *  Heartbeat log (every ~1 second)
         * ================================================================ */
        loop_cnt++;
        if (loop_cnt >= 50000) {
            loop_cnt = 0;
            printf("seq=%lu mode=%d edge=%d level=%u\r\n",
                   (unsigned long)packet_seq, g_op_mode,
                   g_trig_edge, (unsigned)g_trig_level);
        }
    }
}
