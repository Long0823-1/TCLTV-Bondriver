package io.github.long0823.tclbondriver;
import java.time.*;
import java.util.*;
import java.util.concurrent.*;
public final class TsEpg {
 public static final class Event {public int onid,tsid,sid,id;public long start,end;public String title="",description="";public String channelKey(){return onid+":"+tsid+":"+sid;}public String key(){return channelKey()+":"+id+":"+start;}}
 private final ConcurrentHashMap<String,Event> events=new ConcurrentHashMap<>();private final byte[] section=new byte[4096];private int used,wanted,lastCc=-1;public volatile long validSections,crcErrors;public volatile Runnable changed;
 public static int u(byte[] b,int i){return ((b[i]&255)<<8)|(b[i+1]&255);}private static int bcd(int v){return (v>>4)*10+(v&15);}
 public static long time(byte[] b,int p){if((b[p]&255)==255&&(b[p+1]&255)==255)return 0;try{return LocalDate.ofEpochDay(u(b,p)-40587L).atTime(bcd(b[p+2]&255),bcd(b[p+3]&255),bcd(b[p+4]&255)).toInstant(ZoneOffset.ofHours(9)).toEpochMilli();}catch(Exception e){return 0;}}
 public static boolean crc(byte[] b,int n){int c=-1;for(int i=0;i<n;i++){c^=(b[i]&255)<<24;for(int bit=0;bit<8;bit++)c=(c&0x80000000)!=0?(c<<1)^0x04c11db7:c<<1;}return c==0;}
 public synchronized void resetAssembler(){used=wanted=0;lastCc=-1;}
 public synchronized void feed(byte[] p,int off){if(p[off]!=0x47||(((p[off+1]&31)<<8)|(p[off+2]&255))!=18)return;int af=(p[off+3]>>4)&3;if((p[off+1]&128)!=0||(p[off+3]&192)!=0){resetAssembler();return;}int pos=off+4;if((af&2)!=0){int size=p[off+4]&255;if(size>183){resetAssembler();return;}if(size>0&&(p[off+5]&128)!=0)resetAssembler();pos+=1+size;}if((af&1)==0||pos>=off+188)return;int cc=p[off+3]&15;if(lastCc==cc)return;if(lastCc>=0&&cc!=((lastCc+1)&15))used=wanted=0;lastCc=cc;
  if((p[off+1]&64)!=0){int pointer=p[pos++]&255;if(pos+pointer>off+188){used=wanted=0;return;}if(used>0)append(p,pos,pos+pointer,false);used=wanted=0;pos+=pointer;append(p,pos,off+188,true);}else if(used>0)append(p,pos,off+188,false);
 }
 private void append(byte[] p,int a,int end,boolean fresh){while(a<end){if(used==0&&(!fresh||(p[a]&255)==255))return;section[used++]=p[a++];if(used==3){wanted=3+(((section[1]&15)<<8)|(section[2]&255));if(wanted<18||wanted>4096||(section[1]&128)==0){used=wanted=0;return;}}if(wanted>0&&used==wanted){if(crc(section,wanted)){parse(section,wanted);validSections++;}else crcErrors++;used=wanted=0;if(!fresh)return;}}}
 public void parse(byte[] s,int length){if(length<18||length>s.length)return;int table=s[0]&255;if(table<0x4e||table>0x6f||(s[5]&1)==0)return;int sid=u(s,3),tsid=u(s,8),onid=u(s,10);boolean updated=false;long now=System.currentTimeMillis();
  for(int off=14;off+12<=length-4;){int descriptors=u(s,off+10)&4095,stop=off+12+descriptors;if(stop>length-4)break;Event e=new Event();e.onid=onid;e.tsid=tsid;e.sid=sid;e.id=u(s,off);e.start=time(s,off+2);long seconds=bcd(s[off+7]&255)*3600L+bcd(s[off+8]&255)*60L+bcd(s[off+9]&255);e.end=e.start+seconds*1000;StringBuilder detail=new StringBuilder();TreeMap<Integer,String> extended=new TreeMap<>();
   for(int d=off+12;d+2<=stop;){int tag=s[d]&255,n=s[d+1]&255,begin=d+2,end=begin+n;if(end>stop)break;
    if(tag==0x4d&&n>=5){int names=s[begin+3]&255;if(begin+4+names<end){e.title=AribText.decode(s,begin+4,names);int texts=s[begin+4+names]&255;if(begin+5+names+texts<=end)detail.append(AribText.decode(s,begin+5+names,texts));}}
    if(tag==0x4e&&n>=6){int number=(s[begin]&255)>>4,items=s[begin+4]&255,p=begin+5,itemsEnd=p+items;StringBuilder x=new StringBuilder();if(itemsEnd<end){while(p<itemsEnd){int names=s[p++]&255;if(p+names>=itemsEnd)break;String name=AribText.decode(s,p,names);p+=names;int texts=s[p++]&255;if(p+texts>itemsEnd)break;String text=AribText.decode(s,p,texts);p+=texts;if(!name.isEmpty())x.append(name).append('：');x.append(text).append('\n');}int texts=s[itemsEnd]&255;if(itemsEnd+1+texts<=end)x.append(AribText.decode(s,itemsEnd+1,texts));extended.put(number,x.toString());}}
    d=end;
   }
   for(String x:extended.values())if(!x.isEmpty())detail.append('\n').append(x);e.description=detail.toString().trim();if(e.description.length()>4000)e.description=e.description.substring(0,4000);
   if(e.start>0&&e.end>=now-6*3600000L&&e.start<=now+9*86400000L&&!e.title.isEmpty()){Event old=events.get(e.key());if(old==null||old.end!=e.end||!old.title.equals(e.title)||!old.description.equals(e.description)){events.put(e.key(),e);updated=true;}}
   off=stop;
  }
  if(events.size()>10000)prune();if(updated&&changed!=null)changed.run();
 }
 public void restore(Event e){long now=System.currentTimeMillis();if(e.end>now-6*3600000L&&e.start<now+9*86400000L&&events.size()<10000)events.put(e.key(),e);}
 public void prune(){long now=System.currentTimeMillis();for(Event e:events.values())if(e.end<now-6*3600000L||e.start>now+9*86400000L)events.remove(e.key());if(events.size()>10000){ArrayList<Event> all=snapshot();for(int i=10000;i<all.size();i++)events.remove(all.get(i).key());}}
 public ArrayList<Event> snapshot(){ArrayList<Event> all=new ArrayList<>(events.values());all.sort(Comparator.comparingLong(e->e.start));return all;}
}