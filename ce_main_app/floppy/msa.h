/*
  Hatari - msa.h

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/

#include "../datatypes.h"

#define Uint8   BYTE

extern bool MSA_FileNameIsMSA(const char *pszFileName, bool bAllowGZ);
extern Uint8 *MSA_UnCompress(Uint8 *pMSAFile, long *pImageSize);
extern Uint8 *MSA_ReadDisk(const char *pszFileName, long *pImageSize);
extern bool MSA_WriteDisk(const char *pszFileName, Uint8 *pBuffer, int ImageSize);
