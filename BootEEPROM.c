#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "BootEEPROM.h"

void BootEepromPrintInfo(EEPROMDATA *eeprom)
{
	printf("MAC : ");
	printf("%02X%02X%02X%02X%02X%02X  ",
		eeprom->MACAddress[0], eeprom->MACAddress[1], eeprom->MACAddress[2],
		eeprom->MACAddress[3], eeprom->MACAddress[4], eeprom->MACAddress[5]
	);

	printf("Vid: ");
	switch(*((VIDEO_STANDARD *)&eeprom->VideoStandard)) {
		case VID_INVALID:
			printf("0  ");
			break;
		case NTSC_M:
			printf("NTSC-M  ");
			break;
		case NTSC_J:
			printf("NTSC-J  ");
			break;
		case PAL_I:
			printf("PAL-I  ");
			break;
		default:
			printf("%X  ", (int)*((VIDEO_STANDARD *)&eeprom->VideoStandard));
			break;
	}

	printf("Serial: ");
	{
		char sz[13];
		memcpy(sz, &eeprom->SerialNumber[0], 12);
		sz[12]='\0';
		printf(" %s", sz);
	}

	printf("\n");
}

/* The EepromCRC algorithm was obtained from the XKUtils 0.2 source released by
 * TeamAssembly under the GNU GPL.
 * Specifically, from XKCRC.cpp
 *
 * Rewritten to ANSI C by David Pye (dmp@davidmpye.dyndns.org)
 *
 * Thanks! */
void EepromCRC(unsigned char *crc, unsigned char *data, long dataLen) {
	unsigned char* CRC_Data = (unsigned char *)malloc(dataLen+4);
	int pos=0;
	memset(crc,0x00,4);

	memset(CRC_Data,0x00, dataLen+4);
	//Circle shift input data one byte right
	memcpy(CRC_Data + 0x01 , data, dataLen-1);
	memcpy(CRC_Data, data + dataLen-1, 0x01);

	for (pos=0; pos<4; ++pos) {
		unsigned short CRCPosVal = 0xFFFF;
		unsigned long l;
		for (l=pos; l<dataLen; l+=4) {
			CRCPosVal -= *(unsigned short*)(&CRC_Data[l]);
		}
		CRCPosVal &= 0xFF00;
		crc[pos] = (unsigned char) (CRCPosVal >> 8);
	}
	free(CRC_Data);
}
