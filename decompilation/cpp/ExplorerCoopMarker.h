#pragma once
#include <algorithm>
#include <cmath>

// Screen-space teammate locator: deliberately independent of world depth/fog.
struct ExplorerCoopMarker {int x=0,y=0,radius=0;bool edge=false;};
inline ExplorerCoopMarker explorerCoopMarker(double x,double y,double z,double focal,int width,int height){
 ExplorerCoopMarker out;out.radius=std::max(4,int(std::lround(height/100.)));double margin=out.radius*3.,hx=std::max(1.,width*.5-margin),hy=std::max(1.,height*.5-margin);
 double dx=x*focal/std::max(16.,std::abs(z)),dy=-y*focal/std::max(16.,std::abs(z));
 if(z<=16){dx=x;dy=std::max(16.,std::abs(z));out.edge=true;}
 double ratio=std::max(std::abs(dx)/hx,std::abs(dy)/hy);if(ratio>1||out.edge){dx/=std::max(.0001,ratio);dy/=std::max(.0001,ratio);out.edge=true;}
 out.x=std::clamp(int(std::lround(width*.5+dx)),0,width-1);out.y=std::clamp(int(std::lround(height*.5+dy)),0,height-1);return out;
}
inline unsigned explorerDrawCoopMarker(unsigned* pixels,int width,int height,const ExplorerCoopMarker& marker){
 unsigned count=0;int r=marker.radius;
 for(int y=-r-2;y<=r+2;++y)for(int x=-r-2;x<=r+2;++x){int px=marker.x+x,py=marker.y+y;if(px<0||py<0||px>=width||py>=height)continue;int d=std::abs(x)+std::abs(y);if(d>r+2)continue;
  unsigned color=d>r?0xff06151bu:d>=r-1?0xffefffffu:0xff28dcffu;if(d<=std::max(1,r/3))color=0xffffffffu;pixels[py*width+px]=color;++count;
 }
 return count;
}
