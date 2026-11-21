#pragma once
#include <algorithm>
#include <cmath>
// Original 0x4ecdc's stepped fade. World-distance conversion (/8) is
// an empirical match used by the existing renderer reconstruction.
inline unsigned explorerFogFactor(double depth){return 255-unsigned(std::clamp(std::floor(depth/12.)-170.,0.,255.));}
inline unsigned explorerFogPixel(unsigned pixel,double depth){auto f=explorerFogFactor(depth);return (pixel&0xff000000)|((((pixel>>16)&255)*f/256)<<16)|((((pixel>>8)&255)*f/256)<<8)|((pixel&255)*f/256);}
