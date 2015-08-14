uniform vec4 u_color;

varying vec2 v_pos;

void main() {
    float dist = length(v_pos - gl_FragCoord.xy);
    float alpha = smoothstep(0.0, 1.0, dist);
    gl_FragColor = u_color * alpha;
    //gl_FragColor.a = 1.0;
    //gl_FragColor.rgb = u_color.rgb;
    //gl_FragColor = vec4(alpha, alpha, alpha, 1.0);
    //gl_FragColor = vec4(0.0, 0.0, 0.0, 0.1);
    gl_FragColor = vec4(1.0, 1.0, 0.0, 1.0);
}
