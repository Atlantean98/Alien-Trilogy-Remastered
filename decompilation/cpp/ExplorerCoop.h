#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <span>
#include <deque>
#include <array>
#include <cstring>
#include <stdexcept>
#include <type_traits>
namespace trilogy::coop {
inline constexpr unsigned Port=27871,Protocol=2,MaxPacket=8*1024*1024;
struct Marine {std::array<short,3> position{};int yaw{};unsigned health=100,armor=0,weapon=0,kills=0,pickups=0,deaths=0,respawn=0;};
struct PlayerInput {unsigned sequence=0;short forward=0,strafe=0;int yaw=0,pitch=0;unsigned buttons=0,weapon=0;};
enum InputFlags:unsigned {Fire=1,Grenade=2,Use=4,Sprint=8};
enum class Phase:unsigned {Lobby,Briefing,Playing,Extracting,Results,Failed,Finished};
enum MessageType:unsigned {Hello=1,Input,Snapshot,Ready,Keepalive,Leave};
struct Message {unsigned type;std::vector<unsigned char> bytes;};
class Connection {
public:
 Connection();~Connection();bool host(unsigned port=Port);bool join(const std::string&,unsigned port,const std::string& invitation);void poll();void close();bool send(unsigned,std::span<const unsigned char>);bool connected()const;bool hosting()const;bool active()const;unsigned port()const;size_t queuedBytes()const;std::string diagnostic()const;const std::string& status()const;const std::string& invitation()const;std::deque<Message> receive();
private:struct Impl;std::unique_ptr<Impl> p;
};
struct Writer {
 std::vector<unsigned char> data;
 template<class T>void pod(const T& value){static_assert(std::is_trivially_copyable_v<T>);auto ptr=reinterpret_cast<const unsigned char*>(&value);data.insert(data.end(),ptr,ptr+sizeof(T));}
 void bytes(std::span<const unsigned char> value){if(data.size()+value.size()>MaxPacket)throw std::runtime_error("Co-op packet too large");data.insert(data.end(),value.begin(),value.end());}
 void text(const std::string& value){if(value.size()>4096)throw std::runtime_error("Co-op text too long");pod(unsigned(value.size()));bytes({reinterpret_cast<const unsigned char*>(value.data()),value.size()});}
 template<class T>void list(const std::vector<T>& values){pod(unsigned(values.size()));for(const auto& v:values)pod(v);}
};
struct Reader {
 std::span<const unsigned char> data;size_t offset=0;
 template<class T>T pod(){static_assert(std::is_trivially_copyable_v<T>);if(sizeof(T)>data.size()-offset)throw std::runtime_error("Truncated co-op packet");T value{};std::memcpy(&value,data.data()+offset,sizeof(T));offset+=sizeof(T);return value;}
 std::span<const unsigned char> bytes(size_t count){if(count>data.size()-offset)throw std::runtime_error("Truncated co-op packet");auto value=data.subspan(offset,count);offset+=count;return value;}
 std::string text(){auto count=pod<unsigned>();if(count>4096)throw std::runtime_error("Invalid co-op text");auto value=bytes(count);return {reinterpret_cast<const char*>(value.data()),value.size()};}
 template<class T>std::vector<T> list(unsigned maximum){auto count=pod<unsigned>();if(count>maximum)throw std::runtime_error("Invalid co-op collection");std::vector<T> values;values.reserve(count);for(unsigned i=0;i<count;++i)values.push_back(pod<T>());return values;}
 void end(){if(offset!=data.size())throw std::runtime_error("Unexpected co-op packet data");}
};
void delta(Writer&,std::span<const unsigned char>,std::vector<unsigned char>& baseline);
void applyDelta(Reader&,std::vector<unsigned char>& baseline,size_t expected);
unsigned selfTest();
}
