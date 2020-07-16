#version 330

in vec2 v_texcoord; // pass the texCoord if needed
/*in vec3 v_normal;*/
in vec3 v_worldPosition;
in vec2 v_lm_texcoord;

out vec4 outColor;

uniform sampler2D tex0;
uniform sampler2D tex1;

uniform vec4 mat_specular;
uniform vec4 mat_emissive;
/*uniform vec4 global_ambient;*/

// these are passed in from OF programmable renderer
uniform mat4 modelViewMatrix;
uniform mat4 projectionMatrix;
uniform mat4 textureMatrix;
uniform mat4 modelViewProjectionMatrix;


void main (void) {
    vec4 tex = texture(tex0, v_texcoord);
    vec4 lmTex = texture(tex1, v_lm_texcoord);
    vec4 localColor = tex * lmTex;

    outColor = clamp( localColor, 0.0, 1.0 );
}
