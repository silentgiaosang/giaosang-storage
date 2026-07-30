/**
 * @file butterfilter.c
 * @brief ButterFilter 设备串口通信驱动实现
 * 
 * 提供通过串口与 ButterFilter 数字滤波器通信的接口，
 * 支持发送滤波器配置命令、幅频响应数据以及读取版本号。
 */

#include "butterfilter.h"
#include <string.h>
#include <stdio.h>

/* 静态变量：保存 UART 句柄，用于后续串口发送操作 */
static UART_HandleTypeDef *bf_huart = NULL;

/**
 * @brief 初始化 ButterFilter 通信驱动
 * @param huart 指向已初始化 UART 句柄的指针（通常由 CubeMX 生成）
 */
void BF_Init(UART_HandleTypeDef *huart)
{
    bf_huart = huart;   // 保存句柄供后续发送使用
}

/**
 * @brief 发送原始二进制数据到串口
 * @param data 待发送数据的字节缓冲区
 * @param len  数据长度（字节）
 */
void BF_SendRawData(uint8_t *data, uint16_t len)
{
    if (bf_huart == NULL) return;                      // 未初始化则直接返回
    HAL_UART_Transmit(bf_huart, data, len, 100);       // 阻塞发送，超时 100ms
}

/**
 * @brief 发送以 '\0' 结尾的字符串到串口（不自动添加换行符）
 * @param str 字符串指针（调用者需保证字符串已包含所需的换行符）
 */
void BF_SendString(const char *str)
{
    uint16_t len = strlen(str);                        // 计算字符串长度
    BF_SendRawData((uint8_t*)str, len);                // 通过原始数据接口发送
}

/**
 * @brief 将滤波器类型枚举转换为协议字符串
 * @param type 滤波器类型枚举值
 * @return 对应的协议字符串（如 "lowpass"）
 */
static const char* filter_type_to_string(BF_FilterType type)
{
    switch(type)
    {
        case BF_LOWPASS:  return "lowpass";
        case BF_HIGHPASS: return "highpass";
        case BF_BANDPASS: return "bandpass";
        case BF_BANDSTOP: return "bandstop";
        default:          return "lowpass";           // 默认低通
    }
}

/**
 * @brief 发送 setfilter 命令，配置滤波器参数
 * @param type    滤波器类型（低通/高通/带通/带阻）
 * @param freq_l  低截止频率（Hz），对于低通/高通为截止频率，带通/带阻为左侧频率
 * @param freq_h  高截止频率（Hz），低通/高通时忽略，带通/带阻时使用
 * @param multi   阶数倍数（2 的幂，范围 1~32）
 * @param div     分频系数（2 的幂，范围 1~16384）
 * @note 命令格式：setfilter <type> <freq_l> <freq_h> <multi> <div>\n
 */
void BF_SendSetFilter(BF_FilterType type, int freq_l, int freq_h, int multi, int div)
{
    char cmd[100];
    const char *type_str = filter_type_to_string(type);
    snprintf(cmd, sizeof(cmd), "setfilter %s %d %d %d %d\n", type_str, freq_l, freq_h, multi, div);
    BF_SendString(cmd);
}

/**
 * @brief 发送 setamp 命令及 1024 字节幅频响应数据
 * @param amp_data 指向 1024 字节数据的指针（每个字节 0~255，对应 -50dB ~ 10dB）
 * @param multi    阶数倍数（与 setfilter 含义相同）
 * @param div      分频系数（与 setfilter 含义相同）
 * @note 协议格式：先发送 "setamp <multi> <div>\n"，紧接着发送 1024 字节原始数据
 */
void BF_SendSetAmp(uint8_t *amp_data, int multi, int div)
{
    char cmd[50];
    snprintf(cmd, sizeof(cmd), "setamp %d %d\n", multi, div);
    BF_SendString(cmd);
    // 发送 1024 字节幅频数据（紧跟在命令后，无需额外换行）
    BF_SendRawData(amp_data, 1024);
}

/**
 * @brief 发送 rdversion 命令，请求设备返回版本字符串
 * @note 设备响应后需自行通过串口接收解析
 */
void BF_SendReadVersion(void)
{
    BF_SendString("rdversion\n");
}