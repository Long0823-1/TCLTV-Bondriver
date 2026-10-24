using System;
using System.IO;
using TclBonDriverSetup;
class ChannelCatalogTests {
 static int checks;
 static void Check(bool ok,string name){checks++;if(!ok)throw new Exception(name);}
 static string Row(int id,string name,string number,string type="TYPE_ISDB_T",string service="EN_TCL_DTV",string visible="1",int onid=40001,int tsid=41001) {return "Row: 0 display_number="+number+", original_network_id="+onid+", transport_stream_id="+tsid+", service_id="+id+", type="+type+", service_type="+service+", browsable="+visible+", display_name="+name+"\r\n";}
 static void Fails(string rows,string name){bool failed=false;try{ChannelCatalog.Parse(rows);}catch(IOException){failed=true;}Check(failed,name);}
 static int Main(string[] args){try{
  string rows=Row(1,"大阪のテレビ","81")+Row(2,"札幌のテレビ, HD=1","11",onid:40002,tsid:41002)+Row(1,"duplicate","82")+Row(3,"衛星","101",type:"TYPE_ISDB_S")+Row(4,"データ","268",service:"EN_TCL_DATA")+Row(5,"非表示","21",visible:"0")+Row(6,"Preview","0",type:"TYPE_PREVIEW");
  var list=ChannelCatalog.Parse(rows);Check(list.Count==2,"deduplicate and filter");Check(list[0].Name=="札幌のテレビ, HD=1","numeric order and name punctuation");Check(list[0].Onid==40002&&list[0].Tsid==41002,"distinct ONID and TSID");
  var first=ChannelCatalog.FirstFromIni(ChannelCatalog.Ini(list));Check(first.Command=="40002 41002 2","INI tuple roundtrip");Check(ChannelCatalog.Ini(list).Contains("Name=011 札幌のテレビ, HD=1"),"Japanese names");
  Check(ChannelCatalog.Parse(Row(1,"標準TV","1",service:"SERVICE_TYPE_AUDIO_VIDEO")).Count==1,"standard service type");
  Fails("No result found.","empty query");Fails(Row(0,"invalid","1"),"zero service");Fails(Row(70000,"invalid","1"),"out of range service");Fails(Row(1,"NULL","1"),"missing name");Fails(Row(1,new string('x',201),"1"),"long name");
  var many=new System.Text.StringBuilder();for(int i=1;i<=513;i++)many.Append(Row(i,"test",i.ToString()));Fails(many.ToString(),"bounded service count");
  if(args.Length>0){var actual=ChannelCatalog.Parse(File.ReadAllText(args[0]));Check(actual.Count>13,"real TV exceeds old presets");File.WriteAllText(args[1],ChannelCatalog.Ini(actual),System.Text.Encoding.Unicode);Console.WriteLine("real_tv_services="+actual.Count);}
  if(args.Length>2)File.WriteAllText(args[2],ChannelCatalog.Ini(list),System.Text.Encoding.Unicode);
  Console.WriteLine("Channel catalog checks passed: "+checks);return 0;
 }catch(Exception error){Console.Error.WriteLine(error);return 1;}}
}