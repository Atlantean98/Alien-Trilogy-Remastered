$input v_texcoord0, v_color0, v_fog
#include <bgfx_shader.sh>
SAMPLER2D(s_tex,0);
void main(){if(texture2D(s_tex,v_texcoord0).a<0.5)discard;float depth=clamp(v_fog.x/65536.0,0.0,0.9999847);vec3 encoded=fract(depth*vec3(1.0,255.0,65025.0));encoded-=encoded.yzz*vec3(1.0/255.0,1.0/255.0,0.0);gl_FragColor=vec4(encoded,1.0);}
