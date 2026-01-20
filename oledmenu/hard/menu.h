/**
  ******************************************************************************
  * @file           : menu. h
  * @brief          :  多级菜单系统头文件
  ******************************************************************************
  */

#ifndef __MENU_H
#define __MENU_H

#include "stm32f4xx_hal.h"
#include "oled.h"

/* ========================== 配置宏定义 ========================== */
#define MENU_MAX_ITEMS      10          // 单级菜单最大选项数
#define MENU_MAX_DEPTH      5           // 菜单最大层级深度
#define MENU_TITLE_Y        0           // 菜单标题栏Y坐标（像素）
#define MENU_CONTENT_Y      16          // 菜单内容区起始Y坐标（像素）
#define MENU_ITEMS_PER_PAGE 4           // 每页显示的菜单项数量（调整为4以适应8x6字体）

/* ========================== 枚举定义 ========================== */

/**
 * @brief 菜单项类型枚举
 */
typedef enum
{
    MENU_TYPE_SUBMENU = 0,  // 子菜单类型
    MENU_TYPE_FUNCTION,     // 功能执行类型
    MENU_TYPE_VALUE         // 数值调节类型
} MenuType_e;

/* ========================== 结构体定义 ========================== */

/**
 * @brief 菜单项结构体
 */
typedef struct MenuItem_s
{
    const char *name;               // 菜单项显示名称
    MenuType_e type;                // 菜单项类型
    void (*function)(void);         // 功能回调函数
    struct MenuItem_s *sub_menu;    // 子菜单指针
    uint8_t sub_menu_count;         // 子菜单项数量
    int32_t *value_ptr;             // 数值指针
    int32_t value_min;              // 数值最小值
    int32_t value_max;              // 数值最大值
    int32_t value_step;             // 数值调节步长
} MenuItem_t;

/**
 * @brief 菜单控制结构体
 */
typedef struct
{
    MenuItem_t *menu_stack[MENU_MAX_DEPTH];  // 菜单栈
    uint8_t current_depth;                   // 当前菜单深度
    uint8_t select_index[MENU_MAX_DEPTH];    // 各层级选中索引
    uint8_t page_start[MENU_MAX_DEPTH];      // 各层级页面起始索引
    uint8_t is_editing;                      // 是否处于编辑数值状态
} MenuControl_t;

/* ========================== 函数声明 ========================== */
void Menu_Init(MenuItem_t *root_menu, uint8_t root_count);
void Menu_Display(void);
void Menu_KeyHandler(uint8_t key);
void Menu_Up(void);
void Menu_Down(void);
void Menu_Enter(void);
void Menu_Back(void);

#endif /* __MENU_H */