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
uniform vec3 u_CapsuleMins, u_CapsuleMaxs;
uniform vec4 u_SSSParams; // strength, softness, range, surface-normal mode
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
    // Derivatives must precede the per-fragment exit. The bounds include the full penumbra.
    if (any(lessThan(p, u_CapsuleMins)) || any(greaterThan(p, u_CapsuleMaxs)))
    { out_Color = vec4(1.0); return; }
    vec3 n = crossN * inversesqrt(max(dot(crossN, crossN), 1e-12));
    if (dot(n, u_ViewOrigin - p) < 0.0) n = -n;
    float receiver = u_SSSParams.w > 0.5 ? 1.0 : smoothstep(0.4, 0.8, n.z);
    vec3 projection = u_SSSParams.w > 0.5 ? n : vec3(0.0, 0.0, 1.0);
    if (texture(u_ScreenDepthMap, var_Tex).r >= 1.0 || receiver <= 0.0) { out_Color = vec4(1.0); return; }
    float occlusion = 0.0;
    for (int i = 0; i < 12; ++i)
    {
        float radius = u_CapsuleA[i].w;
        if (radius <= 0.0) continue;
        vec3 axis = u_CapsuleB[i].xyz - u_CapsuleA[i].xyz;
        float t = clamp(dot(p - u_CapsuleA[i].xyz, axis) * u_CapsuleB[i].w, 0.0, 1.0);
        vec3 delta = u_CapsuleA[i].xyz + t * axis - p;
        float height = dot(delta, projection);
        if (u_SSSParams.w > 0.5 && height < 0.0) continue;
        if (height < -radius || height > u_SSSParams.z) continue;
        float inner = radius * max(0.0, 1.0 - 0.7 * u_SSSParams.y);
        float outer = radius * (1.0 + u_SSSParams.y) + max(height, 0.0) * 0.6 * u_SSSParams.y;
        vec3 radial = delta - height * projection;
        float distanceSquared = dot(radial, radial);
        if (distanceSquared >= max(inner + 0.01, outer) * max(inner + 0.01, outer)) continue;
        float distanceToAxis = sqrt(distanceSquared);
        float shadow = 1.0 - smoothstep(inner, max(inner + 0.01, outer), distanceToAxis);
        shadow *= 1.0 - smoothstep(u_SSSParams.z * 0.25, u_SSSParams.z, max(height, 0.0));
        occlusion = max(occlusion, shadow);
    }
    out_Color = vec4(vec3(1.0 - u_SSSParams.x * occlusion * receiver), 1.0);
}
