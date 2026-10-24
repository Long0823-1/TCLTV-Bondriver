using System;
using System.IO;
using System.Text;
using System.Text.RegularExpressions;
using System.Collections.Generic;
namespace TclBonDriverSetup {
 internal sealed class TvChannel {
  internal string Number,Name; internal int Onid,Tsid,Sid;
  internal string Command {get{return Onid+" "+Tsid+" "+Sid;}}
 }
 internal static class ChannelCatalog {
  internal const int Maximum=512;
  internal static List<TvChannel> Parse(string output) {
   if(output==null||output.Length>2097152)throw new IOException("チャンネル情報が大きすぎます。");
   var channels=new List<TvChannel>();var seen=new HashSet<string>(StringComparer.Ordinal);
   string pattern=@"(?m)^Row: \d+ display_number=([^,\r\n]*), original_network_id=(\d+), transport_stream_id=(\d+), service_id=(\d+), type=([^,\r\n]+), service_type=([^,\r\n]+), browsable=([01]), display_name=([^\r\n]*)\r?$";
   foreach(Match m in Regex.Matches(output,pattern)) {
    if(m.Groups[5].Value!="TYPE_ISDB_T"||m.Groups[7].Value!="1")continue;
    string kind=m.Groups[6].Value;if(kind!="EN_TCL_DTV"&&kind!="SERVICE_TYPE_AUDIO_VIDEO")continue;
    int onid,tsid,sid;if(!Id(m.Groups[2].Value,out onid)||!Id(m.Groups[3].Value,out tsid)||!Id(m.Groups[4].Value,out sid))throw new IOException("TVのチャンネルIDが不正です。TV側で再スキャンしてください。");
    string number=m.Groups[1].Value.Trim(),name=m.Groups[8].Value.Trim();
    if(number=="NULL")number="";
    if(name.Length==0||name=="NULL"||name.Length>200||number.Length>32||name.IndexOf('\0')>=0||number.IndexOf('\0')>=0)throw new IOException("TVのチャンネル名を確認できません。");
    string key=onid+":"+tsid+":"+sid;if(!seen.Add(key))continue;
    if(channels.Count==Maximum)throw new IOException("地デジの登録数が上限512を超えています。");
    channels.Add(new TvChannel{Number=number,Name=name,Onid=onid,Tsid=tsid,Sid=sid});
   }
   channels.Sort((a,b)=>{int x,y;if(!Int32.TryParse(a.Number,out x))x=Int32.MaxValue;if(!Int32.TryParse(b.Number,out y))y=Int32.MaxValue;int c=x.CompareTo(y);if(c!=0)return c;c=StringComparer.Ordinal.Compare(a.Name,b.Name);if(c!=0)return c;return StringComparer.Ordinal.Compare(a.Command,b.Command);});
   if(channels.Count==0)throw new IOException("TVに表示対象の地デジチャンネルがありません。TV本体でチャンネルスキャンを完了してから再実行してください。既存設定は変更していません。");
   return channels;
  }
  static bool Id(string text,out int value) {return Int32.TryParse(text,out value)&&value>0&&value<=65535;}
  internal static string Ini(List<TvChannel> channels) {
   if(channels==null||channels.Count==0||channels.Count>Maximum)throw new IOException("チャンネル一覧がありません。");
   var text=new StringBuilder("\r\n[Channels]\r\nCount="+channels.Count+"\r\n");
   for(int i=0;i<channels.Count;i++) {var c=channels[i];string label=(c.Number.Length==0?"":c.Number.PadLeft(3,'0')+" ")+c.Name;
    text.Append("\r\n[Channel").Append(i).Append("]\r\nName=").Append(label).Append("\r\nDisplayNumber=").Append(c.Number).Append("\r\nONID=").Append(c.Onid).Append("\r\nTSID=").Append(c.Tsid).Append("\r\nServiceID=").Append(c.Sid).Append("\r\n");
   }
   return text.ToString();
  }
  internal static TvChannel FirstFromIni(string text) {
   Match section=Regex.Match(text,@"(?ms)^\[Channel0\]\r?\n(.*?)(?=^\[|\z)");if(!section.Success)throw new IOException("接続設定を更新してチャンネル一覧を取得してください。");
   var c=new TvChannel();string[] keys={"ONID","TSID","ServiceID"};int[] ids=new int[3];
   for(int i=0;i<keys.Length;i++){Match m=Regex.Match(section.Groups[1].Value,@"(?m)^"+keys[i]+@"=(\d+)\r?$");if(!m.Success||!Id(m.Groups[1].Value,out ids[i]))throw new IOException("チャンネル設定が不正です。接続設定を再作成してください。");}
   c.Onid=ids[0];c.Tsid=ids[1];c.Sid=ids[2];return c;
  }
 }
}