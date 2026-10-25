package io.github.long0823.tclbondriver;

import android.app.Activity;
import android.app.AlertDialog;
import android.os.Bundle;
import android.graphics.Color;
import android.view.View;
import android.widget.*;
import java.io.*;
import java.security.SecureRandom;
import java.util.concurrent.*;

public final class MainActivity extends Activity {
 private final ExecutorService worker=Executors.newSingleThreadExecutor();
 private TextView status,details,network;
 private final android.os.Handler networkHandler=new android.os.Handler(android.os.Looper.getMainLooper());
 private final Runnable networkRefresh=new Runnable(){public void run(){updateNetwork();networkHandler.postDelayed(this,5000);}};
 private ProgressBar progress;
 private Button setup,start,reboot,refresh,restore;
 private File stage; private PhonePairing pairing; private AlertDialog pairingDialog;private android.net.nsd.NsdManager.RegistrationListener phoneAdvertisement;private TextView pairingInfo;private Button pairButton;
 private boolean destroyed;
 private int dp(int value) { return (int)(value*getResources().getDisplayMetrics().density+0.5f); }
 @Override public void onCreate(Bundle saved) {
  super.onCreate(saved);
  ScrollView scroll=new ScrollView(this);
  LinearLayout root=new LinearLayout(this);root.setOrientation(LinearLayout.VERTICAL);root.setPadding(dp(48),dp(28),dp(48),dp(28));root.setBackgroundColor(Color.rgb(15,26,34));scroll.addView(root);setContentView(scroll);
  TextView title=text("TCL BonDriver 0.5.0",28,Color.WHITE);root.addView(title);
  root.addView(text("テレビで別のアプリを使いながら、PCで異なる地デジ2局を同時受信",16,Color.rgb(180,197,208)));
  network=text("有線LAN IP：確認中…",22,Color.rgb(125,218,190));network.setTextIsSelectable(true);network.setPadding(0,dp(16),0,0);root.addView(network);
  status=text("初めて使う場合は「セットアップ」を選んでください。",18,Color.WHITE);status.setPadding(0,dp(24),0,dp(12));root.addView(status);pairingInfo=text("",28,Color.rgb(125,218,190));pairingInfo.setFocusable(true);pairingInfo.setVisibility(View.GONE);root.addView(pairingInfo);
  progress=new ProgressBar(this,null,android.R.attr.progressBarStyleHorizontal);progress.setIndeterminate(true);progress.setVisibility(View.GONE);root.addView(progress);
  setup=button(root,"1. セットアップ",()->perform("対応環境を確認し、セットアップしています…",()->{
   extract();String output=root("sh "+quote(new File(stage,"install.sh").getAbsolutePath())+" "+quote(stage.getAbsolutePath()),90);
   TclPolicy.configure(this,true);NetworkService.enable(this,true);return "セットアップが完了しました。テレビを再起動すると有効になります。\n"+output;
  }));
  reboot=button(root,"2. テレビを再起動",()->new AlertDialog.Builder(this).setTitle("テレビを再起動します").setMessage("再起動後、配信機能を自動的に開始します。").setNegativeButton("戻る",null).setPositiveButton("再起動",(d,w)->perform("テレビを再起動しています…",()->root("sync; reboot",15))).show());
  start=button(root,"配信を開始・復旧",()->perform("配信機能を確認しています…",()->root("sh /data/adb/service.d/91-tcl-pvr.sh; /data/local/tmp/tsctl --status",90)));
  refresh=button(root,"状態を確認",()->perform("状態を確認しています…",()->{
   String output=root("/data/local/tmp/tsctl --status; ip -4 addr show eth0",15);
   boolean first=output.contains(" running=1 "),second=output.contains(" tuner1_ready=1 ");
   return "チューナー1："+(first?"配信中":"セットアップ後に再起動してください")+"\nチューナー2："+(second?"配信中":"選局待ち（PCの視聴ソフトで選局してください）")+"\n"+output;
  }));
  button(root,"画面消灯中の通信維持を有効にする",()->perform("TCLの常駐許可を設定しています…",()->{TclPolicy.configure(this,true);NetworkService.enable(this,true);return "画面消灯中の通信維持とIPTVを有効にしました。";}));
  button(root,"IPTVのM3U接続先を表示",this::showIptv);
  root.addView(text("通信維持中は深い待機に入らず消費電力が増えます。AC給電中の自動消灯も抑制しますが、リモコンで画面は消せます。停止時に変更した電源設定を戻します。",14,Color.rgb(180,197,208)));
  button(root,"通信維持を停止する",()->perform("通信維持を停止しています…",()->{NetworkService.enable(this,false);TclPolicy.configure(this,false);return "通信維持を停止しました。";}));
  pairButton=button(root,"スマートフォンを接続（確認コードを表示）",this::pairPhone);
  root.addView(text("対応環境：TCL G06 日本向け・Magisk導入済み\n初回はMagiskのroot許可が必要です。",14,Color.rgb(180,197,208)));
  details=text("",13,Color.rgb(154,182,194));details.setTextIsSelectable(true);details.setPadding(0,dp(20),0,dp(12));root.addView(details);
  restore=button(root,"セットアップ前の状態に戻す",()->new AlertDialog.Builder(this).setTitle("元の状態に戻しますか？").setMessage("保存済みの設定を復元します。反映にはテレビの再起動が必要です。").setNegativeButton("戻る",null).setPositiveButton("復元",(d,w)->perform("設定を復元しています…",()->{extract();NetworkService.enable(this,false);TclPolicy.configure(this,false);return root("sh "+quote(new File(stage,"restore.sh").getAbsolutePath()),60);})).show());
  setup.requestFocus();
 }
 private void showIptv(){StringBuilder urls=new StringBuilder(NetworkService.state+"\n\n");String code=NetworkService.accessCode(this);try{java.util.Enumeration<java.net.NetworkInterface> interfaces=java.net.NetworkInterface.getNetworkInterfaces();while(interfaces.hasMoreElements()){java.net.NetworkInterface iface=interfaces.nextElement();if(!iface.isUp()||iface.isLoopback())continue;java.util.Enumeration<java.net.InetAddress> addresses=iface.getInetAddresses();while(addresses.hasMoreElements()){java.net.InetAddress address=addresses.nextElement();if(!(address instanceof java.net.Inet4Address))continue;String base="http://"+address.getHostAddress()+":40560/iptv/"+code+"/playlist.m3u?tuner=";urls.append(iface.getName()).append(" · チューナー1\n").append(base).append("0\n\nチューナー2\n").append(base).append("1\n\n");}}}catch(Exception ignored){}urls.append("先に通信維持を有効にしてください。URLをIPTVアプリやVLCへ登録します。同じチューナーはBonDriver・スマホと同時使用しないでください。URLは専用の接続キーを含むため公開しないでください。");TextView text=text(urls.toString(),16,Color.WHITE);text.setTextIsSelectable(true);text.setPadding(dp(20),dp(20),dp(20),dp(20));ScrollView scroll=new ScrollView(this);scroll.addView(text);new AlertDialog.Builder(this).setTitle("IPTV / M3U").setView(scroll).setPositiveButton("閉じる",null).show();}
 private void pairPhone(){if(pairing!=null){pairing.close();pairing=null;stopPhoneAdvertisement();pairingInfo.setVisibility(View.GONE);pairButton.setText("スマートフォンを接続（確認コードを表示）");status.setText("スマートフォンの接続受付を終了しました");return;}pairButton.setEnabled(false);status.setText("スマートフォン用の設定を準備しています…");worker.submit(()->{try{byte[] data=PhonePairing.export(this);runOnUiThread(()->{if(destroyed)return;pairButton.setEnabled(true);try{pairing=new PhonePairing(data,message->runOnUiThread(()->{if(!destroyed){status.setText(message);pairing=null;stopPhoneAdvertisement();pairingInfo.setVisibility(View.GONE);pairButton.setText("スマートフォンを接続（確認コードを表示）");}}));advertisePhone();pairingInfo.setText("確認コード  "+pairing.code+"\nスマホにTVのIPとこの6桁を入力してください。\n2分間・1回のみ有効です。TVで他のアプリを開くと無効になります。");pairingInfo.setVisibility(View.VISIBLE);pairButton.setText("スマートフォンの接続受付を終了");pairingInfo.requestFocus();status.setText("スマートフォンの接続を待っています");}catch(Exception e){status.setText("接続の受付を開始できません。もう一度お試しください。");}});}catch(Exception e){runOnUiThread(()->{if(!destroyed){pairButton.setEnabled(true);status.setText(e.getMessage());}});}});}
 private void advertisePhone(){stopPhoneAdvertisement();android.net.nsd.NsdManager nsd=(android.net.nsd.NsdManager)getSystemService(NSD_SERVICE);android.net.nsd.NsdServiceInfo service=new android.net.nsd.NsdServiceInfo();service.setServiceName("TCL TV");service.setServiceType("_tcltv._tcp.");service.setPort(40559);service.setAttribute("protocol","1");phoneAdvertisement=new android.net.nsd.NsdManager.RegistrationListener(){public void onServiceRegistered(android.net.nsd.NsdServiceInfo i){runOnUiThread(()->{if(pairing==null)stopPhoneAdvertisement();});}public void onRegistrationFailed(android.net.nsd.NsdServiceInfo i,int e){}public void onServiceUnregistered(android.net.nsd.NsdServiceInfo i){}public void onUnregistrationFailed(android.net.nsd.NsdServiceInfo i,int e){}};try{nsd.registerService(service,android.net.nsd.NsdManager.PROTOCOL_DNS_SD,phoneAdvertisement);}catch(Exception ignored){phoneAdvertisement=null;}}
 private void stopPhoneAdvertisement(){if(phoneAdvertisement==null)return;try{((android.net.nsd.NsdManager)getSystemService(NSD_SERVICE)).unregisterService(phoneAdvertisement);}catch(Exception ignored){}phoneAdvertisement=null;}
 private void updateNetwork() {
  StringBuilder wired=new StringBuilder(),wifi=new StringBuilder();
  try {
   java.util.Enumeration<java.net.NetworkInterface> interfaces=java.net.NetworkInterface.getNetworkInterfaces();
   while(interfaces!=null&&interfaces.hasMoreElements()) {
    java.net.NetworkInterface iface=interfaces.nextElement();String name=iface.getName();boolean ethernet=name.matches("eth[0-9]+")||name.matches("en[A-Za-z0-9_.-]+");boolean wireless=name.matches("wlan[0-9]+")||name.matches("wl[A-Za-z0-9_.-]+");
    if(!iface.isUp()||iface.isLoopback()||(!ethernet&&!wireless))continue;
    StringBuilder target=ethernet?wired:wifi;java.util.Enumeration<java.net.InetAddress> addresses=iface.getInetAddresses();
    while(addresses.hasMoreElements()) {java.net.InetAddress address=addresses.nextElement();if(address instanceof java.net.Inet4Address&&!address.isLoopbackAddress()) {if(target.length()>0)target.append(" / ");target.append(address.getHostAddress());}}
   }
   network.setText("有線LAN IP："+(wired.length()==0?"未接続・IPv4未取得":wired.toString())+"\nWi-Fi IP："+(wifi.length()==0?"未接続・IPv4未取得":wifi.toString())+"\nPC・スマートフォンの接続先IP（PCセットアップ時は自動取得できます）\n"+NetworkService.state+"");
  }catch(Exception error) {network.setText("IPを取得できません。TVのネットワーク設定を確認してください。");}
 }
 @Override protected void onResume() {super.onResume();networkHandler.removeCallbacks(networkRefresh);networkHandler.post(networkRefresh);}
 @Override protected void onPause(){networkHandler.removeCallbacks(networkRefresh);super.onPause();}
 @Override protected void onStop(){if(pairing!=null){pairing.close();pairing=null;}stopPhoneAdvertisement();if(pairingInfo!=null)pairingInfo.setVisibility(View.GONE);if(pairButton!=null)pairButton.setText("スマートフォンを接続（確認コードを表示）");super.onStop();}
 private TextView text(String value,int size,int color) { TextView v=new TextView(this);v.setText(value);v.setTextSize(size);v.setTextColor(color);return v; }
 private Button button(LinearLayout parent,String label,Runnable click) {
  Button button=new Button(this);button.setText(label);button.setTextSize(16);button.setAllCaps(false);button.setFocusable(true);button.setMinHeight(dp(52));button.setOnClickListener(v->click.run());
  LinearLayout.LayoutParams layout=new LinearLayout.LayoutParams(-1,-2);layout.setMargins(0,dp(6),0,dp(6));parent.addView(button,layout);return button;
 }
 private void busy(boolean value) { progress.setVisibility(value?View.VISIBLE:View.GONE);for(Button b:new Button[]{setup,start,reboot,refresh,restore})b.setEnabled(!value); }
 private void perform(String message,Callable<String> action) {
  busy(true);status.setText(message);details.setText("");
  worker.submit(()->{String result;boolean ok;
   try { result=action.call();ok=true; } catch(Exception error) { result="処理を完了できませんでした。\n"+error.getMessage();ok=false; }
   final String output=result;final boolean success=ok;
   runOnUiThread(()->{if(destroyed)return;busy(false);String[] lines=output.split("\n",2);status.setText(lines[0]);details.setText(lines.length>1?lines[1]:"");if(!success)setup.requestFocus();});
  });
 }
 private static String quote(String value) { return "'"+value.replace("'","'\"'\"'")+"'"; }
 private String root(String command,int timeout) throws Exception {
  Process process=new ProcessBuilder("su","-c",command).redirectErrorStream(true).start();
  if(!process.waitFor(timeout,TimeUnit.SECONDS)) { process.destroyForcibly();throw new IOException("時間切れです。Magiskの許可とテレビの状態を確認してください。"); }
  ByteArrayOutputStream output=new ByteArrayOutputStream();byte[] buffer=new byte[4096];int count;
  try(InputStream stream=process.getInputStream()) {while((count=stream.read(buffer))>0)if(output.size()<32768)output.write(buffer,0,Math.min(count,32768-output.size()));}
  String text=output.toString("UTF-8").trim();if(process.exitValue()!=0)throw new IOException(text.isEmpty()?"root権限を許可してください。":text);return text;
 }
 private void extract() throws Exception {
  stage=new File(getFilesDir(),"setup-assets");if(!stage.isDirectory()&&!stage.mkdirs())throw new IOException("保存先を作成できません。");
  for(String name:new String[]{"install.sh","restore.sh","libtsd.so","tsctl","90-tsd-inject.sh","91-tcl-pvr.sh","tcl-pvr-net.sh"}) {
   try(InputStream input=getAssets().open(name);OutputStream output=new FileOutputStream(new File(stage,name))) {byte[] b=new byte[16384];int n;while((n=input.read(b))>0)output.write(b,0,n);}
  }
  byte[] bytes=new byte[32];new SecureRandom().nextBytes(bytes);StringBuilder token=new StringBuilder();for(byte b:bytes)token.append(String.format(java.util.Locale.ROOT,"%02x",b&255));
  try(OutputStream output=new FileOutputStream(new File(stage,"token"))) {output.write((token.toString()+"\n").getBytes("UTF-8"));}
 }
 @Override protected void onDestroy() {destroyed=true;worker.shutdown();super.onDestroy();}
}
