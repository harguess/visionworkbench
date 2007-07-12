
uniform sampler2DRect i1;
uniform float f1;

void main() {
   gl_FragColor.rgba = pow(vec4(f1), texture2DRect(i1, gl_TexCoord[0].st));
}

