#include <mint/sysbind.h>
#include <mint/osbind.h>
#include <mint/ostruct.h>
#include <support.h>

#include <stdio.h>
#include "stdlib.h"
#include "out.h"

WORD getTosVersion(void);
void selectDrive(void);

int  drive;
WORD tosVersion;

void test01(void);
void test02(void);
void test03(void);
void test04(void);
void test05(void);

WORD fromDrive = 0;

int main(void)
{
    fromDrive = Dgetdrv();

    out_s("\33E\33pFilesystem Test - by Jookie, 2015\33q");

    initBuffer();

    tosVersion = Supexec(getTosVersion);
    out_sw("TOS version     : ", tosVersion);
    out_sc("Running from drv: ", fromDrive + 'A');
    
    selectDrive();
    out_sc("Tested drive    : ", 'A' + drive);
    
    out_s("");
    test01();
    test02();
    test03();
    test04();
    test05();

    writeBufferToFile();
    deinitBuffer();
    
    out_s("Done.");
    sleep(3);
    return 0;
}

WORD getTosVersion(void)
{
    BYTE  *pSysBase     = (BYTE *) 0x000004F2;
    BYTE  *ppSysBase    = (BYTE *)  ((DWORD )  *pSysBase);                      // get pointer to TOS address
    WORD  ver           = (WORD  ) *(( WORD *) (ppSysBase + 2));                // TOS +2: TOS version
    return ver;
}

void selectDrive(void)
{
    WORD drives = Drvmap();
    
    (void) Cconws("Drives available: ");
    int i;
    for(i=0; i<16; i++) {
        if(drives & (1 << i)) {
            Cconout('A' + i);
        }
    }
    (void) Cconws("\r\nSelect drive    : ");
    
    char drv = 0;
    while(1) {
        drv = Cnecin();
        if(drv >= 'A' && drv <= 'Z') {
            drv = drv - 'A';
        } else if(drv >= 'a' && drv <= 'z') {
            drv = drv - 'a';
        } else {
            continue;
        }
        
        if(drv > 15) {
            continue;
        }
        
        if(drives & (1 << drv)) {
            break;
        }
    } 

    drive = drv;
    Cconout('A' + drv);
    (void) Cconws("\r\n");
}