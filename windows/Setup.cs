using System;
using System.IO;
using System.Net;
using System.Net.Sockets;
using System.Text;
using System.Text.RegularExpressions;
using System.Diagnostics;
using System.Security.AccessControl;
using System.Security.Principal;
using System.Threading.Tasks;
using System.Windows.Forms;
using System.Drawing;
namespace TclBonDriverSetup {
 static class Program {
  [STAThread] static void Main() {Application.EnableVisualStyles();Application.SetCompatibleTextRenderingDefault(false);Application.Run(new SetupForm());}
 }
 sealed class SetupForm:Form {
  readonly TextBox adbSerial=new TextBox(),host=new TextBox(),destination=new TextBox(),log=new TextBox();
  readonly ComboBox networkMode=new ComboBox();
  readonly CheckBox dual=new CheckBox(),autoIp=new CheckBox();
  readonly Button install=new Button(),pair=new Button(),test=new Button(),browse=new Button(),detect=new Button();
  readonly string root=AppDomain.CurrentDomain.BaseDirectory;
  public SetupForm() {
   Text="TCL BonDriver セットアップ";ClientSize=new Size(780,610);MinimumSize=new Size(720,590);Font=new Font("Yu Gothic UI",10);StartPosition=FormStartPosition.CenterScreen;
   var panel=new TableLayoutPanel {Dock=DockStyle.Fill,ColumnCount=3,RowCount=9,Padding=new Padding(20)};
   panel.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute,155));panel.ColumnStyles.Add(new ColumnStyle(SizeType.Percent,100));panel.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute,90));Controls.Add(panel);
   var title=new Label {Text="TCL BonDriver",Font=new Font(Font.FontFamily,20,FontStyle.Bold),AutoSize=true};panel.Controls.Add(title,0,0);panel.SetColumnSpan(title,3);
   var intro=new Label {Text="TV用APKを導入し、PCの視聴ソフトへBonDriverを配置します。",AutoSize=true,Margin=new Padding(0,10,0,15)};panel.Controls.Add(intro,0,1);panel.SetColumnSpan(intro,3);
   AddField(panel,2,"ADB接続先",adbSerial,"");AddField(panel,3,"TVの接続先IP",host,"");detect.Text="IP取得";detect.Dock=DockStyle.Fill;panel.Controls.Add(detect,2,3);AddField(panel,4,"TVTestのフォルダー",destination,"");browse.Text="選択…";browse.Dock=DockStyle.Fill;panel.Controls.Add(browse,2,4);
   browse.Click+=(s,e)=>{using(var d=new FolderBrowserDialog()) {d.Description="TVTest.exeがあるフォルダー";if(d.ShowDialog(this)==DialogResult.OK)destination.Text=d.SelectedPath;}};
   install.Text="1. APKをTVへ導入";pair.Text="2. 接続設定・局一覧を更新";test.Text="3. 配信をテスト";
   var buttons=new FlowLayoutPanel {AutoSize=true,Dock=DockStyle.Fill,Margin=new Padding(0,18,0,10)};foreach(var b in new[]{install,pair,test}) {b.AutoSize=true;b.MinimumSize=new Size(195,38);buttons.Controls.Add(b);}dual.Text="2チューナー分のBonDriverを配置する";dual.Checked=true;dual.AutoSize=true;panel.Controls.Add(dual,0,5);panel.SetColumnSpan(dual,3);
   autoIp.Text="TVのIPを自動取得";autoIp.Checked=true;autoIp.AutoSize=true;buttons.Controls.Add(autoIp);networkMode.DropDownStyle=ComboBoxStyle.DropDownList;networkMode.Items.AddRange(new object[]{"自動（有線LAN優先）","有線LAN","Wi-Fi"});networkMode.SelectedIndex=0;networkMode.Width=180;buttons.Controls.Add(networkMode);panel.Controls.Add(buttons,0,6);panel.SetColumnSpan(buttons,3);
   var hint=new Label {AutoSize=true,Text="初回：TVのAPKでrootを許可し、セットアップ後に再起動してください。\nUSB接続の場合、ADB接続先を空欄にすると接続中の1台を自動選択します。"};panel.Controls.Add(hint,0,7);panel.SetColumnSpan(hint,3);
   log.Multiline=true;log.ReadOnly=true;log.ScrollBars=ScrollBars.Vertical;log.Dock=DockStyle.Fill;log.BackColor=Color.FromArgb(245,247,249);panel.Controls.Add(log,0,8);panel.SetColumnSpan(log,3);for(int i=0;i<8;i++)panel.RowStyles.Add(new RowStyle(SizeType.AutoSize));panel.RowStyles.Add(new RowStyle(SizeType.Percent,100));
   install.Click+=async(s,e)=>await Work(InstallApk);pair.Click+=async(s,e)=>await Work(Pair);test.Click+=async(s,e)=>await Work(TestStream);detect.Click+=async(s,e)=>await Work(()=>DetectHost(Device()));
  }
  static void AddField(TableLayoutPanel p,int row,string label,TextBox field,string value) {p.Controls.Add(new Label {Text=label,AutoSize=true,Anchor=AnchorStyles.Left},0,row);field.Text=value;field.Dock=DockStyle.Fill;field.Margin=new Padding(0,5,0,5);p.Controls.Add(field,1,row);}
  void Note(string value) {if(InvokeRequired) {BeginInvoke(new Action<string>(Note),value);return;}log.AppendText(DateTime.Now.ToString("HH:mm:ss")+"  "+value+Environment.NewLine);}
  async Task Work(Action operation) {foreach(var b in new[]{install,pair,test,browse,detect})b.Enabled=false;dual.Enabled=false;autoIp.Enabled=false;networkMode.Enabled=false;try {await Task.Run(operation);}catch(Exception ex) {Note("完了できませんでした："+ex.Message);}finally {foreach(var b in new[]{install,pair,test,browse,detect})b.Enabled=true;dual.Enabled=true;autoIp.Enabled=true;networkMode.Enabled=true;}}
  bool UseDual() {return (bool)Invoke(new Func<bool>(()=>dual.Checked));}
  string ReadText(TextBox field) {return (string)Invoke(new Func<string>(()=>field.Text.Trim()));}
  static string Q(string arg) {if(arg.IndexOf('"')>=0||arg.IndexOf('\n')>=0||arg.IndexOf('\r')>=0)throw new ArgumentException("入力に使用できない文字があります。");return "\""+arg+"\"";}
  string FindAdb() {
   string packaged=Path.Combine(root,"platform-tools","adb.exe");if(File.Exists(packaged))return packaged;
   foreach(string directory in (Environment.GetEnvironmentVariable("PATH")??"").Split(';')) {try {var p=Path.Combine(directory.Trim('"'),"adb.exe");if(File.Exists(p))return p;}catch(ArgumentException){}}
   throw new IOException("配布ZIP内のplatform-toolsが見つかりません。ZIPをすべて展開してください。");
  }
  string Adb(string args,int seconds) {
   var start=new ProcessStartInfo(FindAdb(),"-P 5077 "+args) {UseShellExecute=false,CreateNoWindow=true,RedirectStandardOutput=true,RedirectStandardError=true,StandardOutputEncoding=Encoding.UTF8,StandardErrorEncoding=Encoding.UTF8};
   using(var p=Process.Start(start)) {var stdout=p.StandardOutput.ReadToEndAsync();var stderr=p.StandardError.ReadToEndAsync();if(!p.WaitForExit(seconds*1000)) {p.Kill();throw new IOException("ADBの応答がありません。TVのIP・デバッグ許可を確認してください。");}Task.WaitAll(stdout,stderr);string output=stdout.Result+stderr.Result;if(p.ExitCode!=0)throw new IOException("ADB操作に失敗しました。接続とroot許可を確認してください。");return output;}
  }
  string Device() {
   string serial=ReadText(adbSerial);if(serial.Length>0) {if(!Regex.IsMatch(serial,@"^[A-Za-z0-9_.:\-]+$"))throw new ArgumentException("ADB接続先の形式を確認してください。");if(serial.Contains(":"))Adb("connect "+Q(serial),20);}
   string devices=Adb("devices",15);var active=Regex.Matches(devices,@"(?m)^([^\s]+)\s+device\s*$");if(serial.Length==0) {if(active.Count!=1)throw new IOException("ADB接続中のTVを1台にするか、接続先を指定してください。");serial=active[0].Groups[1].Value;}
   bool found=false;foreach(Match m in active)if(m.Groups[1].Value==serial)found=true;if(!found)throw new IOException("TVに接続できません。TV側のADB許可とIPを確認してください。");return serial;
  }
  void InstallApk() {
   string apk=Path.Combine(root,"TCL-BonDriver-Setup.apk");if(!File.Exists(apk))throw new FileNotFoundException("配布ZIP内のAPKが見つかりません。");string serial=Device();Note("セットアップAPKをインストールしています…");string result=Adb("-s "+Q(serial)+" install -r "+Q(apk),90);if(!result.Contains("Success"))throw new IOException("APKのインストール結果を確認できません。");Adb("-s "+Q(serial)+" shell am start -n io.github.long0823.tclbondriver/.MainActivity",20);Note("TV画面でセットアップと再起動を行い、次に「接続設定とDLLを配置」を押してください。");
  }
  static bool Is64BitPe(string file) {using(var r=new BinaryReader(File.OpenRead(file))) {if(r.ReadUInt16()!=0x5a4d)throw new IOException("TVTest.exeの形式を確認できません。");r.BaseStream.Position=0x3c;int offset=r.ReadInt32();if(offset<0||offset>r.BaseStream.Length-6)throw new IOException("PEヘッダーが不正です。");r.BaseStream.Position=offset;if(r.ReadUInt32()!=0x4550)throw new IOException("PEヘッダーがありません。");ushort machine=r.ReadUInt16();if(machine!=0x8664&&machine!=0x14c)throw new IOException("対応するTVTestはx86またはx64です。");return machine==0x8664;}}
  string Host() {string value=ReadText(host);IPAddress address;if(!IPAddress.TryParse(value,out address)||address.AddressFamily!=AddressFamily.InterNetwork)throw new ArgumentException("TVのIPv4アドレスを入力してください。");return address.ToString();}
  string DetectHost(string serial) {
   string output=Adb("-s "+Q(serial)+" shell ip -o -4 addr show",20);var wired=new System.Collections.Generic.List<string>();var wifi=new System.Collections.Generic.List<string>();
   int mode=(int)Invoke(new Func<int>(()=>networkMode.SelectedIndex));
   foreach(Match m in Regex.Matches(output,@"(?m)^\s*\d+:\s+((?:eth\d+|en[A-Za-z0-9_.-]+)|(?:wlan\d+|wl[A-Za-z0-9_.-]+))(?:@\S+)?\s+inet\s+(\d+\.\d+\.\d+\.\d+)/\d+\b")) {
    IPAddress ip;string value=m.Groups[2].Value;var list=m.Groups[1].Value.StartsWith("wl")?wifi:wired;
    if(IPAddress.TryParse(value,out ip)&&!IPAddress.IsLoopback(ip)&&value!="0.0.0.0"&&!list.Contains(value))list.Add(value);
   }
   var candidates=new System.Collections.Generic.List<string>();if(mode!=2)candidates.AddRange(wired);if(mode!=1)candidates.AddRange(wifi);
   if(candidates.Count==0)throw new IOException("選択した接続方式のIPv4を取得できません。TVのLAN・Wi-Fi接続を確認してください。手動指定は自動取得をオフにします。");
   string result=null;
   if(candidates.Count==1)result=candidates[0];else foreach(string candidate in candidates) {try{using(var probe=Connect(candidate,40557)){}result=candidate;break;}catch(SocketException){}catch(IOException){}}
   if(result==null)throw new IOException("TVのIPは見つかりましたがPCから配信サービスへ接続できません。ネットワークとTVの配信状態を確認してください。");
   string detected=result;Invoke(new Action(()=>host.Text=detected));Note("TVのIPを取得しました（"+(wired.Contains(result)?"有線LAN":"Wi-Fi")+"）："+result);return result;
  }
  string Server(string serial) {return (bool)Invoke(new Func<bool>(()=>autoIp.Checked))?DetectHost(serial):Host();}
  static void PrivateFile(string file) {var acl=new FileSecurity();acl.SetAccessRuleProtection(true,false);var user=WindowsIdentity.GetCurrent().User;acl.SetOwner(user);acl.AddAccessRule(new FileSystemAccessRule(user,FileSystemRights.FullControl,AccessControlType.Allow));acl.AddAccessRule(new FileSystemAccessRule(new SecurityIdentifier(WellKnownSidType.LocalSystemSid,null),FileSystemRights.FullControl,AccessControlType.Allow));File.SetAccessControl(file,acl);}
  static void Backup(string file) {if(File.Exists(file)) {string backup=file+".before-setup-"+DateTime.Now.ToString("yyyyMMdd-HHmmss-fff");File.Copy(file,backup,false);if(file.EndsWith(".ini",StringComparison.OrdinalIgnoreCase))PrivateFile(backup);}}
  void Pair() {
   string folder=ReadText(destination);if(folder.Length==0||!Directory.Exists(folder))throw new IOException("TVTestのフォルダーを選択してください。");string tvtest=Path.Combine(folder,"TVTest.exe");if(!File.Exists(tvtest))throw new IOException("選択先にTVTest.exeがありません。");bool x64=Is64BitPe(tvtest);string source=Path.Combine(root,x64?"x64":"x86","BonDriver_TCLG06.dll");if(!File.Exists(source))throw new FileNotFoundException("配布ZIPに対応するDLLがありません。");
   string serial=Device(),server=Server(serial);Note("TVの接続設定を取得しています…");string token=Adb("-s "+Q(serial)+" shell su -c 'cat /data/local/tmp/tsd_token'",20).Trim();if(!Regex.IsMatch(token,@"\A[0-9a-fA-F]{64}\z"))throw new IOException("接続キーを取得できません。TVのセットアップと、ADBへのroot許可を確認してください。");string status=Control(server,token,"STATUS");if(!status.StartsWith("OK "))throw new IOException("TVとの認証に失敗しました。");
   Note("TV本体のスキャン結果から地デジの局一覧を取得しています…");
   string rows=Adb("-s "+Q(serial)+" shell su -c 'content query --uri content://android.media.tv/channel --projection display_number:original_network_id:transport_stream_id:service_id:type:service_type:browsable:display_name'",30);
   var channels=ChannelCatalog.Parse(rows);string catalog=ChannelCatalog.Ini(channels);
   Note("地デジ "+channels.Count+"サービスを取得しました（TVで非表示の局・BS/CS・データ専用サービスは対象外）。");
   int count=UseDual()?2:1;if(count==2&&!Regex.IsMatch(status,@"(?:^| )tuners=2(?: |$)"))throw new IOException("TV側の2チューナー対応サービスを確認できません。APKを更新してセットアップ・再起動してください。");
   for(int index=0;index<count;index++) {
    string name=index==0?"BonDriver_TCLG06":"BonDriver_TCLG06_1";
    string ini=Path.Combine(folder,name+".ini"),dll=Path.Combine(folder,name+".dll"),ch2=Path.Combine(folder,name+".ch2");bool changed=!File.Exists(ini)||!File.ReadAllText(ini).EndsWith(catalog,StringComparison.Ordinal);Backup(dll);Backup(ini);if(changed)Backup(ch2);File.Copy(source,dll,true);
    File.WriteAllText(ini,"[Server]\r\nHost="+server+"\r\nPort="+(index==0?40556:40558)+"\r\n\r\n[Control]\r\nMode=Native\r\nPort=40557\r\nTunerIndex="+index+"\r\nToken="+token+"\r\n"+catalog,Encoding.Unicode);PrivateFile(ini);if(changed&&File.Exists(ch2)){File.Delete(ch2);Note(name+"：古いTVTest局設定をバックアップしました。チャンネルスキャンをやり直してください。");}
   }
   Note((x64?"64":"32")+"bit版を"+count+"系統分配置しました。");
   Note(count==2?"TVTestを2つ起動し、BonDriver_TCLG06 と BonDriver_TCLG06_1 をそれぞれ選んでください。":"TVTestでBonDriver_TCLG06を選んでください。");Note("接続認証に成功しました。TVTestでチャンネルスキャンを行ってください。");
  }
  static TcpClient Connect(string host,int port) {var client=new TcpClient();var pending=client.BeginConnect(host,port,null,null);using(pending.AsyncWaitHandle) {if(!pending.AsyncWaitHandle.WaitOne(5000)) {client.Close();throw new IOException("TVへの接続が時間切れになりました。");}client.EndConnect(pending);}client.ReceiveTimeout=3000;client.SendTimeout=3000;return client;}
  static string Control(string host,string token,string command) {using(var client=Connect(host,40557)) {client.ReceiveTimeout=command.StartsWith("TUNE ")?45000:3000;var stream=client.GetStream();byte[] bytes=Encoding.ASCII.GetBytes("AUTH "+token+" "+command+"\n");stream.Write(bytes,0,bytes.Length);using(var reader=new StreamReader(stream,Encoding.ASCII,false,1024)) {string line=reader.ReadLine();if(line==null||line.Length>1024)throw new IOException("TVからの応答を確認できません。");return line;}}}
  static void CheckStream(string server,int port,out long total) {
   total=0;int bad=0,offset=0;byte[] buffer=new byte[65536];var watch=Stopwatch.StartNew();
   using(var client=Connect(server,port))using(var stream=client.GetStream())while(watch.ElapsedMilliseconds<5000) {
    int size=stream.Read(buffer,0,buffer.Length);if(size==0)break;
    for(int i=0;i<size;i++){if(offset==0&&buffer[i]!=0x47)bad++;offset=(offset+1)%188;}total+=size;
   }
   if(total==0||bad!=0)throw new IOException("TS同期を確認できませんでした。TV側の配信状態を確認してください。");
  }
  void TestStream() {
   string server=Server(Device());bool two=UseDual();
   if(two) {
    string ini=Path.Combine(ReadText(destination),"BonDriver_TCLG06_1.ini");if(!File.Exists(ini))throw new IOException("先に接続設定とDLLを配置してください。");
    Match m=Regex.Match(File.ReadAllText(ini),@"(?m)^Token=([0-9a-fA-F]{64})\s*$");if(!m.Success)throw new IOException("接続設定を再作成してください。");
    string status=Control(server,m.Groups[1].Value,"STATUS");if(!status.StartsWith("OK "))throw new IOException("TVとの認証に失敗しました。");
    if(!Regex.IsMatch(status,@"(?:^| )tuner1_running=1(?: |$)")) {
     var first=ChannelCatalog.FirstFromIni(File.ReadAllText(ini));Note("2系統目をTVの局一覧の先頭へ初期選局しています…");string tuned=Control(server,m.Groups[1].Value,"TUNE 1 "+first.Command);
     if(!tuned.StartsWith("OK "))throw new IOException("2系統目を選局できません。TV側のチャンネル設定を確認してください。");
    }
   }
   for(int index=0;index<(two?2:1);index++) {Note((index+1)+"系統目を5秒間確認しています…");long total;CheckStream(server,index==0?40556:40558,out total);Note((index+1)+"系統目：受信成功 "+total.ToString("N0")+" bytes、TS同期エラー 0。");}
  }
 }
}
