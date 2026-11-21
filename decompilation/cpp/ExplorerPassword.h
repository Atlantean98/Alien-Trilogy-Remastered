#pragma once
#include <array>
#include <string>
#include <optional>
#include <algorithm>
#include <cwctype>
struct ExplorerPasswordState {std::array<unsigned char,50> inventory{};unsigned level=0,difficulty=0,weapon=0;};
// Original PS1 0x800471d0/0x80047404 base-32 and 0x80047584/0x800477e4 packet.
inline constexpr wchar_t ExplorerPasswordAlphabet[]=L"BCDFGHJKLMNPQRSTVWXYZ0123456789?";
inline std::wstring explorerPassword(const ExplorerPasswordState& state){
 std::array<unsigned char,20> p{};auto word=[&](unsigned index){return unsigned(state.inventory[index])|(unsigned(state.inventory[index+1])<<8);};auto put=[&](unsigned index,unsigned n){p[index]=n;p[index+1]=n>>8;};
 put(0,word(4));put(2,word(6)&0x3fff);p[4]=word(24);p[6]=state.difficulty&3;p[7]=word(10);p[8]=word(14);p[9]=word(28);put(10,word(16));put(12,word(22));p[14]=word(12);p[15]=word(18);p[16]=state.level;constexpr unsigned selection[]={0,1,2,4,3};p[17]=((state.inventory[0]>>2)&31)|(selection[std::min(4u,state.weapon)]<<5);p[18]=word(20);p[19]=word(26);unsigned checksum=25;for(auto b:p)checksum+=b;p[5]=checksum;
 std::wstring result;for(unsigned bit=0;bit<160;bit+=5){unsigned v=0;for(unsigned j=0;j<5;++j)v=(v<<1)|((p[(bit+j)/8]>>(7-(bit+j)%8))&1);result+=ExplorerPasswordAlphabet[v];}return result;
}
inline std::optional<ExplorerPasswordState> explorerDecodePassword(const std::wstring& text){
 std::wstring code;for(auto c:text)if(!iswspace(c)&&c!=L'-')code+=towupper(c);if(code.size()!=32)return {};std::array<unsigned char,20> p{};
 for(unsigned i=0;i<32;++i){auto n=std::wstring(ExplorerPasswordAlphabet).find(code[i]);if(n==std::wstring::npos)return {};for(unsigned j=0;j<5;++j){unsigned bit=i*5+j;p[bit/8]|=((n>>(4-j))&1)<<(7-bit%8);}}
 unsigned sum=25;for(unsigned i=0;i<20;++i)if(i!=5)sum+=p[i];if((sum&255)!=p[5]||p[16]>=35||(p[6]&3)>2||(p[17]>>5)>4)return {};
 ExplorerPasswordState s;s.level=p[16];s.difficulty=p[6]&3;constexpr unsigned selection[]={0,1,2,4,3};s.weapon=selection[p[17]>>5];s.inventory[0]=(p[17]&31)<<2;auto get=[&](unsigned i){return unsigned(p[i])|(unsigned(p[i+1])<<8);};auto set=[&](unsigned i,unsigned v){s.inventory[i]=v;s.inventory[i+1]=v>>8;};set(4,get(0));set(6,get(2)&0x3fff);set(24,p[4]);set(10,p[7]);set(14,p[8]);set(28,p[9]);set(16,get(10));set(22,get(12));set(12,p[14]);set(18,p[15]);set(20,p[18]);set(26,p[19]);return s;
}
