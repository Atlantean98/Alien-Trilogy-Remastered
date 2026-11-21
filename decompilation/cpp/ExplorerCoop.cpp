#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <objbase.h>
#include <sddl.h>
#include <filesystem>
#include "ExplorerCoop.h"
#include <algorithm>
#include <array>
namespace trilogy::coop {
namespace {constexpr unsigned Magic=0x504f4341;}
struct Connection::Impl {HANDLE pipe=INVALID_HANDLE_VALUE,process=nullptr;DWORD helperPid=0;bool pipeConnected=false,authenticated=false,host=false;unsigned port=Port;std::string status="Offline",invite;std::vector<unsigned char> input,output;size_t sent=0;std::deque<Message> messages;ULONGLONG lastRead=0,lastPing=0,started=0;
 void shutdown(){if(pipe!=INVALID_HANDLE_VALUE){CloseHandle(pipe);}pipe=INVALID_HANDLE_VALUE;if(process){TerminateProcess(process,0);WaitForSingleObject(process,2000);CloseHandle(process);}process=nullptr;pipeConnected=authenticated=false;input.clear();output.clear();sent=0;messages.clear();invite.clear();}
 void fail(const std::string& text){shutdown();status=text;}
 bool start(const std::string& config){GUID id{};if(FAILED(CoCreateGuid(&id))){status="Cannot create private transport channel";return false;}wchar_t guid[64];StringFromGUID2(id,guid,64);std::wstring name=L"AlienTrilogyCoop-"+std::wstring(guid),path=LR"(\\.\pipe\)"+name;PSECURITY_DESCRIPTOR descriptor=nullptr;if(!ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:P(A;;GA;;;OW)",SDDL_REVISION_1,&descriptor,nullptr)){status="Cannot protect local transport channel";return false;}SECURITY_ATTRIBUTES security{sizeof(security),descriptor,FALSE};pipe=CreateNamedPipeW(path.c_str(),PIPE_ACCESS_DUPLEX|FILE_FLAG_FIRST_PIPE_INSTANCE,PIPE_TYPE_BYTE|PIPE_READMODE_BYTE|PIPE_NOWAIT|PIPE_REJECT_REMOTE_CLIENTS,1,65536,65536,0,&security);LocalFree(descriptor);if(pipe==INVALID_HANDLE_VALUE){status="Cannot create private transport channel";return false;}ConnectNamedPipe(pipe,nullptr);wchar_t executable[32768];GetModuleFileNameW(nullptr,executable,32768);auto helper=std::filesystem::path(executable).parent_path()/L"TrilogyCoopTransport.exe";if(!std::filesystem::exists(helper)){fail("Secure co-op helper is missing; rebuild the main application");return false;}std::wstring command=L"\""+helper.wstring()+L"\" \""+name+L"\" "+std::to_wstring(GetCurrentProcessId());STARTUPINFOW startup{};startup.cb=sizeof(startup);startup.dwFlags=STARTF_USESHOWWINDOW;startup.wShowWindow=SW_HIDE;PROCESS_INFORMATION info{};if(!CreateProcessW(helper.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&info)){fail("Cannot start secure co-op transport");return false;}CloseHandle(info.hThread);process=info.hProcess;helperPid=info.dwProcessId;for(unsigned value:{Magic,200u,unsigned(config.size())}){auto ptr=reinterpret_cast<unsigned char*>(&value);output.insert(output.end(),ptr,ptr+4);}output.insert(output.end(),config.begin(),config.end());started=lastRead=GetTickCount64();return true;}
};
Connection::Connection():p(std::make_unique<Impl>()){}Connection::~Connection(){p->shutdown();}
void Connection::close(){p->shutdown();p->status="Offline";}
bool Connection::host(unsigned port){close();if(!port||port>65535){p->status="Invalid co-op port";return false;}p->host=true;p->port=port;p->status="Preparing encrypted host";return p->start("H||"+std::to_string(port)+"|");}
bool Connection::join(const std::string& ip,unsigned port,const std::string& invitation){close();if(ip.empty()||ip.size()>15||ip.find_first_not_of("0123456789.")!=std::string::npos||invitation.size()!=88||invitation.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/=")!=std::string::npos||!port||port>65535){p->status="Enter host IPv4, port and complete invitation code";return false;}p->host=false;p->port=port;p->status="Connecting securely";return p->start("J|"+ip+"|"+std::to_string(port)+"|"+invitation);}
bool Connection::send(unsigned type,std::span<const unsigned char> bytes){auto& s=*p;if(!connected()||type<Hello||type>Leave||bytes.size()>MaxPacket)return false;if(queuedBytes()+bytes.size()+12>16*1024*1024){s.fail("Connection too slow: outgoing queue exceeded limit");return false;}if(s.sent){s.output.erase(s.output.begin(),s.output.begin()+s.sent);s.sent=0;}for(unsigned value:{Magic,type,unsigned(bytes.size())}){auto data=reinterpret_cast<unsigned char*>(&value);s.output.insert(s.output.end(),data,data+4);}s.output.insert(s.output.end(),bytes.begin(),bytes.end());return true;}
void Connection::poll(){
 auto& s=*p;if(s.pipe==INVALID_HANDLE_VALUE)return;auto now=GetTickCount64();std::string transportError;
 if(!s.pipeConnected){
  ConnectNamedPipe(s.pipe,nullptr);ULONG clientPid=0;
  if(GetNamedPipeClientProcessId(s.pipe,&clientPid)&&clientPid){if(clientPid!=s.helperPid){s.fail("Unexpected process on private co-op channel");return;}s.pipeConnected=true;}
  else{if(now-s.started>7000)s.fail("Secure transport startup timed out");return;}
 }
 if(s.authenticated&&now-s.lastPing>=1000){send(Keepalive,{});s.lastPing=now;}
 // PIPE_NOWAIT byte mode completes each call before the queue can be moved.
 // A full pipe may accept only a prefix (or zero bytes); retain the remainder.
 if(s.sent<s.output.size()){
  DWORD count=0;auto size=DWORD(std::min<size_t>(65536,s.output.size()-s.sent));
  if(WriteFile(s.pipe,s.output.data()+s.sent,size,&count,nullptr))s.sent+=count;
  else if(GetLastError()!=ERROR_NO_DATA)transportError="Secure transport closed while sending";
 }
 std::array<unsigned char,65536> buffer{};size_t budget=1024*1024;
 while(budget){
  DWORD available=0;if(!PeekNamedPipe(s.pipe,nullptr,0,nullptr,&available,nullptr)){transportError="Secure transport stopped";break;}if(!available)break;
  DWORD count=0;if(!ReadFile(s.pipe,buffer.data(),DWORD(std::min({budget,size_t(available),buffer.size()})),&count,nullptr)||!count){transportError="Secure transport stopped while receiving";break;}
  s.input.insert(s.input.end(),buffer.begin(),buffer.begin()+count);s.lastRead=now;budget-=count;if(s.input.size()>MaxPacket+12){s.fail("Invalid co-op receive buffer");return;}
 }
 size_t offset=0;
 while(s.input.size()-offset>=12){
  unsigned header[3];std::memcpy(header,s.input.data()+offset,12);
  if(header[0]!=Magic||header[2]>MaxPacket||(header[1]>6&&(header[1]<100||header[1]>103))){s.fail("Invalid secure transport message");return;}
  if(s.input.size()-offset-12<header[2])break;auto begin=s.input.begin()+offset+12;
  if(header[1]>=100){std::string message(begin,begin+header[2]);if(header[1]==100)s.status=message;else if(header[1]==101)s.invite=message;else if(header[1]==102){s.authenticated=true;s.status=message;}else{s.fail(message);return;}}
  else if(header[1]!=Keepalive){if(!s.authenticated||s.messages.size()>=64){s.fail("Invalid authenticated message queue");return;}s.messages.push_back({header[1],{begin,begin+header[2]}});}
  offset+=12+header[2];
 }
 if(offset)s.input.erase(s.input.begin(),s.input.begin()+offset);
 if(!transportError.empty()){s.fail(transportError);return;}
 if(s.authenticated&&now-s.lastRead>15000)s.fail("Connection timed out: no response from peer");
 else if(s.process&&WaitForSingleObject(s.process,0)==WAIT_OBJECT_0)s.fail("Secure transport exited; check Windows .NET Framework support");
}
std::string Connection::diagnostic()const{unsigned header[3]{};if(p->input.size()>=12)std::memcpy(header,p->input.data(),12);return " out="+std::to_string(queuedBytes())+" in="+std::to_string(p->input.size())+" header="+std::to_string(header[0])+","+std::to_string(header[1])+","+std::to_string(header[2]);}
bool Connection::connected()const{return p->authenticated;}bool Connection::active()const{return p->pipe!=INVALID_HANDLE_VALUE;}bool Connection::hosting()const{return p->host;}unsigned Connection::port()const{return p->port;}size_t Connection::queuedBytes()const{return p->output.size()-p->sent;}const std::string& Connection::status()const{return p->status;}const std::string& Connection::invitation()const{return p->invite;}std::deque<Message> Connection::receive(){std::deque<Message> result;result.swap(p->messages);return result;}
void delta(Writer& out,std::span<const unsigned char> memory,std::vector<unsigned char>& baseline){bool reset=baseline.size()!=memory.size();out.pod(unsigned(reset));if(reset)baseline.assign(memory.size(),0);size_t i=0;while(i<memory.size()){if(memory[i]==baseline[i]){++i;continue;}size_t begin=i,last=i++;while(i<memory.size()&&i-begin<65536){if(memory[i]!=baseline[i])last=i;if(i-last>16)break;++i;}size_t count=last-begin+1;out.pod(unsigned(begin));out.pod(unsigned(count));out.bytes(memory.subspan(begin,count));std::copy_n(memory.begin()+begin,count,baseline.begin()+begin);i=last+1;}out.pod(~0u);}
void applyDelta(Reader& in,std::vector<unsigned char>& baseline,size_t expected){unsigned reset=in.pod<unsigned>();if(reset>1)throw std::runtime_error("Invalid co-op delta reset");if(reset)baseline.assign(expected,0);if(baseline.size()!=expected)throw std::runtime_error("Missing co-op state baseline");unsigned previous=0;for(;;){auto offset=in.pod<unsigned>();if(offset==~0u)break;auto count=in.pod<unsigned>();if(!count||offset<previous||offset>expected||count>expected-offset)throw std::runtime_error("Invalid co-op state delta");auto bytes=in.bytes(count);std::copy(bytes.begin(),bytes.end(),baseline.begin()+offset);previous=offset+count;}}
unsigned selfTest(){unsigned checks=0;std::vector<unsigned char> memory(4*1024*1024),sent,received;for(unsigned i=0;i<memory.size();i+=127)memory[i]=i%255+1;for(unsigned round=0;round<4;++round){memory[round*97]^=73;Writer out;delta(out,memory,sent);Reader in{out.data};applyDelta(in,received,memory.size());in.end();if(received!=memory)throw std::runtime_error("Co-op delta round trip failed");++checks;}Connection host,client;if(!host.host(Port+1))throw std::runtime_error("Co-op secure host setup failed");auto until=GetTickCount64()+10000;while(host.invitation().empty()&&host.active()&&GetTickCount64()<until){host.poll();Sleep(1);}if(host.invitation().size()!=88)throw std::runtime_error("Co-op invitation failed: "+host.status());Connection occupied;if(!occupied.host(Port+1))throw std::runtime_error("Occupied-port fixture failed");until=GetTickCount64()+10000;while(occupied.active()&&GetTickCount64()<until){host.poll();occupied.poll();Sleep(1);}if(occupied.active()||!occupied.invitation().empty()||occupied.status().find("already in use")==std::string::npos)throw std::runtime_error("Occupied port did not report actionable error: "+occupied.status());++checks;for(unsigned retry=0;retry<3;++retry){Connection restarted;if(!restarted.host(Port+2))throw std::runtime_error("Host retry setup failed");until=GetTickCount64()+10000;while(restarted.invitation().empty()&&restarted.active()&&GetTickCount64()<until){restarted.poll();Sleep(1);}if(restarted.invitation().size()!=88)throw std::runtime_error("Host retry did not release port: "+restarted.status());restarted.close();++checks;}for(unsigned attempt=0;attempt<2;++attempt){Connection rejected;auto wrong=host.invitation();unsigned index=attempt?0:60;wrong[index]=wrong[index]=='A'?'B':'A';if(!rejected.join("127.0.0.1",Port+1,wrong))throw std::runtime_error("Negative invitation fixture failed");until=GetTickCount64()+10000;while(rejected.active()&&GetTickCount64()<until){host.poll();rejected.poll();Sleep(1);}if(rejected.connected()||!host.active()||host.connected())throw std::runtime_error("Invalid invitation or certificate pin accepted");++checks;}
 if(!client.join("127.0.0.1",Port+1,host.invitation()))throw std::runtime_error("Co-op secure join setup failed");until=GetTickCount64()+15000;while((!host.connected()||!client.connected())&&GetTickCount64()<until&&host.active()&&client.active()){host.poll();client.poll();Sleep(1);}if(!host.connected()||!client.connected())throw std::runtime_error("Co-op encrypted loopback failed: "+host.status()+" / "+client.status());Writer payload;payload.text("encrypted co-op loopback");payload.bytes(memory);if(!host.send(Snapshot,payload.data))throw std::runtime_error("Co-op loopback queue failed");bool delivered=false;until=GetTickCount64()+10000;while(!delivered&&GetTickCount64()<until){host.poll();client.poll();for(auto& message:client.receive())if(message.type==Snapshot){if(message.bytes!=payload.data)throw std::runtime_error("Co-op encrypted stream changed payload");delivered=true;}Sleep(1);}if(!delivered)throw std::runtime_error("Co-op encrypted state not delivered: "+host.status()+" / "+client.status());++checks;
 unsigned sentPackets=0,readPackets=0;until=GetTickCount64()+15000;
 while(readPackets<120&&GetTickCount64()<until){
  host.poll();client.poll();
  if(sentPackets<120&&sentPackets-readPackets<16&&host.queuedBytes()<512*1024){unsigned size=sentPackets%20==0?800000:463;std::vector<unsigned char> bytes(size);for(unsigned i=0;i<size;++i)bytes[i]=static_cast<unsigned char>(sentPackets*17+i*13);if(!host.send(Snapshot,bytes))throw std::runtime_error("Co-op stress queue failed");++sentPackets;}
  for(auto& message:client.receive()){unsigned size=readPackets%20==0?800000:463;if(message.type!=Snapshot||message.bytes.size()!=size)throw std::runtime_error("Co-op stress framing failed at "+std::to_string(readPackets));for(unsigned i=0;i<size;++i)if(message.bytes[i]!=static_cast<unsigned char>(readPackets*17+i*13))throw std::runtime_error("Co-op stress payload corrupted");++readPackets;}
  Sleep(1);
 }
 if(readPackets!=120)throw std::runtime_error("Co-op stress stalled at "+std::to_string(readPackets)+" sent="+std::to_string(sentPackets)+" host="+host.status()+host.diagnostic()+" client="+client.status()+client.diagnostic());++checks;
 client.close();for(unsigned i=0;i<1000&&host.connected();++i){host.poll();Sleep(1);}if(host.connected())throw std::runtime_error("Co-op disconnect not detected");++checks;Connection invalid;if(invalid.join("not-an-ip",Port,""))throw std::runtime_error("Invalid co-op IP/invitation accepted");return checks+1;}
}
