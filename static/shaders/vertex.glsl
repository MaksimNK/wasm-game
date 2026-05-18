#define MAX_RIPPLES 64

attribute vec2 a_pos;
attribute vec4 a_color;
varying vec4 v_color;
varying vec2 v_displacement;
uniform mat4 u_projection;
uniform vec2 u_screen_center;
uniform float u_fisheye;

uniform vec4 u_ripples[MAX_RIPPLES];
uniform float u_ripple_time[MAX_RIPPLES];
uniform float u_ripple_strength[MAX_RIPPLES];
uniform int u_ripple_count;

void main() {
    vec2 pos = a_pos;
    vec2 displacement = vec2(0.0);
    
    for (int i = 0; i < MAX_RIPPLES; i++) {
        if (i >= u_ripple_count) break;
        
        vec2 ripple_pos = u_ripples[i].xy;
        vec2 ripple_dir = u_ripples[i].zw;
        float t = u_ripple_time[i];
        float strength = u_ripple_strength[i];
        
        vec2 diff = pos - ripple_pos;
        float dist = length(diff);
        
        float radius = 250.0;
        if (dist > radius || dist < 0.001) continue;
        
        float wave = tan(sin(dist * 0.07 - t * 5.0));
        float falloff = (1.0 - dist / radius) * exp(-t * 7.0);
        
        vec2 disp = ripple_dir * wave * strength * falloff;
        pos += disp;
        displacement += disp;
    }
    
    vec2 centered = pos - u_screen_center;
    float dist = length(centered);
    float max_dist = length(u_screen_center);
    float norm_dist = dist / max_dist;
    
    float a = u_fisheye;
    float new_norm = norm_dist * (1.0 + a) / (1.0 + a * norm_dist);
    vec2 new_centered = centered / (new_norm / max(norm_dist, 0.0001));
    pos = u_screen_center + new_centered;
    
    gl_Position = u_projection * vec4(pos, 0.0, 1.0);
    v_color = a_color;
    v_displacement = displacement;
}
