package io.github.long0823.tclbondriver;

import android.util.AtomicFile;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.util.*;
import org.json.*;

/** Bounded, private EIT cache. Reading the guide never touches a tuner. */
final class EpgStore {
 private final IptvCatalog catalog; private final AtomicFile file;
 private final Map<String,XmlTv.Programme> programmes=new HashMap<>();
 private long lastSave; private boolean dirty;
 EpgStore(IptvCatalog c,File path){catalog=c;file=new AtomicFile(path);try{if(path.length()>20*1024*1024)return;JSONArray rows=new JSONArray(new String(file.readFully(),StandardCharsets.UTF_8));for(int i=0;i<Math.min(rows.length(),10000);i++){JSONObject p=rows.getJSONObject(i);put(new XmlTv.Programme(p.getString("channel"),p.getLong("start"),p.getLong("end"),p.getString("title"),p.optString("description")));}prune();dirty=false;}catch(Exception ignored){}}
 private void put(XmlTv.Programme p){long now=System.currentTimeMillis();if(!catalog.channels.containsKey(p.channel)||p.start<=0||p.end<=p.start||p.end<now-6*3600000L||p.start>now+9*86400000L||p.title.isEmpty())return;String key=p.channel+":"+p.start;XmlTv.Programme old=programmes.get(key);if(old==null||old.end!=p.end||!old.title.equals(p.title)||!old.description.equals(p.description)){programmes.put(key,p);dirty=true;}}
 synchronized void merge(Collection<TsEpg.Event> events){for(TsEpg.Event e:events)put(new XmlTv.Programme(e.onid+"-"+e.tsid+"-"+e.sid,e.start,e.end,e.title,e.description));prune();}
 TsEpg receiver(){TsEpg parser=new TsEpg();parser.changed=()->merge(parser.snapshot());return parser;}
 private void prune(){long now=System.currentTimeMillis();programmes.values().removeIf(p->p.end<now-6*3600000L||p.start>now+9*86400000L);if(programmes.size()>10000){ArrayList<XmlTv.Programme> all=new ArrayList<>(programmes.values());all.sort(Comparator.comparingLong(p->p.start));for(int i=10000;i<all.size();i++)programmes.remove(all.get(i).channel+":"+all.get(i).start);}}
 synchronized int size(){prune();return programmes.size();}
 synchronized byte[] xml(){prune();return XmlTv.build(catalog,programmes.values(),System.currentTimeMillis());}
 synchronized void save(boolean force){long now=System.currentTimeMillis();if(!dirty||(!force&&now-lastSave<30000))return;FileOutputStream out=null;try{prune();JSONArray rows=new JSONArray();for(XmlTv.Programme p:programmes.values()){JSONObject row=new JSONObject();row.put("channel",p.channel).put("start",p.start).put("end",p.end).put("title",p.title).put("description",p.description);rows.put(row);}byte[] bytes=rows.toString().getBytes(StandardCharsets.UTF_8);if(bytes.length>20*1024*1024)return;out=file.startWrite();out.write(bytes);file.finishWrite(out);out=null;lastSave=now;dirty=false;}catch(Exception ignored){if(out!=null)file.failWrite(out);}}
}
