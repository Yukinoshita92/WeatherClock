#ifndef __FONT_H
#define __FONT_H

#include <stdint.h>

/**
 * @brief 字体文件
 * @note 注意这里vscode的报错，不是中文字体双引号报错 而是{}缺少表达式 在里面补0充当NULL判断空
 * @note 所有的中文字模文件font/page需要切换编码类型为gb2312 否则在keil中会报错
 */

// !"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\]^_`abcdefghijklmnopqrstuvwxyz{|}~

//创建一个结构体，存放字体结构体中指针Chinese的字符和对应的字模
typedef struct Chinese_font
{
	const char* name;
	const uint8_t *Cmodel;
}Chinese_font;

//创建一个结构体，用来存放每一个字体的字模类型和大小
typedef struct font
{
	uint16_t fSize;
	const uint8_t *model;//普通asc字模
	const char*ascii_map;//字符集（节省空间）
	const Chinese_font *Chinese;//中文字模
}font_t;
extern const font_t font12_sbold_t;
extern const font_t font16_sbold_t;
extern const font_t font20_sbold_t;
extern const font_t font24_sbold_t;
extern const font_t font24_bold_t;
extern const font_t font32_sbold_t;
extern const font_t font50_sbold_t;
extern const font_t font60_extra_t;
extern const font_t font76_bold_t;

#endif
