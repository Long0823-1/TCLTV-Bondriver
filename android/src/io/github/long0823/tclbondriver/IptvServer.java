package io.github.long0823.tclbondriver;
import java.io.*;
import java.net.*;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.util.*;
import java.util.concurrent.*;
import java.util.zip.GZIPOutputStream;

final class IptvServer implements AutoCloseable {
 private final IptvCatalog catalog; private final NativeGateway gateway; private final EpgStore epg;
 private final String iptvAccessCode; private final ServerSocket server;
 private final ThreadPoolExecutor workers=new ThreadPoolExecutor(0,8,30,TimeUnit.SECONDS,new SynchronousQueue<>());
 private final Set<Socket> sockets=ConcurrentHashMap.newKeySet();
 private final Semaphore[] tuners={new Semaphore(1),new Semaphore(1)};
 private volatile boolean closed;private final EpgCollector collector;
 IptvServer(IptvCatalog c,NativeGateway g,EpgStore e,String code,int port)throws IOException{
  catalog=c;gateway=g;epg=e;iptvAccessCode=code;server=new ServerSocket();server.setReuseAddress(true);server.bind(new InetSocketAddress(port));try{collector=new EpgCollector(c,g,e);}catch(IOException error){server.close();throw error;}
  Thread accept=new Thread(this::accept,"tcl-iptv-http");accept.setDaemon(true);accept.start();
 }
 private void accept(){while(!closed)try{Socket s=server.accept();sockets.add(s);try{workers.execute(()->serve(s));}catch(RejectedExecutionException e){sockets.remove(s);s.close();}}catch(IOException e){if(!closed)android.util.Log.w("TclIptv","Accept failed");}}
 static String line(InputStream in)throws IOException{ByteArrayOutputStream b=new ByteArrayOutputStream();for(int i=0;i<2048;i++){int ch=in.read();if(ch<0)throw new EOFException();if(ch==10)return b.toString("US-ASCII").replace("\r","");b.write(ch);}throw new IOException("Request too long");}
 static void reply(Socket s,int status,String type,byte[] body,boolean head)throws IOException{OutputStream out=s.getOutputStream();out.write(("HTTP/1.1 "+status+" Result\r\nContent-Type: "+type+"\r\nContent-Length: "+body.length+"\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n").getBytes(StandardCharsets.US_ASCII));if(!head)out.write(body);out.flush();}
 private void serve(Socket client){int reserved=-1;Socket nativeStream=null;try{
  client.setSoTimeout(5000);InputStream in=client.getInputStream();String[] request=line(in).split(" ");if(request.length!=3)return;
  boolean head=request[0].equals("HEAD");if(!head&&!request[0].equals("GET")){reply(client,405,"text/plain",new byte[0],false);return;}
  for(int count=0;;count++){String h=line(in);if(h.isEmpty())break;if(count>=30)throw new IOException("Headers too long");}
  String[] parts=request[1].split("/",-1);
  if(parts.length<4||!parts[1].equals("iptv")||!MessageDigest.isEqual(iptvAccessCode.getBytes(StandardCharsets.US_ASCII),parts[2].getBytes(StandardCharsets.US_ASCII))){reply(client,403,"text/plain",new byte[0],head);return;}
  String leaf=parts[3];
  if(parts.length==4&&leaf.equals("epg-status.json")){reply(client,200,"application/json; charset=utf-8",new org.json.JSONObject().put("programmes",epg.size()).put("collecting_tuner",collector.tuner()).put("state",collector.status()).toString().getBytes(StandardCharsets.UTF_8),head);return;}
  if(parts.length==4&&(leaf.equals("epg.xml")||leaf.equals("epg.xml.gz"))){
   byte[] data=epg.xml();if(leaf.endsWith(".gz")){ByteArrayOutputStream bytes=new ByteArrayOutputStream();try(GZIPOutputStream gzip=new GZIPOutputStream(bytes)){gzip.write(data);}data=bytes.toByteArray();}
   reply(client,200,leaf.endsWith(".gz")?"application/gzip":"application/xml; charset=utf-8",data,head);return;
  }
  if(parts.length==4&&(leaf.equals("playlist.m3u")||leaf.equals("playlist.m3u?tuner=0")||leaf.equals("playlist.m3u?tuner=1"))){
   int tuner=leaf.endsWith("=0")?0:1;String host=client.getLocalAddress().getHostAddress();if(host.contains(":"))host="["+host+"]";
   reply(client,200,"audio/x-mpegurl; charset=utf-8",catalog.playlist("http://"+host+":"+server.getLocalPort(),iptvAccessCode,tuner).getBytes(StandardCharsets.UTF_8),head);return;
  }
  if(parts.length!=7||!parts[3].equals("tuner")||!parts[5].equals("channel")||(!parts[4].equals("0")&&!parts[4].equals("1"))||!parts[6].endsWith(".ts")){reply(client,404,"text/plain",new byte[0],head);return;}
  int tuner=Integer.parseInt(parts[4]);IptvCatalog.Channel channel=catalog.channels.get(parts[6].substring(0,parts[6].length()-3));if(channel==null){reply(client,404,"text/plain",new byte[0],head);return;}
  if(head){OutputStream header=client.getOutputStream();header.write("HTTP/1.1 200 OK\r\nContent-Type: video/mp2t\r\nCache-Control: no-store\r\nAccept-Ranges: none\r\nConnection: close\r\n\r\n".getBytes(StandardCharsets.US_ASCII));header.flush();return;}
  if(!tuners[tuner].tryAcquire()){reply(client,409,"text/plain","This IPTV tuner is busy. Use the other tuner playlist.".getBytes(StandardCharsets.UTF_8),false);return;}reserved=tuner;
  String tuned=gateway.tune(tuner,channel);if(!tuned.startsWith("OK ")){reply(client,503,"text/plain","TV channel unavailable".getBytes(StandardCharsets.UTF_8),false);return;}if(closed)return;
  nativeStream=gateway.stream(tuner);sockets.add(nativeStream);TsEpg receiver=epg.receiver();OutputStream out=client.getOutputStream();
  out.write("HTTP/1.1 200 OK\r\nContent-Type: video/mp2t\r\nCache-Control: no-store\r\nAccept-Ranges: none\r\nConnection: close\r\n\r\n".getBytes(StandardCharsets.US_ASCII));out.flush();
  client.setSoTimeout(0);final Socket stream=nativeStream;Thread disconnect=new Thread(()->{try{while(in.read()!=-1){}}catch(IOException ignored){}finally{try{stream.close();}catch(IOException ignored){}}},"iptv-disconnect");disconnect.setDaemon(true);disconnect.start();
  IptvProgram program=new IptvProgram(channel.tsid,channel.sid);byte[] buffer=new byte[48316];int carry=0,n;
  while(!closed&&(n=nativeStream.getInputStream().read(buffer,carry,48128))>0){int total=carry+n,pos=0;ByteArrayOutputStream chunk=new ByteArrayOutputStream(total);
   while(pos+188<=total){if(buffer[pos]!=0x47||(pos+376<=total&&buffer[pos+188]!=0x47)){pos++;continue;}receiver.feed(buffer,pos);program.packet(buffer,pos,chunk);pos+=188;}
   carry=total-pos;System.arraycopy(buffer,pos,buffer,0,carry);chunk.writeTo(out);out.flush();epg.save(false);
  }
 }catch(Exception e){if(!closed)android.util.Log.d("TclIptv","Client finished: "+e.getClass().getSimpleName());}
 finally{if(nativeStream!=null){sockets.remove(nativeStream);try{nativeStream.close();}catch(IOException ignored){}}if(reserved>=0)tuners[reserved].release();sockets.remove(client);try{client.close();}catch(IOException ignored){}epg.save(reserved>=0);}}
 String refreshEpg(){return collector.refresh();}
 String epgStatus(){return collector.status();}
 public void close(){closed=true;collector.close();try{server.close();}catch(IOException ignored){}for(Socket s:sockets)try{s.close();}catch(IOException ignored){}workers.shutdownNow();epg.save(true);}
}
