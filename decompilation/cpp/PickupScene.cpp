#include "PickupScene.h"
#include "PickupRuntime.h"
#include "ProjectileRuntime.h"
#include "CombatRuntime.h"
#include <bit>
#include <stdexcept>
#include <vector>

namespace trilogy::original {
namespace {
using Mem=PlayerControlMemory;
unsigned sx(int n){return static_cast<unsigned>(n);}
int si(unsigned n){return std::bit_cast<std::int32_t>(n);}
std::int16_t word(unsigned n){return std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(n));}
Matrix3s matrixAt(Mem m,unsigned a){Matrix3s matrix;for(unsigned i=0;i<9;++i)matrix[i]=word(m.w(a+i*2));return matrix;}
int floorAt(Mem m,unsigned x,unsigned z){
    auto width=m.w(0xa0c30),height=m.w(0xa0c32);std::vector<LevelCell> cells(static_cast<unsigned>(width)*height);
    for(unsigned i=0;i<cells.size();++i)for(unsigned j=0;j<16;++j)cells[i].bytes[j]=m.b(m.d(0xa0c40)+i*16+j);
    LevelGrid grid{width,height,cells};return floorHeight(grid,x,z);
}
unsigned invoke(Mem m,const RenderService& f,unsigned routine,std::initializer_list<unsigned> args){return f(m,routine,std::span<const unsigned>(args.begin(),args.size()));}
}
void buildObjectRotation(Mem m,unsigned angles,unsigned output){
    std::array<std::int16_t,4096> sine;for(unsigned i=0;i<4096;++i)sine[i]=word(m.w(0x9763c+i*2));
    auto result=buildRotation({word(m.w(angles)),word(0u-m.w(angles+2)),word(m.w(angles+4))},sine);
    for(unsigned i=0;i<9;++i)m.w(output+i*2,result[i]);
}
void composeObjectRotation(Mem m,unsigned left,unsigned rightOutput){
    auto a=matrixAt(m,left),b=matrixAt(m,rightOutput);
    for(unsigned row=0;row<3;++row)for(unsigned col=0;col<3;++col){unsigned sum=0;for(unsigned k=0;k<3;++k)sum+=sx(a[row*3+k])*sx(b[k*3+col]);m.w(rightOutput+(row*3+col)*2,sx(si(sum)>>12));}
}
void scaleObjectRotation(Mem m,unsigned matrix,std::array<std::int32_t,3> scales){for(unsigned i=0;i<9;++i)m.w(matrix+i*2,sx(si(sx(m.s(matrix+i*2))*sx(scales[i%3]))>>12));}
void installObjectRotation(Mem m,unsigned matrix){for(unsigned i=0;i<9;++i)m.w(0x23ad08+i*2,m.w(matrix+i*2));}
void installObjectTranslation(Mem m,unsigned transform){for(unsigned i=0;i<3;++i)m.d(0x23ad1c+i*4,m.d(transform+20+i*4));}
void assignObjectTranslation(Mem m,unsigned transform,unsigned translation){for(unsigned i=0;i<3;++i)m.d(transform+20+i*4,m.d(translation+i*4));}
void populateLevelPickups(Mem m,unsigned rules,std::uint16_t count){
    m.d(0x236c34,rules);m.w(0x236c4c,count);
    for(unsigned i=0;i<count;++i){auto rule=rules+i*8;m.b(rule+5,0);if(m.b(rule+2)==15)m.w(0xa0c0e,m.w(0xa0c0e)+1u);
        if(m.b(rule+6))continue;auto p=m.d(0x236c38);if(!p)throw std::out_of_range("Original level pickup population requires free records");
        m.d(0x236c38,m.d(p+0x28));auto kind=m.b(rule+2);m.b(p+0x20,kind==24?21:kind);m.b(p+0x21,m.b(rule+3));m.b(p+0x22,m.b(rule+4));
        m.w(p,m.b(rule)*512u+256u-m.w(0xa0c34));m.w(p+4,m.b(rule+1)*512u+256u-m.w(0xa0c36));
        auto x=sx(m.s(p))+m.w(0xa0c34),z=sx(m.s(p+4))+m.w(0xa0c36);
        m.w(p+2,sx(floorAt(m,x,z))+m.w(0x9cda4+m.b(p+0x20)*2));
        for(unsigned offset:{8u,10u,12u,16u,18u,20u})m.w(p+offset,0);
        m.d(p+0x1c,m.d(0xa0c40)+((z>>9)*m.w(0xa0c30)+(x>>9))*16);m.d(p+0x18,m.d(0x236bc4+m.b(p+0x20)*4));
        auto head=m.d(0x236c44);if(head)m.d(head+0x2c,p);m.d(p+0x2c,0);m.d(0x236c44,p);m.d(p+0x28,head);
    }
}
void initializeNetworkPickupPlacement(Mem m){
    for(unsigned i=0;i<128;++i){m.b(0x236a44+i,0);m.b(0x236b44+i,0);m.b(0x236ac4+i,255);}
    if(!m.b(0x9e0a8)||!m.b(0x3f953e))return;
    unsigned ordinal=0;
    for(auto p=m.d(0x236c44);p;p=m.d(p+0x28),++ordinal){
        if(ordinal>10){
            unsigned candidate;
            for(;;){candidate=nextGameRandom(m)&127;if(si(m.d(0xa0c16))>>16<=static_cast<int>(candidate))continue;
                bool used=false;for(unsigned i=0;i<128;++i)if(m.b(0x236ac4+i)==candidate){used=true;break;}if(!used)break;
            }
            auto index=sx(si(p-m.d(0x236c40))/48);m.b(0x236ac4+index,candidate);auto point=m.d(0xa0c50)+candidate*8;
            m.w(p,m.b(point)*512u-m.w(0xa0c34));m.w(p+4,m.b(point+1)*512u-m.w(0xa0c36));m.w(p+2,floorAt(m,sx(m.s(p))+m.w(0xa0c34),sx(m.s(p+4))+m.w(0xa0c36)));
        }
        if(m.b(p+0x20)==7){m.w(p,65535);m.w(p+2,65535);m.w(p+4,65535);m.b(0x236c4e,0);}
    }
}
void renderDroppedPickup(Mem m,unsigned p,const RenderService& external){
    if(!m.d(p+24)||(m.s(p)==-1&&m.s(p+4)==-1)||m.b(0x95f9c)!=1||!(m.w(m.d(p+28)+8)&m.w(0xa0cc0)))return;
    auto f=withNativeRenderGeometry(external);m.d(0x2344a4,0x2331d4);blendVectorsQ12(m,p,0xa0a20,4096,-4096,0xa0b98);saveTransformState(m);
    auto transformed=transformVector(matrixAt(m,0xa0b68),{word(m.w(0xa0b98)),word(m.w(0xa0b9a)),word(m.w(0xa0b9c))});for(unsigned i=0;i<3;++i)m.d(0xa0b88+i*4,sx(transformed[i]));
    if(si(m.d(0xa0b90))>384&&si(m.d(0xa0b90))<8192&&si(m.d(0xa0b88))<8192){
        buildObjectRotation(m,p+16,0xa0a28);composeObjectRotation(m,0xa0b68,0xa0a28);auto scale=0x9cb74+m.b(p+32)*16u;
        scaleObjectRotation(m,0xa0a28,{si(m.d(scale)),si(m.d(scale+4)),si(m.d(scale+8))});installObjectRotation(m,0xa0a28);assignObjectTranslation(m,0xa0a28,0xa0b88);installObjectTranslation(m,0xa0a28);
        auto z=si(m.d(0xa0b90));invoke(m,f,z<1536?0x2504c:z<4096?0x26358:0x25a6c,{m.d(p+24),m.d(p+28)});
    }
    m.w(p+18,(m.w(p+18)+16u)&4095);m.d(0x2344a4,0x2311d4);restoreTransformState(m);
}
RenderService withNativeRenderGeometry(const RenderService& external){
    return [external](Mem& m,unsigned routine,std::span<const unsigned> a)->unsigned{
        switch(routine){
            case 0x47630:buildObjectRotation(m,a[0],a[1]);return a[1];
            case 0x47294:composeObjectRotation(m,a[0],a[1]);return a[1];
            case 0x46ef4:scaleObjectRotation(m,a[0],{si(a[1]),si(a[2]),si(a[3])});return a[0];
            case 0x46e4c:installObjectRotation(m,a[0]);return 0;
            case 0x477f8:assignObjectTranslation(m,a[0],a[1]);return a[0];
            case 0x46ec4:installObjectTranslation(m,a[0]);return 0;
            default:return external(m,routine,a);
        }
    };
}
PlayerControlService withPickupScene(const PlayerControlService& external,const RenderService& renderer){
    return [external,renderer](Mem& m,unsigned routine,unsigned a,unsigned b,unsigned c)->PlayerControlResult{
        if(routine==0x3b2bc){populateLevelPickups(m,a,static_cast<std::uint16_t>(b));return {};}
        if(routine==0x3af20){initializeNetworkPickupPlacement(m);return {};}
        if(routine==0x3c074){renderDroppedPickup(m,a,renderer);return {};}
        return external(m,routine,a,b,c);
    };
}
}
