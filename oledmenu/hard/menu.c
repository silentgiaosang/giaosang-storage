/**
  ******************************************************************************
  * @file           : menu.c
  * @brief          : 多级菜单系统实现文件（适配波特律动OLED驱动）
  ******************************************************************************
  */

#include "menu.h"
#include "font.h"
#include <stdio.h>
#include <string.h>

/* ========================== 私有变量 ========================== */
static MenuControl_t g_menu_ctrl; // 菜单控制结构体

/* ========================== 公共函数实现 ========================== */

/**
 * @brief  菜单系统初始化
 */
void Menu_Init(MenuItem_t *root_menu, uint8_t root_count)
{
    // 清空菜单控制结构体
    memset(&g_menu_ctrl, 0, sizeof(MenuControl_t));
    
    // 设置根菜单
    g_menu_ctrl.menu_stack[0] = root_menu;
    g_menu_ctrl.current_depth = 0;
    g_menu_ctrl.select_index[0] = 0;
    g_menu_ctrl.page_start[0] = 0;
    g_menu_ctrl.is_editing = 0;
}

/**
 * @brief  菜单显示刷新函数
 */
void Menu_Display(void)
{
    // 开始新的一帧
    OLED_NewFrame();
    
    // 获取当前菜单层级信息
    MenuItem_t *current_menu = g_menu_ctrl.menu_stack[g_menu_ctrl.current_depth];
    uint8_t current_index = g_menu_ctrl.select_index[g_menu_ctrl.current_depth];
    uint8_t page_start = g_menu_ctrl.page_start[g_menu_ctrl.current_depth];
    
    // 显示标题栏（固定在第0页）
    if(g_menu_ctrl.current_depth == 0)
    {
        OLED_PrintASCIIString(0, MENU_TITLE_Y, "== Main Menu ==", &afont8x6, OLED_COLOR_NORMAL);
    }
    else
    {
        char title[22];
        snprintf(title, sizeof(title), "< %. 18s", current_menu[0].name);
        OLED_PrintASCIIString(0, MENU_TITLE_Y, title, &afont8x6, OLED_COLOR_NORMAL);
    }
    
    // 绘制标题栏分隔线
    OLED_DrawLine(0, MENU_TITLE_Y + 8, 127, MENU_TITLE_Y + 8, OLED_COLOR_NORMAL);
    
    // 获取当前菜单的子项数量
    uint8_t menu_count = 0;
    if(g_menu_ctrl.current_depth == 0)
    {
        while(current_menu[menu_count].name != NULL && menu_count < MENU_MAX_ITEMS)
        {
            menu_count++;
        }
    }
    else
    {
        MenuItem_t *parent_menu = g_menu_ctrl.menu_stack[g_menu_ctrl.current_depth - 1];
        uint8_t parent_index = g_menu_ctrl.select_index[g_menu_ctrl.current_depth - 1];
        menu_count = parent_menu[parent_index].sub_menu_count;
    }
    
    // 显示菜单项（每页显示MENU_ITEMS_PER_PAGE项）
    for(uint8_t i = 0; i < MENU_ITEMS_PER_PAGE && (page_start + i) < menu_count; i++)
    {
        uint8_t item_index = page_start + i;
        uint8_t y_pos = MENU_CONTENT_Y + i * 10; // 每行间隔10像素
        
        // 绘制选中标记
        if(item_index == current_index && ! g_menu_ctrl.is_editing)
        {
            OLED_PrintASCIIChar(0, y_pos, '>', &afont8x6, OLED_COLOR_NORMAL);
        }
        else
        {
            OLED_PrintASCIIChar(0, y_pos, ' ', &afont8x6, OLED_COLOR_NORMAL);
        }
        
        // 显示菜单项名称
        OLED_PrintASCIIString(8, y_pos, (char*)current_menu[item_index].name, &afont8x6, OLED_COLOR_NORMAL);
        
        // 如果是数值类型，显示当前数值
        if(current_menu[item_index].type == MENU_TYPE_VALUE)
        {
            char value_str[10];
            snprintf(value_str, sizeof(value_str), ":%d", (int)(*(current_menu[item_index].value_ptr)));
            OLED_PrintASCIIString(70, y_pos, value_str, &afont8x6, OLED_COLOR_NORMAL);
            
            // 编辑模式下显示闪烁标记
            if(item_index == current_index && g_menu_ctrl.is_editing)
            {
                OLED_PrintASCIIString(110, y_pos, "<*>", &afont8x6, OLED_COLOR_NORMAL);
            }
        }
        else if(current_menu[item_index].type == MENU_TYPE_SUBMENU)
        {
            // 子菜单显示箭头标记
            OLED_PrintASCIIString(110, y_pos, "->", &afont8x6, OLED_COLOR_NORMAL);
        }
    }
    
    // 显示滚动条提示
    if(menu_count > MENU_ITEMS_PER_PAGE)
    {
        char scroll_info[10];
        snprintf(scroll_info, sizeof(scroll_info), "%d/%d", current_index + 1, menu_count);
        OLED_PrintASCIIString(100, MENU_TITLE_Y, scroll_info, &afont8x6, OLED_COLOR_NORMAL);
    }
    
    // 显示到屏幕
    OLED_ShowFrame();
}

/**
 * @brief  菜单按键处理函数
 */
void Menu_KeyHandler(uint8_t key)
{
    switch(key)
    {
        case 1:   // 上键
            Menu_Up();
            break;
        case 2:  // 下键
            Menu_Down();
            break;
        case 3:  // 确认键
            Menu_Enter();
            break;
        case 4:  // 返回键
            Menu_Back();
            break;
        default:
            break;
    }
}

/**
 * @brief  菜单向上切换
 */
void Menu_Up(void)
{
    uint8_t depth = g_menu_ctrl.current_depth;
    MenuItem_t *current_menu = g_menu_ctrl.menu_stack[depth];
    
    // 计算当前菜单项数量
    uint8_t menu_count = 0;
    if(depth == 0)
    {
        while(current_menu[menu_count].name != NULL && menu_count < MENU_MAX_ITEMS)
        {
            menu_count++;
        }
    }
    else
    {
        MenuItem_t *parent_menu = g_menu_ctrl.menu_stack[depth - 1];
        uint8_t parent_index = g_menu_ctrl.select_index[depth - 1];
        menu_count = parent_menu[parent_index].sub_menu_count;
    }
    
    // 数值编辑模式
    if(g_menu_ctrl.is_editing)
    {
        uint8_t index = g_menu_ctrl.select_index[depth];
        MenuItem_t *item = &current_menu[index];
        if(item->type == MENU_TYPE_VALUE)
        {
            *(item->value_ptr) += item->value_step;
            if(*(item->value_ptr) > item->value_max)
            {
                *(item->value_ptr) = item->value_max;
            }
        }
        return;
    }
    
    // 菜单项切换
    if(g_menu_ctrl.select_index[depth] > 0)
    {
        g_menu_ctrl.select_index[depth]--;
        
        // 页面滚动处理
        if(g_menu_ctrl.select_index[depth] < g_menu_ctrl.page_start[depth])
        {
            g_menu_ctrl.page_start[depth]--;
        }
    }
    else
    {
        // 循环到末尾
        g_menu_ctrl.select_index[depth] = menu_count - 1;
        if(menu_count > MENU_ITEMS_PER_PAGE)
        {
            g_menu_ctrl.page_start[depth] = menu_count - MENU_ITEMS_PER_PAGE;
        }
    }
}

/**
 * @brief  菜单向下切换
 */
void Menu_Down(void)
{
    uint8_t depth = g_menu_ctrl.current_depth;
    MenuItem_t *current_menu = g_menu_ctrl.menu_stack[depth];
    
    // 计算当前菜单项数量
    uint8_t menu_count = 0;
    if(depth == 0)
    {
        while(current_menu[menu_count].name != NULL && menu_count < MENU_MAX_ITEMS)
        {
            menu_count++;
        }
    }
    else
    {
        MenuItem_t *parent_menu = g_menu_ctrl.menu_stack[depth - 1];
        uint8_t parent_index = g_menu_ctrl.select_index[depth - 1];
        menu_count = parent_menu[parent_index].sub_menu_count;
    }
    
    // 数值编辑模式
    if(g_menu_ctrl.is_editing)
    {
        uint8_t index = g_menu_ctrl.select_index[depth];
        MenuItem_t *item = &current_menu[index];
        if(item->type == MENU_TYPE_VALUE)
        {
            *(item->value_ptr) -= item->value_step;
            if(*(item->value_ptr) < item->value_min)
            {
                *(item->value_ptr) = item->value_min;
            }
        }
        return;
    }
    
    // 菜单项切换
    if(g_menu_ctrl.select_index[depth] < menu_count - 1)
    {
        g_menu_ctrl.select_index[depth]++;
        
        // 页面滚动处理
        if(g_menu_ctrl.select_index[depth] >= g_menu_ctrl.page_start[depth] + MENU_ITEMS_PER_PAGE)
        {
            g_menu_ctrl.page_start[depth]++;
        }
    }
    else
    {
        // 循环到开头
        g_menu_ctrl.select_index[depth] = 0;
        g_menu_ctrl.page_start[depth] = 0;
    }
}

/**
 * @brief  菜单确认操作
 */
void Menu_Enter(void)
{
    uint8_t depth = g_menu_ctrl.current_depth;
    MenuItem_t *current_menu = g_menu_ctrl. menu_stack[depth];
    uint8_t index = g_menu_ctrl.select_index[depth];
    MenuItem_t *item = &current_menu[index];
    
    // 数值编辑模式切换
    if(item->type == MENU_TYPE_VALUE)
    {
        g_menu_ctrl.is_editing = !g_menu_ctrl. is_editing;
        return;
    }
    
    // 进入子菜单
    if(item->type == MENU_TYPE_SUBMENU)
    {
        if(depth < MENU_MAX_DEPTH - 1 && item->sub_menu != NULL)
        {
            g_menu_ctrl.current_depth++;
            g_menu_ctrl. menu_stack[g_menu_ctrl.current_depth] = item->sub_menu;
            g_menu_ctrl.select_index[g_menu_ctrl.current_depth] = 0;
            g_menu_ctrl.page_start[g_menu_ctrl. current_depth] = 0;
        }
    }
    // 执行功能回调
    else if(item->type == MENU_TYPE_FUNCTION)
    {
        if(item->function != NULL)
        {
            item->function();
        }
    }
}

/**
 * @brief  菜单返回操作
 */
void Menu_Back(void)
{
    // 退出数值编辑模式
    if(g_menu_ctrl.is_editing)
    {
        g_menu_ctrl.is_editing = 0;
        return;
    }
    
    // 返回上级菜单
    if(g_menu_ctrl.current_depth > 0)
    {
        g_menu_ctrl.current_depth--;
    }
}