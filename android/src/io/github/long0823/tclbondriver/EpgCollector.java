package io.github.long0823.tclbondriver;
import java.net.*;
import java.util.*;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicBoolean;

/** EIT taps are passive; only a native low-priority lease permits background tuning. */
final class EpgCollector implements AutoCloseable {
 private final NativeGateway gateway;private final EpgStore epg;private final ArrayList<IptvCatalog.Channel> muxes;
 private final DatagramSocket[] taps=new DatagramSocket[2];private final AtomicBoolean requested=new AtomicBoolean();
 private volatile boolean closed;private volatile String state="自動更新の準備中";private volatile int owner=-1,lease;
 private final Random random=new java.security.SecureRandom();
 EpgCollector(IptvCatalog catalog,NativeGateway g,EpgStore e)throws java.io.IOException{gateway=g;epg=e;LinkedHashMap<String,IptvCatalog.Channel> map=new LinkedHashMap<>();for(IptvCatalog.Channel c:catalog.channels.values())map.putIfAbsent(c.onid+":"+c.tsid,c);muxes=new ArrayList<>(map.values());try{for(int t=0;t<2;t++){final int tuner=t;taps[t]=new DatagramSocket(null);taps[t].setReceiveBufferSize(262144);taps[t].bind(new InetSocketAddress("127.0.0.1",40561+t));taps[t].setSoTimeout(1000);Thread tap=new Thread(()->receive(tuner),"xmltv-eit-"+t);tap.setDaemon(true);tap.start();}}catch(java.io.IOException error){close();throw error;}Thread task=new Thread(this::collect,"xmltv-background");task.setDaemon(true);task.start();}
 private void receive(int tuner){TsEpg parser=epg.receiver();byte[] bytes=new byte[188];while(!closed){try{DatagramPacket packet=new DatagramPacket(bytes,bytes.length);taps[tuner].receive(packet);if(packet.getLength()==188&&packet.getAddress().isLoopbackAddress()){parser.feed(bytes,0);epg.save(false);}}catch(SocketTimeoutException ignored){}catch(Exception error){if(!closed)state="番組情報の受信を確認してください";}}}
 private boolean pause(long ms){long until=System.nanoTime()+TimeUnit.MILLISECONDS.toNanos(ms);while(!closed&&System.nanoTime()<until)try{Thread.sleep(Math.min(250,ms));}catch(InterruptedException error){Thread.currentThread().interrupt();return false;}return !closed;}
 int tuner(){return owner;}
 String status(){return "XMLTV "+epg.size()+"番組 · "+state;}
 String refresh(){requested.set(true);return "空きチューナーで自動更新します。視聴アクセスを優先します。";}
 private void collect(){int cursor=0,failed=0;long due=System.nanoTime()+TimeUnit.SECONDS.toNanos(15);while(!closed){if(requested.getAndSet(false)){cursor=0;failed=0;due=0;}
  if(System.nanoTime()<due){if(!pause(1000))break;continue;}
  int chosen=-1;int ticket=1+random.nextInt(Integer.MAX_VALUE-1);
  try{for(int t:new int[]{1,0}){String reply=gateway.command("EPG_CLAIM "+t+" "+ticket);if(reply.startsWith("OK ")){chosen=t;break;}if(reply.equals("ERR command")){state="配信本体の更新とTV再起動が必要です";due=System.nanoTime()+TimeUnit.MINUTES.toNanos(1);break;}}
   if(chosen<0){if(due<System.nanoTime())state="視聴優先・空きチューナー待ち";pause(5000);continue;}
   owner=chosen;lease=ticket;IptvCatalog.Channel c=muxes.get(cursor);state="自動更新 "+(cursor+1)+"/"+muxes.size()+" · チューナー"+(chosen+1);
   String reply=gateway.command("EPG_TUNE "+chosen+" "+ticket+" "+c.onid+" "+c.tsid+" "+c.sid);boolean interrupted=false;
   if(!reply.startsWith("OK ")){if(!gateway.command("EPG_POLL "+chosen+" "+ticket).startsWith("OK "))interrupted=true;else failed++;}
   else{long until=System.nanoTime()+TimeUnit.SECONDS.toNanos(12);while(!closed&&System.nanoTime()<until){if(!gateway.command("EPG_POLL "+chosen+" "+ticket).startsWith("OK ")){interrupted=true;break;}if(!pause(400))break;}}
   if(interrupted){state="視聴アクセスで収集を中断・再開待ち";due=System.nanoTime()+TimeUnit.SECONDS.toNanos(5);}else if(++cursor>=muxes.size()){cursor=0;state="自動更新完了（未取得 "+failed+"多重）・次回3時間後";failed=0;due=System.nanoTime()+TimeUnit.HOURS.toNanos(3);}
  }catch(Exception error){state="番組表の自動更新を再試行します";due=System.nanoTime()+TimeUnit.SECONDS.toNanos(15);}
  finally{if(chosen>=0)try{gateway.command("EPG_RELEASE "+chosen+" "+ticket);}catch(Exception ignored){}owner=-1;lease=0;epg.save(true);}
  if(!pause(500))break;
 }}
 public void close(){closed=true;int t=owner,l=lease;if(t>=0&&l>0)try{gateway.command("EPG_RELEASE "+t+" "+l);}catch(Exception ignored){}for(DatagramSocket socket:taps)if(socket!=null)socket.close();epg.save(true);}
}
