#include "RenderCommands.h"
#include <bit>
namespace trilogy::original {
namespace {using Mem=PlayerControlMemory;int si(unsigned n){return std::bit_cast<std::int32_t>(n);}int word(unsigned n){return std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(n));}}
void initializeRenderRectangle(Mem m,unsigned p,int x,int y,int width,int height){for(unsigned i=8;i<20;i+=2)m.w(p+i,0);m.w(p,x);m.w(p+4,static_cast<unsigned>(x)+width);m.w(p+2,y);m.w(p+6,static_cast<unsigned>(y)+height);}
void buildDrawAreaPacket(Mem m,unsigned p,unsigned r){m.w(p+12,m.w(r));m.w(p+14,m.w(r+2));m.w(p+16,m.w(r+4));auto last=m.w(r+6);m.d(p+4,0x4bc38);m.w(p+18,last);}
void buildDepthStatePacket(Mem m,unsigned p,std::uint16_t n){m.d(p+4,0x48a3c);m.w(p+20,n);}
void writeScreenPoint(Mem m,unsigned p,std::uint16_t x,std::uint16_t y){m.w(p,x);m.w(p+2,y);}
void setRenderClearColor(Mem m,std::uint8_t a,std::uint8_t b,std::uint8_t c){m.b(0x3f6d38,a);m.b(0x3f6d39,b);m.b(0x3f6d3a,c);}
void setProjectionCenter(Mem m,unsigned x,unsigned y){m.d(0x3f6d24,x);m.d(0x3f6d20,y);}
void readProjectionDepths(Mem m,unsigned a,unsigned b,unsigned c,unsigned d){m.d(a,m.d(0x23a4d8));m.d(b,m.d(0x23a4c8));m.d(c,m.d(0x23a4b8));m.d(d,m.d(0x23a4a8));}
void writeNeutralRgb(Mem m,unsigned p){m.b(p,128);m.b(p+1,128);m.b(p+2,128);}
void setPacketHeight(Mem m,unsigned p,std::uint16_t n){m.w(p+18,n);}
void applyDepthStatePacket(Mem m,unsigned p){m.w(0x3f6e68,m.w(p+20));}
void applyDrawAreaPacket(Mem m,unsigned p){m.w(0x9d5d0,m.w(p+12));m.w(0x9d5d2,m.w(p+14));auto third=m.w(p+16),last=m.w(p+18);m.w(0x9d5d4,third);m.w(0x9d5d6,last);}
void buildClearBufferPacket(Mem m,unsigned p){m.d(p+4,0x4bc6c);}
void clearBaseBufferPacket(Mem m){for(unsigned i=0;i<65536;++i)m.b(0x28c888+i,0);}
void rasterizeRectanglePacket(Mem m,unsigned p){
    int x=m.s(p+12),y=m.s(p+14),w=m.s(p+16),h=m.s(p+18);auto color=m.b(p+8);
    if(x+w<=0||x>=320||y+h<=0||y>=240)return;
    if(x<0){w=word(w+x);x=0;}if(y<0){h=word(h+y);y=0;}
    if(x+w>320)w=320-x;if(y+h>240)h=240-y;
    m.d(0x3f6e40,m.d(0x9d544));m.d(0x3f6e50,m.d(0x9d504));m.d(0x3f6e54,m.d(0x9d508));
    auto dest=0x28c888u+x+y*320;
    // Valid visible rectangles have nonnegative clipped extents.
    for(int row=0;row<h;++row){for(int col=0;col<w;++col)m.b(dest+col,color);dest+=320;}
    m.d(0x9d544,m.d(0x3f6e40));m.d(0x9d504,m.d(0x3f6e50));m.d(0x9d508,m.d(0x3f6e54));
}
void submitFlatQuadPacket(Mem m,unsigned p,const RenderService& f){
    m.b(0x3f6e79,m.b(p+8));m.d(0x3f6dec,m.s(p+24));
    m.d(0x3f6df0,m.s(p+14));m.d(0x3f6df4,m.s(p+18));m.d(0x3f6d9c,m.s(p+22));m.b(0x3f6e83,5);m.d(0x3f6da0,m.s(p+26));
    bool base=m.b(p+11)!=0;
    if(base){m.d(0x3f6e40,m.d(0x9d544));m.d(0x9d544,0x28c888);m.d(0x3f6e50,m.d(0x9d504));m.b(p+11,0);m.d(0x3f6e54,m.d(0x9d508));m.d(0x9d504,319);m.d(0x9d508,239);}
    auto scale=[&](int n,unsigned a,int divisor){return base?n:si(static_cast<unsigned>(n)*m.d(a))/divisor;};
    auto triangle=[&](unsigned first,bool initial){
        int xs[3],ys[3];
        for(unsigned i=0;i<3;++i){xs[i]=scale(m.s(p+12+(first+i)*4),0x3f6e44,320);ys[i]=scale(m.s(p+14+(first+i)*4),0x3f6e4c,240);}
        unsigned ya[]={0x3f6df0,0x3f6df4,0x3f6d9c},xa[]={0x3f6de0,0x3f6de4,0x3f6de8};
        for(unsigned i=0;i<3;++i)m.d(ya[i],ys[i]);
        // First-triangle X values live in registers until culling; the second
        // triangle writes the globals before testing visibility.
        if(!initial)for(unsigned i=0;i<3;++i)m.d(xa[i],xs[i]);
        bool visible=(xs[0]>=0||xs[1]>=0||xs[2]>=0)&&(xs[0]<=si(m.d(0x9d504))||xs[1]<=si(m.d(0x9d504))||xs[2]<=si(m.d(0x9d504)))&&(ys[0]>=0||ys[1]>=0||ys[2]>=0)&&(ys[0]<=si(m.d(0x9d508))||ys[1]<=si(m.d(0x9d508))||ys[2]<=si(m.d(0x9d508)));
        if(visible){for(unsigned i=0;i<3;++i)m.d(xa[i],static_cast<unsigned>(xs[i])<<16);f(m,0x4acb8,{});}
    };
    triangle(0,true);triangle(1,false);
    if(base){m.d(0x9d544,m.d(0x3f6e40));m.d(0x9d504,m.d(0x3f6e50));m.d(0x9d508,m.d(0x3f6e54));}
}
unsigned resolveTexturePageBuffer(Mem m,std::uint16_t page){
    int index=-1;switch(page){case 2:case 3:case 4:case 5:case 6:case 7:index=page-2;break;case 10:case 11:case 12:case 13:index=page-4;break;case 16:case 17:case 18:case 19:case 20:index=page-6;break;}
    m.w(0x3f6e70,index<0);return m.d(0x9d540)+(index<0?0u:static_cast<unsigned>(index)<<16);
}
unsigned screenTriangleArea(unsigned a,unsigned b,unsigned c){
    int ax=word(a),ay=word(a>>16),bx=word(b),by=word(b>>16),cx=word(c),cy=word(c>>16);
    return 0u-(static_cast<unsigned>(cy-by)*static_cast<unsigned>(ax-bx)-static_cast<unsigned>(ay-by)*static_cast<unsigned>(cx-bx));
}
int triangleDepthBucket(Mem m){int n=si(m.d(0x23a4d8));for(unsigned a:{0x23a4c8u,0x23a4b8u})if(n<si(m.d(a)))n=si(m.d(a));return n>>5;}
int quadDepthBucket(Mem m){int n=si(m.d(0x23a4d8));for(unsigned a:{0x23a4c8u,0x23a4b8u,0x23a4a8u})if(n<si(m.d(a)))n=si(m.d(a));return n>>5;}
void modulateRgb(Mem m,unsigned factors,unsigned rgb,unsigned output){
    int values[3];for(unsigned i=0;i<3;++i)values[i]=si(m.b(rgb+i)*m.d(factors+i*4))/3072;
    for(unsigned i=0;i<3;++i)m.b(output+i,values[i]>255?255:values[i]);
}
void blendLongVectorsQ12(Mem m,unsigned a,unsigned b,unsigned scaleA,unsigned scaleB,unsigned out){for(unsigned i=0;i<3;++i)m.d(out+i*4,si(m.d(a+i*4)*scaleA+m.d(b+i*4)*scaleB)>>12);}
void rasterizeGradientRectangle(Mem m,unsigned p){
    auto x=m.s(p+12),y=m.s(p+14);auto width=static_cast<std::uint16_t>(m.w(p+20)-m.w(p+12)),height=static_cast<std::uint16_t>(m.w(p+30)-m.w(p+14));auto color=m.b(p+8);
    if(!height)return;auto dest=m.d(0x9d544)+static_cast<unsigned>(y*320+x);
    for(unsigned row=0;row<height;++row){for(unsigned col=0;col<width;++col)m.b(dest+col,col/5+color);dest+=320;}
}
RenderService withRenderCommands(const RenderService& external){return [external](Mem& m,unsigned routine,std::span<const unsigned> a)->unsigned{
    switch(routine){case 0x48a3c:applyDepthStatePacket(m,a[0]);break;case 0x4bc38:applyDrawAreaPacket(m,a[0]);break;case 0x4bc6c:clearBaseBufferPacket(m);break;case 0x4bc88:rasterizeRectanglePacket(m,a[0]);break;case 0x48e40:submitFlatQuadPacket(m,a[0],external);break;case 0x4d578:return resolveTexturePageBuffer(m,a[0]);case 0x4d834:rasterizeGradientRectangle(m,a[0]);break;default:return external(m,routine,a);}return 0;
};}
}
