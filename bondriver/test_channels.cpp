#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
#include "IBonDriver2.h"
int wmain(int argc,wchar_t **argv) {
 if(argc<4)return 1;HMODULE module=LoadLibraryW(argv[1]);if(!module)return 2;
 auto create=(IBonDriver *(*)())GetProcAddress(module,"CreateBonDriver");if(!create)return 3;
 IBonDriver2 *driver=static_cast<IBonDriver2 *>(create());if(!driver)return 4;
 DWORD expected=(DWORD)_wtoi(argv[2]),count=0;
 while(driver->EnumChannelName(0,count)){if(!driver->EnumChannelName(0,count)[0]||++count>512)return 5;}
 bool ok=count==expected&&!driver->EnumChannelName(1,0)&&!driver->EnumChannelName(0,0xffffffff)&&!driver->SetChannel(1,0);
 if(expected)ok=ok&&wcscmp(driver->EnumChannelName(0,0),argv[3])==0;
 else ok=ok&&!driver->OpenTuner();
 printf("channels=%lu expected=%lu unicode_first=%s result=%s\n",count,expected,expected?"checked":"none",ok?"PASS":"FAIL");
 driver->Release();FreeLibrary(module);return ok?0:6;
}