import pathlib,struct,zlib,zipfile,sys
p=pathlib.Path(sys.argv[1]);b=p.read_bytes();pos=0;entries=[]
while pos+30<=len(b) and b[pos:pos+4]==b'PK\x03\x04':
 sig,ver,flags,method,tm,dt,crc,cs,us,nl,el=struct.unpack_from('<I5H3I2H',b,pos)
 if flags&1 or method not in (0,8):raise ValueError('Unsupported partial ZIP encoding')
 start=pos+30+nl+el;end=start+cs
 if flags&8:
  if method!=8:raise ValueError('Unbounded stored entry')
  decoder=zlib.decompressobj(-15);raw=decoder.decompress(b[start:]);raw+=decoder.flush()
  if not decoder.eof:raise ValueError('Incomplete deflate stream')
  cs=len(b)-start-len(decoder.unused_data);end=start+cs
  descriptor=end+(4 if b[end:end+4]==b'PK\x07\x08' else 0)
  crc,described_cs,us=struct.unpack_from('<III',b,descriptor)
  if described_cs!=cs:raise ValueError('Descriptor size mismatch')
  nextpos=descriptor+12
 else:nextpos=end
 if end>len(b):raise ValueError('Incomplete entry')
 name=b[pos+30:pos+30+nl].decode('utf8');data=b[start:end];raw=zlib.decompress(data,-15) if method==8 else data
 if len(raw)!=us or zlib.crc32(raw)!=crc:raise ValueError('CRC/size mismatch: '+name)
 entries.append((name,raw,method,crc,cs,us,pos,ver,flags,tm,dt,b[pos+30+nl:start]));pos=nextpos
if pos!=len(b) or not entries:raise ValueError('Unexpected trailing data')
required={'AndroidManifest.xml','resources.arsc','res/drawable/icon.xml','res/drawable/banner.xml'}
if not required.issubset({entry[0] for entry in entries}):raise ValueError('Missing compiled resources')
for name,data,*_ in entries:
 if name=='AndroidManifest.xml' and data[:4]!=b'\x03\x00\x08\x00':raise ValueError('Not compiled Android XML')
 if name=='resources.arsc' and data[:4]!=b'\x02\x00\x0c\x00':raise ValueError('Not Android resource table')
central=bytearray()
for name,raw,method,crc,cs,us,offset,ver,flags,tm,dt,extra in entries:
 name_bytes=name.encode('utf8')
 central+=struct.pack('<I6H3I5H2I',0x02014b50,ver,ver,flags,method,tm,dt,crc,cs,us,len(name_bytes),0,0,0,0,0,offset)+name_bytes
complete=b+central+struct.pack('<I4H2IH',0x06054b50,0,0,len(entries),len(entries),len(central),len(b),0)
recovered=p.with_suffix('.recovered.apk');recovered.write_bytes(complete)
with zipfile.ZipFile(recovered) as check:
 if check.testzip() is not None:raise ValueError('Recovered ZIP failed verification')
recovered.replace(p);print('Appended ZIP central directory; verified',len(entries),'complete entries and preserved offsets')
