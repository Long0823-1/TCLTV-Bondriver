#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include "IBonDriver2.h"
#include <process.h>
#include <atomic>
#include <algorithm>
#include <deque>
#include <vector>
#include <string>
#include <stdio.h>
#pragma comment(lib,"ws2_32.lib")

static HMODULE g_module;
static constexpr DWORD Chunk = 188 * 256;
static constexpr size_t MaxBlocks = (32 * 1024 * 1024) / Chunk;
struct Channel { std::wstring name; DWORD onid, tsid, service; };

class Driver final : public IBonDriver2 {
 std::atomic<SOCKET> sock{INVALID_SOCKET};
 HANDLE thread = nullptr, dataEvent = nullptr, tunerMutex = nullptr, signalThread=nullptr, signalStop=nullptr;
 std::atomic<SOCKET> signalSocket{INVALID_SOCKET};
 std::atomic<float> signalLevel{0.0f};
 CRITICAL_SECTION cs;
 std::atomic<bool> stop{false}, connected{false};
 std::atomic<bool> opened{false};
 bool winsock = false;
 std::atomic<DWORD> current{0xffffffff};
 char host[256] = "169.254.95.200";
 int port = 40556, controlPort = 40557, tunerIndex = 0;
 bool helperMode = false;
 char token[128] = {};
 std::wstring ini, script, python, adbSerial;
 std::vector<Channel> channels;
 std::deque<std::vector<BYTE>> queue;
 std::vector<BYTE> output;
 static unsigned __stdcall Receive(void *p) { static_cast<Driver *>(p)->ReceiveLoop(); return 0; }
 void ReceiveLoop();
 static unsigned __stdcall SignalWorker(void *p){static_cast<Driver *>(p)->SignalLoop();return 0;}
 void SignalLoop();
 void StopSignal();
 bool Connect();
 void Disconnect();
 bool Tune(DWORD index);
 bool TuneWithHelper(DWORD index);
 void LoadSettings();
 void LoadChannels();
public:
 Driver() { InitializeCriticalSection(&cs); dataEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr); signalStop=CreateEventW(nullptr,TRUE,FALSE,nullptr); output.reserve(Chunk); LoadSettings(); LoadChannels(); }
 ~Driver() { CloseTuner(); if(dataEvent)CloseHandle(dataEvent); if(signalStop)CloseHandle(signalStop); DeleteCriticalSection(&cs); }
 const BOOL OpenTuner() override;
 void CloseTuner() override;
 const BOOL SetChannel(BYTE channel) override { return SetChannel(0,channel); }
 const float GetSignalLevel() override { return opened&&connected?signalLevel.load():0.0f; }
 const DWORD WaitTsStream(DWORD timeout) override;
 const DWORD GetReadyCount() override;
 const BOOL GetTsStream(BYTE *,DWORD *,DWORD *) override;
 const BOOL GetTsStream(BYTE **,DWORD *,DWORD *) override;
 void PurgeTsStream() override;
 void Release() override { delete this; }
 LPCWSTR GetTunerName() override { return tunerIndex==1?L"TCL G06 ISDB-T #2":L"TCL G06 ISDB-T #1"; }
 const BOOL IsTunerOpening() override { return opened ? TRUE : FALSE; }
 LPCWSTR EnumTuningSpace(DWORD space) override { return space==0 ? L"Terrestrial ISDB-T" : nullptr; }
 LPCWSTR EnumChannelName(DWORD space,DWORD channel) override { return space==0 && channel<channels.size() ? channels[channel].name.c_str() : nullptr; }
 const BOOL SetChannel(DWORD space,DWORD channel) override;
 const DWORD GetCurSpace() override { return current==0xffffffff ? 0xffffffff : 0; }
 const DWORD GetCurChannel() override { return current; }
};

void Driver::LoadSettings() {
 wchar_t path[32768];
 DWORD n=GetModuleFileNameW(g_module,path,_countof(path));
 ini.assign(path,n);
 size_t dot=ini.find_last_of(L'.');
 if(dot!=std::wstring::npos) ini.resize(dot);
 ini+=L".ini";
 // Read the network address with Unicode path support as well.
 wchar_t value[32768];
 GetPrivateProfileStringW(L"Server",L"Host",L"169.254.95.200",value,_countof(value),ini.c_str());
 WideCharToMultiByte(CP_UTF8,0,value,-1,host,sizeof(host),nullptr,nullptr);
 tunerIndex=(int)GetPrivateProfileIntW(L"Control",L"TunerIndex",0,ini.c_str());
 port=GetPrivateProfileIntW(L"Server",L"Port",tunerIndex==1?40558:40556,ini.c_str());
 controlPort=GetPrivateProfileIntW(L"Control",L"Port",40557,ini.c_str());
 GetPrivateProfileStringW(L"Control",L"Mode",L"Native",value,_countof(value),ini.c_str()); helperMode=_wcsicmp(value,L"Helper")==0;
 GetPrivateProfileStringW(L"Control",L"Token",L"",value,_countof(value),ini.c_str());
 WideCharToMultiByte(CP_UTF8,0,value,-1,token,sizeof(token),nullptr,nullptr);
 GetPrivateProfileStringW(L"Control",L"Python",L"python.exe",value,_countof(value),ini.c_str()); python=value;
 GetPrivateProfileStringW(L"Control",L"Script",L"..\\tsd\\tune.py",value,_countof(value),ini.c_str()); script=value;
 if(!(script.size()>1 && script[1]==L':') && script.rfind(L"\\\\",0)!=0) script=ini.substr(0,ini.find_last_of(L"\\/")+1)+script;
 GetPrivateProfileStringW(L"Control",L"AdbSerial",L"192.168.0.160:5555",value,_countof(value),ini.c_str()); adbSerial=value;
}
static bool ChannelNumber(const std::wstring &ini,const wchar_t *section,const wchar_t *key,DWORD &result) {
 wchar_t value[16];DWORD n=GetPrivateProfileStringW(section,key,L"",value,_countof(value),ini.c_str());
 if(!n||n>=_countof(value)-1||wcsspn(value,L"0123456789")!=n)return false;
 unsigned long parsed=wcstoul(value,nullptr,10);if(!parsed||parsed>65535)return false;result=(DWORD)parsed;return true;
}
void Driver::LoadChannels() {
 UINT count=GetPrivateProfileIntW(L"Channels",L"Count",0,ini.c_str());if(!count||count>512)return;
 std::vector<Channel> loaded;loaded.reserve(count);
 for(UINT i=0;i<count;i++) {
  wchar_t section[32],name[512];swprintf_s(section,L"Channel%u",i);Channel channel{};
  DWORD length=GetPrivateProfileStringW(section,L"Name",L"",name,_countof(name),ini.c_str());
  if(!length||length>=_countof(name)-1||!ChannelNumber(ini,section,L"ONID",channel.onid)||!ChannelNumber(ini,section,L"TSID",channel.tsid)||!ChannelNumber(ini,section,L"ServiceID",channel.service))return;
  for(const auto &previous:loaded)if(previous.onid==channel.onid&&previous.tsid==channel.tsid&&previous.service==channel.service)return;
  channel.name=name;loaded.push_back(std::move(channel));
 }
 channels.swap(loaded);
}
const BOOL Driver::OpenTuner() {
 if(opened) return TRUE;
 if(!dataEvent || !signalStop || channels.empty()) return FALSE;
 LoadSettings();
 if(tunerIndex<0||tunerIndex>1)return FALSE;
 wchar_t lockName[320], lockHost[256];
 MultiByteToWideChar(CP_UTF8,0,host,-1,lockHost,_countof(lockHost));
 swprintf_s(lockName,L"Local\\TCLG06.PVR.%s.%d",lockHost,tunerIndex);
 SetLastError(ERROR_SUCCESS);
 tunerMutex=CreateMutexW(nullptr,FALSE,lockName);
 if(!tunerMutex || GetLastError()==ERROR_ALREADY_EXISTS) { if(tunerMutex)CloseHandle(tunerMutex); tunerMutex=nullptr; return FALSE; }
 WSADATA info;
 if(WSAStartup(MAKEWORD(2,2),&info)) { CloseHandle(tunerMutex); tunerMutex=nullptr; return FALSE; }
 winsock=true;
 // Opening also permits attaching to the already running background channel.
 if(!Connect()) { WSACleanup(); winsock=false; CloseHandle(tunerMutex); tunerMutex=nullptr; return FALSE; }
 opened=true;
 ResetEvent(signalStop);signalThread=(HANDLE)_beginthreadex(nullptr,0,SignalWorker,this,0,nullptr);
 return TRUE;
}
static SOCKET OpenSocket(const char *host,int port,int milliseconds=5000) {
 if(port<1 || port>65535) return INVALID_SOCKET;
 SOCKET candidate=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
 if(candidate==INVALID_SOCKET) return INVALID_SOCKET;
 sockaddr_in address{}; address.sin_family=AF_INET; address.sin_port=htons((u_short)port);
 if(InetPtonA(AF_INET,host,&address.sin_addr)!=1) { closesocket(candidate); return INVALID_SOCKET; }
 u_long mode=1; ioctlsocket(candidate,FIONBIO,&mode);
 int r=connect(candidate,(sockaddr *)&address,sizeof(address));
 if(r!=0 && WSAGetLastError()!=WSAEWOULDBLOCK) { closesocket(candidate); return INVALID_SOCKET; }
 if(r!=0) {
  fd_set writes,errors; FD_ZERO(&writes); FD_ZERO(&errors); FD_SET(candidate,&writes); FD_SET(candidate,&errors);
  timeval timeout{milliseconds/1000,(milliseconds%1000)*1000};
  int error=0,len=sizeof(error);
  if(select(0,nullptr,&writes,&errors,&timeout)<=0 || FD_ISSET(candidate,&errors) ||
    getsockopt(candidate,SOL_SOCKET,SO_ERROR,(char *)&error,&len)!=0 || error) { closesocket(candidate); return INVALID_SOCKET; }
 }
 mode=0; ioctlsocket(candidate,FIONBIO,&mode);
 DWORD timeout=1000; setsockopt(candidate,SOL_SOCKET,SO_RCVTIMEO,(char *)&timeout,sizeof(timeout));
 return candidate;
}
void Driver::StopSignal() {
 if(signalStop)SetEvent(signalStop);
 SOCKET active=signalSocket.exchange(INVALID_SOCKET);if(active!=INVALID_SOCKET){shutdown(active,SD_BOTH);closesocket(active);}
 if(signalThread){WaitForSingleObject(signalThread,INFINITE);CloseHandle(signalThread);signalThread=nullptr;}
 signalLevel=0.0f;
}
void Driver::SignalLoop() {
 if(strlen(token)!=64 || strspn(token,"0123456789abcdefABCDEF")!=64)return;
 while(WaitForSingleObject(signalStop,0)==WAIT_TIMEOUT) {
  float level=0.0f;SOCKET control=OpenSocket(host,controlPort,1000);
  if(control!=INVALID_SOCKET) {
   signalSocket=control;DWORD timeout=1000;setsockopt(control,SOL_SOCKET,SO_RCVTIMEO,(char *)&timeout,sizeof timeout);setsockopt(control,SOL_SOCKET,SO_SNDTIMEO,(char *)&timeout,sizeof timeout);
   char command[128],response[256]={};int length=sprintf_s(command,"AUTH %s SIGNAL %d\n",token,tunerIndex),sent=0,used=0;
   while(sent<length){int n=send(control,command+sent,length-sent,0);if(n<=0)break;sent+=n;}
   if(sent==length)while(used<(int)sizeof(response)-1){int n=recv(control,response+used,1,0);if(n<=0)break;if(response[used++]=='\n')break;}
   int index=-1,locked=0,valid=0;unsigned cn=0,strength=0,quality=0;
   if(used&&response[used-1]=='\n'&&sscanf_s(response,"OK tuner=%d locked=%d cn_valid=%d cn_mdb=%u strength=%u quality=%u",&index,&locked,&valid,&cn,&strength,&quality)==6&&index==tunerIndex&&locked==1&&valid==1&&cn<=100000&&strength<=100&&quality<=100)level=(float)cn/1000.0f;
   SOCKET closeSocket=signalSocket.exchange(INVALID_SOCKET);if(closeSocket!=INVALID_SOCKET)closesocket(closeSocket);
  }
  signalLevel=level;if(WaitForSingleObject(signalStop,1000)!=WAIT_TIMEOUT)break;
 }
 signalLevel=0.0f;
}
bool Driver::Connect() {
 SOCKET candidate=OpenSocket(host,port);
 if(candidate==INVALID_SOCKET) return false;
 sock=candidate; stop=false; connected=true;
 PurgeTsStream();
 thread=(HANDLE)_beginthreadex(nullptr,0,Receive,this,0,nullptr);
 if(!thread) { closesocket(sock); sock=INVALID_SOCKET; connected=false; return false; }
 return true;
}
void Driver::Disconnect() {
 stop=true;
 SOCKET active=sock.exchange(INVALID_SOCKET);
 if(active!=INVALID_SOCKET) { shutdown(active,SD_BOTH); closesocket(active); }
 if(thread) { WaitForSingleObject(thread,INFINITE); CloseHandle(thread); thread=nullptr; }
 connected=false; SetEvent(dataEvent);
}

void Driver::CloseTuner() {
 StopSignal();Disconnect(); opened=false; current=0xffffffff;
 PurgeTsStream();
 if(winsock) { WSACleanup(); winsock=false; }
 if(tunerMutex) { CloseHandle(tunerMutex); tunerMutex=nullptr; }
}
void Driver::ReceiveLoop() {
 SOCKET source=sock.load();
 std::vector<BYTE> pending; pending.reserve(Chunk+65536);
 BYTE input[65536]; bool synchronized=false;
 while(!stop) {
  int n=recv(source,(char *)input,sizeof(input),0);
  if(n<0 && WSAGetLastError()==WSAETIMEDOUT) continue;
  if(n<=0) {
   SOCKET expected=source;
   if(sock.compare_exchange_strong(expected,INVALID_SOCKET))closesocket(source);
   connected=false;
   if(stop)break;
   PurgeTsStream(); pending.clear(); synchronized=false;
   while(!stop) {
    for(int pause=0;pause<10 && !stop;++pause)Sleep(100);
    if(stop)break;
    SOCKET next=OpenSocket(host,port);
    if(next==INVALID_SOCKET)continue;
    if(stop) { closesocket(next);break; }
    sock=next;
    if(stop) { SOCKET closing=sock.exchange(INVALID_SOCKET);if(closing!=INVALID_SOCKET)closesocket(closing);break; }
    source=next;connected=true;break;
   }
   continue;
  }
  pending.insert(pending.end(),input,input+n);
  if(!synchronized) {
   size_t i=0;
   for(;i+188*4<pending.size();++i) if(pending[i]==0x47 && pending[i+188]==0x47 && pending[i+376]==0x47 && pending[i+564]==0x47 && pending[i+752]==0x47) { synchronized=true; break; }
   if(i) pending.erase(pending.begin(),pending.begin()+i);
   if(!synchronized) continue;
  }
  size_t off=0;
  while(pending.size()-off>=Chunk) {
   bool valid=true;
   for(size_t i=0;i<Chunk;i+=188) if(pending[off+i]!=0x47) { valid=false; break; }
   if(!valid) { synchronized=false; ++off; break; }
   std::vector<BYTE> block(pending.begin()+off,pending.begin()+off+Chunk);
   EnterCriticalSection(&cs);
   if(queue.size()>=MaxBlocks) queue.pop_front(); // Drop whole TS blocks on consumer overrun.
   queue.emplace_back(std::move(block)); SetEvent(dataEvent);
   LeaveCriticalSection(&cs);
   off+=Chunk;
  }
  if(off) pending.erase(pending.begin(),pending.begin()+off);
 }
 SOCKET expected=source;
 if(sock.compare_exchange_strong(expected,INVALID_SOCKET))closesocket(source);
 connected=false; SetEvent(dataEvent);
}
const DWORD Driver::WaitTsStream(DWORD timeout) {
 if(!opened) return WAIT_ABANDONED;
 if(GetReadyCount()) return WAIT_OBJECT_0;
 if(stop) return WAIT_ABANDONED;
 DWORD result=WaitForSingleObject(dataEvent,timeout ? timeout : INFINITE);
 if(result==WAIT_OBJECT_0 && !GetReadyCount() && stop) return WAIT_ABANDONED;
 return result;
}
const DWORD Driver::GetReadyCount() {
 EnterCriticalSection(&cs); DWORD count=(DWORD)queue.size(); LeaveCriticalSection(&cs); return count;
}
const BOOL Driver::GetTsStream(BYTE **destination,DWORD *size,DWORD *remaining) {
 if(!destination || !size || !remaining) return FALSE;
 EnterCriticalSection(&cs);
 output.clear();
 if(!queue.empty()) { output=std::move(queue.front()); queue.pop_front(); }
 *destination=output.empty() ? nullptr : output.data();
 *size=(DWORD)output.size(); *remaining=(DWORD)queue.size();
 if(queue.empty() && connected) ResetEvent(dataEvent);
 LeaveCriticalSection(&cs);
 return TRUE;
}
const BOOL Driver::GetTsStream(BYTE *destination,DWORD *size,DWORD *remaining) {
 if(!destination || !size || !remaining) return FALSE;
 BYTE *source=nullptr;
 BOOL result=GetTsStream(&source,size,remaining);
 if(result && *size) memcpy(destination,source,*size);
 return result;
}
void Driver::PurgeTsStream() {
 EnterCriticalSection(&cs); queue.clear(); ResetEvent(dataEvent); LeaveCriticalSection(&cs);
}
bool Driver::TuneWithHelper(DWORD index) {
 const Channel &channel=channels[index];
 if(channel.onid!=channel.tsid)return false;
 wchar_t numbers[128]; swprintf_s(numbers,L" --triplet %lu --service %lu",channel.tsid,channel.service);
 std::wstring command=L"\""+python+L"\" \""+script+L"\" --host ";
 wchar_t widehost[256]; MultiByteToWideChar(CP_UTF8,0,host,-1,widehost,_countof(widehost));
 command+=widehost; command+=L" --adb \""+adbSerial+L"\""; command+=numbers;
 STARTUPINFOW startup{}; startup.cb=sizeof(startup); PROCESS_INFORMATION process{};
 SECURITY_ATTRIBUTES attributes{sizeof(attributes),nullptr,TRUE};
 HANDLE log=CreateFileW((ini+L".log").c_str(),FILE_APPEND_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE,&attributes,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
 HANDLE input=CreateFileW(L"NUL",GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,&attributes,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
 if(log!=INVALID_HANDLE_VALUE && input!=INVALID_HANDLE_VALUE) {
  startup.dwFlags=STARTF_USESTDHANDLES; startup.hStdOutput=log; startup.hStdError=log; startup.hStdInput=input;
 }
 auto logResult=[&](const char *label,DWORD value) { if(log!=INVALID_HANDLE_VALUE) { char line[128]; int n=sprintf_s(line,"%s=%lu\r\n",label,value); DWORD written; WriteFile(log,line,n,&written,nullptr); } };

 std::vector<wchar_t> buffer(command.begin(),command.end()); buffer.push_back(0);
 if(!CreateProcessW(nullptr,buffer.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process)) {
  logResult("CreateProcess error",GetLastError());
  if(log!=INVALID_HANDLE_VALUE)CloseHandle(log);
  if(input!=INVALID_HANDLE_VALUE)CloseHandle(input);
  return false;
 }
 DWORD wait=WaitForSingleObject(process.hProcess,60000),code=1;
 if(wait==WAIT_OBJECT_0) GetExitCodeProcess(process.hProcess,&code);
 else { TerminateProcess(process.hProcess,1); WaitForSingleObject(process.hProcess,5000); }
 logResult("helper exit",code);
 CloseHandle(process.hThread); CloseHandle(process.hProcess);
 if(log!=INVALID_HANDLE_VALUE)CloseHandle(log);
 if(input!=INVALID_HANDLE_VALUE)CloseHandle(input);
 return wait==WAIT_OBJECT_0 && code==0;
}
bool Driver::Tune(DWORD index) {
 if(helperMode)return tunerIndex==0 && TuneWithHelper(index);
 if(strlen(token)!=64 || strspn(token,"0123456789abcdefABCDEF")!=64)return false;
 SOCKET control=OpenSocket(host,controlPort);
 if(control==INVALID_SOCKET)return false;
 DWORD timeout=45000; setsockopt(control,SOL_SOCKET,SO_RCVTIMEO,(char *)&timeout,sizeof(timeout));
 timeout=5000; setsockopt(control,SOL_SOCKET,SO_SNDTIMEO,(char *)&timeout,sizeof(timeout));
 const Channel &channel=channels[index];
 char command[256];
 int length=sprintf_s(command,"AUTH %s TUNE %d %lu %lu %lu\n",token,tunerIndex,channel.onid,channel.tsid,channel.service);
 int offset=0;
 while(offset<length) { int n=send(control,command+offset,length-offset,0);if(n<=0)break;offset+=n; }
 char response[1024]={};int used=0;
 if(offset==length)while(used<(int)sizeof(response)-1) {
  int n=recv(control,response+used,1,0);if(n<=0)break;
  if(response[used++]=='\n')break;
 }
 closesocket(control);
 HANDLE log=CreateFileW((ini+L".log").c_str(),FILE_APPEND_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
 if(log!=INVALID_HANDLE_VALUE) { DWORD written;WriteFile(log,response,used,&written,nullptr);CloseHandle(log); }
 return used>=3 && response[used-1]=='\n' && strncmp(response,"OK ",3)==0;
}
const BOOL Driver::SetChannel(DWORD space,DWORD channel) {
 if(!opened || space!=0 || channel>=channels.size()) return FALSE;
 if(current==channel && connected) return TRUE;
 Disconnect(); PurgeTsStream();
 if(!Tune(channel)) { current=0xffffffff; Connect(); return FALSE; }
 if(!Connect()) { current=0xffffffff; return FALSE; }
 current=channel; return TRUE;
}
BOOL APIENTRY DllMain(HMODULE module,DWORD reason,LPVOID) {
 if(reason==DLL_PROCESS_ATTACH) { g_module=module; DisableThreadLibraryCalls(module); }
 return TRUE;
}
extern "C" __declspec(dllexport) IBonDriver *CreateBonDriver() {
 try { return new Driver(); } catch(...) { return nullptr; }
}
