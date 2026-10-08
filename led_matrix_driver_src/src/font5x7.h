/*
 * font5x7.h — 5×7 像素点阵字符库接口
 *
 * 覆盖全部可打印 ASCII 字符 (0x20 ~ 0x7E，共 95 个)。
 * 内部以列格式(column-major)存储，每字符 5 列，每列 7 bit。
 * 同时提供列→行转换，方便不同使用场景。
 *
 * 用法示例：
 *   #include "font5x7.h"
 *
 *   // 获取列数据 (适合 LED 矩阵横向滚动)
 *   const uint8_t *cols = font5x7_get_cols('A');
 *
 *   // 获取行数据 (与 anyverse.c 中 glyph5x7r_t 兼容)
 *   uint8_t rows[7];
 *   font5x7_get_rows('A', rows);
 *
 *   // 将字符串转为连续列流
 *   uint8_t buf[512];
 *   int n = font5x7_text_to_cols("Hello!", buf, sizeof(buf));
 */

#ifndef FONT5X7_H
#define FONT5X7_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------- 常量 ---------- */
#define FONT5X7_WIDTH       5       /* 每字符宽度 (像素/列) */
#define FONT5X7_HEIGHT      7       /* 每字符高度 (像素/行) */
#define FONT5X7_SPACING     1       /* 字符间默认空列数     */
#define FONT5X7_FIRST_CHAR  0x20    /* 第一个字符 (空格)    */
#define FONT5X7_LAST_CHAR   0x7E    /* 最后一个字符 (~)     */
#define FONT5X7_CHAR_COUNT  (FONT5X7_LAST_CHAR - FONT5X7_FIRST_CHAR + 1)

/* ---------- 列格式 API ---------- */

/**
 * font5x7_get_cols - 获取某字符的列格式点阵数据
 * @c: ASCII 字符
 *
 * 返回指向 5 字节数组的指针 (5 列)。
 * 每字节: bit0 = 最顶行, bit6 = 最底行。
 * 若字符不在字库范围内，返回空格的数据。
 */
const uint8_t *font5x7_get_cols(char c);

/* ---------- 行格式 API ---------- */

/**
 * font5x7_get_rows - 获取某字符的行格式点阵数据
 * @c:        ASCII 字符
 * @rows_out: 输出 7 字节数组 (7 行)
 *
 * 每字节使用低 5 bit: bit4 = 最左列, bit0 = 最右列。
 * 与 anyverse.c 中 glyph5x7r_t.row[] 格式一致。
 * 成功返回 0。
 */
int font5x7_get_rows(char c, uint8_t rows_out[7]);

/* ---------- 文本转列流 ---------- */

/**
 * font5x7_text_to_cols - 将字符串转换为列字节流
 * @text:     以 '\0' 结尾的字符串
 * @out_cols: 输出缓冲区
 * @max_cols: 缓冲区最大列数
 *
 * 字符间自动插入 FONT5X7_SPACING 个空列。
 * 返回实际写入的列数。
 */
int font5x7_text_to_cols(const char *text, uint8_t *out_cols, int max_cols);

/**
 * font5x7_text_to_cols_ex - 自定义间距版本
 * @spacing:  字符间空列数
 */
int font5x7_text_to_cols_ex(const char *text, uint8_t *out_cols,
                             int max_cols, int spacing);

/* ---------- 辅助工具 ---------- */

/**
 * font5x7_text_width - 计算字符串的总列宽 (默认间距)
 */
int font5x7_text_width(const char *text);

/**
 * font5x7_text_width_ex - 计算字符串的总列宽 (自定义间距)
 */
int font5x7_text_width_ex(const char *text, int spacing);

/**
 * font5x7_glyph_count - 返回字库中字形总数
 */
int font5x7_glyph_count(void);

#ifdef __cplusplus
}
#endif

#endif /* FONT5X7_H */
