attribute vec2 a_pos;
attribute vec4 a_color;
varying vec4 v_color;
uniform mat4 u_projection;
uniform vec2 u_screen_center;
uniform float u_fisheye;

void main() {
    vec2 pos = a_pos;
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
}
