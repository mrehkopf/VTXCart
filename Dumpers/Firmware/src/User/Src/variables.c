#include "main.h"
#include "variables.h"

// MX Handles
UART_HandleTypeDef huart3;
SPI_HandleTypeDef hspi4;
SD_HandleTypeDef hsd1;
USBD_HandleTypeDef hUsbDeviceFS;
TIM_HandleTypeDef htim1;

// flags
volatile uint32_t flag_mounted;
volatile uint32_t flag_lcddraw;
volatile uint32_t flag_button;

// FatFs
char SDPath[4];
FATFS SDFatFs;

// lcd
char lcd_char_cache[LCD_LINES][LCD_COLS+2];
uint8_t lcd_attr_cache[LCD_LINES][LCD_COLS+2];

char video_buf[LCD_LINES*256];
char *video_line[LCD_LINES];
uint8_t video_attr_buf[LCD_LINES*256];
uint8_t *video_attr[LCD_LINES];

// uart
uint8_t UARTTxBuffer[UART_BUFFER_SIZE] ALIGN(4);
uint32_t UARTTxBuffer_head, UARTTxBuffer_tail, UARTTxBuffer_len;

// menu
uint32_t cur_menu = MENU_CSEL;
uint32_t cur_chip = CHIP_P;
uint32_t cur_mode = MODE_TEST;

// dump buffer
uint16_t buffer [BUFFER_SIZE] AXI_BUFFER;
uint32_t buffer_pos;

// other
uint32_t address;
uint32_t sector;
uint32_t test;
uint32_t error;

// flags
uint32_t flg_test;
uint32_t flg_seek;
