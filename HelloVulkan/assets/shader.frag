#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) in vec3 worldNormal;
layout(location = 3) in vec4 fragShadowTransform;

layout(location = 0) out vec4 outColor;

layout(binding = 1) uniform sampler2D texSampler;
layout(binding = 2) uniform sampler2D texShadowSampler;

void main() {
    float shadowFactor = 1.0f;

    vec4 shadowTrans = fragShadowTransform;
	shadowTrans.xyz /= shadowTrans.w;

    float epsilon = 0.003;
    float shadow = texture(texShadowSampler, shadowTrans.xy).r;
    shadow += epsilon;
    if (shadow  < shadowTrans.z) {
    	shadowFactor = 0.1f;
    }

    outColor = shadowFactor * texture(texSampler, fragTexCoord) * max(0.f, dot(normalize(vec3(2.f, 2.f, -2.f)), worldNormal));
}