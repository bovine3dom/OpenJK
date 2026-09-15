/*[Vertex]*/
out vec2 var_Tex;
void main()
{
    vec2 p = vec2(gl_VertexID == 2 ? 3.0 : -1.0, gl_VertexID == 1 ? 3.0 : -1.0);
    gl_Position = vec4(p, 0.0, 1.0);
    var_Tex = p * 0.5 + 0.5;
}
/*[Fragment]*/
uniform sampler2D u_ScreenDepthMap;
uniform vec4 u_ViewInfo;
uniform vec3 u_ViewOrigin, u_ViewForward, u_ViewLeft, u_ViewUp;
uniform vec4 u_CapsuleA[12], u_CapsuleB[12];
uniform vec4 u_SSSParams;
in vec2 var_Tex;
out vec4 out_Color;
vec3 positionAt(vec2 uv)
{
    float z = u_ViewInfo.y / mix(u_ViewInfo.x, 1.0, texture(u_ScreenDepthMap, uv).r);
    return u_ViewOrigin + z * (u_ViewForward - (uv.x * 2.0 - 1.0) * u_ViewInfo.z * u_ViewLeft
        + (uv.y * 2.0 - 1.0) * u_ViewInfo.w * u_ViewUp);
}
void main()
{
    vec3 p = positionAt(var_Tex);
    vec3 crossN = cross(dFdx(p), dFdy(p));
    vec3 n = crossN * inversesqrt(max(dot(crossN, crossN), 1e-12));
    if (dot(n, u_ViewOrigin - p) < 0.0) n = -n;
    if (texture(u_ScreenDepthMap, var_Tex).r >= 1.0 || n.z < 0.4) { out_Color = vec4(1.0); return; }
    float occlusion = 0.0;
    for (int i = 0; i < 12; ++i)
    {
        float radius = u_CapsuleA[i].w;
        if (radius <= 0.0) continue;
        vec3 axis = u_CapsuleB[i].xyz - u_CapsuleA[i].xyz;
        float t = clamp(dot(p - u_CapsuleA[i].xyz, axis) / max(dot(axis, axis), 0.001), 0.0, 1.0);
        vec3 delta = u_CapsuleA[i].xyz + t * axis - p;
        if (delta.z < -radius || delta.z > 64.0) continue;
        float penumbra = radius + max(delta.z, 0.0) * 0.6;
        float shadow = 1.0 - smoothstep(radius * 0.3, penumbra + radius, length(delta.xy));
        shadow *= 1.0 - smoothstep(16.0, 64.0, max(delta.z, 0.0));
        occlusion = max(occlusion, shadow);
    }
    out_Color = vec4(vec3(1.0 - u_SSSParams.x * occlusion * smoothstep(0.4, 0.8, n.z)), 1.0);
}
