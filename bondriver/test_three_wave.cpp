#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include "IBonDriver2.h"
int wmain(int argc,wchar_t **argv){
 if(argc!=4)return 1;
 HMODULE module=LoadLibraryW(argv[1]);if(!module)return 2;
 auto create=(IBonDriver *(*)())GetProcAddress(module,"CreateBonDriver");if(!create)return 3;
 auto driver=static_cast<IBonDriver2 *>(create());if(!driver)return 4;
 bool ok=driver->EnumTuningSpace(0)&&driver->EnumTuningSpace(1)&&!driver->EnumTuningSpace(2);
 for(DWORD space=0;space<2;space++){
  DWORD count=0;while(driver->EnumChannelName(space,count)&&count<513)count++;
  ok=ok&&count==(DWORD)_wtoi(argv[2+space])&&!driver->EnumChannelName(space,0xffffffff);
 }
 ok=ok&&!driver->SetChannel(2,0)&&driver->GetCurSpace()==0xffffffff&&driver->GetCurChannel()==0xffffffff;
 driver->Release();FreeLibrary(module);printf("Three-wave DLL enumeration %s\n",ok?"PASS":"FAIL");return ok?0:5;
}
