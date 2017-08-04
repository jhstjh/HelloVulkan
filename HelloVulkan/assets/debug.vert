#version 450 

layout(binding = 0) uniform UniformBufferObject {
	mat4 gViewProj;
};

out gl_PerVertex {
    vec4 gl_Position;
};

layout(location = 0) out vec4 fragColor;

void main() 
{
	const vec4 posW[] = 
	{
		vec4(-5.f, 0.f, 5.f, 1.f),
		vec4(-5.f, 0.f, -5.f, 1.f),
		vec4(-4.f, 0.f, 5.f, 1.f),
		vec4(-4.f, 0.f, -5.f, 1.f),
		vec4(-3.f, 0.f, 5.f, 1.f),
		vec4(-3.f, 0.f, -5.f, 1.f),
		vec4(-2.f, 0.f, 5.f, 1.f),
		vec4(-2.f, 0.f, -5.f, 1.f),
		vec4(-1.f, 0.f, 5.f, 1.f),
		vec4(-1.f, 0.f, -5.f, 1.f),
		vec4(0.f, 0.f, 5.f, 1.f),
		vec4(0.f, 0.f, -5.f, 1.f),
		vec4(1.f, 0.f, 5.f, 1.f),
		vec4(1.f, 0.f, -5.f, 1.f),
		vec4(2.f, 0.f, 5.f, 1.f),
		vec4(2.f, 0.f, -5.f, 1.f),
		vec4(3.f, 0.f, 5.f, 1.f),
		vec4(3.f, 0.f, -5.f, 1.f),
		vec4(4.f, 0.f, 5.f, 1.f),
		vec4(4.f, 0.f, -5.f, 1.f),
		vec4(5.f, 0.f, 5.f, 1.f),
		vec4(5.f, 0.f, -5.f, 1.f),
				
		vec4(5.f, 0.f, -5.f, 1.f),
		vec4(-5.f, 0.f, -5.f, 1.f),
		vec4(5.f, 0.f, -4.f, 1.f),
		vec4(-5.f, 0.f, -4.f, 1.f),
		vec4(5.f, 0.f, -3.f, 1.f),
		vec4(-5.f, 0.f, -3.f, 1.f),
		vec4(5.f, 0.f, -2.f, 1.f),
		vec4(-5.f, 0.f, -2.f, 1.f),
		vec4(5.f, 0.f, -1.f, 1.f),
		vec4(-5.f, 0.f, -1.f, 1.f),
		vec4(5.f, 0.f, 0.f, 1.f),
		vec4(-5.f, 0.f, 0.f, 1.f),
		vec4(5.f, 0.f, 1.f, 1.f),
		vec4(-5.f, 0.f, 1.f, 1.f),
		vec4(5.f, 0.f, 2.f, 1.f),
		vec4(-5.f, 0.f, 2.f, 1.f),
		vec4(5.f, 0.f, 3.f, 1.f),
		vec4(-5.f, 0.f, 3.f, 1.f),
		vec4(5.f, 0.f, 4.f, 1.f),
		vec4(-5.f, 0.f, 4.f, 1.f),
		vec4(5.f, 0.f, 5.f, 1.f),
		vec4(-5.f, 0.f, 5.f, 1.f),
		
		vec4(5.f, 0.f, 0.f, 1.f),
		vec4(0.f, 0.f, 0.f, 1.f),
		vec4(0.f, 5.f, 0.f, 1.f),
		vec4(0.f, 0.f, 0.f, 1.f),
		vec4(0.f, 0.f, 5.f, 1.f),
		vec4(0.f, 0.f, 0.f, 1.f),
	};

	gl_Position = gViewProj * posW[gl_VertexIndex];
	fragColor = vec4(0.5f, 0.5f, 0.5f, 1.f);
	
	// I know this shader is soo bad..
	if (gl_VertexIndex >= 44)
	{
		const vec4 color[] = 
		{
			vec4(1.f, 0.f, 0.f, 1.f),
			vec4(0.f, 1.f, 0.f, 1.f),
			vec4(0.f, 0.f, 1.f, 1.f)
		};
		fragColor = color[(gl_VertexIndex - 44) >> 1];
	}
}