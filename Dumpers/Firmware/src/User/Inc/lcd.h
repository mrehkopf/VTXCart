#ifndef __LCD_H
#define __LCD_H

#ifdef __cplusplus
 extern "C" {
#endif

#include "st7735.h"

extern ST7735_Ctx_t ST7735Ctx;

void LCD_Init(void);
void LCD_Clear(void);
void LCD_UpdateText(void);
void LCD_ShowChar(uint16_t x, uint16_t y, uint8_t num, uint8_t pal);
void LCD_ShowString(uint16_t x, uint16_t y, uint8_t *p);
int LCD_vprintf(int c, char *format, va_list ap);
int LCD_printf(int c, char *format, ...);
int LCD_xyprintf(int x, int y, int c, char *format, ...);

#ifdef __cplusplus
}
#endif

#endif /* __LCD_H */
