#include "main.h"
#include "CV.h"
#include "menu.h"

// F0095H0 (8xMT28GU01G)
#define SECTOR_SIZE 0x20000
#define REGION_SIZE 512

/*
 * Chip layout:
 * ============
 *
 * 4 separate CE/OE pairs
 * 2 chips each are connected in parallel on DQ31:16 and DQ15:0
 *
 * CE1/2: DQ15:0
 * CE3/4: DQ31:16
 * 1 and 3 are mapped to A27 = 0
 * 2 and 4 are mapped to A27 = 1
 *
 * consequently, for reading, CE1/2 and CE3/4 must not be active simultaneously!
 *
 * Each of the logical chips has its own command interface and chip ID etc. that
 * can be read.
 *
 *
 * Signal to pin mapping:
 * ======================
 *
 *  DQ7:0  -> PA7:0
 *  DQ15:8 -> PC7:0
 *  A15:0  -> PB15:0
 *  A25:16 -> PE9:0
 *  A26    -> PE15
 *  CE1#   -> PD0
 *  CE2#   -> PD1
 *  CE3#   -> PD3
 *  CE4#   -> PD4
 *  OE1#   -> PD5
 *  OE2#   -> PD6
 *  OE3#   -> PD7
 *  OE4#   -> PD8
 *  RST#   -> PD9
 *  WE#    -> PD10
 *  WP#    -> PD11
 *
 * (MOSFET -> PA10)
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
 *  WP#    PD11  QSPI Flash IO0 / DI
 *  OE2#   PD6   SPI Flash CS#
 *  OE3#   PD7   SPI Flash DI
 *
 */

static char *addr_names[27] = {
  "A0", "A1", "A2", "A3", "A4", "A5", "A6", "A7",
  "A8", "A9", "A10", "A11", "A12", "A13", "A14", "A15",
  "A16", "A17", "A18", "A19", "A20", "A21", "A22", "A23",
  "A24", "A25", "A26"
};

static char *addr_pins[27] = {
  "B0", "B1", "B2", "B3", "B4", "B5", "B6", "B7",
  "B8", "B9", "B10", "B11", "B12", "B13", "B14", "B15",
  "E0", "E1", "E2", "E3", "E4", "E5", "E6", "E7",
  "E8", "E9", "E15"
};

static char *data_names[16] = {
  "D0", "D1", "D2", "D3", "D4", "D5", "D6", "D7",
  "D8", "D9", "D10", "D11", "D12", "D13", "D14", "D15"
};

static char *data_pins[16] = {
  "A0", "A1", "A2", "A3", "A4", "A5", "A6", "A7",
  "C0", "C1", "C2", "C3", "C4", "C5", "C6", "C7"
};

static char *ctrl_names[11] = {
  "CE1#", "CE2#", "CE3#", "CE4#", "OE1#", "OE2#", "OE3#", "OE4#",
  "RST#", "WE#", "WP#"
};

static char *ctrl_pins[11] = {
  "D0", "D1", "D3", "D4", "D5", "D6", "D7", "D8",
  "D9", "D10", "D11"
};

int CV_ADAPTER_INDEX_POGOPIN = 0;
int CV_ADAPTER_INDEX_POST = 1;

menu_entry CV_ADAPTER_SELECT[] = {
  MENU_ENTRY_TITLE("Select Adapter"),
  MENU_ENTRY_SELECT("Pogo Pin Adapter", &CV_ADAPTER_INDEX_POGOPIN),
  MENU_ENTRY_SELECT("Post Connector", &CV_ADAPTER_INDEX_POST),
  MENU_ENTRY_TERM()
};

/* line capacitance measurements (in cycle counts / 8), high->low
  *******************************************
  * high -> low, Pogo Pin adapter connected *
  *******************************************
    OPEN LINE:
    ==========
    A0-A26: 0x3c, 0x39, 0x47, 0x41, 0x3f, 0x33, 0x5b, 0x3b,
           +0x0a,+0x0a, 0x39, 0x3d, 0x3d, 0x41, 0x49, 0x49,
            0x40, 0x3d, 0x39,*0x1d, 0x39, 0x3b, 0x3b, 0x43,
            0x3b, 0x3b, 0x38

               + = pull-ups connected, never reaches low level
                   -> measured as low-high with counter pull-down

               * = measured with pull-up, not pull-down

    D0-D15: 0x50, 0x51, 0x53, 0x57, 0x69, 0x5d, 0x65, 0x65,
            0x52, 0x53, 0x59, 0x5b, 0x65, 0x5f, 0x63, 0x63

    CE1-4#: 0x2e, 0x30, 0x40, 0x41
    OE1-4#: 0x33, 0x5f, 0x3f, 0x3f
    RP#:    0x3f
    WE#:    0x3d
    WP#:    0x43


    CHIP CONNECTED:
    ===============
    A0-A26: 0x64, 0x61, 0x71, 0x6d, 0x69, 0x5f, 0x95, 0x67,
           +0x0e,+0x0e, 0x67, 0x67, 0x69, 0x69, 0x75, 0x75,
            0x6a, 0x6b, 0x61,*0x33, 0x61, 0x63, 0x65, 0x6b,
            0x65, 0x67, 0x6a

    D0-D15: 0x94, 0x97, 0x99, 0x9d, 0xaf, 0xa1, 0xa9, 0xa9,
            0x94, 0x97, 0x9b, 0x9d, 0xa7, 0xa1, 0xa7, 0xa7

    CE1-4#: 0x4a, 0x4c, 0x5c, 0x5f
    OE1-4#: 0x45, 0x75, 0x51, 0x51
    RP#:    0xd3
    WE#:    0x69
    WP#:    0x71

  *************************************************
  * high -> low, post connector adapter connected *
  *************************************************
    OPEN LINE:
    ==========
    A0-A26: 0x2e, 0x2b, 0x39, 0x35, 0x31, 0x25, 0x45, 0x2d,
           +0x08,+0x08, 0x2b, 0x2b, 0x27, 0x29, 0x31, 0x33,
            0x2a, 0x27, 0x2f,*0x17, 0x2d, 0x2f, 0x2f, 0x37,
            0x2f, 0x2b, 0x28

               + = pull-ups connected, never reaches low level
                   -> measured as low-high with counter pull-down

               * = measured with pull-up, not pull-down

    D0-D15: 0x2a, 0x2d, 0x2d, 0x2f, 0x41, 0x33, 0x39, 0x3f,
            0x2c, 0x2f, 0x33, 0x33, 0x39, 0x33, 0x3d, 0x3b

    CE1-4#: 0x20, 0x20, 0x2a, 0x2d
    OE1-4#: 0x21, 0x45, 0x29, 0x27
    RP#:    0x25
    WE#:    0x23
    WP#:    0x37


    CHIP CONNECTED:
    ===============
    A0-A26: 0x62, 0x5d, 0x6b, 0x6b, 0x67, 0x5b, 0x91, 0x63,
           +0x0e,+0x0e, 0x63, 0x61, 0x61, 0x61, 0x6b, 0x6d,
            0x64, 0x63, 0x5f,*0x33, 0x61, 0x63, 0x61, 0x67,
            0x63, 0x61, 0x61

    D0-D15: 0x86, 0x8b, 0x89, 0x89, 0x9b, 0x8b, 0x91, 0x9f,
            0x8c, 0x8f, 0x8f, 0x91, 0x99, 0x93, 0x9d, 0x9d

    CE1-4#: 0x42, 0x42, 0x52, 0x53
    OE1-4#: 0x3b, 0x63, 0x45, 0x41
    RP#:    0xbf
    WE#:    0x57
    WP#:    0x6b


*/


static const int addr_capa_thres[2][27] = {
  {
    0x52, 0x4e, 0x5b, 0x5a, 0x56, 0x4a, 0x7a, 0x52,
    0x0c, 0x0c, 0x52, 0x50, 0x4f, 0x50, 0x59, 0x5b,
    0x52, 0x51, 0x50, 0x2a, 0x51, 0x53, 0x51, 0x58,
    0x53, 0x50, 0x4f
  },
  {
    0x48, 0x44, 0x52, 0x50, 0x4c, 0x40, 0x6b, 0x48,
    0x0b, 0x0b, 0x47, 0x46, 0x44, 0x45, 0x4e, 0x50,
    0x47, 0x45, 0x47, 0x25, 0x47, 0x49, 0x48, 0x4f,
    0x49, 0x46, 0x44
  }
};

static const int data_capa_thres[2][16] = {
  {
    0x6a, 0x6e, 0x6d, 0x6d, 0x80, 0x70, 0x76, 0x82,
    0x6f, 0x72, 0x73, 0x74, 0x7c, 0x76, 0x80, 0x7f
  },
  {
    0x58, 0x5c, 0x5b, 0x5c, 0x6e, 0x5f, 0x65, 0x6f,
    0x5c, 0x5f, 0x61, 0x62, 0x69, 0x63, 0x6d, 0x6c
  }
};

static const int ctrl_capa_thres[2][11] = {
  {
    0x37, 0x37, 0x46, 0x47,
    0x19, 0x5a, 0x3c, 0x39,
    0x90, 0x47, 0x5b
  },
  {
    0x31, 0x31, 0x3e, 0x40,
    0x2e, 0x54, 0x37, 0x34,
    0x72, 0x3d, 0x51
  }
};

uint16_t cv_desc_data(uint16_t dat) // china pinout descramble
{
  uint16_t data = 0;

  if (dat & BIT0)  data |= BIT12;
  if (dat & BIT1)  data |= BIT9;
  if (dat & BIT2)  data |= BIT8;
  if (dat & BIT3)  data |= BIT2;
  if (dat & BIT4)  data |= BIT14;
  if (dat & BIT5)  data |= BIT0;
  if (dat & BIT6)  data |= BIT15;
  if (dat & BIT7)  data |= BIT4;
  if (dat & BIT8)  data |= BIT1;
  if (dat & BIT9)  data |= BIT5;
  if (dat & BIT10) data |= BIT13;
  if (dat & BIT11) data |= BIT3;
  if (dat & BIT12) data |= BIT7;
  if (dat & BIT13) data |= BIT6;
  if (dat & BIT14) data |= BIT11;
  if (dat & BIT15) data |= BIT10;

  return data;
}

uint16_t cv_scr_data(uint16_t dat) // china pinout scramble
{
  uint16_t data = 0;

  if (dat & BIT0)  data |= BIT5;
  if (dat & BIT1)  data |= BIT8;
  if (dat & BIT2)  data |= BIT3;
  if (dat & BIT3)  data |= BIT11;
  if (dat & BIT4)  data |= BIT7;
  if (dat & BIT5)  data |= BIT9;
  if (dat & BIT6)  data |= BIT13;
  if (dat & BIT7)  data |= BIT12;
  if (dat & BIT8)  data |= BIT2;
  if (dat & BIT9)  data |= BIT1;
  if (dat & BIT10) data |= BIT15;
  if (dat & BIT11) data |= BIT14;
  if (dat & BIT12) data |= BIT0;
  if (dat & BIT13) data |= BIT10;
  if (dat & BIT14) data |= BIT4;
  if (dat & BIT15) data |= BIT6;

  return data;
}

static inline void CV_nCE(uint32_t st)
{
  GPIOD -> BSRR = (0x1b << 16) | (st & 0x3) | ((st & 0xc) << 1);
}

static inline void CV_nOE(uint32_t st)
{
  GPIOD -> BSRR = (0x1e0 << 16) | ((st & 0xf) << 5);
}

static inline void CV_nRST(uint32_t st)
{
  GPIOD -> BSRR = BIT9 << 16 | ((st & 1) << 9);
}

static inline void CV_nWE(uint32_t st)
{
  GPIOD -> BSRR = BIT10 << 16 | ((st & 1) << 10);;
}

static inline void CV_nWP(uint32_t st)
{
  GPIOD -> BSRR = BIT11 << 16 | ((st & 1) << 11);
}

static inline void CV_SetAddress(uint32_t addr)
{
  GPIOB -> BSRR = (0xffff << 16) | (addr & 0xffff);
  GPIOE -> BSRR = (0x03ff << 16) | ((addr >> 16) & 0x03ff);
  GPIOE -> BSRR = BIT15 << 16 | ((addr & BIT26) >> 11);
}

/* Read 16 bits from the data bus */
static inline uint32_t CV_GetData(void)
{
  uint32_t data = ((GPIOA -> IDR) & 0xff);
	data |= ((GPIOC -> IDR) & 0xff) << 8;

  return data;
}

/* Output 16 bits to the data bus */
static inline void CV_SetData(uint16_t data)
{
  GPIOA -> BSRR = (0x00ff << 16) | ( data & 0x00ff);
  GPIOC -> BSRR = (0x00ff << 16) | ((data & 0xff00) >> 8);
}

void CV_Reset(void) {
  CV_nRST(0);
  Delay_us(1000);
  CV_nRST(1);
  Delay_us(1000);
}

void CV_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  // PA0-PA7: D0-D7
  __HAL_RCC_GPIOA_CLK_ENABLE();
  GPIO_InitStruct.Pin = GPIO_PIN_0|GPIO_PIN_1|GPIO_PIN_2|GPIO_PIN_3|GPIO_PIN_4|GPIO_PIN_5|GPIO_PIN_6|GPIO_PIN_7;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLDOWN;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
  
  // PB0-PB15: A0-A15
  __HAL_RCC_GPIOB_CLK_ENABLE();
  GPIO_InitStruct.Pin = GPIO_PIN_0|GPIO_PIN_1|GPIO_PIN_2|GPIO_PIN_3|GPIO_PIN_4|GPIO_PIN_5|GPIO_PIN_6|GPIO_PIN_7|
	                    GPIO_PIN_8|GPIO_PIN_9|GPIO_PIN_10|GPIO_PIN_11|GPIO_PIN_12|GPIO_PIN_13|GPIO_PIN_14|GPIO_PIN_15;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  // PC0-PC7: D8-D15
  __HAL_RCC_GPIOC_CLK_ENABLE();
  GPIO_InitStruct.Pin = GPIO_PIN_0|GPIO_PIN_1|GPIO_PIN_2|GPIO_PIN_3|GPIO_PIN_4|GPIO_PIN_5|GPIO_PIN_6|GPIO_PIN_7;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLDOWN;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  // PD0-PD1, PD3-PD11: CE1#, CE2#, CE3#, CE4#, OE1#, OE2#, OE3#, OE4#, RP#, WE#, WP#
  __HAL_RCC_GPIOD_CLK_ENABLE();
  GPIO_InitStruct.Pin = GPIO_PIN_0|GPIO_PIN_1|GPIO_PIN_3|GPIO_PIN_4|GPIO_PIN_5|GPIO_PIN_6|GPIO_PIN_7|
	                    GPIO_PIN_8|GPIO_PIN_9|GPIO_PIN_10|GPIO_PIN_11;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

  // PE0-PE9,PE15: A16-A26
  __HAL_RCC_GPIOE_CLK_ENABLE();
  GPIO_InitStruct.Pin = GPIO_PIN_0|GPIO_PIN_1|GPIO_PIN_2|GPIO_PIN_3|GPIO_PIN_4|GPIO_PIN_5|GPIO_PIN_6|GPIO_PIN_7|
	                    GPIO_PIN_8|GPIO_PIN_9|GPIO_PIN_15;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOE, &GPIO_InitStruct);

  CV_nCE(0x0F);
  CV_nOE(0x0F);
  CV_nRST(1);
  CV_nWE(1);
  CV_nWP(1);
}

void CV_Init(void) {
  CV_GPIO_Init();
  CV_Reset();
//  has_powercycle = CV_CheckPowercycle();
}

/*
 * inputs            outputs
 * halfword | A27 || CE1 | CE2 | CE3 | CE4
 * ---------+-----++-----+-----+-----+-----
 *   0  0   |  0  ||  1  |  1  |  1  |  1
 *   0  0   |  1  ||  1  |  1  |  1  |  1
 *   0  1   |  0  ||  0  |  1  |  1  |  1
 *   0  1   |  1  ||  1  |  0  |  1  |  1
 *   1  0   |  0  ||  1  |  1  |  0  |  1
 *   1  0   |  1  ||  1  |  1  |  1  |  0
 *   1  1   |  0  ||  0  |  1  |  0  |  1
 *   1  1   |  1  ||  1  |  0  |  1  |  0
 *
 */
/* index: {A27, halfword[1:0]} */
const uint8_t ST_MASK[8] = {
/*        inputs             outputs
                            { DQ15:8 }   { DQ7:0 }
           A27 | halfword || CE4 | CE3 | CE2 | CE1
          -----+----------++-----+-----+-----+----- */
  0x0f, /*  0  |   0  0   ||  1  |  1  |  1  |  1   */
  0x0e, /*  0  |   0  1   ||  1  |  1  |  1  |  0   */
  0x0b, /*  0  |   1  0   ||  1  |  0  |  1  |  1   */
  0x0a, /*  0  |   1  1   ||  1  |  0  |  1  |  0   */
  0x0f, /*  1  |   0  0   ||  1  |  1  |  2  |  1   */
  0x0d, /*  1  |   0  1   ||  1  |  1  |  0  |  1   */
  0x07, /*  1  |   1  0   ||  0  |  1  |  1  |  1   */
  0x05  /*  1  |   1  1   ||  0  |  1  |  0  |  1   */
};

/* Reverse bit order for bottom nibble of address */
const uint8_t ADDR_SCRTAB[16] = {
  0x0, 0x8, 0x4, 0xc,
  0x2, 0xa, 0x6, 0xe,
  0x1, 0x9, 0x5, 0xd,
  0x3, 0xb, 0x7, 0xf
};

uint32_t CV_ReadCycle(uint8_t halfword, uint32_t addr)
{
  uint32_t data;

  CV_SetAddress(addr);
  CV_nCE(CV_ADDR2ST(halfword, addr));
  CV_nOE(CV_ADDR2ST(halfword, addr));
  Delay_cycles(58); // ~120ns
  data = CV_GetData();
  CV_nOE(0x0F);
  CV_nCE(0x0F);
  return data;
}

void CV_WriteCycle(uint8_t halfword, uint32_t addr, uint16_t data)
{
  CV_SetAddress(addr);
  CV_SetData(data);
  CV_nOE(0x0F);
  CV_nCE(CV_ADDR2ST(halfword, addr));
  DATADIR_OUT();
  CV_nWE(0);
  Delay_cycles(47); // ~96ns
  CV_nWE(1);
  DATADIR_IN();
  CV_nCE(0x0F);
}

/**
 * Wait for status bit to be set on a chip pair (selectable single/dual word, pair
 * identified by halfword(s) and address MSB)
 * @param status optional pointer to an array of two uin16_t to be assigned the last read status words
 * @param halfword 1 = lower word, 2 = upper word, 3 = both words (full word)
 * @param addr select upper or lower half of storage space (A27)
 * @param mask compare mask for status word, success if at least one bit in mask is set in status word
 * @param timeout timeout in number of timer ticks (currently 10ms)
 *
 * @return result 2-bit flags indicating which halfword(s) the timeout occurred on, if any
 */
int CV_WaitStatus(uint16_t *status, uint8_t halfword, uint32_t addr, uint16_t mask, int timeout) {
  int result = 0;
  uint16_t data[2] = { 0xffff, 0xffff };
  uint32_t endtime = ticks + timeout;

  do {
    if(halfword & 1) {
      /* speed up data line pull-down in case chip has gone High-Z
         to prevent previous bus data from being mistaken as status word */
      CV_SetData(0x0000);
      DATADIR_OUT();
      DATADIR_IN();
      data[0] = CV_ReadCycle(1, addr);
    }
    if(halfword & 2) {
      /* speed up data line pull-down in case chip has gone High-Z
         to prevent previous bus data from being mistaken as status word */
      CV_SetData(0x0000);
      DATADIR_OUT();
      DATADIR_IN();
      data[1] = CV_ReadCycle(2, addr);
    }

    if(ticks > endtime) {
      break;
    }
  } while (((halfword & 1) && !(data[0] & mask))
        || ((halfword & 2) && !(data[1] & mask)));

  if(status) {
    status[0] = data[0];
    status[1] = data[1];
  }

  /* Timeout - which half triggered it? */
  if((halfword & 1) && !(data[0] & mask)) {
    result |= 1;
  }
  if((halfword & 2) && !(data[1] & mask)) {
    result |= 2;
  }

  return result;
}

/** Get Manufacturer ID from chip
 * @param chip Number of the chip to get the ID from (0-3)
 */
Flash_ID CV_ReadID(uint8_t halfword, uint32_t addr) {
  Flash_ID result;

  CV_WriteCycle(halfword, addr, 0x90);
  Delay_us(100);
  result.vendor_id = CV_ReadCycle(halfword, addr);
  result.chip_id = CV_ReadCycle(halfword, addr + 1);
  return result;
}

int CV_CheckID(uint8_t halfword, uint32_t addr) {
  Flash_ID id = CV_ReadID(halfword, addr);
  if(id.chip_id != 0x88b0 || id.vendor_id != 0x0089) {
    return 1;
  }
  return 0;
}

int CV_SectorBlankCheck(uint8_t halfword, uint32_t addr) {
  uint16_t sr[2] = { 0x80, 0x80 };
  int res = 0;
  CV_WriteCycle(halfword, addr, 0x50);
  CV_WriteCycle(halfword, addr, 0x60);
  CV_WriteCycle(halfword, addr, 0xd0);
  CV_WaitStatus(sr, halfword, addr, 0x80, 10);
  LCD_xyprintf(0, 1, 0, "BC %08lx.%d     \n", addr, halfword);
  CV_WriteCycle(halfword, addr, 0xbc);
  CV_WriteCycle(halfword, addr, 0xd0);
  CV_WaitStatus(sr, halfword, addr, 0x80, 500);
  if(sr[0] != 0x80) res |= 1;
  if(sr[1] != 0x80) res |= 2;
  res &= halfword;
  if(res) {
    LCD_xyprintf(0, 2, 1, "BC sr=%04x %04x\n", sr[0], sr[1]);
  } else {
    LCD_xyprintf(0, 2, 1, "                    \n");
  }
  return res;
}

uint32_t CV_SectorErase(uint8_t halfword, uint32_t addr)
{
  uint16_t sr[2];
  uint32_t res = 0;

  int try = 0;
  int dirty = halfword;
  do {
    CV_WriteCycle(dirty, addr, 0x50);
    CV_WriteCycle(dirty, addr, 0x60);
    CV_WriteCycle(dirty, addr, 0xd0);
    CV_WaitStatus(sr, dirty, addr, 0x80, 10);
    LCD_xyprintf(0, 1, 0, "ER %08lx.%d [%d]\n", addr, dirty, try++);
    CV_WriteCycle(dirty, addr, 0x20);
    CV_WriteCycle(dirty, addr, 0xd0);
    Delay_us(100);
    res = CV_WaitStatus(sr, dirty, addr, 0x80, 500);
    if(res) {
      CV_Reset();
      LCD_printf(0, "ER Timeout          \n");
      CV_SectorBlankCheck(dirty, addr);
      if(flag_button & FLAG_BTN_BRD_LONG) {
        flag_button &= ~(FLAG_BTN_BRD_LONG);
        break;
      }
      if(dirty & 1) {
        if(CV_CheckID(1, addr)) {
          res |= 4;
          goto abort;
        }
      }
      if(dirty & 2) {
        if(CV_CheckID(2, addr)) {
          res |= 4;
          goto abort;
        }
      }
      continue;
    }
    if(sr[0] == 0x80) dirty &= ~1;
    if(sr[1] == 0x80) dirty &= ~2;
    if(dirty) {
      LCD_xyprintf(0, 2, 1, "ER sr=%04x %04x\n", sr[0], sr[1]);
      CV_SectorBlankCheck(dirty, addr);
      CV_Reset();
    }
    if(flag_button & (FLAG_BTN_BRD)) {
      flag_button &= ~(FLAG_BTN_BRD);
      res |= 8;
      break;
    }
  } while (dirty);

  CV_WriteCycle(halfword, addr, 0x50); // clear status
//  CV_WriteCycle(halfword, addr, 0xFF); // exit
abort:
  return res;
}

int CV_SectorVerify(uint8_t halfword, uint32_t addr, uint16_t *buffer) {
  uint16_t data, compare;
  uint32_t src;
  int dirty = 0;

  CV_WriteCycle(3, addr, 0x50);
  CV_WriteCycle(3, addr, 0xff);
  LCD_xyprintf(0, 1, 0, "VR %08lx.%d       \r", addr, halfword);
  for(int j = 0; j < SECTOR_SIZE; j++) {
    src = (j & ~0x1ff) | addr_lookup[j & 0x1ff];
    if((halfword & 1) && !(dirty & 1)) {
      data = CV_ReadCycle(1, addr+j);
      compare = buffer[src*2];
      if(data != compare) {
        LCD_xyprintf(0, 2, 3, "VR %04x != %04x\r", data, compare);
        dirty |= 1;
      }
    }
    if((halfword & 2) && !(dirty & 2)) {
      data = CV_ReadCycle(2, addr+j);
      compare = buffer[src*2+1];
      if(data != compare) {
        LCD_xyprintf(0, 2, 3, "VR %04x != %04x\r", data, compare);
        dirty |= 2;
      }
    }
    if(dirty == 3) break;
  }
  return dirty;
}

/**
 * @brief Program a sector on one or both half words
 *
 * @param halfword select halfword to program (1 = low, 2 = high, 3 = full word)
 * @param addr sector address
 * @return int bitmask of failed halfwords
 */
int CV_SectorProgram(uint8_t halfword, uint32_t addr, uint16_t *buf) {
  uint16_t sr[2];
  uint16_t data1, data2;
  uint32_t addr_final;
  int src;

  int active = halfword;
  LCD_xyprintf(0, 1, 0, "PG %08lx.%d\n", addr, active);
  for(int j = 0; j < SECTOR_SIZE; j += REGION_SIZE) {
    CV_WriteCycle(active, addr+j, 0x50);
    CV_WriteCycle(active, addr+j, 0x60);
    CV_WriteCycle(active, addr+j, 0xd0);
    CV_WaitStatus(sr, active, addr+j, 0x0080, 10);
    CV_WriteCycle(active, addr+j, 0xe9);
    CV_WaitStatus(sr, active, addr+j, 0x0080, 10);
    CV_WriteCycle(active, addr+j, 0x1ff);
    for(int d = 0; d < 512; d++) {
      src = addr_lookup[d];
      addr_final = addr + j + d;

      data1 = buf[(j+src)*2];
      data2 = buf[(j+src)*2+1];
      if(active & 1) {
        CV_WriteCycle(1, addr_final, data1);
      }
      if(active & 2) {
        CV_WriteCycle(2, addr_final, data2);
      }
    }
    CV_WriteCycle(active, addr+j, 0xd0);
    CV_WaitStatus(sr, active, addr+j, 0x0080, 10);
    if(((active & 1) && sr[0] != 0x80)
     ||((active & 2) && sr[1] != 0x80)) {
      LCD_xyprintf(0, 2, 1, "PG sr=%04x %04x\n", sr[0], sr[1]);
    }
    CV_WriteCycle(active, addr+j, 0x50);
    if(sr[0] != 0x80) active &= ~1;
    if(sr[1] != 0x80) active &= ~2;
    if(!active) {
      break;
    }
  }
  if(active & halfword)LCD_xyprintf(0, 2, 0, "                    \n");
  return (~active) & 3 & halfword;
}

void CV_SectorDump(uint32_t addr, uint16_t *buffer) {
  uint32_t src;
  uint16_t data;
  CV_WriteCycle(3, addr, 0x50);
  CV_WriteCycle(3, addr, 0xff);
  LCD_xyprintf(0, 1, 0, "DP %08lx         \r", addr);
  for(int j = 0; j < SECTOR_SIZE; j++) {
    src = (j & ~0xf) | addr_lookup[j & 0xf];
    data = CV_ReadCycle(1, addr+j);
    buffer[src*2] = data;
    data = CV_ReadCycle(2, addr+j);
    buffer[src*2+1] = data;
  }
}

void CV_TestAllPins(uint16_t data_mask, uint32_t addr_mask, uint16_t ctrl_mask, char *probename, char *probepin) {
  uint16_t test_data = (GPIOA->IDR & 0xff) | ((GPIOC->IDR & 0xff) << 8);
  uint32_t test_addr = (GPIOB->IDR | ((GPIOE->IDR & 0x3ff) << 16) | ((GPIOE->IDR & BIT15) << 11));
  uint16_t test_ctrl = (GPIOD->IDR & 0x3) | ((GPIOD->IDR >> 1) & 0x7fc);

  test_data ^= data_mask;
  test_addr ^= (addr_mask | 0x300);
  test_ctrl ^= ctrl_mask;

  for(int i = 0; i < 16; i++) {
    if(test_data & (1 << i)) {
      LCD_printf(1, "SHORT %s-%s\nCheck pins %s-%s\n", probename, data_names[i], probepin, data_pins[i]);
      waitButton();
    }
  }
  for(int i = 0; i < 27; i++) {
    if(test_addr & (1 << i)) {
      LCD_printf(1, "SHORT %s-%s\nCheck pins %s-%s\n", probename, addr_names[i], probepin, addr_pins[i]);
      waitButton();
    }
  }
  for(int i = 0; i < 11; i++) {
    if(test_ctrl & (1 << i)) {
      LCD_printf(1, "SHORT %s-%s\nCheck pins %s-%s\n", probename, ctrl_names[i], probepin, ctrl_pins[i]);
      waitButton();
    }
  }
}

/** Measure line capacitances for checking chip connectivity.
 * @param addr pointer to an array of 27 ints to hold measurements for address lines.
 * @param data pointer to an array of 16 ints to hold measurements for data lines.
 * @param ctrl pointer to an array of 11 ints to hold measurements for control lines.
 */
void CV_GetLineCapacitances(int *addr, int *data, int *ctrl) {
  /* capacitance check: enable pull-downs, output high level
     EXCEPTIONS:
      - A8-A9 (PB8-PB9) are measured as low-high transitions,
        so a low level is output initially
      - A19 (PE3) is measured with a pull-up to counteract quick
        pull-down by the connected LED driver transistor
  */
  GPIOA->PUPDR = (GPIOA->PUPDR & ~0xffff) | 0xaaaa;
  GPIOC->PUPDR = (GPIOC->PUPDR & ~0xffff) | 0xaaaa;
  GPIOB->PUPDR = 0xaaaaaaaa;
  GPIOE->PUPDR = (GPIOE->PUPDR & ~0xc00fffff) | 0x800aaa6a;
  GPIOD->PUPDR = (GPIOD->PUPDR & ~0x00ffffcf) | 0x00aaaa8a;

  GPIOA->MODER = (GPIOA->MODER & ~0xffff);
  GPIOC->MODER = (GPIOC->MODER & ~0xffff);
  GPIOB->MODER = 0;
  GPIOE->MODER = (GPIOE->MODER & ~0xc00fffff);
  GPIOD->MODER = (GPIOD->MODER & ~0x00ffffcf);

  GPIOA->BSRR = 0xff;
  GPIOC->BSRR = 0xff;
  GPIOB->BSRR = (0x0300 << 16) | 0xfcff;
  GPIOE->BSRR = 0x83ff;
  GPIOD->BSRR = 0xffb;

  /* disable any sources that might interrupt measurement */
  SysTick->CTRL &= ~(SysTick_CTRL_ENABLE_Msk);
  NVIC_DisableIRQ(TIM1_UP_IRQn);
  NVIC_DisableIRQ(OTG_FS_WKUP_IRQn);
  NVIC_ClearPendingIRQ(TIM1_UP_IRQn);
  NVIC_ClearPendingIRQ(OTG_FS_WKUP_IRQn);

  int count[27];

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

    /* measure A26, high->low transition */
    count[26] = 0;
    GPIO_MODE_OUT(GPIOE, 15);
    Delay_us(100);
    GPIO_MODE_IN(GPIOE, 15);
    DWT->CYCCNT = 0;
    while((GPIOE->IDR & (1 << 15)));
    count[26] = DWT->CYCCNT / 8;

    memcpy(addr, count, 27*sizeof(int));
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
    /* measure CE1-2#, high->low transition */
    for(int i = 0; i < 2; i++) {
      count[i] = 0;
      GPIO_MODE_OUT(GPIOD, i);
      Delay_us(100);
      GPIO_MODE_IN(GPIOD, i);
      DWT->CYCCNT = 0;
      while((GPIOD->IDR & (1 << i)));
      count[i] = DWT->CYCCNT / 8;
    }
    /* measure CE3-4#, OE1-4#, RP#, WE#, WP#, high->low transition */
    for(int i = 3; i < 12; i++) {
      count[i - 1] = 0;
      GPIO_MODE_OUT(GPIOD, i);
      Delay_us(100);
      GPIO_MODE_IN(GPIOD, i);
      DWT->CYCCNT = 0;
      while((GPIOD->IDR & (1 << i)));
      count[i - 1] = DWT->CYCCNT / 8;
    }
    memcpy(ctrl, count, 11*sizeof(int));
  }
  __DSB(); __DMB(); __ISB();
  NVIC_EnableIRQ(TIM1_UP_IRQn);
  NVIC_EnableIRQ(OTG_FS_WKUP_IRQn);
  SysTick->CTRL |= SysTick_CTRL_ENABLE_Msk;

  /* revert to normal GPIO operation */
  CV_GPIO_Init();
}

void CV_Hexdump(uint32_t addr, uint32_t count) {
  int valuecount = 0;
  for(int i = 0; i < count; i++) {
    LCD_printf(0, "%04x ", CV_ReadCycle(1, addr+i));
    Delay_us(1000);
    LCD_printf(0, "%04x ", CV_ReadCycle(2, addr+i));
    Delay_us(1000);
    valuecount++;
    if(valuecount == 4) {
      LCD_printf(0, "\n");
      valuecount = 0;
    }
  }
  if(valuecount) LCD_printf(0, "\n");
}

void CV_Test() {
  menu_entry *adapter;
  int adapter_idx;
  adapter = menu_select(CV_ADAPTER_SELECT);
  adapter_idx = *((int*)adapter->tgt);
  LCD_Clear();
  LCD_xyprintf(0, 0, 0, "%s", "Chip Connection Test\n");
  Flash_ID chip_id[4];

  int error = 0;

  for(int i = 0; i < 4; i++) {
    chip_id[i] = CV_ReadID(i & 2 ? 2 : 1, i & 1 ? BIT27 : 0);
    if(chip_id[i].vendor_id != 0x89 || chip_id[i].chip_id != 0x88b0) {
      error++;
      LCD_printf(1, "ID %d: %04x:%04x\n", i+1, chip_id[i].vendor_id, chip_id[i].chip_id);
    }
  }
  if(!error) {
    LCD_printf(2, "Chip IDs ok\n");
  } else {
    LCD_printf(1, "Chip IDs bad\n");
    waitButton();
  }
  error = 0;
  CV_WriteCycle(3, 0, 0xFF);
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
  GPIOE->PUPDR = (GPIOE->PUPDR & ~0xc00fffff) | 0x40055555;
  GPIOD->PUPDR = (GPIOD->PUPDR & ~0x00ffffcf) | 0x00555545;

  GPIOA->MODER = (GPIOA->MODER & ~0xffff);
  GPIOC->MODER = (GPIOC->MODER & ~0xffff);
  GPIOB->MODER = 0;
  GPIOE->MODER = (GPIOE->MODER & ~0xc00fffff);
  GPIOD->MODER = (GPIOD->MODER & ~0x00ffffcf);

  /* give some time for signals to float and pull-ups to take effect */
  Delay_us(10000);

  test_data = (GPIOA->IDR & 0xff) | ((GPIOC->IDR & 0xff) << 8);
  test_addr = (GPIOB->IDR | ((GPIOE->IDR & 0x3ff) << 16) | ((GPIOE->IDR & BIT15) << 11));
  test_ctrl = (GPIOD->IDR & 0x3) | ((GPIOD->IDR >> 1) & 0x7fc);
  LCD_printf(0, "Stuck pins check...\n");
  for(int i = 0; i < 16; i++) {
    if(!(test_data & (1 << i))) {
      LCD_printf(1, "D%d/%d stuck low!\nCheck pin %s\n", i, i+16, data_pins[i]);
      waitButton();
    }
  }

  /* A19 (pin E3) cannot be tested this way because the LED transistor defeats the
     internal pull-up. */
  for(int i = 0; i < 27; i++) {
    if(!(test_addr & (1 << i)) && i != 19) {
      LCD_printf(1, "A%d stuck low!\nCheck pin %s\n", i, addr_pins[i]);
      waitButton();
    }
  }

  for(int i = 0; i < 11; i++) {
    if(!(test_ctrl & (1 << i))) {
      LCD_printf(1, "%s stuck low!\nCheck pin %s\n", ctrl_names[i], ctrl_pins[i]);
      waitButton();
    }
  }

  /* switch to pull-down and test for the opposite */
  GPIOA->PUPDR = (GPIOA->PUPDR & ~0xffff) | 0xaaaa;
  GPIOC->PUPDR = (GPIOC->PUPDR & ~0xffff) | 0xaaaa;
  GPIOB->PUPDR = 0xaaaaaaaa;
  GPIOE->PUPDR = (GPIOE->PUPDR & ~0xc00fffff) | 0x800aaaaa;
  GPIOD->PUPDR = (GPIOD->PUPDR & ~0x00ffffcf) | 0x00aaaa8a;
  Delay_us(10000);

  test_data = (GPIOA->IDR & 0xff) | ((GPIOC->IDR & 0xff) << 8);
  test_addr = (GPIOB->IDR | ((GPIOE->IDR & 0x3ff) << 16) | ((GPIOE->IDR & BIT15) << 11));
  test_ctrl = (GPIOD->IDR & 0x3) | ((GPIOD->IDR >> 1) & 0x7fc);

  for(int i = 0; i < 16; i++) {
    if(test_data & (1 << i)) {
      LCD_printf(1, "D%d/%d stuck high!\nCheck pin %s\n", i, i+16, data_pins[i]);
      waitButton();
    }
  }

  /* A8+A9 (pins B8+B9) cannot be tested this way because they are connected to
     I2C pull-ups on the WeAct board. */
  for(int i = 0; i < 27; i++) {
    if((test_addr & (1 << i)) && i != 8 && i != 9) {
      LCD_printf(1, "A%d stuck high!\nCheck pin %s\n", i, addr_pins[i]);
      waitButton();
    }
  }

  for(int i = 0; i < 11; i++) {
    if(test_ctrl & (1 << i)) {
      LCD_printf(1, "%s stuck high!\nCheck pin %s\n", ctrl_names[i], ctrl_pins[i]);
      waitButton();
    }
  }


  LCD_printf(0, "Open data line check\n");
  for(int chip = 0; chip < 4; chip++) {
    CV_GPIO_Init();
    CV_nCE(0xf & ~(1 << chip));
    CV_nOE(0xf & ~(1 << chip));
    DATADIR_IN();
    CV_SetAddress(0);
    GPIOA->PUPDR = (GPIOA->PUPDR & ~0xffff) | 0xaaaa;
    GPIOC->PUPDR = (GPIOC->PUPDR & ~0xffff) | 0xaaaa;
    Delay_us(10000);
    test_data = CV_GetData();
    GPIOA->PUPDR = (GPIOA->PUPDR & ~0xffff) | 0x5555;
    GPIOC->PUPDR = (GPIOC->PUPDR & ~0xffff) | 0x5555;
    Delay_us(10000);
    uint16_t test_data_errors = CV_GetData() ^ test_data;

    error = 0;
    for(int i = 0; i < 16; i++) {
      if(test_data_errors & (1 << i)) {
        error++;
      }
    }

    if(error > 10) {
      LCD_printf(1, "No output?\nCheck CE%d/OE%d\n", chip+1, chip+1);
      waitButton();
    } else {
      for(int i = 0; i < 16; i++) {
        if(test_data_errors & (1 << i)) {
          LCD_printf(1, "D%d open!\nCheck pin %s\n", i + ((chip & 2) << 3), data_pins[i]);
          waitButton();
        }
      }
    }
  }

  LCD_printf(0, "Short check...\n");

  GPIOA->PUPDR = (GPIOA->PUPDR & ~0xffff) | 0xaaaa;
  GPIOC->PUPDR = (GPIOC->PUPDR & ~0xffff) | 0xaaaa;
  GPIOB->PUPDR = 0xaaaaaaaa;
  GPIOE->PUPDR = (GPIOE->PUPDR & ~0xc00fffff) | 0x800aaaaa;
  GPIOD->PUPDR = (GPIOD->PUPDR & ~0x00ffffcf) | 0x00aaaa8a;

  GPIOA->MODER = (GPIOA->MODER & ~0xffff);
  GPIOC->MODER = (GPIOC->MODER & ~0xffff);
  GPIOB->MODER = 0;
  GPIOE->MODER = (GPIOE->MODER & ~0xc00fffff);
  GPIOD->MODER = (GPIOD->MODER & ~0x00ffffcf);

  GPIOA->BSRR = 0xff;
  GPIOC->BSRR = 0xff;
  GPIOB->BSRR = 0xffff;
  GPIOE->BSRR = 0x83ff;
  GPIOD->BSRR = (BIT9 << 16) | 0xdfb;

  Delay_us(10000);

  /* D0-D7 (GPIOA) */
  for(int i = 0; i < 8; i++) {
    GPIO_MODE_OUT(GPIOA, i);
    Delay_us(1000);
    CV_TestAllPins(1 << i, 0, 0, data_names[i], data_pins[i]);
    GPIO_MODE_IN(GPIOA, i);
  }

  /* D8-D15 (GPIOC) */
  for(int i = 0; i < 8; i++) {
    GPIO_MODE_OUT(GPIOC, i);
    Delay_us(1000);
    CV_TestAllPins(1 << (i + 8), 0, 0, data_names[i+8], data_pins[i+8]);
    GPIO_MODE_IN(GPIOC, i);
  }

  /* A0-A15 (GPIOB) */
  for(int i = 0; i < 16; i++) {
    GPIO_MODE_OUT(GPIOB, i);
    Delay_us(1000);
    CV_TestAllPins(0, 1 << i, 0, addr_names[i], addr_pins[i]);
    GPIO_MODE_IN(GPIOB, i);
  }

  /* A16-A25 (GPIOE) */
  for(int i = 0; i < 10; i++) {
    GPIO_MODE_OUT(GPIOE, i);
    Delay_us(1000);
    CV_TestAllPins(0, 1 << (i + 16), 0, addr_names[i+16], addr_pins[i+16]);
    GPIO_MODE_IN(GPIOE, i);
  }

  /* A26 (GPIOE) */
  GPIO_MODE_OUT(GPIOE, 15);
  Delay_us(1000);
  CV_TestAllPins(0, 1 << 26, 0, addr_names[26], addr_pins[26]);
  GPIO_MODE_IN(GPIOE, 15);

  /* CE1#, CE2# (GPIOD) */
  for(int i = 0; i < 2; i++) {
    GPIO_MODE_OUT(GPIOD, i);
    Delay_us(1000);
    CV_TestAllPins(0, 0, 1 << i, ctrl_names[i], ctrl_pins[i]);
    GPIO_MODE_IN(GPIOD, i);
  }

  /* CE3#, CE4#, OE#1-4, RP#, WE#, WP# (GPIOD) */
  for(int i = 3; i < 12; i++) {
    /* test RP# separately lest chip asserts data lines */
    if(i == 9) continue;
    GPIO_MODE_OUT(GPIOD, i);
    Delay_us(1000);
    CV_TestAllPins(0, 0, 1 << (i - 1), ctrl_names[i-1], ctrl_pins[i-1]);
    GPIO_MODE_IN(GPIOD, i);
  }

  /* RP# test: deassert CE# signals to prevent chip from driving lines */
  GPIO_MODE_OUT(GPIOD, 4);
  GPIO_MODE_OUT(GPIOD, 3);
  GPIO_MODE_OUT(GPIOD, 1);
  GPIO_MODE_OUT(GPIOD, 0);
  /* then set RP# level high as well */
  GPIOD->BSRR = 0xffb;
  GPIO_MODE_OUT(GPIOD, 9);
  Delay_us(1000);
  CV_TestAllPins(0, 0, 0xf | (1 << 8), ctrl_names[8], ctrl_pins[8]);
  GPIO_MODE_IN(GPIOD, 9);

  LCD_printf(0, "Capacitance check...\n");
  int addr_capa[27];
  int data_capa[16];
  int ctrl_capa[11];
  CV_GetLineCapacitances(addr_capa, data_capa, ctrl_capa);
  for(int i = 0; i < 27; i++) {
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
  for(int i = 0; i < 11; i++) {
    if(ctrl_capa[i] < ctrl_capa_thres[adapter_idx][i]) {
      LCD_printf(1, "%s open (pin %s)\n", ctrl_names[i], ctrl_pins[i]);
      waitButton();
    }
  }
  LCD_printf(0, "Done. <key>\n");
  waitButton();
  LCD_Clear();
}

void CV_PrintCapas(int *results, const int *thres, int num) {
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

void CV_CapaView() {
  menu_entry *adapter_entry;
  int adapter;
  adapter_entry = menu_select(CV_ADAPTER_SELECT);
  adapter = *(int*)adapter_entry->tgt;

  LCD_Clear();
  LCD_xyprintf(0,0,0,"Capa check - Address\n");
  int result[27];
  while(!(flag_button & FLAG_BTN_BRD)) {
    CV_GetLineCapacitances(result, NULL, NULL);
    CV_PrintCapas(result, addr_capa_thres[adapter], 27);
  }
  flag_button &= ~FLAG_BTN_BRD;

  LCD_Clear();
  LCD_xyprintf(0,0,0,"Capa check - Data   \n");
  while(!(flag_button & FLAG_BTN_BRD)) {
    CV_GetLineCapacitances(NULL, result, NULL);
    CV_PrintCapas(result, data_capa_thres[adapter], 16);
  }
  flag_button &= ~FLAG_BTN_BRD;

  LCD_Clear();
  LCD_xyprintf(0,0,0,"Capa check - Control\n");
  while(!(flag_button & FLAG_BTN_BRD)) {
    CV_GetLineCapacitances(NULL, NULL, result);
    CV_PrintCapas(result, ctrl_capa_thres[adapter], 11);
  }
  flag_button &= ~FLAG_BTN_BRD;
  SysTick->CTRL |= SysTick_CTRL_ENABLE_Msk;

  /* revert to normal GPIO operation */
  CV_GPIO_Init();
}

void CV_Erase() {
  uint32_t starttime = ticks;
  LCD_Clear();
  LCD_xyprintf(0, 0, 0, "Erasing Chip\n(aggressively)\n");
  for(int i = 0; i < END_ADDRESS_C; i += SECTOR_SIZE) {
    int res = CV_SectorErase(3, i);
    if(res & 0xc) {
      if(res & 0x8) {
        LCD_printf(3, "Erase aborted on    \nuser request.     \n");
      } else {
        LCD_printf(1, "Error during erase\n");
      }
      goto erase_abort;
    }
  }
  LCD_printf(2, "Erase complete!\nTime: %d s     \n", (ticks - starttime) / 100);
erase_abort:
  waitButton();
}

void CV_BlankCheck() {
  uint16_t sr[2];
  LCD_Clear();
  LCD_xyprintf(0, 0, 0, "Blank Check\n");
  int color = 0;
  int nonblank = 0;

  for(int i = 0; i < END_ADDRESS_C; i += SECTOR_SIZE) {
    CV_WriteCycle(3, i, 0x50);
    CV_WriteCycle(3, i, 0x60);
    CV_WriteCycle(3, i, 0xd0);
    CV_WaitStatus(sr, 3, i, 0x80, 10);
    LCD_xyprintf(0, 1, 0, "%08lx.%d [%d]\n", i, 3, nonblank);
    CV_WriteCycle(3, i, 0xbc);
    CV_WriteCycle(3, i, 0xd0);
    CV_WaitStatus(sr, 3, i, 0x80, 500);
    if(sr[0] != 0x80 || sr[1] != 0x80) {
      LCD_printf(0, "sr = %04x %04x\r", sr[0], sr[1]);
    } else {
      LCD_printf(0, "              \r");
    }
    if(sr[0] != 0x80) nonblank++;
    if(sr[1] != 0x80) nonblank++;
  }
  color = nonblank ? 1 : 2;
  LCD_printf(color, "Blank check done!\n%d sectors\nnon-blank.", nonblank);
  waitButton();
}

void CV_genScrambleLookup(chip_t chiptype) {
  if(chiptype == CHIP_C) {
    for(int i = 0; i < 65536; i++) {
      scramble_lookup[i] = cv_scr_data(i);
    }
    for(int i = 0; i < 512; i++) {
      addr_lookup[i] = (i & ~0xf) | ADDR_SCRTAB[i & 0xf];
    }
  } else {
    for(int i = 0; i < 65536; i++) {
      scramble_lookup[i] = i;
    }
    for(int i = 0; i < 512; i++) {
      addr_lookup[i] = i;
    }
  }
}

void CV_genDescrambleLookup(chip_t chiptype) {
  if(chiptype == CHIP_C) {
    for(int i = 0; i < 65536; i++) {
      scramble_lookup[i] = cv_desc_data(i);
    }
    for(int i = 0; i < 512; i++) {
      addr_lookup[i] = (i & ~0xf) | ADDR_SCRTAB[i & 0xf];
    }
  } else {
    for(int i = 0; i < 65536; i++) {
      scramble_lookup[i] = i;
    }
    for(int i = 0; i < 512; i++) {
      addr_lookup[i] = i;
    }
  }
}

void CV_ScrambleBuffer(uint16_t *buffer, uint32_t length) {
  for(int i = 0; i < length; i++) {
    buffer[i] = scramble_lookup[buffer[i]];
  }
}

void CV_Program_Internal(const char *filename, uint32_t address, chip_t chiptype) {
  uint32_t addr;
  FIL file;
  UINT bytes_read;
  uint32_t starttime = ticks;
  uint8_t erase_status = 0;
  uint8_t fatal = 0, cancel = 0;
  FRESULT res;

  LCD_Clear();
  res = f_open(&file, filename, FA_READ);
  if(check_fresult(res, "Could not open file:\n%s\n", filename)) {
    return;
  }

  res = f_lseek(&file, address * 4);
  if(check_fresult(res, "Seek to %lx failed\n", address * 4)) {
    f_close(&file);
    return;
  };

  CV_genScrambleLookup(chiptype);

  for(addr = address; addr < END_ADDRESS_C; addr += SECTOR_SIZE) {
    LCD_xyprintf(0, 0, 0, "Programming %3d%%\n", (int)((double)100.0 * (double)addr / (double)END_ADDRESS_C));
    uint8_t erase = 3;
    res = f_read(&file, buffer, SECTOR_SIZE * 4, &bytes_read);
    check_fresult(res, "File read failed\n");
    CV_ScrambleBuffer(buffer, SECTOR_SIZE * 2);
    if(!bytes_read) break;
    /* first, determine if we need to reprogram at all */
    while((erase = CV_SectorVerify(3, addr, buffer))) {
      do {
        erase_status = CV_SectorErase(erase, addr);
        fatal = erase_status & 4;
        cancel = erase_status & 8;
        if(fatal || cancel) goto program_abort;
        CV_SectorBlankCheck(erase, addr);
        erase = CV_SectorProgram(erase, addr, buffer);
        if(erase) {
          LCD_xyprintf(0, 4, 1, "Retrying half %d     \n", erase);
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
    if(saveProgress(addr, filename, chiptype) == FR_OK) {
      LCD_printf(0, "Progress has been\nsaved. Cycle power\nto continue.\n");
    }
  } else if (cancel) {
    LCD_Clear();
    LCD_printf(0, "Programming canceled\n");
    LCD_printf(0, "on user request.\n");
    LCD_printf(0, "Save progress to\n");
    LCD_printf(0, "continue later?\n");
    if(waitYesNo()) {
      if(saveProgress(addr, filename, chiptype) == FR_OK) {
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

void CV_Program(chip_t chiptype) {
  FILINFO fno;

  LCD_Clear();
  choose_file(&fno, "/", FA_READ);
  CV_Program_Internal(fno.fname, 0, chiptype);
}

void CV_Verify(chip_t chiptype) {
  uint32_t error = 0;
  UINT bytes_read;

  FILINFO fno;
  FIL file;

  uint32_t starttime = ticks;

  LCD_Clear();
  choose_file(&fno, "/", FA_READ);
  LCD_Clear();
  CV_genScrambleLookup(chiptype);
  f_open(&file, fno.fname, FA_READ);

  for(int i = 0; i < END_ADDRESS_C; i += SECTOR_SIZE) {
    LCD_xyprintf(0, 0, 0, "Verify %3d%%\n", (int)((double)100.0*(double)i/(double)END_ADDRESS_C+0.5));
    f_read(&file, buffer, SECTOR_SIZE * 4, &bytes_read);
    CV_ScrambleBuffer(buffer, SECTOR_SIZE * 2);
    if(!bytes_read) break;
    if(CV_SectorVerify(3, i, buffer)) {
      error++;
    };
  }
  f_close(&file);
  LCD_printf(error ? 1 : 2, "Verify done,        \n%d bad blocks.\nTime: %d\n", error, (ticks-starttime) / 100);
  waitButton();
}

void CV_Dump(chip_t chiptype) {
  FIL file;
  FRESULT res;

  uint32_t starttime = ticks;

  LCD_Clear();
  CV_genDescrambleLookup(chiptype);

  res = f_open(&file, DUMP_FILENAMES[chiptype], FA_CREATE_ALWAYS | FA_WRITE);
  if(check_fresult(res, "Could not open file\n%s\n", DUMP_FILENAMES[chiptype])) {
    return;
  }

  LCD_xyprintf(0, 2, 0, "-> %s\n", DUMP_FILENAMES[chiptype]);
  for(int i = 0; i < END_ADDRESS_C; i += SECTOR_SIZE) {
    LCD_xyprintf(0, 0, 0, "Dumping %3d%%\n", (int)((double)100.0*(double)i/(double)END_ADDRESS_C+0.5));
    CV_SectorDump(i, buffer);
    CV_ScrambleBuffer(buffer, SECTOR_SIZE * 2);
    res = f_write(&file, buffer, SECTOR_SIZE * 4, NULL);
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

void CV_ReadTest() {
  Flash_ID chip_id[4];
  int error = 0;
  CV_Init();
  srand(ticks);
  LCD_Clear();
  LCD_xyprintf(0, 0, 0, "Read Stress Test\n");
  while(1) {
    uint32_t addr = (rand() & 0xfffffff) & ~(SECTOR_SIZE - 1);
    LCD_xyprintf(0, 1, 0, "ST %08lx           \n", addr);
    for(int i = 0; i < SECTOR_SIZE; i++) {
      CV_ReadCycle(1, addr + i);
      CV_ReadCycle(2, addr + i);
      CV_ReadCycle(1, addr + (~i % SECTOR_SIZE));
      CV_ReadCycle(2, addr + (~i % SECTOR_SIZE));
    }
    CV_WriteCycle(3, 0, 0xff);
    CV_WriteCycle(3, BIT27, 0xff);
    for(int i = 0; i < 4; i++) {
      chip_id[i] = CV_ReadID(i & 2 ? 2 : 1, i & 1 ? BIT27 : 0);
      if(chip_id[i].vendor_id != 0x89 || chip_id[i].chip_id != 0x88b0) {
        error++;
        LCD_printf(1, "ID %d: %04x:%04x\n", i+1, chip_id[i].vendor_id, chip_id[i].chip_id);
      }
    }
    if(error) {
      LCD_printf(1, "FAILED            \n");
      waitButton();
      return;
    }
  }
}

void C_Program() {
  CV_Init();
  CV_Program(CHIP_C);
}
void V_Program() {
  CV_Init();
  CV_Program(CHIP_V);
}
void C_Verify() {
  CV_Init();
  CV_Verify(CHIP_C);
}
void V_Verify() {
  CV_Init();
  CV_Verify(CHIP_V);
}
void C_Dump() {
  CV_Init();
  CV_Dump(CHIP_C);
}
void V_Dump() {
  CV_Init();
  CV_Dump(CHIP_V);
}