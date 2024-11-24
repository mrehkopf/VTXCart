//-----------------------------------------------------------------------------
// SNK MultiCart Dumper/Writer v1.00 on STM32H750VBT6
// project started 05.19.2023                                  (c) Vortex '2023
//-----------------------------------------------------------------------------

// 1.00 - (08.16.20xx)

#include "main.h"
#include "menu.h"
#include "tools.h"

// cn de/scramble address in defines

uint32_t Mount_SD(void)
{
  FRESULT fr;

  fr = f_mount(&SDFatFs, "0:", 1);
  return fr;
}

int main(void)
{
  /* MPU Configuration */
  MPU_Config();

  /* Enable I-Cache */
  SCB_EnableICache();

  /* Enable D-Cache */
  SCB_EnableDCache();

  /* STM32H7xx HAL library initialization */
  HAL_Init();

  /* Configure the system clock */
  SystemClock_Config();
  Delay_Init();

  /* Initialize GPIO */
  MX_GPIO_Init();
  CV_GPIO_Init(); // CV oh

  /* Initialize LED/BTN  */
  BSP_LED_Init(LED_BLUE);
  BSP_PB_Init(BUTTON_BRD,  BUTTON_MODE_GPIO);

  /* Initialize Timers */
  MX_TIM1_Init();

  /* Initialize SD-Card */
  BSP_SD_Init();
  MX_FATFS_Init();

  /* Init USB device Library */
  BSP_USB_DEVICE_Init();
  /* make USB work with __WFI() in main loop */
  __HAL_RCC_USB_OTG_FS_ULPI_CLK_SLEEP_DISABLE();
  __HAL_RCC_USB_OTG_FS_CLK_SLEEP_ENABLE();


  /* Initialize SPI */
  MX_SPI4_Init();

  /* Initialize UART */
  // MX_USART3_UART_Init();

  /* Initialize LCD */
  LCD_Init();
  LCD_Clear();

  FRESULT fr = f_mount(&SDFatFs, "0:", 1);

  if(fr != FR_OK) {
    LCD_xyprintf(0,0,0, "SD Card error %d:\n%s\n", fr, get_fresult_friendlyname(fr));
    waitButton();
  }
  uint32_t saved_addr;
  chip_t saved_chiptype;
  char saved_filename[80];

  if(loadProgress(&saved_addr, saved_filename, &saved_chiptype) == FR_OK) {
    LCD_Clear();
    LCD_printf(2, "Saved Progress found\n");
    LCD_printf(0, "A:%08lx %s\n", saved_addr, CHIP_NAMES[saved_chiptype]);
    LCD_printf(0, "<%s>\n", saved_filename);
    LCD_printf(0, "Resume?\n");
    if(waitYesNo()) {
      switch(saved_chiptype) {
        case CHIP_C:
        case CHIP_V:
          CV_Program_Internal(saved_filename, saved_addr, saved_chiptype);
          break;
        case CHIP_P:
          P_Program_Internal(saved_filename, saved_addr);
        default:
          LCD_printf(0, "Chip Type %s\nnot implemented\n",CHIP_NAMES[saved_chiptype]);
          waitButton();
          break;
      }
    } else {
      LCD_Clear();
      LCD_printf(0, "Delete current saved\nprogress?\n\n\n");
      if(waitYesNo()) {
        f_unlink(PROG_SAVE_FILE);
      }
    }
  }

  while(1) {
    menu_select(MENU_TOP);
  }
}

// Main (0.01s) Timer
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  // timer
  static uint32_t timLcdCnt = 0;
  static uint32_t timBtnCnt[BUTTONn] = { 0 };

  if(htim->Instance!=TIM1) return;

  ticks++;

  if (++timLcdCnt >= LCD_REFRESH_INTERVAL) {
    timLcdCnt = 0;
    LCD_UpdateText();
  }

  if (BSP_PB_GetState(BUTTON_BRD) == GPIO_PIN_SET) {
    timBtnCnt[BUTTON_BRD]++;
    if (!(flag_button & FLAG_BTN_PRESSED) && timBtnCnt[BUTTON_BRD] >= 35) { // BTN: 0.35 s.
      flag_button |= FLAG_BTN_BRD_LONG;
      flag_button |= FLAG_BTN_PRESSED;
    }
  } else {
    if (!(flag_button & FLAG_BTN_PRESSED) && timBtnCnt[BUTTON_BRD] >= 7) { // BTN: 0.07 s.
      flag_button |= FLAG_BTN_BRD;
    }
    flag_button &= ~FLAG_BTN_PRESSED;
    timBtnCnt[BUTTON_BRD] = 0;
  }
}
