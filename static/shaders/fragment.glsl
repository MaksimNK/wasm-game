precision mediump float;
varying vec4 v_color;
varying vec2 v_displacement;
uniform float u_brightness;

void main() {
    vec4 col = v_color;
    
    float ab = length(v_displacement) * 7.0;
    col.r = col.r + ab * 0.07;
    col.g = col.g + ab * 0.01;
    col.b = col.b - ab * 0.04;
    
    col.rgb *= u_brightness;
    gl_FragColor = col;
}
