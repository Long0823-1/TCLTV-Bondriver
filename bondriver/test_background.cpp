#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <vector>
#include "IBonDriver2.h"
using Create = IBonDriver *(*)();
int wmain(int argc,wchar_t **argv) {
 const wchar_t *dll=argc>1 ? argv[1] : L"BonDriver_TCLG06_background.dll";
 int channel=argc>2 ? _wtoi(argv[2]) : -1;
 int seconds=argc>3 ? _wtoi(argv[3]) : 10;
 const wchar_t *filename=argc>4 ? argv[4] : L"background_harness.ts";
 HMODULE module=LoadLibraryW(dll);
 if(!module) { printf("LoadLibrary error=%lu\n",GetLastError()); return 1; }
 auto create=(Create)GetProcAddress(module,"CreateBonDriver");
 if(!create) return 2;
 IBonDriver2 *driver=static_cast<IBonDriver2 *>(create());
 if(!driver) return 3;
 wprintf(L"Tuner: %s; Space: %s\n",driver->GetTunerName(),driver->EnumTuningSpace(0));
 for(DWORD i=0;driver->EnumChannelName(0,i);++i) wprintf(L"%lu: %s\n",i,driver->EnumChannelName(0,i));
 if(!driver->OpenTuner() || !driver->OpenTuner()) { printf("OpenTuner failed\n"); driver->Release(); FreeLibrary(module); return 4; }
 if(driver->SetChannel(99,999) || !driver->IsTunerOpening()) { printf("invalid channel handling failed\n"); return 5; }
 if(channel>=0 && !driver->SetChannel(0,(DWORD)channel)) { printf("SetChannel failed\n"); driver->Release(); FreeLibrary(module); return 6; }
 FILE *out=nullptr; _wfopen_s(&out,filename,L"wb");
 if(!out) return 7;
 unsigned long long total=0; DWORD bad=0,reads=0,timeouts=0; float maxSignal=0.0f;
 ULONGLONG start=GetTickCount64(); BYTE copy[48128];
 while(GetTickCount64()-start<(ULONGLONG)seconds*1000) {
  float signal=driver->GetSignalLevel();if(signal>maxSignal)maxSignal=signal;
  DWORD wait=driver->WaitTsStream(2000);
  if(wait!=WAIT_OBJECT_0) { ++timeouts; if(wait!=WAIT_TIMEOUT) break; continue; }
  BYTE *data=nullptr; DWORD size=0,remaining=0; BOOL ok;
  if(reads%2) { data=copy; ok=driver->GetTsStream(copy,&size,&remaining); }
  else ok=driver->GetTsStream(&data,&size,&remaining);
  if(!ok || size>sizeof(copy) || size%188 || (size && !data)) { printf("Invalid buffer contract\n"); ++bad; break; }
  for(DWORD i=0;i<size;i+=188) if(data[i]!=0x47) ++bad;
  if(size) { fwrite(data,1,size,out); total+=size; ++reads; }
 }
 fclose(out);
 printf("bytes=%llu reads=%lu bad_sync=%lu timeouts=%lu channel=%lu\n",total,reads,bad,timeouts,driver->GetCurChannel());
 printf("signal_db=%.3f\n",maxSignal);
 driver->CloseTuner();
 bool closed=!driver->IsTunerOpening() && driver->WaitTsStream(1)==WAIT_ABANDONED;
 driver->Release(); FreeLibrary(module);
 return total && !bad && closed ? 0 : 8;
}
