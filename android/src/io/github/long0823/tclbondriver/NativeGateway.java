package io.github.long0823.tclbondriver;
import java.io.*;import java.net.*;import java.nio.charset.StandardCharsets;
/** Existing native credential stays private to loopback connections, never HTTP URLs. */
final class NativeGateway {
 private final String upstreamCredential;
 NativeGateway(byte[] ini)throws IOException{String value=null;for(String line:new String(ini,StandardCharsets.UTF_8).split("\\r?\\n"))if(line.startsWith("Token="))value=line.substring(6).trim();if(value==null||!value.matches("[a-fA-F0-9]{64}"))throw new IOException("Native configuration missing");upstreamCredential=value;}
 String tune(int tuner,IptvCatalog.Channel ch)throws IOException{try(Socket s=new Socket()){s.connect(new InetSocketAddress("127.0.0.1",40557),4000);s.setSoTimeout(45000);s.getOutputStream().write(("AUTH "+upstreamCredential+" TUNE "+tuner+" "+ch.onid+" "+ch.tsid+" "+ch.sid+"\n").getBytes(StandardCharsets.US_ASCII));s.getOutputStream().flush();return IptvServer.line(s.getInputStream());}}
 Socket stream(int tuner)throws IOException{Socket s=new Socket();try{s.connect(new InetSocketAddress("127.0.0.1",tuner==0?40556:40558),4000);s.setSoTimeout(15000);s.getOutputStream().write(("AUTH "+upstreamCredential+"\n").getBytes(StandardCharsets.US_ASCII));s.getOutputStream().flush();return s;}catch(IOException e){s.close();throw e;}}
}