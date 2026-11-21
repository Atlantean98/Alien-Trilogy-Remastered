#include "PolygonRasterizer.h"
#include <bit>
namespace trilogy::original {
namespace {
using Mem=PlayerControlMemory;
int si(unsigned n){return std::bit_cast<std::int32_t>(n);}
struct ShaderPatch {
    unsigned uInteger,uIntegerAlt,vInteger,uFraction,uFractionAlt,vFraction;
    unsigned redBase,redPatch,greenBase,greenPatch,blueBase,bluePatch,clip,remaining;
};
constexpr ShaderPatch patches[]={
    {0x5ab4b,0,0x5ab30,0x5ab3f,0,0x5ab24,0x5a981,0x5ab54,0x5a987,0x5ab4e,0x5a98d,0x5ab39,0x5a904,0},
    {0x5af0c,0x5aed5,0x5aeed,0x5af00,0x5aecf,0x5aee1,0x5ad08,0x5af15,0x5ad0e,0x5af0f,0x5ad14,0x5aefa,0x5ac58,0x5ac68},
    {0x5b2d7,0x5b29b,0x5b2b0,0x5b2cb,0x5b294,0x5b2a9,0x5b0e7,0x5b2e1,0x5b0ed,0x5b2da,0x5b0f3,0x5b2bf,0x5b038,0}
};
void shadedSpans(Mem m,unsigned mode){
    auto rows=m.d(0x3f6d58)&255u;m.d(0x3f6d58,rows);if(!rows)return;
    auto buffer=m.d(0x9d544),pointer=m.d(0x3f6cf0);if(pointer<buffer||pointer>=buffer+0x12ac0u)return;
    const auto& p=patches[mode];auto du=m.d(0x3f6d4c),dv=m.d(0x3f6d48);
    m.b(p.uInteger,du>>16);if(p.uIntegerAlt)m.b(p.uIntegerAlt,du>>16);m.b(p.vInteger,dv>>16);
    m.d(p.uFraction,du<<16);if(p.uFractionAlt)m.d(p.uFractionAlt,du<<16);m.d(p.vFraction,dv<<16);
    auto red=m.d(p.redBase)+m.d(0x3f6da4),green=m.d(p.greenBase)+m.d(0x3f6dbc),blue=m.d(p.blueBase)+m.d(0x3f6d8c);
    m.d(p.redPatch,red);m.d(p.greenPatch,green);m.d(p.bluePatch,blue);
    auto slope=m.d(0x3f6dcc),fraction=(si(slope)<0?0u-slope:slope)&65535u;
    m.d(0x3f6d34,static_cast<unsigned>((static_cast<std::int64_t>(si(du))*fraction)>>16));
    m.d(0x3f6d30,static_cast<unsigned>((static_cast<std::int64_t>(si(dv))*fraction)>>16));
    do {
        int left=m.s(0x3f6db2),right=m.s(0x3f6d92);
        if(right>=0&&left<si(m.d(0x9d504))){
            auto u=m.d(0x3f6da8)-m.d(0x3f6d68),v=m.d(0x3f6dac)-m.d(0x3f6d74);m.d(0x3f6db8,u);m.d(0x3f6dc0,v);
            auto dest=m.d(0x3f6cf0);unsigned end=m.w(0x3f6d92);
            if(!(mode==0&&left<0)&&end>m.w(0x9d504))end=m.w(0x9d504);
            unsigned count;
            if(left<0){auto clip=0u-static_cast<unsigned>(left);m.d(p.clip,clip);u+=du*clip;v+=dv*clip;m.d(p.clip+4,u);m.d(p.clip+8,v);count=end;}
            else {dest+=static_cast<unsigned>(left);count=(end-static_cast<unsigned>(left))&65535u;}
            unsigned remainder=0;
            if(mode==1){m.b(p.remaining,0);if(count&0xff00u){remainder=(count+1)&255u;m.b(p.remaining,remainder);count=255;}}
            else if(!(count&255u))count=0;
            if(count){
                auto source=(m.d(0x3f6d28)&0xffff0000u)|((v>>8)&0xff00u)|((u>>16)&255u);
                unsigned uf=u<<16,vf=v<<16;
                // The clipped entry for the transparent/blended routines uses
                // the signed integer U step in EBP, rather than the U fraction.
                if(mode!=0&&left<0)uf=static_cast<unsigned>(static_cast<int>(std::bit_cast<std::int8_t>(static_cast<std::uint8_t>(du>>16))));
                do {
                    auto old=vf;vf+=0x80000000u;if(vf<old)source=(source&0xffff00ffu)|((source+256)&0xff00u);
                    old=uf;uf+=0x80000000u;if(uf<old)++source;
                    unsigned lastIndex=0;
                    for(unsigned i=0;i<count;++i){
                        auto index=m.b(source);lastIndex=index;
                        old=vf;vf+=dv<<16;auto nextV=((source>>8)+(dv>>16)+(vf<old))&255u;
                        old=uf;uf+=du<<16;auto nextU=(source+(du>>16)+(uf<old))&255u;source=(source&0xffff0000u)|(nextV<<8)|nextU;
                        if(mode==0||index){
                            auto rgb=m.d(blue+m.b(0x3f2bf0+index))|m.d(green+m.b(0x3f29f0+index))|m.d(red+m.b(0x3f2af0+index));auto color=m.b(0x3e89d8+rgb);
                            if(mode==2)color=m.b(0x3d88d8+color+m.b(dest+i)*256u);
                            m.b(dest+i,color);
                        }
                    }
                    // A transparent final texel takes an early row exit in
                    // the original and leaves the pending second segment set.
                    if(!remainder||!lastIndex)break;
                    dest+=count-1;count=remainder;remainder=0;m.b(p.remaining,0);
                    // ECX was reused for the last palette component. Reloading
                    // CL with the remainder discards the V fraction here.
                    vf=0;
                }while(true);
            }
        }
        m.d(0x3f6d90,m.d(0x3f6d90)+m.d(0x3f6d44));m.d(0x3f6da8,m.d(0x3f6da8)+m.d(0x3f6dc8));m.d(0x3f6dac,m.d(0x3f6dac)+m.d(0x3f6dc4));m.d(0x3f6cf0,m.d(0x3f6cf0)+320);
        auto previous=m.d(0x3f6db0),next=previous+slope,cu=m.d(0x3f6d68),cv=m.d(0x3f6d74);m.d(0x3f6db0,next);
        if(si(slope)>=0){if((previous&65535u)>(next&65535u)){cu-=du;cv-=dv;}cu+=m.d(0x3f6d34);cv+=m.d(0x3f6d30);}
        else {if((previous&65535u)<(next&65535u)){cu+=du;cv+=dv;}cu-=m.d(0x3f6d34);cv-=m.d(0x3f6d30);}
        m.d(0x3f6d68,cu);m.d(0x3f6d74,cv);m.d(0x3f6d58,m.d(0x3f6d58)-1);
    }while(m.d(0x3f6d58));
}
}
void rasterizeShadedTextureSpans(PlayerControlMemory m){shadedSpans(m,0);}
void rasterizeTransparentShadedSpans(PlayerControlMemory m){shadedSpans(m,1);}
void rasterizeBlendedShadedSpans(PlayerControlMemory m){shadedSpans(m,2);}
}
