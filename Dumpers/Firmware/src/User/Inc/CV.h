#ifndef __CV_H
#define __CV_H

#ifdef __cplusplus
 extern "C" {
#endif

#define CV_ADDR2ST(halfword, addr) (ST_MASK[((addr & BIT27) >> 25) | (halfword & 3)])

void CV_GPIO_Init(void);
void CV_Test(void);
void CV_Erase(void);
void CV_BlankCheck(void);
void CV_CapaView(void);

void C_Verify(void);
void V_Verify(void);

void C_Program(void);
void V_Program(void);

void C_Dump(void);
void V_Dump(void);

void CV_ReadTest(void);

void CV_Program_Internal(const char *filename, uint32_t address, chip_t chiptype);

#ifdef __cplusplus
}
#endif

#endif /* __CV_H */
