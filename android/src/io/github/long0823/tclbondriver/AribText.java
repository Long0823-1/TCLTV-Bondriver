package io.github.long0823.tclbondriver;
import java.nio.charset.*;
public final class AribText {
 private static final Charset JIS=Charset.forName("EUC-JP");
 private static final String[] SIGNS={"[HV]","[SD]","[P]","[W]","[MV]","[手]","[字]","[双]","[デ]","[S]","[二]","[多]","[解]","[SS]","[B]","[N]","■","●","[天]","[交]","[映]","[無]","[料]","[年齢制限]","[前]","[後]","[再]","[新]","[初]","[終]","[生]","[販]","[声]","[吹]","[PPV]"};
 public static String decode(byte[] data,int begin,int length){StringBuilder out=new StringBuilder();int end=Math.min(data.length,begin+length);int[] g={0x42,0x4a,0x30,0x31};int gl=0,gr=2,ss=-1;
  for(int i=begin;i<end;i++){int c=data[i]&255;if(c==0x1b){if(++i>=end)break;int e=data[i]&255;if(e==0x6e){gl=2;continue;}if(e==0x6f){gl=3;continue;}if(e>=0x7c&&e<=0x7e){gr=0x7f-e;continue;}boolean two=e==0x24;if(two){if(++i>=end)break;e=data[i]&255;}int slot=0;if(e>=0x28&&e<=0x2b){slot=e-0x28;if(++i>=end)break;e=data[i]&255;}if(e==0x20){if(++i>=end)break;e=0x100|(data[i]&255);}g[slot]=e;continue;}
   if(c==0x0e){gl=1;continue;}if(c==0x0f){gl=0;continue;}if(c==0x19){ss=2;continue;}if(c==0x1d){ss=3;continue;}if(c==0x0d){out.append('\n');continue;}if(c==0x20||c==0xa0){out.append(' ');continue;}if(c==0x16){i++;continue;}if(c==0x1c){i+=2;continue;}if(c==0x9b){while(i+1<end){int v=data[++i]&255;if(v>=0x40&&v<=0x7e)break;}continue;}if(c==0x90||c==0x91||c==0x92||c==0x93||c==0x94||c==0x97||c==0x98||c==0x9d){if(i+1<end){int v=data[++i]&255;if(v==0x20&&i+1<end)i++;}continue;}if(c<0x21||c==0x7f||(c>=0x80&&c<0xa1)||c==0xff)continue;
   int set=g[ss>=0?ss:(c>=0xa1?gr:gl)];ss=-1;int a=c&0x7f;boolean two=set==0x42||set==0x39||set==0x3a||set==0x3b;
   if(two){if(i+1>=end)break;int b=data[++i]&0x7f;if(a==0x7a&&b>=0x50&&b<0x50+SIGNS.length){out.append(SIGNS[b-0x50]);}else if(a>=0x75){out.append('□');}else{byte[] pair={(byte)(a|128),(byte)(b|128)};out.append(new String(pair,JIS));}}
   else if(set==0x30||set==0x37||set==0x31||set==0x38){int row=(set==0x30||set==0x37)?0xa4:0xa5;if(a>=0x77){String[] tail={"ゝ","ゞ","ー","。","「","」","、","・"};out.append(tail[Math.min(tail.length-1,a-0x77)]);}else out.append(new String(new byte[]{(byte)row,(byte)(a|128)},JIS));}
   else if(set==0x49){out.append(new String(new byte[]{(byte)0x8e,(byte)(a|128)},JIS));}else if(set==0x4a||set==0x36){out.append((char)a);}else out.append('□');
  }return out.toString().replace('\ufffd','□').trim();
 }
}