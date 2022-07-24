#ifndef _BOOTHDDKEY_H_
#define _BOOTHDDKEY_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "BootEEPROM.h"

void HMAC_SHA1(unsigned char *result,
               unsigned char *key, int key_length,
                unsigned char *text1, int text1_length,
                 unsigned char *text2, int text2_length );
int copy_swap_trim( char *dst, const char *src, int len);
int BootDecryptEEPROM(EEPROMDATA *eeprom);

#ifdef __cplusplus
}
#endif

#endif // _BOOTHDDKEY_H_
