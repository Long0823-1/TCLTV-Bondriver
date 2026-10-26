package io.github.long0823.tclbondriver;
import java.io.*;import java.net.*;import java.nio.charset.StandardCharsets;
/** Native credentials stay in authenticated loopback control requests. */
final class NativeGateway {
 private final String upstreamCredential;
 NativeGateway(byte[] ini)throws IOException{String value=null;for(String line:new String(ini,StandardCharsets.UTF_8).split("\\r?\\n"))if(line.startsWith("Token="))value=line.substring(6).trim();if(value==null||!value.matches("[a-fA-F0-9]{64}"))throw new IOException("Native configuration missing");upstreamCredential=value;}
 String command(String command)throws IOException{try(Socket s=new Socket()){s.connect(new InetSocketAddress("127.0.0.1",40557),4000);s.setSoTimeout(command.startsWith("TUNE ")||command.startsWith("EPG_TUNE ")?45000:4000);s.getOutputStream().write(("AUTH "+upstreamCredential+" "+command+"\n").getBytes(StandardCharsets.US_ASCII));s.getOutputStream().flush();return IptvServer.line(s.getInputStream());}}
 String tune(int tuner,IptvCatalog.Channel ch)throws IOException{return command("TUNE "+tuner+" "+ch.onid+" "+ch.tsid+" "+ch.sid);}
 String status()throws IOException{return command("STATUS");}
 Socket stream(int tuner)throws IOException{Socket s=new Socket();try{s.connect(new InetSocketAddress("127.0.0.1",tuner==0?40556:40558),4000);s.setSoTimeout(15000);s.getOutputStream().write(("AUTH "+upstreamCredential+"\n").getBytes(StandardCharsets.US_ASCII));s.getOutputStream().flush();return s;}catch(IOException e){s.close();throw e;}}
}
