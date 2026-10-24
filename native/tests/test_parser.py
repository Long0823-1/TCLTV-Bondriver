import ctypes as C, pathlib, struct
lib=C.CDLL(str(pathlib.Path(__file__).with_name('test_psi.dll')))
U8=C.c_uint8; U16=C.c_uint16; Size=C.c_size_t; Ptr=C.POINTER(U8)
class State(C.Structure):
 _fields_=[('pid',U16),('data',U8*4096),('used',Size),('expected',Size),('last_cc',C.c_int)]
class Service(C.Structure):
 _fields_=[('pids',U16*63),('count',U16),('pcr',U16),('video',U16),('audio',U16),('ecm',U16)]
Callback=C.CFUNCTYPE(None,C.c_void_p,Ptr,Size)
lib.tsd_crc32.argtypes=[Ptr,Size]; lib.tsd_crc32.restype=C.c_uint32
lib.tsd_psi_reset.argtypes=[C.POINTER(State),U16]
lib.tsd_psi_feed.argtypes=[C.POINTER(State),Ptr,Callback,C.c_void_p]
lib.tsd_pat_pmt.argtypes=[Ptr,Size,U16,U16,C.POINTER(U16)]; lib.tsd_pat_pmt.restype=C.c_int
lib.tsd_pmt_pids.argtypes=[Ptr,Size,U16,C.POINTER(Service)]; lib.tsd_pmt_pids.restype=C.c_int
lib.tsd_pid_add.argtypes=[C.POINTER(Service),C.c_uint]; lib.tsd_pid_add.restype=C.c_int

def arr(b): return (U8*len(b)).from_buffer_copy(b)
def crc(b): return lib.tsd_crc32(arr(b),len(b))
def section(table,body):
 length=len(body)+4; header=bytes([table,0xb0|(length>>8),length&255])+body
 # Independent Python implementation, anchored to the MPEG-2 check vector below.
 value=0xffffffff
 for byte in header:
  value^=byte<<24
  for _ in range(8): value=((value<<1)^0x04c11db7 if value&0x80000000 else value<<1)&0xffffffff
 return header+value.to_bytes(4,'big')
def pat(entries=1):
 body=bytes.fromhex('7fe2c10000')+b''.join(struct.pack('>HH',1040+i,0xe110+i) for i in range(entries))
 return section(0,body)
def pmt(info=b'',streams=2):
 body=bytes.fromhex('0410c10000e100')+struct.pack('>H',0xf000|len(info))+info
 for i in range(streams): body+=bytes([2 if i==0 else 15])+struct.pack('>HH',0xe111+i,0xf000)
 return section(2,body)
def packet(payload,pid=0,cc=0,start=True):
 if start: payload=b'\0'+payload
 assert len(payload)<=184
 return bytes([0x47,(0x40 if start else 0)|(pid>>8),pid&255,0x10|cc])+payload+b'\xff'*(184-len(payload))
seen=[]
@Callback
def callback(ctx,data,length): seen.append(bytes(data[:length]))
state=State()
def reset(pid=0): lib.tsd_psi_reset(C.byref(state),pid);seen.clear()
def feed(p): assert len(p)==188;lib.tsd_psi_feed(C.byref(state),arr(p),callback,None)
checks=0
def check(value):
 global checks
 assert value
 checks+=1
check(crc(b'123456789')==0x0376e6e7)
small=pat(); check(crc(small)==0)
reset(); feed(packet(small)); check(seen==[small]);feed(packet(small));check(len(seen)==1)
wrong=bytearray(small);wrong[-1]^=1
feed(packet(wrong,cc=1));check(len(seen)==1)
reset();long=pat(50);feed(packet(long[:183],cc=0));check(not seen);feed(packet(long[183:],cc=1,start=False));check(seen==[long])
reset();feed(packet(long[:183],cc=0));feed(packet(long[183:],cc=2,start=False));check(not seen)
feed(packet(small,cc=3));check(seen==[small])
reset();feed(packet(long[:183],cc=0)); rest=long[183:]
feed(bytes([0x47,0x40,0,0x11,len(rest)])+rest+small+b'\xff'*(183-len(rest)-len(small)))
check(seen==[long,small])
reset();adapt=bytes([0x47,0x40,0,0x30,255])+bytes(183);feed(adapt);check(not seen and not state.used)
reset();error=bytearray(packet(small));error[1]|=0x80;feed(error);check(not seen)
reset();error=bytearray(packet(small));error[3]|=0x80;feed(error);check(not seen)
reset(272);feed(packet(small));check(not seen)
out=U16();check(lib.tsd_pat_pmt(arr(small),len(small),32738,1040,C.byref(out))==0 and out.value==272)
check(lib.tsd_pat_pmt(arr(small),len(small),32737,1040,C.byref(out))!=0)
check(lib.tsd_pat_pmt(arr(small),len(small),32738,1041,C.byref(out))!=0)
service=Service(); ca=bytes.fromhex('09040005fec0');table=pmt(ca)
check(lib.tsd_pmt_pids(arr(table),len(table),1040,C.byref(service))==0)
check(service.pcr==256 and service.video==273 and service.audio==274 and service.ecm==7872)
check(7872 in service.pids[:service.count] and 1 in service.pids[:service.count] and 0x12 in service.pids[:service.count])
count=service.count; check(lib.tsd_pid_add(C.byref(service),273)==0 and service.count==count)
malformed=pmt(bytes.fromhex('09040005fe'))
check(lib.tsd_pmt_pids(arr(malformed),len(malformed),1040,C.byref(service))!=0)
toomany=pmt(streams=70)
check(lib.tsd_pmt_pids(arr(toomany),len(toomany),1040,C.byref(service))!=0)
longpmt=pmt(bytes([0x80,170])+bytes(170))
reset(272);feed(packet(longpmt[:183],pid=272));feed(packet(longpmt[183:],pid=272,cc=1,start=False));check(seen==[longpmt])
print(f'PSI parser: {checks} assertions passed')

