using System;
using System.IO;
namespace TclBonDriverSetup {
 class CatvCatalogTest {
  static int checks;
  static void Check(bool value){checks++;if(!value)throw new Exception("check "+checks);}
  static void Invalid(string text){bool rejected=false;try{ChannelCatalog.Parse("",text);}catch(IOException){rejected=true;}Check(rejected);}
  static void Main(){
   string cable="4 16528 211 345000 1 1 BS11イレブン\n";
   var channels=ChannelCatalog.Parse("No result found",cable);
   Check(channels.Count==1);Check(channels[0].System=="ISDB-C");Check(channels[0].Name=="BS11イレブン");
   string ini=ChannelCatalog.Ini(channels);Check(ini.Contains("System=ISDB-C"));Check(ChannelCatalog.FirstFromIni(ini).System=="ISDB-C");
   string satellite="Row: 0 display_number=211, original_network_id=4, transport_stream_id=16528, service_id=211, type=TYPE_ISDB_S, service_type=SERVICE_TYPE_AUDIO_VIDEO, browsable=1, display_name=BS11衛星\n";
   Check(ChannelCatalog.Parse(satellite,cable).Count==1);Check(ChannelCatalog.Parse(satellite,cable)[0].System=="ISDB-C");
   Invalid(cable+cable);Invalid("4 16528 211 345000 3 1 bad\n");Invalid("4 16528 211 345000 1 16 bad\n");Invalid("4294967300 16528 211 345000 1 1 bad\n");
   Console.WriteLine("Windows CATV catalog checks: "+checks);
  }
 }
}
