#ifndef __P_H
#define __P_H

#ifdef __cplusplus
 extern "C" {
#endif

void P_GPIO_Init(void);
void P_Dump(void);
void P_Test(void);

void P_Verify(void);
void P_Program(void);
void P_Dump(void);
void P_Erase(void);
void P_CapaView(void);

void P_Program_Internal(const char *filename, uint32_t address);

#ifdef __cplusplus
}
#endif

#endif /* __P_H */
