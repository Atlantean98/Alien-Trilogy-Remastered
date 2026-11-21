$input v_texcoord0, v_color0, v_fog
#include <bgfx_shader.sh>
SAMPLER2D(s_tex,0);
SAMPLER2D(s_photoDepth,1);
uniform vec4 u_photoDof;
float readDepth(vec2 uv){return min(65535.0,dot(texture2DLod(s_photoDepth,uv,0.0).rgb,vec3(1.0,1.0/255.0,1.0/65025.0))*65536.0);}
void main(){vec2 uv=v_texcoord0;float depth=readDepth(uv),focus=readDepth(vec2(0.5,0.5));float radius=u_photoDof.z*14.0*u_photoDof.w/720.0*clamp(abs(depth-focus)/max(max(depth,focus),256.0)*2.0-0.04,0.0,1.0);vec3 color=texture2DLod(s_tex,uv,0.0).rgb;float weight=1.0;if(radius>=0.5){for(int i=0;i<16;++i){float angle=float(i)*2.39996323;float distance=sqrt((float(i)+0.5)/16.0);vec2 sampleUv=uv+vec2(cos(angle),sin(angle))*distance*radius*u_photoDof.xy;if(readDepth(sampleUv)<depth*0.85)continue;color+=texture2DLod(s_tex,sampleUv,0.0).rgb;weight+=1.0;}}gl_FragColor=vec4(color/weight,1.0);}
