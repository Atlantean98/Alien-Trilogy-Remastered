using System;
using System.IO;
using System.IO.Pipes;
using System.Net;
using System.Net.Sockets;
using System.Net.Security;
using System.Security.Authentication;
using System.Security.Cryptography;
using System.Security.Cryptography.X509Certificates;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using System.Diagnostics;
using System.Collections.Generic;
// Windows SslStream/SChannel supplies TLS; game traffic never uses plaintext TCP.
class CoopSecureTransport {
 const uint Magic=0x504f4341;const int Maximum=8*1024*1024;
 static readonly string tracePath=Environment.GetEnvironmentVariable("TRILOGY_COOP_TRACE")=="1"?Path.Combine(AppDomain.CurrentDomain.BaseDirectory,"coop_transport_debug_"+Process.GetCurrentProcess().Id+".log"):"";static readonly object traceLock=new object();
 static void Trace(string text){if(tracePath.Length==0)return;lock(traceLock){try{File.AppendAllText(tracePath,Environment.TickCount+" "+text+"\n");}catch{}}}
 static Stream pipe;static readonly object pipeWrite=new object();
 class Frame {public uint Type;public byte[] Bytes;}
 class Peer {public TcpClient Client;public SslStream Stream;}
 static Task Background(Action action){return Task.Factory.StartNew(action,CancellationToken.None,TaskCreationOptions.LongRunning,TaskScheduler.Default);}
 static byte[] ReadExact(Stream stream,int count){byte[] bytes=new byte[count];for(int at=0;at<count;){int n=stream.Read(bytes,at,count-at);if(n==0)throw new EndOfStreamException();at+=n;}return bytes;}
 static Frame ReadFrame(Stream stream,bool remote,bool host){Trace("read start "+stream.GetType().Name);byte[] header=ReadExact(stream,12);uint magic=BitConverter.ToUInt32(header,0),type=BitConverter.ToUInt32(header,4),length=BitConverter.ToUInt32(header,8);if(magic!=Magic||length>Maximum)throw new IOException("Invalid co-op frame");if(remote){if(type<1||type>6)throw new IOException("Invalid co-op message type");uint limit=type==3&&!host?(uint)Maximum:type==2&&host?64u:type==1?128u:type==4?32u:0u;if(length>limit)throw new IOException("Co-op message exceeds role limit");}Trace("read header "+stream.GetType().Name+" type="+type+" size="+length);var body=ReadExact(stream,(int)length);Trace("read done "+stream.GetType().Name+" type="+type);return new Frame{Type=type,Bytes=body};}
 static void WriteFrame(Stream stream,uint type,byte[] bytes){Trace("write start "+stream.GetType().Name+" type="+type+" size="+bytes.Length);byte[] packet=new byte[12+bytes.Length];Buffer.BlockCopy(BitConverter.GetBytes(Magic),0,packet,0,4);Buffer.BlockCopy(BitConverter.GetBytes(type),0,packet,4,4);Buffer.BlockCopy(BitConverter.GetBytes(bytes.Length),0,packet,8,4);Buffer.BlockCopy(bytes,0,packet,12,bytes.Length);stream.Write(packet,0,packet.Length);stream.Flush();Trace("write done "+stream.GetType().Name+" type="+type);}
 static void Notify(uint type,string text){lock(pipeWrite){WriteFrame(pipe,type,Encoding.UTF8.GetBytes(text));}}
 static bool Equal(byte[] a,byte[] b){int difference=a.Length^b.Length;for(int i=0;i<Math.Min(a.Length,b.Length);++i)difference|=a[i]^b[i];return difference==0;}
 static byte[] Hash(byte[] data){using(var sha=SHA256.Create())return sha.ComputeHash(data);}
 static Peer Host(int port){
  var rsa=new RSACng(2048);var request=new CertificateRequest("CN=AlienTrilogyCoop",rsa,HashAlgorithmName.SHA256,RSASignaturePadding.Pkcs1);var generated=request.CreateSelfSigned(DateTimeOffset.UtcNow.AddMinutes(-1),DateTimeOffset.UtcNow.AddDays(1));byte[] pfx=generated.Export(X509ContentType.Pfx);var certificate=new X509Certificate2(pfx,"",X509KeyStorageFlags.UserKeySet|X509KeyStorageFlags.Exportable);Array.Clear(pfx,0,pfx.Length);generated.Dispose();byte[] token=new byte[32];using(var rng=RandomNumberGenerator.Create())rng.GetBytes(token);byte[] invite=new byte[64];Buffer.BlockCopy(Hash(certificate.RawData),0,invite,0,32);Buffer.BlockCopy(token,0,invite,32,32);
  var listener=new TcpListener(IPAddress.Any,port);listener.Server.ExclusiveAddressUse=true;try{listener.Start(4);}catch(SocketException error){listener.Stop();if(error.SocketErrorCode==SocketError.AddressAlreadyInUse)throw new IOException("TCP port "+port+" is already in use. Choose another port in the co-op menu; your friend must use the same port.");throw;}Notify(101,Convert.ToBase64String(invite));Notify(100,"Waiting for a friend on TCP port "+port);var completion=new TaskCompletionSource<Peer>();var slots=new SemaphoreSlim(2);var attempts=new Dictionary<string,Tuple<DateTime,int>>();object gate=new object();int winner=0;
  Background(delegate {
   try{while(!completion.Task.IsCompleted){var socket=listener.AcceptTcpClient();string ip=((IPEndPoint)socket.Client.RemoteEndPoint).Address.ToString();bool allowed=false;lock(gate){Tuple<DateTime,int> value;if(!attempts.TryGetValue(ip,out value)||DateTime.UtcNow-value.Item1>TimeSpan.FromMinutes(1)){if(attempts.Count>=128)attempts.Clear();value=Tuple.Create(DateTime.UtcNow,0);}allowed=value.Item2<4;attempts[ip]=Tuple.Create(value.Item1,value.Item2+1);}if(!allowed||!slots.Wait(0)){socket.Close();continue;}
    Background(delegate {
     SslStream ssl=null;Timer timeout=null;bool accepted=false;
     try{socket.NoDelay=true;socket.ReceiveTimeout=5000;socket.SendTimeout=5000;timeout=new Timer(delegate{try{socket.Close();}catch{}},null,5000,Timeout.Infinite);ssl=new SslStream(socket.GetStream(),false);ssl.AuthenticateAsServer(certificate,false,SslProtocols.Tls12,false);if(!Equal(token,ReadExact(ssl,32)))throw new AuthenticationException("Invalid invitation");if(Interlocked.CompareExchange(ref winner,1,0)!=0)throw new IOException("Session full");ssl.WriteByte(1);ssl.Flush();timeout.Dispose();timeout=null;ssl.ReadTimeout=15000;ssl.WriteTimeout=5000;accepted=true;completion.TrySetResult(new Peer{Client=socket,Stream=ssl});listener.Stop();}
     catch(Exception rejected){Notify(100,"Rejected join: "+rejected.GetBaseException().Message);}
     finally{if(timeout!=null)timeout.Dispose();if(!accepted){if(ssl!=null)ssl.Dispose();socket.Close();}slots.Release();}
    });
   }}catch(Exception error){if(!completion.Task.IsCompleted)completion.TrySetException(error);}
  });
  return completion.Task.Result;
 }
 static Peer Join(string ip,int port,string code){byte[] invite=Convert.FromBase64String(code);if(invite.Length!=64)throw new AuthenticationException("Invitation must be the complete host code");byte[] expected=new byte[32],token=new byte[32];Buffer.BlockCopy(invite,0,expected,0,32);Buffer.BlockCopy(invite,32,token,0,32);IPAddress address;if(!IPAddress.TryParse(ip,out address)||address.AddressFamily!=AddressFamily.InterNetwork)throw new IOException("Enter a valid host IPv4 address");var socket=new TcpClient(AddressFamily.InterNetwork);Notify(100,"Connecting securely to "+ip+":"+port);var pending=socket.ConnectAsync(address,port);if(!pending.Wait(10000)){socket.Close();throw new IOException("Host unreachable: check public IP, TCP port forwarding and CGNAT");}socket.NoDelay=true;socket.ReceiveTimeout=5000;socket.SendTimeout=5000;var ssl=new SslStream(socket.GetStream(),false,delegate(object sender,X509Certificate cert,X509Chain chain,SslPolicyErrors errors){return cert!=null&&Equal(expected,Hash(cert.GetRawCertData()));});using(var timeout=new Timer(delegate{try{socket.Close();}catch{}},null,5000,Timeout.Infinite)){ssl.AuthenticateAsClient("AlienTrilogyCoop",null,SslProtocols.Tls12,false);ssl.Write(token,0,token.Length);ssl.Flush();if(ssl.ReadByte()!=1)throw new AuthenticationException("Host did not accept invitation");}ssl.ReadTimeout=15000;ssl.WriteTimeout=5000;return new Peer{Client=socket,Stream=ssl};}
 static void Relay(Peer peer,bool host){Notify(102,"Authenticated encrypted connection");var inbound=Background(delegate{for(;;){var frame=ReadFrame(peer.Stream,true,host);lock(pipeWrite)WriteFrame(pipe,frame.Type,frame.Bytes);}});var outbound=Background(delegate{for(;;){var frame=ReadFrame(pipe,false,host);if(frame.Type<1||frame.Type>6)throw new IOException("Invalid local co-op frame");WriteFrame(peer.Stream,frame.Type,frame.Bytes);}});int ended=Task.WaitAny(inbound,outbound);try{(ended==0?inbound:outbound).GetAwaiter().GetResult();}finally{peer.Client.Close();peer.Stream.Dispose();}}
 static int Main(string[] args){ThreadPool.SetMinThreads(16,16);try{if(args.Length!=2)return 2;int pid=int.Parse(args[1]);Background(delegate{for(;;){Thread.Sleep(500);try{if(Process.GetProcessById(pid).HasExited)Environment.Exit(0);}catch{Environment.Exit(0);}}});using(var local=new NamedPipeClientStream(".",args[0],PipeDirection.InOut,PipeOptions.Asynchronous)){local.Connect(5000);pipe=local;var config=ReadFrame(pipe,false,false);if(config.Type!=200||config.Bytes.Length>512)throw new IOException("Invalid transport configuration");string[] parts=Encoding.UTF8.GetString(config.Bytes).Split('|');int port;bool host=parts[0]=="H";if(parts.Length!=4||!int.TryParse(parts[2],out port)||port<1||port>65535)throw new IOException("Invalid host port");try{Relay(host?Host(port):Join(parts[1],port,parts[3]),host);Notify(103,"The other player disconnected");}catch(Exception error){Notify(103,"Secure connection failed: "+error.GetBaseException().Message);Thread.Sleep(200);}}return 0;}catch{return 1;}}
}
