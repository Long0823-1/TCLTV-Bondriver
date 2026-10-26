package io.github.long0823.tclbondriver;

import java.nio.charset.StandardCharsets;
import java.time.Instant;
import java.time.ZoneOffset;
import java.time.format.DateTimeFormatter;
import java.util.*;

/** XMLTV output uses the same stable ONID-TSID-SID identifiers as the M3U. */
final class XmlTv {
 static final class Programme {
  final String channel,title,description; final long start,end;
  Programme(String c,long s,long e,String t,String d){channel=c;start=s;end=e;title=t;description=d;}
 }
 private static final DateTimeFormatter TIME=DateTimeFormatter.ofPattern("yyyyMMddHHmmss Z",Locale.ROOT).withZone(ZoneOffset.UTC);
 static String time(long value){return TIME.format(Instant.ofEpochMilli(value));}
 static String escape(String value){StringBuilder out=new StringBuilder();if(value==null)return "";value.codePoints().forEach(c->{if(c!=9&&c!=10&&c!=13&&(c<32||c>0x10ffff||(c>=0xd800&&c<=0xdfff)||c==0xfffe||c==0xffff))return;switch(c){case '&':out.append("&amp;");break;case '<':out.append("&lt;");break;case '>':out.append("&gt;");break;case '"':out.append("&quot;");break;case '\'':out.append("&apos;");break;default:out.appendCodePoint(c);}});return out.toString();}
 static byte[] build(IptvCatalog catalog,Collection<Programme> programmes,long now){
  StringBuilder out=new StringBuilder("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<tv generator-info-name=\"TCLTV-Bondriver\">\n");
  for(IptvCatalog.Channel c:catalog.channels.values())out.append("  <channel id=\"").append(c.id()).append("\"><display-name lang=\"ja\">").append(escape(c.name)).append("</display-name><display-name>").append(escape(c.number)).append("</display-name></channel>\n");
  ArrayList<Programme> all=new ArrayList<>(programmes);all.sort(Comparator.comparingLong((Programme p)->p.start).thenComparing(p->p.channel));Set<String> seen=new HashSet<>();
  for(Programme p:all){if(!catalog.channels.containsKey(p.channel)||p.title==null||p.title.isEmpty()||p.start<=0||p.end<=p.start||p.end<now-6*3600000L||p.start>now+9*86400000L||!seen.add(p.channel+":"+p.start))continue;
   out.append("  <programme start=\"").append(time(p.start)).append("\" stop=\"").append(time(p.end)).append("\" channel=\"").append(p.channel).append("\"><title lang=\"ja\">").append(escape(p.title)).append("</title>");
   if(p.description!=null&&!p.description.isEmpty())out.append("<desc lang=\"ja\">").append(escape(p.description)).append("</desc>");out.append("</programme>\n");
  }
  return out.append("</tv>\n").toString().getBytes(StandardCharsets.UTF_8);
 }
}
