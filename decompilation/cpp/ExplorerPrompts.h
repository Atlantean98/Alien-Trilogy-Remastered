#pragma once
#include "ExplorerPresentation.h"
#include <vector>
struct ExplorerPrompt {unsigned button;std::wstring action;};
inline std::wstring explorerButtonName(unsigned id,bool ps){
 static const wchar_t* xbox[]={L"A",L"B",L"X",L"Y",L"View",L"Guide",L"Menu",L"Left stick",L"Right stick",L"LB",L"RB",L"D-pad up",L"D-pad down",L"D-pad left",L"D-pad right"};
 static const wchar_t* sony[]={L"Cross",L"Circle",L"Square",L"Triangle",L"Share",L"PS",L"Options",L"L3",L"R3",L"L1",L"R1",L"D-pad up",L"D-pad down",L"D-pad left",L"D-pad right"};
 if(id<15)return ps?sony[id]:xbox[id];if(id==15)return ps?L"Mic":L"Share";if(id==20)return L"Touchpad";if(id==32)return ps?L"L2":L"LT";if(id==33)return ps?L"R2":L"RT";return L"Button "+std::to_wstring(id+1);
}
inline void explorerDrawPrompts(HDC dc,const ExplorerFont& font,const std::vector<ExplorerPrompt>& prompts,bool ps,int width,int top,double scale){
 auto iconWidth=[&](unsigned id){return id<4||id==100?int(20*scale):std::max(int(20*scale),font.width(explorerButtonName(id,ps),scale*.8)+int(8*scale));};
 int total=0;for(auto& p:prompts)total+=iconWidth(p.button)+font.width(p.action,scale)+int(22*scale);if(total>width-16){scale*=double(width-16)/total;total=width-16;}int x=(width-total)/2;
 for(auto& p:prompts){int size=std::max(10,int(20*scale)),iw=iconWidth(p.button),pad=std::max(2,int(4*scale));COLORREF color=RGB(220,240,225);if(p.button<4){static COLORREF xc[]={RGB(90,240,100),RGB(255,100,100),RGB(100,170,255),RGB(255,220,70)},pc[]={RGB(130,185,255),RGB(255,120,135),RGB(245,150,210),RGB(110,240,180)};color=ps?pc[p.button]:xc[p.button];}
 auto pen=CreatePen(PS_SOLID,std::max(1,int(scale*1.5)),color);auto oldPen=SelectObject(dc,pen);auto oldBrush=SelectObject(dc,GetStockObject(NULL_BRUSH));
 if(p.button<4){if(ps){int l=x+pad,r=x+size-pad,t=top+pad,b=top+size-pad;if(p.button==0){MoveToEx(dc,l,t,nullptr);LineTo(dc,r,b);MoveToEx(dc,r,t,nullptr);LineTo(dc,l,b);}else if(p.button==1)Ellipse(dc,l,t,r,b);else if(p.button==2)Rectangle(dc,l,t,r,b);else{POINT points[]={{x+size/2,t},{r,b},{l,b},{x+size/2,t}};Polyline(dc,points,4);}}else{Ellipse(dc,x,top,x+size,top+size);auto text=explorerButtonName(p.button,false);font.draw(dc,text,x+(size-font.width(text,scale*.8))/2,top+int(3*scale),scale*.8,GetRValue(color)*65536+GetGValue(color)*256+GetBValue(color));}}
 else if(p.button==100){int centerX=x+size/2,centerY=top+size/2;MoveToEx(dc,centerX,top+pad,nullptr);LineTo(dc,centerX,top+size-pad);MoveToEx(dc,x+pad,centerY,nullptr);LineTo(dc,x+size-pad,centerY);}
 else{RoundRect(dc,x,top,x+iw,top+size,pad,pad);font.draw(dc,explorerButtonName(p.button,ps),x+pad,top+int(3*scale),scale*.8,0xe0f0e0);}
 SelectObject(dc,oldBrush);SelectObject(dc,oldPen);DeleteObject(pen);font.draw(dc,p.action,x+iw+int(6*scale),top+int(2*scale),scale,0xe8ffe8);x+=iw+font.width(p.action,scale)+int(22*scale);
 }
}
