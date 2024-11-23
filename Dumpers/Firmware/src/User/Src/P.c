#include "main.h"
#include "P.h"
#include "menu.h"

// 55LV100S
#define SECTOR_SIZE 0x20000
#define REGION_SIZE 0x20 // in words


/*
 * Chip layout:
 * ============
 *
 * 2 chips (2x S29GL512P) with shared OE/CE/WE are connected in parallel
 * on DQ7:0 and DQ15:8 respectively. Chips seem to run in byte mode.
 *
 * According to the datasheet, program buffer size of each chip is
 * 64 bytes in byte mode, and 32 words in word mode, but in reality it only seems
 * to be 32 bytes in byte mode.
 *
 * Each of the logical chips has its own command interface and chip ID etc. that
 * can be read.
 * Commands can be sent to either chip by using the low or high byte respectively.
 *
 *
 * Signal to pin mapping:
 * ======================
 *
 *  DQ7:0  -> PA7:0
 *  DQ15:8 -> PC7:0
 *  A15:0  -> PB15:0
 *  A25:16 -> PE9:0
 *  CE#    -> PD0
 *  OE#    -> PD1
 *  WE#    -> PD3
 *  WP#    -> Tied high on adapter
 *  RST#   -> Tied high on adapter
 *
 *
 * Pin mapping conflicts with on-board hardware:
 * =============================================
 * Signal  Pin   shared with
 *
 *   A2    PB2   QSPI Flash CLK
 *   A3    PB3   SPI Flash CLK
 *   A4    PB4   SPI Flash DO (!)
 *   A6    PB6   QSPI Flash CS# (100kΩ pullup)
 *   A8    PB8   DVP I2C SCL
 *   A9    PB9   DVP I2C SDA
 *  A18    PE2   QSPI Flash IO2
 *  A19    PE3   LED drive transistor base
 *
 */

static char *addr_names[26] = {
  "A0", "A1", "A2", "A3", "A4", "A5", "A6", "A7",
  "A8", "A9", "A10", "A11", "A12", "A13", "A14", "A15",
  "A16", "A17", "A18", "A19", "A20", "A21", "A22", "A23",
  "A24", "A25"
};

static char *addr_pins[26] = {
  "B0", "B1", "B2", "B3", "B4", "B5", "B6", "B7",
  "B8", "B9", "B10", "B11", "B12", "B13", "B14", "B15",
  "E0", "E1", "E2", "E3", "E4", "E5", "E6", "E7",
  "E8", "E9"
};

static char *data_names[16] = {
  "D0", "D1", "D2", "D3", "D4", "D5", "D6", "D7",
  "D8", "D9", "D10", "D11", "D12", "D13", "D14", "D15"
};

static char *data_pins[16] = {
  "A0", "A1", "A2", "A3", "A4", "A5", "A6", "A7",
  "C0", "C1", "C2", "C3", "C4", "C5", "C6", "C7"
};

static char *ctrl_names[3] = {
  "CE#", "OE#", "WE#"
};

static char *ctrl_pins[3] = {
  "D0", "D1", "D3"
};

/* line capacitance measurements (in cycle counts / 8), high->low
  **********************************************
  * high -> low, standard solder PCB connected *
  **********************************************
    OPEN LINE:
    ==========
    A0-A26: 0x28, 0x25, 0x33, 0x2d, 0x29, 0x1f, 0x3f, 0x2f,
           +0x08,+0x08, 0x25, 0x27, 0x25, 0x27, 0x31, 0x2f,
            0x2e, 0x31, 0x29,*0x17, 0x27, 0x29, 0x2b, 0x31,
            0x29, 0x29

               + = pull-ups connected, never reaches low level
                   -> measured as low-high with counter pull-down

               * = measured with pull-up, not pull-down

    D0-D15: 0x24, 0x25, 0x25, 0x27, 0x39, 0x2b, 0x33, 0x33,
            0x26, 0x29, 0x2d, 0x2d, 0x31, 0x2b, 0x35, 0x31

    CE#:    0x1a
    OE#:    0x1c
    WE#:    0x28


    CHIP CONNECTED:
    ===============
    A0-A26: 0x46, 0x37, 0x45, 0x3b, 0x37, 0x2b, 0x53, 0x3b,
           +0x0a,+0x0a, 0x33, 0x35, 0x33, 0x35, 0x3f, 0x3f,
            0x3e, 0x3f, 0x37,*0x21, 0x35, 0x37, 0x39, 0x3f,
            0x39, 0x3b

    D0-D15: 0x2e, 0x31, 0x31, 0x31, 0x43, 0x37, 0x3d, 0x3d,
            0x30, 0x35, 0x39, 0x39, 0x3d, 0x37, 0x3f, 0x3d

    CE#:    0x30
    OE#:    0x2c
    WE#:    0x3c
*/

static const int addr_capa_thres[1][26] = {
  {
    0x3d, 0x31, 0x3f, 0x36, 0x32, 0x27, 0x4d, 0x34,
    0x09, 0x09, 0x2e, 0x30, 0x2e, 0x30, 0x3a, 0x3a,
    0x39, 0x3a, 0x32, 0x1d, 0x30, 0x32, 0x34, 0x3a,
    0x34, 0x35
  }
};

static const int data_capa_thres[1][16] = {
  {
    0x2a, 0x2d, 0x2d, 0x2e, 0x40, 0x33, 0x39, 0x39,
    0x2c, 0x31, 0x35, 0x35, 0x39, 0x33, 0x3b, 0x39
  }
};

static const int ctrl_capa_thres[1][3] = {
  {
    0x29,
    0x27,
    0x36
  }
};

static uint32_t p_desc_data(uint32_t dat) // china pinout descramble
{
  uint32_t data = 0;

  if (dat & BIT0)  data |= BIT8;
  if (dat & BIT1)  data |= BIT10;
  if (dat & BIT2)  data |= BIT12;
  if (dat & BIT3)  data |= BIT14;
  if (dat & BIT4)  data |= BIT7;
  if (dat & BIT5)  data |= BIT5;
  if (dat & BIT6)  data |= BIT3;
  if (dat & BIT7)  data |= BIT1;
  if (dat & BIT8)  data |= BIT9;
  if (dat & BIT9)  data |= BIT11;
  if (dat & BIT10) data |= BIT13;
  if (dat & BIT11) data |= BIT15;
  if (dat & BIT12) data |= BIT6;
  if (dat & BIT13) data |= BIT4;
  if (dat & BIT14) data |= BIT2;
  if (dat & BIT15) data |= BIT0;

  return data;
}

static uint32_t p_scr_data(uint32_t dat) // china pinout descramble
{
  uint32_t data = 0;

  if (dat & BIT0)  data |= BIT15;
  if (dat & BIT1)  data |= BIT7;
  if (dat & BIT2)  data |= BIT14;
  if (dat & BIT3)  data |= BIT6;
  if (dat & BIT4)  data |= BIT13;
  if (dat & BIT5)  data |= BIT5;
  if (dat & BIT6)  data |= BIT12;
  if (dat & BIT7)  data |= BIT4;
  if (dat & BIT8)  data |= BIT0;
  if (dat & BIT9)  data |= BIT8;
  if (dat & BIT10) data |= BIT1;
  if (dat & BIT11) data |= BIT9;
  if (dat & BIT12) data |= BIT2;
  if (dat & BIT13) data |= BIT10;
  if (dat & BIT14) data |= BIT3;
  if (dat & BIT15) data |= BIT11;

  return data;
}

static inline void P_nCE(uint32_t st)
{
  GPIOD -> BSRR = (BIT0 << 16) | ((st & 1) << 0);
}

static inline void P_nOE(uint32_t st)
{
  GPIOD -> BSRR = (BIT1 << 16) | ((st & 1) << 1);
}

static inline void P_nWE(uint32_t st)
{
  GPIOD -> BSRR = BIT3 << 16 | ((st & 1) << 3);;
}

static inline void P_SetAddress(uint32_t addr)
{
  GPIOB -> BSRR = (0xffff << 16) | (addr & 0xffff);
  GPIOE -> BSRR = (0x03ff << 16) | ((addr >> 16) & 0x03ff);
}

static inline void P_SetData(uint32_t data)
{
  GPIOA -> BSRR = (0x00ff << 16) | ( data & 0x00ff);
  GPIOC -> BSRR = (0x00ff << 16) | ((data & 0xff00) >> 8);
}

static inline uint32_t P_GetData(void)
{
  uint32_t data = ((GPIOA -> IDR) & 0xff);
	data |= ((GPIOC -> IDR) & 0xff) << 8;

  return data;
}

void P_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  // PA0-PA7: D0-D7
  __HAL_RCC_GPIOA_CLK_ENABLE();
  GPIO_InitStruct.Pin = GPIO_PIN_0|GPIO_PIN_1|GPIO_PIN_2|GPIO_PIN_3|GPIO_PIN_4|GPIO_PIN_5|GPIO_PIN_6|GPIO_PIN_7;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLDOWN;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_MEDIUM;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
  
  // PB0-PB15: A0-A15
  __HAL_RCC_GPIOB_CLK_ENABLE();
  GPIO_InitStruct.Pin = GPIO_PIN_0|GPIO_PIN_1|GPIO_PIN_2|GPIO_PIN_3|GPIO_PIN_4|GPIO_PIN_5|GPIO_PIN_6|GPIO_PIN_7|
	                    GPIO_PIN_8|GPIO_PIN_9|GPIO_PIN_10|GPIO_PIN_11|GPIO_PIN_12|GPIO_PIN_13|GPIO_PIN_14|GPIO_PIN_15;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_MEDIUM;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  // PC0-PC7: D8-D15
  __HAL_RCC_GPIOC_CLK_ENABLE();
  GPIO_InitStruct.Pin = GPIO_PIN_0|GPIO_PIN_1|GPIO_PIN_2|GPIO_PIN_3|GPIO_PIN_4|GPIO_PIN_5|GPIO_PIN_6|GPIO_PIN_7;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLDOWN;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_MEDIUM;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);
  
  // PD0, PD1, PD3: CE# OE# WE#
  __HAL_RCC_GPIOD_CLK_ENABLE();
  GPIO_InitStruct.Pin = GPIO_PIN_0|GPIO_PIN_1|GPIO_PIN_3;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_MEDIUM;
  HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);
  
  // PE0-PE9: A16-A25
  __HAL_RCC_GPIOE_CLK_ENABLE();
  GPIO_InitStruct.Pin = GPIO_PIN_0|GPIO_PIN_1|GPIO_PIN_2|GPIO_PIN_3|GPIO_PIN_4|GPIO_PIN_5|GPIO_PIN_6|GPIO_PIN_7|
	                    GPIO_PIN_8|GPIO_PIN_9;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_MEDIUM;
  HAL_GPIO_Init(GPIOE, &GPIO_InitStruct);

  P_nCE(1);
  P_nOE(1);
  P_nWE(1);
}

uint32_t P_ReadCycle(uint32_t addr)
{
  uint32_t data;

  P_SetAddress(addr);
  P_nCE(0);
  P_nOE(0);
  Delay_cycles(64); // ~133ns
  data = P_GetData();
  P_nOE(1);
  P_nCE(1);
  return data;
}

void P_WriteCycle(uint32_t addr, uint16_t data)
{
  P_SetAddress(addr);
  P_SetData(data);
  P_nOE(1);
  P_nCE(0);
  DATADIR_OUT();
  P_nWE(0);
  Delay_cycles(64); // ~133ns
  P_nWE(1);
  DATADIR_IN();
  P_nCE(1);
}

void P_WriteUnlockSequence(void) {
  P_WriteCycle(0xaaa, 0xaaaa);
  P_WriteCycle(0x555, 0x5555);
}

void P_Reset(void) {
  P_WriteCycle(0, 0xf0f0);
  P_WriteUnlockSequence();
  P_WriteCycle(0, 0xf0f0);
  P_WriteCycle(0, 0xf0f0);
}

void P_Init(void) {
  P_GPIO_Init();
  P_Reset();
}

/**
 * Wait for DQ7 to return to non-complementary state (ready)
 * @param status optional pointer to a uint16_t to be assigned the last read status word
 * @param addr address of partition to read status word from (usually 0)
 * @param mask compare mask for status word, success if bit 7 equals the bit read from the chip
 * @param timeout timeout in number of timer ticks (currently 10ms)
 *
 * @return result flag indicating timeout (1)
 */
int P_WaitStatus(uint16_t *status, uint32_t addr, uint16_t mask, int timeout) {
  int result = 0;
  uint16_t data = 0xffff;
  uint32_t endtime = ticks + timeout;

  do {
    /* speed up data line pull-down in case chip has gone High-Z
       to prevent previous bus data from being mistaken as status word */
    P_SetData(0x0000);
    DATADIR_OUT();
    DATADIR_IN();
    data = P_ReadCycle(addr);

    if(ticks > endtime) {
      break;
    }
  } while ((data & 0x8080) != (mask & 0x8080));

  if(status) {
    *status = data;
  }

  /* Timeout - which half triggered it? */
  if((data & 0x8080) != (mask & 0x8080)) {
    result = 1;
  }

  return result;
}

/** Get Manufacturer ID from chip
 * @param chip Number of the chip to get the ID from (0-3)
 */
Flash_ID P_ReadID(int chip) {
  Flash_ID result;
  uint16_t data1, data2;
  P_WriteUnlockSequence();
  P_WriteCycle(0xaaa, 0x9090);
  Delay_us(100);

  data1 = P_ReadCycle(0 << 1);
  result.vendor_id = ((data1 >> (chip * 8)) & 0xff);

  data1 = P_ReadCycle(0x1 << 1);
  data2 = P_ReadCycle(0xe << 1);
  result.chip_id = ((data1 >> (chip * 8)) & 0xff)
                   | (((data2 >> (chip * 8)) & 0xff) << 8);
  return result;
}

int P_CheckID(void) {
  for(int i = 0; i < 2; i++) {
    Flash_ID id = P_ReadID(i);
    if(id.chip_id != 0x0001 || id.vendor_id != 0x237e) {
      return 1;
    }
  }
  return 0;
}

uint32_t P_SectorErase(uint32_t addr)
{
  uint16_t sr;
  uint32_t res = 0;

  int try = 0;
  int dirty = 1;
  P_WriteCycle(addr, 0xf0f0);
  do {
    LCD_xyprintf(0, 1, 0, "ER %08lx [%d]\n", addr, try++);
    P_WriteUnlockSequence();
    P_WriteCycle(0xaaa, 0x8080);
    P_WriteUnlockSequence();
    P_WriteCycle(addr, 0x3030);
    res = P_WaitStatus(&sr, addr, 0x8080, 400);
    if(res) {
      LCD_printf(0, "ER Timeout          \n");
      if(flag_button & FLAG_BTN_BRD_LONG) {
        flag_button &= ~(FLAG_BTN_BRD_LONG);
        break;
      }
      if(dirty) {
        if(P_CheckID()) {
          res |= 4;
          goto abort;
        }
      }
      continue;
    }
    if((sr & 0x8080) == 0x8080) dirty = 0;
    if(dirty) {
      LCD_xyprintf(0, 2, 1, "ER sr=%04x\n", sr);
    }
    if(flag_button & (FLAG_BTN_BRD)) {
      flag_button &= ~(FLAG_BTN_BRD);
      res |= 8;
      break;
    }
  } while (dirty);

abort:
  return res;
}

/**
 * @brief Combined function for erase-checking and diff-checking sector prior
 *        to programming.
 *
 * @param addr
 * @param buffer
 * @return int
 */
int P_SectorCheckForProgram(uint32_t addr, uint16_t *buffer) {
  uint16_t data, compare;
  int need_program = 0;
  int need_erase = 0;
  P_WriteCycle(addr, 0xf0f0);
  LCD_xyprintf(0, 1, 0, "VR %08lx         \r", addr);
  for(int j = 0; j < SECTOR_SIZE; j++) {
    data = P_ReadCycle(addr+j);
    compare = buffer[j];
    if(data != compare) {
      if(!need_program) {
        LCD_xyprintf(0, 2, 3, "VR %04x != %04x\r", data, compare);
      }
      need_program = 1;
      if((data | compare) != data) {
        need_erase = 1;
      }
    }
    if(need_erase) break;
  }
  return need_program | (need_erase << 1);
}


int P_SectorVerify(uint32_t addr, uint16_t *buffer) {
  uint16_t data, compare;
  int dirty = 0;

  P_WriteCycle(addr, 0xf0f0);
  LCD_xyprintf(0, 1, 0, "VR %08lx         \r", addr);
  for(int j = 0; j < SECTOR_SIZE; j++) {
    data = P_ReadCycle(addr+j);
    compare = buffer[j];
    if(data != compare) {
      LCD_xyprintf(0, 2, 3, "VR %04x != %04x\r", data, compare);
      dirty |= 1;
    }
    if(dirty) break;
  }
  return dirty;
}

void P_TestAllPins(uint16_t data_mask, uint32_t addr_mask, uint16_t ctrl_mask, char *probename, char *probepin) {
  uint16_t test_data = (GPIOA->IDR & 0xff) | ((GPIOC->IDR & 0xff) << 8);
  uint32_t test_addr = GPIOB->IDR | ((GPIOE->IDR & 0x3ff) << 16);
  uint16_t test_ctrl = (GPIOD->IDR & 0x3) | ((GPIOD->IDR >> 1) & 0x4);

  test_data ^= data_mask;
  test_addr ^= (addr_mask | 0x300);
  test_ctrl ^= (ctrl_mask | 0x1);

  for(int i = 0; i < 16; i++) {
    if(test_data & (1 << i)) {
      LCD_printf(1, "SHORT %s-%s\nCheck pins %s-%s\n", probename, data_names[i], probepin, data_pins[i]);
      waitButton();
    }
  }
  for(int i = 0; i < 26; i++) {
    if(test_addr & (1 << i)) {
      LCD_printf(1, "SHORT %s-%s\nCheck pins %s-%s\n", probename, addr_names[i], probepin, addr_pins[i]);
      waitButton();
    }
  }
  for(int i = 0; i < 3; i++) {
    if(test_ctrl & (1 << i)) {
      LCD_printf(1, "SHORT %s-%s\nCheck pins %s-%s\n", probename, ctrl_names[i], probepin, ctrl_pins[i]);
      waitButton();
    }
  }
}

/** Measure line capacitances for checking chip connectivity.
 * @param addr pointer to an array of 26 ints to hold measurements for address lines.
 * @param data pointer to an array of 16 ints to hold measurements for data lines.
 * @param ctrl pointer to an array of 11 ints to hold measurements for control lines.
 */
void P_GetLineCapacitances(int *addr, int *data, int *ctrl) {
  /* capacitance check: enable pull-downs, output high level
     EXCEPTIONS:
      - A8-A9 (PB8-PB9) are measured as low-high transitions,
        so a low level is output initially
      - A19 (PE3) is measured with a pull-up to counteract quick
        pull-down by the connected LED driver transistor
      - This chip has no RST# pin to pull down so we need to explicitly pull-up
        CE# / OE# to stop it from driving the data lines. Pull-downs are then
        activated for control line test later.
  */
  GPIOA->PUPDR = (GPIOA->PUPDR & ~0xffff) | 0xaaaa;
  GPIOC->PUPDR = (GPIOC->PUPDR & ~0xffff) | 0xaaaa;
  GPIOB->PUPDR = 0xaaaaaaaa;
  GPIOE->PUPDR = (GPIOE->PUPDR & ~0x000fffff) | 0x000aaa6a;
  GPIOD->PUPDR = (GPIOD->PUPDR & ~0x000000cf) | 0x00000085;

  GPIOA->MODER = (GPIOA->MODER & ~0xffff);
  GPIOC->MODER = (GPIOC->MODER & ~0xffff);
  GPIOB->MODER = 0;
  GPIOE->MODER = (GPIOE->MODER & ~0x000fffff);
  GPIOD->MODER = (GPIOD->MODER & ~0x000000cf);

  GPIOA->BSRR = 0xff;
  GPIOC->BSRR = 0xff;
  GPIOB->BSRR = (0x0300 << 16) | 0xfcff;
  GPIOE->BSRR = 0x3ff;
  GPIOD->BSRR = 0xb;

  Delay_us(100);

  /* disable any sources that might interrupt measurement */
  SysTick->CTRL &= ~(SysTick_CTRL_ENABLE_Msk);
  NVIC_DisableIRQ(TIM1_UP_IRQn);
  NVIC_DisableIRQ(OTG_FS_WKUP_IRQn);
  NVIC_ClearPendingIRQ(TIM1_UP_IRQn);
  NVIC_ClearPendingIRQ(OTG_FS_WKUP_IRQn);

  int count[26];

  /*****************
   * ADDRESS LINES *
   *****************/
  if(addr) {
    /* measure A0-A15 (except A8+A9), high->low transition */
    for(int i = 0; i < 16; i++) {
      if(i == 8 || i == 9) continue;
      count[i] = 0;
      GPIO_MODE_OUT(GPIOB, i);
      Delay_us(100);
      GPIO_MODE_IN(GPIOB, i);
      DWT->CYCCNT = 0;
      while((GPIOB->IDR & (1 << i)));
      count[i] = DWT->CYCCNT / 8;
    }

    /* measure A8+A9, low->high transition */
    for(int i = 8; i < 10; i++) {
      count[i] = 0;
      GPIO_MODE_OUT(GPIOB, i);
      Delay_us(100);
      GPIO_MODE_IN(GPIOB, i);
      DWT->CYCCNT = 0;
      while(!(GPIOB->IDR & (1 << i)));
      count[i] = DWT->CYCCNT / 8;
    }

    /* measure A16-A25, high->low transition */
    for(int i = 0; i < 10; i++) {
      count[i + 16] = 0;
      GPIO_MODE_OUT(GPIOE, i);
      Delay_us(100);
      GPIO_MODE_IN(GPIOE, i);
      DWT->CYCCNT = 0;
      while((GPIOE->IDR & (1 << i)));
      count[i + 16] = DWT->CYCCNT / 8;
    }
    memcpy(addr, count, 26*sizeof(int));
  }

  /**************
   * DATA LINES *
   **************/
  if(data) {
    /* measure D0-D7, high->low transition */
    for(int i = 0; i < 8; i++) {
      count[i] = 0;
      GPIO_MODE_OUT(GPIOA, i);
      Delay_us(100);
      GPIO_MODE_IN(GPIOA, i);
      DWT->CYCCNT = 0;
      while((GPIOA->IDR & (1 << i)));
      count[i] = DWT->CYCCNT / 8;
    }

    /* measure D8-D15, high->low transition */
    for(int i = 0; i < 8; i++) {
      count[i + 8] = 0;
      GPIO_MODE_OUT(GPIOC, i);
      Delay_us(100);
      GPIO_MODE_IN(GPIOC, i);
      DWT->CYCCNT = 0;
      while((GPIOC->IDR & (1 << i)));
      count[i + 8] = DWT->CYCCNT / 8;
    }
    memcpy(data, count, 16*sizeof(int));
  }

  /*****************
   * CONTROL LINES *
   *****************/
  if(ctrl) {
    GPIO_PULL_DOWN(GPIOD, 0);
    GPIO_PULL_DOWN(GPIOD, 1);
    Delay_us(100);
    /* measure CE#/OE#, high->low transition */
    for(int i = 0; i < 2; i++) {
      count[i] = 0;
      GPIO_MODE_OUT(GPIOD, i);
      Delay_us(100);
      GPIO_MODE_IN(GPIOD, i);
      DWT->CYCCNT = 0;
      while((GPIOD->IDR & (1 << i)));
      count[i] = DWT->CYCCNT / 8;
    }

    /* measure WE#, high->low transition */
    count[2] = 0;
    GPIO_MODE_OUT(GPIOD, 3);
    Delay_us(100);
    GPIO_MODE_IN(GPIOD, 3);
    DWT->CYCCNT = 0;
    while((GPIOD->IDR & (1 << 3)));
    count[2] = DWT->CYCCNT / 8;

    memcpy(ctrl, count, 3*sizeof(int));
  }
  __DSB(); __DMB(); __ISB();
  NVIC_EnableIRQ(TIM1_UP_IRQn);
  NVIC_EnableIRQ(OTG_FS_WKUP_IRQn);
  SysTick->CTRL |= SysTick_CTRL_ENABLE_Msk;

  /* revert to normal GPIO operation */
  P_GPIO_Init();
}

void P_Hexdump(uint32_t addr, uint32_t count) {
  int valuecount = 0;
  for(int i = 0; i < count; i++) {
    LCD_printf(0, "%04x ", P_ReadCycle(addr+i));
    valuecount++;
    if(valuecount == 4) {
      LCD_printf(0, "\n");
      valuecount = 0;
    }
  }
  if(valuecount) LCD_printf(0, "\n");
}

void P_Test() {
  P_Init();
  LCD_Clear();
  LCD_xyprintf(0, 0, 0, "%s", "Chip Connection Test\n");
  Flash_ID chip_id[2];

  int error = 0;

  for(int i = 0; i < 2; i++) {
    chip_id[i] = P_ReadID(i);
    if(chip_id[i].vendor_id != 0x0001 || chip_id[i].chip_id != 0x237e) {
      error++;
      LCD_printf(1, "ID: %04x:%04x\n", chip_id[i].vendor_id, chip_id[i].chip_id);
    }
  }

  if(!error) {
    LCD_printf(2, "Chip ID ok\n");
  } else {
    LCD_printf(1, "Chip ID bad\n");
    waitButton();
  }

  LCD_Clear();
  error = 0;
  P_Hexdump(0, 2);
  P_WriteCycle(0, 0xFF);
  P_Hexdump(0, 2);
  waitButton();
  /****************************************
   * try analyzing electrical connections *
   ****************************************

      I. stuck data/address/control lines
      ===================================
        1. set all signals as inputs with pull-ups (so the chip gets disabled)
        2. check if all lines are high
        3. any low read value denotes a line that is stuck low.
        4. enable pull-down for all signals (RST# should keep outputs disabled)
        5. check if all lines are low
        6. any high read value denotes a line that is stuck high.

      II. open data lines
      ==================
      PER CHIP, do:
        1. set data lines as inputs
        2. enable chip outputs
        3. enable pull-ups
        4. save pin states
        5. enable pull-downs
        6. compare pin states
        7. any changed pins are probably open line.
        8. if many/all pins are open line, CE/OE are probably bad.

      III. shorted data/address lines
      ===============================
        1. assert RST, deassert CE/OE
        2. set all lines as inputs with pull-downs
        3. for each line, set a single line as output, high level
        4. check that no other lines assume a high level as well.

      IV. other open lines / bad connections - capacitance test
      =========================================================
        1. enable pull-ups on all pins
        2. set all lines as outputs
        3. output low level
        4. for each line, switch to input
        5. measure time until pin value becomes high
        6. too low levels mean bad or open connection (low capacitance)

   */

  uint16_t test_data, test_ctrl;
  uint32_t test_addr;

  /* enable pull-ups and check for pins stuck low */
  GPIOA->PUPDR = (GPIOA->PUPDR & ~0xffff) | 0x5555;
  GPIOC->PUPDR = (GPIOC->PUPDR & ~0xffff) | 0x5555;
  GPIOB->PUPDR = 0x55555555;
  GPIOE->PUPDR = (GPIOE->PUPDR & ~0x000fffff) | 0x00055555;
  GPIOD->PUPDR = (GPIOD->PUPDR & ~0x000000cf) | 0x00000045;

  GPIOA->MODER = (GPIOA->MODER & ~0xffff);
  GPIOC->MODER = (GPIOC->MODER & ~0xffff);
  GPIOB->MODER = 0;
  GPIOE->MODER = (GPIOE->MODER & ~0x000fffff);
  GPIOD->MODER = (GPIOD->MODER & ~0x000000cf);

  /* give some time for signals to float and pull-ups to take effect */
  Delay_us(10000);

  test_data = (GPIOA->IDR & 0xff) | ((GPIOC->IDR & 0xff) << 8);
  test_addr = GPIOB->IDR | ((GPIOE->IDR & 0x3ff) << 16);
  test_ctrl = (GPIOD->IDR & 0x3) | ((GPIOD->IDR >> 1) & 0x4);
  LCD_printf(0, "Stuck pins check...\n");
  for(int i = 0; i < 16; i++) {
    if(!(test_data & (1 << i))) {
      LCD_printf(1, "D%d/%d stuck low!\nCheck pin %s\n", i, i+16, data_pins[i]);
      waitButton();
    }
  }

  /* A19 (pin E3) cannot be tested this way because the LED transistor defeats the
     internal pull-up. */
  for(int i = 0; i < 26; i++) {
    if(!(test_addr & (1 << i)) && i != 19) {
      LCD_printf(1, "A%d stuck low!\nCheck pin %s\n", i, addr_pins[i]);
      waitButton();
    }
  }

  for(int i = 0; i < 3; i++) {
    if(!(test_ctrl & (1 << i))) {
      LCD_printf(1, "%s stuck low!\nCheck pin %s\n", ctrl_names[i], ctrl_pins[i]);
      waitButton();
    }
  }

  /* switch to pull-down and test for the opposite */
  GPIOA->PUPDR = (GPIOA->PUPDR & ~0xffff) | 0xaaaa;
  GPIOC->PUPDR = (GPIOC->PUPDR & ~0xffff) | 0xaaaa;
  GPIOB->PUPDR = 0xaaaaaaaa;
  GPIOE->PUPDR = (GPIOE->PUPDR & ~0x000fffff) | 0x000aaaaa;
  GPIOD->PUPDR = (GPIOD->PUPDR & ~0x000000cf) | 0x00000089;
  Delay_us(10000);

  test_data = (GPIOA->IDR & 0xff) | ((GPIOC->IDR & 0xff) << 8);
  test_addr = GPIOB->IDR | ((GPIOE->IDR & 0x3ff) << 16);

  for(int i = 0; i < 16; i++) {
    if(test_data & (1 << i)) {
      LCD_printf(1, "D%d/%d stuck high!\nCheck pin %s\n", i, i+16, data_pins[i]);
      waitButton();
    }
  }

  /* A8+A9 (pins B8+B9) cannot be tested this way because they are connected to
     I2C pull-ups on the WeAct board. */
  for(int i = 0; i < 26; i++) {
    if((test_addr & (1 << i)) && i != 8 && i != 9) {
      LCD_printf(1, "A%d stuck high!\nCheck pin %s\n", i, addr_pins[i]);
      waitButton();
    }
  }

  GPIO_PULL_DOWN(GPIOD, 0);
  Delay_us(10000);
  test_ctrl = (GPIOD->IDR & 0x3) | ((GPIOD->IDR >> 1) & 0x4);

  for(int i = 0; i < 3; i++) {
    if(test_ctrl & (1 << i)) {
      LCD_printf(1, "%s stuck high!\nCheck pin %s\n", ctrl_names[i], ctrl_pins[i]);
      waitButton();
    }
  }


  LCD_printf(0, "Open data line check\n");
  P_GPIO_Init();
  P_nCE(0);
  P_nOE(0);
  DATADIR_IN();
  P_SetAddress(0);
  GPIOA->PUPDR = (GPIOA->PUPDR & ~0xffff) | 0xaaaa;
  GPIOC->PUPDR = (GPIOC->PUPDR & ~0xffff) | 0xaaaa;
  Delay_us(10000);
  test_data = P_GetData();
  GPIOA->PUPDR = (GPIOA->PUPDR & ~0xffff) | 0x5555;
  GPIOC->PUPDR = (GPIOC->PUPDR & ~0xffff) | 0x5555;
  Delay_us(10000);
  uint16_t test_data_errors = P_GetData() ^ test_data;

  error = 0;
  for(int i = 0; i < 16; i++) {
    if(test_data_errors & (1 << i)) {
      error++;
    }
  }

  if(error > 10) {
    LCD_printf(1, "No output?\nCheck CE/OE\n");
    waitButton();
  } else {
    for(int i = 0; i < 16; i++) {
      if(test_data_errors & (1 << i)) {
        LCD_printf(1, "D%d open!\nCheck pin %s\n", i, data_pins[i]);
        waitButton();
      }
    }
  }

  LCD_printf(0, "Short check...\n");

  GPIOA->PUPDR = (GPIOA->PUPDR & ~0xffff) | 0xaaaa;
  GPIOC->PUPDR = (GPIOC->PUPDR & ~0xffff) | 0xaaaa;
  GPIOB->PUPDR = 0xaaaaaaaa;
  GPIOE->PUPDR = (GPIOE->PUPDR & ~0x000fffff) | 0x000aaaaa;
  GPIOD->PUPDR = (GPIOD->PUPDR & ~0x000000cf) | 0x00000089;

  GPIOA->MODER = (GPIOA->MODER & ~0xffff);
  GPIOC->MODER = (GPIOC->MODER & ~0xffff);
  GPIOB->MODER = 0;
  GPIOE->MODER = (GPIOE->MODER & ~0x000fffff);
  GPIOD->MODER = (GPIOD->MODER & ~0x000000cf);

  GPIOA->BSRR = 0xff;
  GPIOC->BSRR = 0xff;
  GPIOB->BSRR = 0xffff;
  GPIOE->BSRR = 0x3ff;
  GPIOD->BSRR = (BIT9 << 16) | 0xb;

  Delay_us(10000);

  /* D0-D7 (GPIOA) */
  for(int i = 0; i < 8; i++) {
    GPIO_MODE_OUT(GPIOA, i);
    Delay_us(1000);
    P_TestAllPins(1 << i, 0, 0, data_names[i], data_pins[i]);
    GPIO_MODE_IN(GPIOA, i);
  }

  /* D8-D15 (GPIOC) */
  for(int i = 0; i < 8; i++) {
    GPIO_MODE_OUT(GPIOC, i);
    Delay_us(1000);
    P_TestAllPins(1 << (i + 8), 0, 0, data_names[i+8], data_pins[i+8]);
    GPIO_MODE_IN(GPIOC, i);
  }

  /* A0-A15 (GPIOB) */
  for(int i = 0; i < 16; i++) {
    GPIO_MODE_OUT(GPIOB, i);
    Delay_us(1000);
    P_TestAllPins(0, 1 << i, 0, addr_names[i], addr_pins[i]);
    GPIO_MODE_IN(GPIOB, i);
  }

  /* A16-A25 (GPIOE) */
  for(int i = 0; i < 10; i++) {
    GPIO_MODE_OUT(GPIOE, i);
    Delay_us(1000);
    P_TestAllPins(0, 1 << (i + 16), 0, addr_names[i+16], addr_pins[i+16]);
    GPIO_MODE_IN(GPIOE, i);
  }

  /* CE#, OE# (GPIOD) */
  for(int i = 0; i < 2; i++) {
    GPIO_MODE_OUT(GPIOD, i);
    Delay_us(1000);
    P_TestAllPins(0, 0, 1 << i, ctrl_names[i], ctrl_pins[i]);
    GPIO_MODE_IN(GPIOD, i);
  }

  /* WE# (GPIOD) */
  GPIO_MODE_OUT(GPIOD, 3);
  Delay_us(1000);
  P_TestAllPins(0, 0, 1 << 2, ctrl_names[2], ctrl_pins[2]);
  GPIO_MODE_IN(GPIOD, 3);

  LCD_printf(0, "Capacitance check...\n");
  int addr_capa[26];
  int data_capa[16];
  int ctrl_capa[11];
  int adapter_idx = 0;
  P_GetLineCapacitances(addr_capa, data_capa, ctrl_capa);
  for(int i = 0; i < 26; i++) {
    if(addr_capa[i] < addr_capa_thres[adapter_idx][i]) {
      LCD_printf(1, "%s open (pin %s)\n", addr_names[i], addr_pins[i]);
      waitButton();
    }
  }
  for(int i = 0; i < 16; i++) {
    if(data_capa[i] < data_capa_thres[adapter_idx][i]) {
      LCD_printf(1, "%s open (pin %s)\n", data_names[i], data_pins[i]);
      waitButton();
    }
  }
  for(int i = 0; i < 3; i++) {
    if(ctrl_capa[i] < ctrl_capa_thres[adapter_idx][i]) {
      LCD_printf(1, "%s open (pin %s)\n", ctrl_names[i], ctrl_pins[i]);
      waitButton();
    }
  }
  LCD_printf(0, "Done. <key>\n");
  waitButton();
  LCD_Clear();
}

void P_PrintCapas(int *results, const int *thres, int num) {
    int xcount = 0;
    LCD_xyprintf(0, 1, 0, "");
    for(int i = 0; i < num; i++) {
      LCD_printf(results[i] > thres[i] ? 2 : 1, "%02x ", results[i]);
      xcount++;
      if(xcount == 7) {
        LCD_printf(0, "\n");
        xcount = 0;
      }
    }
}

void P_CapaView(void) {
  int adapter = 0;
  LCD_Clear();
  LCD_xyprintf(0,0,0,"Capacitance check A\n");
  int result[26];
  while(!(flag_button & FLAG_BTN_BRD)) {
    P_GetLineCapacitances(result, NULL, NULL);
    P_PrintCapas(result, addr_capa_thres[adapter], 26);
  }
  flag_button &= ~FLAG_BTN_BRD;

  LCD_Clear();
  LCD_xyprintf(0,0,0,"Capacitance check D\n");
  while(!(flag_button & FLAG_BTN_BRD)) {
    P_GetLineCapacitances(NULL, result, NULL);
    P_PrintCapas(result, data_capa_thres[adapter], 16);
  }
  flag_button &= ~FLAG_BTN_BRD;

  LCD_Clear();
  LCD_xyprintf(0,0,0,"Capacitance check C\n");
  while(!(flag_button & FLAG_BTN_BRD)) {
    P_GetLineCapacitances(NULL, NULL, result);
    P_PrintCapas(result, ctrl_capa_thres[adapter], 3);
  }
  flag_button &= ~FLAG_BTN_BRD;
  SysTick->CTRL |= SysTick_CTRL_ENABLE_Msk;

  /* revert to normal GPIO operation */
  P_GPIO_Init();
}

void P_Erase() {
  P_Init();
  LCD_Clear();
  LCD_xyprintf(0, 0, 0, "Erasing Chip\n");
  for(int i = 0; i < END_ADDRESS_P; i += SECTOR_SIZE) {
    P_WriteUnlockSequence();
    P_SectorErase(i);
    if(flag_button & (FLAG_BTN_BRD)) {
      LCD_printf(1, "Erase aborted\n");
      flag_button &= ~(FLAG_BTN_BRD);
      goto abort;
    }
  }
  LCD_printf(2, "Erase complete!\n");
abort:
  waitButton();
}


/**
 * @brief Program a sector
 *
 * @param addr sector address
 * @return int 1=failed, 0=OK
 */
int P_SectorProgram(uint32_t addr, uint16_t *buf) {
  uint16_t sr;
  uint16_t data;
  uint32_t addr_final;

  P_WriteCycle(addr, 0xf0f0);

  int active = 1;
  LCD_xyprintf(0, 1, 0, "PG %08lx\n", addr);
  for(int j = 0; j < SECTOR_SIZE; j += REGION_SIZE) {
    P_WriteUnlockSequence();
    P_WriteCycle(addr+j, 0x2525);
    P_WriteCycle(addr+j, (REGION_SIZE - 1) | ((REGION_SIZE - 1) << 8));
    for(int d = 0; d < REGION_SIZE; d++) {
      addr_final = addr + j + d;

      data = buf[(j+d)];
      P_WriteCycle(addr_final, data);
    }
    P_WriteCycle(addr+j, 0x2929);
    P_WaitStatus(&sr, addr+j+REGION_SIZE-1, data, 100);
    if((sr & 0x8080) != (data & 0x8080)) {
      LCD_xyprintf(0, 2, 1, "PG sr=%04x\n", sr);
      active = 0;
    }
    P_WriteCycle(addr+j, 0xf0f0);
    if(!active) {
      break;
    }
  }
  if(active)LCD_xyprintf(0, 2, 0, "                    \n");
  return (~active) & 1;
}

void P_SectorDump(uint32_t addr, uint16_t *buffer) {
  uint16_t data;
  P_WriteCycle(addr, 0xf0f0);
  LCD_xyprintf(0, 1, 0, "DP %08lx         \r", addr);
  for(int j = 0; j < SECTOR_SIZE; j++) {
    data = P_ReadCycle(addr+j);
    buffer[j] = data;
  }
}

void P_genScrambleLookup(void) {
  for(int i = 0; i < 65536; i++) {
    scramble_lookup[i] = p_scr_data(i);
  }
}

void P_genDescrambleLookup(void) {
  for(int i = 0; i < 65536; i++) {
    scramble_lookup[i] = p_desc_data(i);
  }
}

void P_ScrambleBuffer(uint16_t *buffer, uint32_t length) {
  for(int i = 0; i < length; i++) {
    buffer[i] = scramble_lookup[buffer[i]];
  }
}

void P_Program_Internal(const char *filename, uint32_t address) {
  uint32_t addr;
  FIL file;
  UINT bytes_read;
  uint32_t starttime = ticks;
  uint8_t erase_status = 0;
  uint8_t fatal = 0, cancel = 0;
  FRESULT res;

  P_Init();

  LCD_Clear();
  res = f_open(&file, filename, FA_READ);
  if(check_fresult(res, "Could not open file:\n%s\n", filename)) {
    return;
  }

  res = f_lseek(&file, address * 2);
  if(check_fresult(res, "Seek to %lx failed\n", address * 2)) {
    return;
  };

  P_genScrambleLookup();

  for(addr = address; addr < END_ADDRESS_P; addr += SECTOR_SIZE) {
    LCD_xyprintf(0, 0, 0, "Programming %3d%%\n", (int)((double)100.0 * (double)addr / (double)END_ADDRESS_P + 0.25));
    uint8_t erase = 1;
    res = f_read(&file, buffer, SECTOR_SIZE * 2, &bytes_read);
    check_fresult(res, "File read failed\n");
    P_ScrambleBuffer(buffer, SECTOR_SIZE);
    if(!bytes_read) break;
    /* first, determine if we need to reprogram at all */
    while((erase = P_SectorCheckForProgram(addr, buffer))) {
      do {
        if(erase & 2) { // need erase
          erase_status = P_SectorErase(addr);
        } else {
          erase_status = 0;
        }
        fatal = erase_status & 4;
        cancel = erase_status & 8;
        if(fatal || cancel) goto program_abort;
        erase = P_SectorProgram(addr, buffer);
        if(erase) {
          LCD_xyprintf(0, 4, 1, "Retrying ...         \n");
        } else {
          LCD_xyprintf(0, 4, 2, "Happy Happy Happy :)\n");
        }
      } while (erase);
    }
  }
  program_abort:
  f_close(&file);
  if(fatal) {
    LCD_Clear();
    LCD_printf(1, "Fatal error!\nAddress: %08lx\n", addr);
    if(saveProgress(addr, filename, CHIP_P) == FR_OK) {
      LCD_printf(0, "Progress has been\nsaved. Cycle power\nto continue.\n");
    }
  } else if (cancel) {
    LCD_Clear();
    LCD_printf(0, "Programming canceled\n");
    LCD_printf(0, "on user request.\n");
    LCD_printf(0, "Save progress to\n");
    LCD_printf(0, "continue later?\n");
    if(waitYesNo()) {
      if(saveProgress(addr, filename, CHIP_P) == FR_OK) {
        LCD_printf(0, "Progress has been\nsaved.");
      } else {
        f_unlink(PROG_SAVE_FILE);
      }
    }
  } else {
    LCD_xyprintf(0, 3, 2, "                    \rProgram complete!\n                    \rTime: %d s\n", (ticks - starttime) / 100);
    f_unlink(PROG_SAVE_FILE);
  }
  waitButton();
}

void P_Program() {
  FILINFO fno;

  P_Init();

  LCD_Clear();
  choose_file(&fno, "/", FA_READ);
  P_Program_Internal(fno.fname, 0);
}

void P_Verify() {
  uint32_t error = 0;
  UINT bytes_read;

  FILINFO fno;
  FIL file;

  uint32_t starttime = ticks;

  P_Init();

  LCD_Clear();
  choose_file(&fno, "/", FA_READ);
  LCD_Clear();
  P_genScrambleLookup();
  f_open(&file, fno.fname, FA_READ);

  for(int i = 0; i < END_ADDRESS_P; i += SECTOR_SIZE) {
    LCD_xyprintf(0, 0, 0, "Verify %3d%%\n", (int)((double)100.0*(double)i/(double)END_ADDRESS_P+0.5), i);
    P_WriteCycle(i, 0xf0);
    f_read(&file, buffer, SECTOR_SIZE * 2, &bytes_read);
    P_ScrambleBuffer(buffer, SECTOR_SIZE);
    if(!bytes_read) break;
    if(P_SectorVerify(i, buffer)) {
      error++;
    };
  }
  LCD_printf(error ? 1 : 2, "Verify done,\n%d bad blocks.\nTime: %d\n", error, (ticks-starttime) / 100);
  waitButton();
}

void P_Dump() {
  FIL file;
  FRESULT res;

  uint32_t starttime = ticks;

  P_Init();

  LCD_Clear();
  P_genDescrambleLookup();

  res = f_open(&file, DUMP_FILENAMES[CHIP_P], FA_CREATE_ALWAYS | FA_WRITE);
  if(check_fresult(res, "Could not open file\n%s\n", DUMP_FILENAMES[CHIP_P])) {
    return;
  }

  LCD_xyprintf(0, 2, 0, "-> %s\n", DUMP_FILENAMES[CHIP_P]);
  for(int i = 0; i < END_ADDRESS_P; i += SECTOR_SIZE) {
    LCD_xyprintf(0, 0, 0, "Dumping %3d%%\n", (int)((double)100.0*(double)i/(double)END_ADDRESS_P+0.5));
    P_SectorDump(i, buffer);
    P_ScrambleBuffer(buffer, SECTOR_SIZE);
    res = f_write(&file, buffer, SECTOR_SIZE * 2, NULL);
    if(check_fresult(res, "File write error\n")) {
      f_close(&file);
      return;
    }
  }
  f_close(&file);
  LCD_printf(2, "Dump finished!      \n");
  LCD_printf(2, "Time: %d s        \n", (ticks - starttime) / 100);
  waitButton();
}