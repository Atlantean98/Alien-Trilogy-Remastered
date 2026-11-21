#pragma once
#include "ExplorerCoop.h"
#include <filesystem>
#include <fstream>
#include <cstring>
#include <string_view>

class ExplorerReplay {
public:
    using Writer=trilogy::coop::Writer;
    using Reader=trilogy::coop::Reader;
    static constexpr unsigned Version=1,Hz=30;
    static constexpr std::uint64_t Limit=256ull*1024*1024;
    std::ofstream recording;
    std::ifstream playback;
    std::vector<unsigned char> baseline,packet;
    std::filesystem::path temporary,destination;
    unsigned frames=0;
    std::uint64_t bytes=0;
    bool paused=false;

    static bool hasPlayback(const std::filesystem::path& path,std::uintmax_t length){
        std::ifstream file(path,std::ios::binary);
        file.seekg(8);
        unsigned size=0;
        std::uintmax_t offset=8;
        // Skip the metadata packet and the first frame. A second frame gives
        // the clip a playback interval.
        for(int packet=0;packet<2;++packet){
            if(!file.read(reinterpret_cast<char*>(&size),sizeof(size))||
               !size||size>trilogy::coop::MaxPacket)return false;
            offset+=12ull+size;
            if(offset>=length)return false;
            file.seekg(static_cast<std::streamoff>(offset));
        }
        return file.read(reinterpret_cast<char*>(&size),sizeof(size))&&size&&
            size<=trilogy::coop::MaxPacket&&offset+12ull+size<=length;
    }
    void writeMemory(Writer& frame,std::span<const unsigned char> memory){
        const bool reset=baseline.size()!=memory.size();
        frame.pod(unsigned(reset));
        if(reset)baseline.assign(memory.size(),0);
        constexpr size_t block=256;
        for(size_t begin=0;begin<memory.size();begin+=block){
            const auto count=std::min(block,memory.size()-begin);
            if(std::memcmp(memory.data()+begin,baseline.data()+begin,count)==0)continue;
            frame.pod(unsigned(begin));frame.pod(unsigned(count));
            frame.bytes(memory.subspan(begin,count));
            std::memcpy(baseline.data()+begin,memory.data()+begin,count);
        }
        frame.pod(~0u);
    }

    static std::uint64_t checksum(std::span<const unsigned char> data){
        std::uint64_t hash=1469598103934665603ull;
        for(auto byte:data)hash=(hash^byte)*1099511628211ull;
        return hash;
    }
    void start(const std::filesystem::path& path,const Writer& header){
        finish();
        std::filesystem::create_directories(path.parent_path());
        destination=path;
        temporary=path;temporary+=L".tmp";
        recording.open(temporary,std::ios::binary|std::ios::trunc);
        if(!recording)throw std::runtime_error("Cannot create replay");
        frames=0;bytes=0;
        recording.write("ATRPLAY1",8);
        write(header);
    }
    void write(const Writer& frame){
        const unsigned size=unsigned(frame.data.size());
        const auto hash=checksum(frame.data);
        recording.write(reinterpret_cast<const char*>(&size),sizeof(size));
        recording.write(reinterpret_cast<const char*>(&hash),sizeof(hash));
        recording.write(reinterpret_cast<const char*>(frame.data.data()),size);
        bytes+=size+12;
        if(!recording)throw std::runtime_error("Cannot write replay");
    }
    void finish(){
        if(!recording.is_open())return;
        recording.close();
        std::error_code error;
        if(frames>=2&&recording)std::filesystem::rename(temporary,destination,error);
        else std::filesystem::remove(temporary,error);
        frames=0;baseline.clear();
    }
    void abortRecording(){
        recording.close();
        std::error_code error;
        std::filesystem::remove(temporary,error);
        frames=0;baseline.clear();
    }
    void open(const std::filesystem::path& path){
        finish();
        playback.open(path,std::ios::binary);
        char signature[8]{};
        if(!playback.read(signature,8)||std::string_view(signature,8)!="ATRPLAY1")
            throw std::runtime_error("Unsupported replay format");
        baseline.clear();paused=false;
    }
    bool read(){
        if(playback.peek()==std::char_traits<char>::eof())return false;
        unsigned size=0;
        std::uint64_t hash=0;
        if(!playback.read(reinterpret_cast<char*>(&size),sizeof(size))||
           !playback.read(reinterpret_cast<char*>(&hash),sizeof(hash))||
           !size||size>trilogy::coop::MaxPacket)throw std::runtime_error("Invalid replay frame");
        packet.resize(size);
        if(!playback.read(reinterpret_cast<char*>(packet.data()),size)||checksum(packet)!=hash)
            throw std::runtime_error("Damaged replay frame");
        return true;
    }
};
