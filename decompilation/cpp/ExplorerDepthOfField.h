#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>
inline float explorerDofDepth(float value){return std::isfinite(value)&&value>0?std::min(value,65535.f):65535.f;}
inline float explorerDofRadius(float depth,float focus,float strength,unsigned height){depth=explorerDofDepth(depth);focus=explorerDofDepth(focus);return strength*14.f*height/720.f*std::clamp(std::abs(depth-focus)/std::max({depth,focus,256.f})*2.f-.04f,0.f,1.f);}
inline void explorerDepthOfField(std::vector<unsigned>& pixels,unsigned width,unsigned height,const std::vector<float>& depth,unsigned depthWidth,unsigned depthHeight,float strength){
 if(strength<=0||pixels.size()!=size_t(width)*height||depth.size()!=size_t(depthWidth)*depthHeight)return;
 auto atDepth=[&](unsigned x,unsigned y){return explorerDofDepth(depth[std::min(depthHeight-1,y*depthHeight/height)*depthWidth+std::min(depthWidth-1,x*depthWidth/width)]);};
 float focus=atDepth(width/2,height/2);auto source=pixels;std::array<float,16> ox{},oy{};for(unsigned i=0;i<16;++i){float distance=std::sqrt((i+.5f)/16);ox[i]=std::cos(i*2.39996323f)*distance;oy[i]=std::sin(i*2.39996323f)*distance;}
 for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x){float z=atDepth(x,y),radius=explorerDofRadius(z,focus,strength,height);if(radius<.5f)continue;auto color=source[y*width+x];float red=float(color>>16&255),green=float(color>>8&255),blue=float(color&255),weight=1;for(unsigned i=0;i<16;++i){unsigned sx=unsigned(std::clamp(int(std::lround(x+ox[i]*radius)),0,int(width)-1)),sy=unsigned(std::clamp(int(std::lround(y+oy[i]*radius)),0,int(height)-1));if(atDepth(sx,sy)<z*.85f)continue;auto sample=source[sy*width+sx];red+=sample>>16&255;green+=sample>>8&255;blue+=sample&255;++weight;}pixels[y*width+x]=(color&0xff000000)|(unsigned(std::lround(red/weight))<<16)|(unsigned(std::lround(green/weight))<<8)|unsigned(std::lround(blue/weight));}
}
