$input a_position, a_texcoord0, a_color0
$output v_texcoord0, v_color0, v_fog
#include <bgfx_shader.sh>
uniform vec4 u_screen;
void main(){
 float z=a_position.z;
 gl_Position=vec4((a_position.x*2.0/u_screen.x-1.0)*z,(1.0-a_position.y*2.0/u_screen.y)*z,z-32.0,z);
 v_texcoord0=a_texcoord0;
 v_color0=a_color0;
 v_fog=vec2(z,u_screen.z);
}
