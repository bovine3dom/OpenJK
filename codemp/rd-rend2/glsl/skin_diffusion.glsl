/*[Vertex]*/
out vec2 var_Tex;
void main()
{
    vec2 p = vec2(gl_VertexID == 2 ? 3.0 : -1.0, gl_VertexID == 1 ? 3.0 : -1.0);
    gl_Position = vec4(p, 0.0, 1.0);
    var_Tex = p * 0.5 + 0.5;
}
/*[Fragment]*/
uniform sampler2D u_ScreenImageMap, u_ScreenDepthMap, u_DiffuseMap, u_NormalMap;
uniform vec4 u_SSSParams; // direction XY, world radius, mode: 0 filter, 1 composite, 2 mask
uniform vec4 u_ViewInfo; // far/near, far, tan(fovX/2), tan(fovY/2)
in vec2 var_Tex;
out vec4 out_Color;
void main()
{
    vec4 raw = texture(u_NormalMap, var_Tex);
    if (u_SSSParams.w > 1.5) { out_Color = vec4(vec3(raw.a), 1.0); return; }
    vec4 center = texture(u_ScreenImageMap, var_Tex);
    if (u_SSSParams.w > 0.5)
    {
        vec4 scene = texture(u_ScreenDepthMap, var_Tex);
        out_Color = vec4(scene.rgb + (center.rgb - raw.rgb) * texture(u_DiffuseMap, var_Tex).rgb * raw.a * u_SSSParams.z, scene.a);
        return;
    }
    if (center.a < 0.5) { out_Color = vec4(0.0); return; }
    float depth = texture(u_ScreenDepthMap, var_Tex).r;
    float z = u_ViewInfo.y / mix(u_ViewInfo.x, 1.0, depth);
    vec2 stepUV = u_SSSParams.xy * u_SSSParams.z / (2.0 * z * u_ViewInfo.zw);
    vec3 sum = center.rgb;
    vec3 total = vec3(1.0);
    for (int i = -4; i <= 4; ++i)
    {
        if (i == 0) continue;
        vec2 uv = var_Tex + stepUV * (float(i) / 4.0);
        vec4 tap = texture(u_ScreenImageMap, uv);
        float sampleZ = u_ViewInfo.y / mix(u_ViewInfo.x, 1.0, texture(u_ScreenDepthMap, uv).r);
        if (tap.a < 0.99 || abs(sampleZ - z) > u_SSSParams.z) continue;
        // Wider red diffusion than green/blue. Filter irradiance, not painted albedo.
        vec3 weight = exp(-float(i * i) / vec3(8.0, 3.0, 1.5));
        sum += tap.rgb * weight;
        total += weight;
    }
    out_Color = vec4(sum / total, center.a);
}
