package io.github.long0823.tclbondriver;
import java.io.IOException;
import java.util.*;

final class CatvCatalog {
 interface RootAccess {String run(String command)throws Exception;}
 static final String PATH="/data/local/tmp/tsd_catv_channels";
 static final class Entry {int onid,tsid,sid,frequency,mode,slot;String name;}
 static List<Entry> parse(String text)throws IOException {
  if(text==null||text.length()>524288)throw new IOException("CATVの局一覧が大きすぎます");
  ArrayList<Entry> entries=new ArrayList<>();HashSet<String> seen=new HashSet<>();
  for(String line:text.split("\\r?\\n")){
   if(line.trim().isEmpty()||line.startsWith("#"))continue;
   String[] p=line.trim().split("\\s+",7);if(p.length<6)throw new IOException("CATVの局一覧が不正です");
   Entry e=new Entry();int[] v=new int[6];try{for(int i=0;i<6;i++){if(!p[i].matches("[0-9]+"))throw new NumberFormatException();v[i]=Integer.parseInt(p[i]);}}catch(NumberFormatException ex){throw new IOException("CATVの選局値が不正です",ex);}
   if(v[0]<1||v[0]>65535||v[1]<1||v[1]>65535||v[2]<1||v[2]>65535||v[3]<90000||v[3]>770000||v[4]<0||v[4]>2||v[5]<1||v[5]>15)throw new IOException("CATVの選局値が範囲外です");
   e.onid=v[0];e.tsid=v[1];e.sid=v[2];e.frequency=v[3];e.mode=v[4];e.slot=v[5];e.name=p.length==7?p[6]:"";
   if(e.name.startsWith("arib:")){String hex=e.name.substring(5);if(!hex.matches("(?:[0-9a-fA-F]{2}){0,160}"))throw new IOException("CATVの局名が不正です");byte[] data=new byte[hex.length()/2];for(int i=0;i<data.length;i++)data[i]=(byte)Integer.parseInt(hex.substring(2*i,2*i+2),16);e.name=AribText.decode(data,0,data.length);}
   e.name=e.name.replace('\r',' ').replace('\n',' ').replace('\0',' ').trim();if(e.name.isEmpty())e.name="CATV "+e.sid;
   if(e.name.length()>200||!seen.add(e.onid+":"+e.tsid+":"+e.sid)||entries.size()==512)throw new IOException("CATVの局一覧が重複または上限超過です");entries.add(e);
  }return entries;
 }
 static String normalize(String text)throws IOException {StringBuilder out=new StringBuilder();for(Entry e:parse(text))out.append(e.onid).append(' ').append(e.tsid).append(' ').append(e.sid).append(' ').append(e.frequency).append(' ').append(e.mode).append(' ').append(e.slot).append(' ').append(e.name).append('\n');return out.toString();}
 static String read(RootAccess root)throws Exception{return root.run("if [ -f "+PATH+" ]; then cat "+PATH+"; fi");}
 static String scan(android.content.Context context,RootAccess root)throws Exception{
  String result=root.run("/data/local/tmp/tsctl --catv-scan");
  String normalized=normalize(read(root));
  java.io.File file=new java.io.File(context.getCacheDir(),"catv-normalized.txt");
  try(java.io.OutputStream out=new java.io.FileOutputStream(file)){out.write(normalized.getBytes(java.nio.charset.StandardCharsets.UTF_8));}
  String path="'"+file.getAbsolutePath().replace("'","'\\''")+"'";
  root.run("cat "+path+" > "+PATH+".new && chmod 600 "+PATH+".new && mv "+PATH+".new "+PATH);
  return "CATVスキャン完了："+parse(normalized).size()+"サービス\n"+result;
 }
 static String rows(String text)throws IOException {StringBuilder out=new StringBuilder();int index=10000;for(Entry e:parse(text))out.append("Row: ").append(index++).append(" display_number=").append(e.sid).append(", original_network_id=").append(e.onid).append(", transport_stream_id=").append(e.tsid).append(", service_id=").append(e.sid).append(", type=TYPE_ISDB_C, service_type=SERVICE_TYPE_AUDIO_VIDEO, browsable=1, display_name=").append(e.name).append('\n');return out.toString();}
}
