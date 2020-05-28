#version 330

out vec2 v_texcoord; // pass the texCoord if needed
/*out vec3 v_normal;*/
out vec3 v_worldPosition;
out vec2 v_lm_texcoord;

in vec4 position;
in vec4 color;
/*in vec4 normal;*/
in vec2 texcoord;
layout (location = 9) in vec2 t_texcoord; // packed triangle UVs

// these are passed in from OF programmable renderer
uniform mat4 modelViewMatrix;
uniform mat4 modelMatrix;
uniform mat4 viewMatrix;
uniform mat4 textureMatrix;
uniform mat4 projectionMatrix;
uniform mat4 modelViewProjectionMatrix;
uniform mat4 normalMatrix;


void main (void) {
    /*v_normal = normal.xyz;*/
    v_worldPosition = (modelMatrix * position).xyz;

    /*v_texcoord = (textureMatrix*vec4(texcoord.x,texcoord.y,0,1)).xy;*/
    v_texcoord = texcoord;
    v_lm_texcoord = t_texcoord;
    gl_Position = modelViewProjectionMatrix * position;
}
