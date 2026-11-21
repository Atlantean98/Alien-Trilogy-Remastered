#pragma once
#include <cmath>
#include <iomanip>
#include <sstream>
#include <string>
// Actual rendered-frame cadence, including simulation, rendering and frame pacing.
class ExplorerPerformance {
 double previous=-1,seconds=0,rate=0,milliseconds=0;unsigned frames=0;
public:
 void reset(){previous=-1;seconds=rate=milliseconds=0;frames=0;}
 void frame(double completed){if(!std::isfinite(completed))return;if(previous<0){previous=completed;return;}double delta=completed-previous;previous=completed;if(delta<=0)return;seconds+=delta;++frames;if(seconds>=.5){rate=frames/seconds;milliseconds=seconds*1000/frames;seconds=0;frames=0;}}
 double fps()const{return rate;}double frameMs()const{return milliseconds;}
 std::wstring label()const{if(!rate)return L"FPS --   -- ms";std::wostringstream out;out<<L"FPS "<<std::fixed<<std::setprecision(1)<<rate<<L"   "<<std::setprecision(2)<<milliseconds<<L" ms";return out.str();}
};
