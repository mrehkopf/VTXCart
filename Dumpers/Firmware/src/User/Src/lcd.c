#include "main.h"
#include "lcd.h"
#include "font.h"
#include "printf.h"

// LCD_RS
#define LCD_RS_SET HAL_GPIO_WritePin(LCD_WR_RS_GPIO_PORT, LCD_WR_RS_PIN, GPIO_PIN_SET)
#define LCD_RS_RESET HAL_GPIO_WritePin(LCD_WR_RS_GPIO_PORT, LCD_WR_RS_PIN, GPIO_PIN_RESET)

// LCD_CS
#define LCD_CS_SET HAL_GPIO_WritePin(LCD_CS_GPIO_PORT, LCD_CS_PIN, GPIO_PIN_SET)
#define LCD_CS_RESET HAL_GPIO_WritePin(LCD_CS_GPIO_PORT, LCD_CS_PIN, GPIO_PIN_RESET)

// SPI Driver
#define SPI spi4
#define SPI_Drv (&hspi4)

static int32_t lcd_init(void);
static int32_t lcd_gettick(void);
static int32_t lcd_writereg(uint8_t reg, uint8_t *pdata, uint32_t length);
static int32_t lcd_readreg(uint8_t reg, uint8_t *pdata);
static int32_t lcd_senddata(uint8_t *pdata, uint32_t length);
static int32_t lcd_recvdata(uint8_t *pdata, uint32_t length);

int cur_x = 0, cur_y = 0, cur_color = 0;

const uint16_t lcd_palette[4][16] = {
    // 444 ref:
    //  0x0000, 0x0f88, 0x0c66, 0x0944, 0x0633, 0x0311, 0x0100, 0x0000
    { // normal text
        0x0000, 0xffff, 0x79ce, 0xd39c, 0x2c63, 0x8631, 0x8210, 0x0000,
        0xffe0, 0xffe0, 0xffe0, 0xffe0, 0xffe0, 0xffe0, 0xffe0, 0xffe0
    },
    { // sad text (red)
        0x0000, 0x51fc, 0x2ccb, 0x289a, 0x8661, 0x8230, 0x0010, 0x0000,
        0xffe0, 0xffe0, 0xffe0, 0xffe0, 0xffe0, 0xffe0, 0xffe0, 0xffe0
    },
    { // happy text (green)
        0x0000, 0xf18f, 0x6c66, 0xc844, 0x2633, 0x8211, 0x8000, 0x0000,
        0xffe0, 0xffe0, 0xffe0, 0xffe0, 0xffe0, 0xffe0, 0xffe0, 0xffe0
    },
    { // so-so text (yellow)
        0x0000, 0xf1ff, 0x6cce, 0xc89c, 0x2663, 0x8231, 0x8010, 0x0000,
        0xffe0, 0xffe0, 0xffe0, 0xffe0, 0xffe0, 0xffe0, 0xffe0, 0xffe0
    }
};

ST7735_IO_t st7735_pIO = {
	lcd_init,
	NULL,
	0,
	lcd_writereg,
	lcd_readreg,
	lcd_senddata,
	lcd_recvdata,
	lcd_gettick};

ST7735_Object_t st7735_pObj;

static int32_t lcd_init(void)
{
	int32_t result = ST7735_OK;
	return result;
}

static int32_t lcd_gettick(void)
{
	return HAL_GetTick();
}

static int32_t lcd_writereg(uint8_t reg, uint8_t *pdata, uint32_t length)
{
	int32_t result;
	LCD_CS_RESET;
	LCD_RS_RESET;
	result = HAL_SPI_Transmit(SPI_Drv, &reg, 1, 100);
	LCD_RS_SET;
	if (length > 0)
		result += HAL_SPI_Transmit(SPI_Drv, pdata, length, 500);
	LCD_CS_SET;
	if (result > 0)
	{
		result = -1;
	}
	else
	{
		result = 0;
	}
	return result;
}

static int32_t lcd_readreg(uint8_t reg, uint8_t *pdata)
{
	int32_t result;
	LCD_CS_RESET;
	LCD_RS_RESET;

	result = HAL_SPI_Transmit(SPI_Drv, &reg, 1, 100);
	LCD_RS_SET;
	result += HAL_SPI_Receive(SPI_Drv, pdata, 1, 500);
	LCD_CS_SET;
	if (result > 0)
	{
		result = -1;
	}
	else
	{
		result = 0;
	}
	return result;
}

static int32_t lcd_senddata(uint8_t *pdata, uint32_t length)
{
	int32_t result;
	LCD_CS_RESET;
	// LCD_RS_SET;
	result = HAL_SPI_Transmit(SPI_Drv, pdata, length, 100);
	LCD_CS_SET;
	if (result > 0)
	{
		result = -1;
	}
	else
	{
		result = 0;
	}
	return result;
}

static int32_t lcd_recvdata(uint8_t *pdata, uint32_t length)
{
	int32_t result;
	LCD_CS_RESET;
	// LCD_RS_SET;
	result = HAL_SPI_Receive(SPI_Drv, pdata, length, 500);
	LCD_CS_SET;
	if (result > 0)
	{
		result = -1;
	}
	else
	{
		result = 0;
	}
	return result;
}


extern const uint16_t font32 [];
uint16_t lcd_buf[FONT_WIDTH*FONT_HEIGHT] ALIGN(4);
uint8_t prev_lm, prev_attr[2];

void LCD_ShowChar(uint16_t x, uint16_t y, uint8_t num, uint8_t pal)
{
    uint8_t  xl, yl;
    uint32_t pixbuf;
    uint32_t src_addr;
    uint16_t tgt_addr = 0;

    int shift = 0; // shift / bit buffer fill

    if ((num < 0x20) || (num > 0x7f)) return;
    num -= 0x20;
    src_addr = num * FONT_WIDTH * FONT_HEIGHT / 2;
    for (yl = 0; yl < FONT_HEIGHT; yl++)
    {
        for (xl = 0; xl < FONT_WIDTH; xl++)
        {
            if(shift == 0) {
                pixbuf = font_src[src_addr++];
                shift = 8;
            }
            lcd_buf[tgt_addr++] = lcd_palette[pal][pixbuf & 0xf];
            pixbuf >>= 4;
            shift -= 4;
        }
    }
    ST7735_FillRGBRect(&st7735_pObj, x, y, (uint8_t *)&lcd_buf, FONT_WIDTH, FONT_HEIGHT);
}

void LCD_Clear(void)
{
    memset(video_buf, 0x20, sizeof(video_buf));
    memset(video_attr_buf, 0, sizeof(video_attr_buf));
    memset(lcd_char_cache, 0x20, sizeof(lcd_char_cache));
    memset(lcd_attr_cache, 0, sizeof(lcd_attr_cache));
    ST7735_FillRect(&st7735_pObj, 0, 0, 160, 80, 0x0000);
    cur_x = 0;
    cur_y = 0;
}

void LCD_Init(void)
{
    ST7735Ctx.Orientation = ST7735_ORIENTATION_LANDSCAPE_ROT180;
    ST7735Ctx.Panel = HannStar_Panel;
    ST7735Ctx.Type = ST7735_0_9_inch_screen;
    ST7735_RegisterBusIO(&st7735_pObj, &st7735_pIO);
    ST7735_LCD_Driver.Init(&st7735_pObj, ST7735_FORMAT_RBG565, &ST7735Ctx);
    for(int i = 0; i < LCD_LINES; i++) {
        video_line[i] = video_buf + 256 * i;
        video_attr[i] = video_attr_buf + 256 * i;
    }
//    LCD_Generate_Font(font, font_src, FONT_WIDTH, FONT_HEIGHT);
}

void LCD_UpdateText(void)
{
  int x = 0, y = 0;
//  if (f) LCD_Clear();

  for (int line = 0; line < LCD_LINES; line++) {
    x = 0;
    for (int col = 0; col < LCD_COLS; col++) {
      char ch = video_line[line][col];
      uint8_t at = video_attr[line][col];
      if (lcd_char_cache[line][col] != ch
       || lcd_attr_cache[line][col] != at) {
        lcd_char_cache[line][col] = ch;
        lcd_attr_cache[line][col] = at;
        LCD_ShowChar(x, y, ch, at);
      }
      x += FONT_WIDTH;
    }
    y += FONT_HEIGHT;
  }
}

void LCD_setcolor(int c) {
    cur_color = c;
}

void LCD_setattr(int x, int y, int c) {
    cur_x = x;
    cur_y = y;
    LCD_setcolor(c);
}

void LCD_putc(char c) {
    if (c == '\n') {
        cur_y++;
        cur_x = 0;
    } else if (c == '\r') {
        cur_x = 0;
    } else {
        /* line scroll on next output only to avoid constant empty line
           at bottom of screen when using normal \n terminated printfs */
        while(cur_y >= LCD_LINES) {
            memset(video_line[0], ' ', LCD_COLS);
            char *tmp_line = video_line[0];
            uint8_t *tmp_attr = video_attr[0];
            for(int i = 0; i < LCD_LINES - 1; i++) {
                video_attr[i] = video_attr[i + 1];
                video_line[i] = video_line[i + 1];
            }
            video_attr[LCD_LINES - 1] = tmp_attr;
            video_line[LCD_LINES - 1] = tmp_line;
            cur_y--;
        }
        video_attr[cur_y][cur_x] = cur_color;
        video_line[cur_y][cur_x] = c;
        cur_x++;
    }
}

int LCD_vprintf(int c, char *format, va_list ap) {
    int res;

    LCD_setcolor(c);
    res = vxprintf(LCD_putc, format, ap);
    return res;
}

int LCD_printf(int c, char *format, ...) {
    va_list ap;
    int res;

    LCD_setcolor(c);

    va_start(ap, format);
    res = vxprintf(LCD_putc, format, ap);
    va_end(ap);
    return res;
}

int LCD_xyprintf(int x, int y, int c, char *format, ...) {
    va_list ap;
    int res;

    LCD_setattr(x, y, c);

    va_start(ap, format);
    res = vxprintf(LCD_putc, format, ap);
    va_end(ap);
    return res;
}
