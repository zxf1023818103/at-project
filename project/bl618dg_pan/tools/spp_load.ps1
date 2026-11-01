param([ValidateSet('tx','rx')][string]$Mode='tx', [ValidateRange(1,285)][int]$Seconds=30,
 [string]$Address='B4E8423CB52B', [string]$Log='spp-result.json', [switch]$Reset)
$ErrorActionPreference='Stop'
Add-Type -TypeDefinition @'
using System;
using System.Net;
using System.Net.Sockets;
using System.IO.Ports;
using System.Diagnostics;
using System.Threading;
public sealed class SppEndpoint : EndPoint {
 ulong address;
 public SppEndpoint(ulong a) { address=a; }
 public override AddressFamily AddressFamily {get{return (AddressFamily)32;}}
 public override SocketAddress Serialize() {
  var s=new SocketAddress((AddressFamily)32,30);
  var a=BitConverter.GetBytes(address); for(int i=0;i<8;i++)s[2+i]=a[i];
  var g=new Guid("00001101-0000-1000-8000-00805f9b34fb").ToByteArray();
  for(int i=0;i<16;i++)s[10+i]=g[i];
  return s;
 }
 public override EndPoint Create(SocketAddress s) {return this;}
}
public class SppMeasure {
 public static string Run(ulong address,string mode,int seconds,bool reset) {
  var uart=new SerialPort("COM15",2000000); var ctl=new SerialPort("COM23",115200);
  Socket sock=null; long count=0,bad=0; double elapsed=0; string log="";
  try {
   ctl.DtrEnable=true; ctl.RtsEnable=false; ctl.Open(); uart.Open();
   if(reset) {ctl.RtsEnable=true;Thread.Sleep(1500);ctl.RtsEnable=false;Thread.Sleep(5000);}
   log+=uart.ReadExisting();
   sock=new Socket((AddressFamily)32,SocketType.Stream,(ProtocolType)3);
   sock.ReceiveTimeout=1000; sock.SendTimeout=3000;
   var ar=sock.BeginConnect(new SppEndpoint(address),null,null);
   if(!ar.AsyncWaitHandle.WaitOne(20000))throw new Exception("SPP connect timeout");
   sock.EndConnect(ar); Console.WriteLine("SPP_CONNECTED"); Thread.Sleep(500);
   int boardSeconds=mode=="rx"?seconds+15:seconds;
   uart.Write("\r\nspp_load "+mode+" "+boardSeconds+"\r\n");
   var wait=Stopwatch.StartNew(); string ack="";
   while(!(ack.Contains("[spp] ST") && ack.Contains("mode="+mode+" seconds="))&&wait.Elapsed.TotalSeconds<5){ack+=uart.ReadExisting();Thread.Sleep(5);}
   log+=ack; if(!(ack.Contains("[spp] ST") && ack.Contains("mode="+mode+" seconds=")))throw new Exception("No board START: "+log);
   byte[] buffer=new byte[16384]; var clock=Stopwatch.StartNew();
   if(mode=="tx") {
    double last=0;
    while(clock.Elapsed.TotalSeconds<seconds+6) {
     if(sock.Poll(10000,SelectMode.SelectRead)) {
      int n=sock.Receive(buffer); if(n==0)break;
      for(int i=0;i<n;i++)if(buffer[i]!=(byte)((count+i)%251))bad++;
      count+=n;last=clock.Elapsed.TotalSeconds;
     }
     log+=uart.ReadExisting();
     if(log.Contains("[spp] RESULT")&&clock.Elapsed.TotalSeconds>last+1)break;
    }
    elapsed=last;
   } else {
    while(clock.Elapsed.TotalSeconds<seconds) {
     for(int i=0;i<buffer.Length;i++)buffer[i]=(byte)((count+i)%251);
     count+=sock.Send(buffer);log+=uart.ReadExisting();
    }
    elapsed=clock.Elapsed.TotalSeconds;
    Thread.Sleep(2000); uart.Write("\r\nspp_load stop\r\n");
   }
   wait.Restart();while(wait.Elapsed.TotalSeconds<3){log+=uart.ReadExisting();Thread.Sleep(20);}
   Console.WriteLine(log);
   return "{\"mode\":\""+mode+"\",\"bytes\":"+count+",\"elapsed_s\":"+elapsed.ToString("F6",System.Globalization.CultureInfo.InvariantCulture)+",\"mbps\":"+(count*8.0/elapsed/1000000).ToString("F6",System.Globalization.CultureInfo.InvariantCulture)+",\"bad_bytes\":"+bad+",\"uart_b64\":\""+Convert.ToBase64String(System.Text.Encoding.UTF8.GetBytes(log))+"\"}";
  } catch(Exception) {Console.WriteLine(log);throw;}
  finally {if(sock!=null)sock.Dispose();uart.Dispose();ctl.Dispose();}
 }
}
'@
$result=[SppMeasure]::Run([Convert]::ToUInt64(($Address -replace '[:-]',''),16),$Mode,$Seconds,$Reset.IsPresent)
$record=$result | ConvertFrom-Json
$uart=[Text.Encoding]::UTF8.GetString([Convert]::FromBase64String($record.uart_b64))
$match=[regex]::Match($uart,'RESULT mode=(tx|rx) bytes=(\d+) elapsed_ms=(\d+) bad=(\d+) blocked=(\d+) errors=(\d+) connected=(\d+)(?: rx_span_ms=(\d+))?')
$valid=$match.Success
if($valid){
 $record | Add-Member board_bytes ([long]$match.Groups[2].Value)
 $record | Add-Member board_bad_bytes ([long]$match.Groups[4].Value)
 $record | Add-Member board_errors ([long]$match.Groups[6].Value)
 $valid=($record.board_bytes -eq $record.bytes -and $record.bytes -gt 0 -and $record.bad_bytes -eq 0 -and $record.board_bad_bytes -eq 0 -and $record.board_errors -eq 0 -and $match.Groups[7].Value -eq '1')
 if($Mode -eq 'rx' -and $match.Groups[8].Success){
  $record | Add-Member receiver_span_ms ([long]$match.Groups[8].Value)
  $record | Add-Member receiver_mbps ($record.board_bytes*8.0/$record.receiver_span_ms/1000)
 }else{
  $record | Add-Member receiver_mbps $record.mbps
 }
}
$record | Add-Member passed $valid
$record | ConvertTo-Json -Compress | Set-Content -Encoding UTF8 $Log
$record.PSObject.Properties.Remove('uart_b64')
$record | ConvertTo-Json -Compress
if(!$valid){throw 'SPP result missing, counters differ, payload errors, or disconnection; see saved log'}
