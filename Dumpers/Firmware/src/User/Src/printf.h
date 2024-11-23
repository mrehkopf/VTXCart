#ifndef __PRINTF_H
#define __PRINTF_H

int vxprintf(void (*output_function)(char c), const char *format, va_list ap);

#endif
