#ifndef BUTTERFILTER_H
#define BUTTERFILTER_H

#include "main.h"
#include <stdint.h>

// �˲�������ö��
typedef enum {
    BF_LOWPASS  = 0,
    BF_HIGHPASS = 1,
    BF_BANDPASS = 2,
    BF_BANDSTOP = 3
} BF_FilterType;

// ��ʼ��
void BF_Init(UART_HandleTypeDef *huart);

// ���� setfilter ����
void BF_SendSetFilter(BF_FilterType type, int freq_l, int freq_h, int multi, int div);

// ���� setamp �����1024�ֽڷ�Ƶ���ݣ�
void BF_SendSetAmp(uint8_t *amp_data, int multi, int div);

// ���� rdversion ����
void BF_SendReadVersion(void);

// ����ԭʼ�ַ���
void BF_SendString(const char *str);

// ����ԭʼ����
void BF_SendRawData(uint8_t *data, uint16_t len);

 
#endif