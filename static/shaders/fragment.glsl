precision mediump float;
varying vec4 v_color;
uniform float u_brightness;

void main() {
    vec4 col = v_color;
    col.rgb *= u_brightness;
    gl_FragColor = col;
}
