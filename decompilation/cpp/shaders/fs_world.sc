$input v_texcoord0, v_color0, v_fog
#include <bgfx_shader.sh>
SAMPLER2D(s_tex,0);
uniform vec4 u_screen;
void main(){
 vec4 t=texture2D(s_tex,v_texcoord0);
 if(t.a<0.5)discard;
 float fog=(255.0-clamp(floor(v_fog.x/12.0)-170.0,0.0,255.0))/256.0;
 vec3 c=t.rgb*v_color0.rgb*mix(1.0,fog,v_fog.y);
 float filterMode=u_screen.w;
 if(filterMode>0.5){
  float lum=clamp(dot(c,vec3(0.299,0.587,0.114)),0.0,1.0);
  float lift=pow(lum,0.72);
  if(filterMode<1.5){
   float value=clamp((lift-0.38)*1.85+0.35,0.0,1.0);c=vec3(value,value,value);
  }else if(filterMode<2.5){
   float value=clamp(pow(lum,0.58)*1.35,0.0,1.0);c=vec3(value*0.035,value,value*0.12);
  }else if(filterMode<3.5){
   float value=clamp((lift-0.32)*1.4+0.35,0.0,1.0);c=vec3(value*0.30,value*0.82,min(1.0,value*1.05+0.035));
  }else if(filterMode<4.5){
   float value=clamp(lift*1.18,0.0,1.0);c=vec3(min(1.0,value*1.30),value*0.64,value*0.055);
  }else if(filterMode<5.5){
   float value=clamp((lift-0.3)*1.25+0.3,0.0,1.0);c=vec3(min(1.0,value*1.18),value*0.85,value*0.48);
  }else if(filterMode<6.5){
   float heat=pow(lum,0.65)*5.0;
   if(heat<1.0)c=mix(vec3(0.005,0.005,0.06),vec3(0.10,0.025,0.48),heat);
   else if(heat<2.0)c=mix(vec3(0.10,0.025,0.48),vec3(0.72,0.025,0.25),heat-1.0);
   else if(heat<3.0)c=mix(vec3(0.72,0.025,0.25),vec3(1.0,0.18,0.015),heat-2.0);
   else if(heat<4.0)c=mix(vec3(1.0,0.18,0.015),vec3(1.0,0.78,0.02),heat-3.0);
   else c=mix(vec3(1.0,0.78,0.02),vec3(1.0,1.0,0.90),min(1.0,heat-4.0));
  }
 }
 gl_FragColor=vec4(c,t.a*v_color0.a);
}
