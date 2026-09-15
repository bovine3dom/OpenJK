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
uniform vec4 u_SSSParams; // filter: direction XY/radius; composite: debug gain/unused/strength; W selects mode
uniform vec4 u_ViewInfo; // far/near, far, tan(fovX/2), tan(fovY/2)
in vec2 var_Tex;
out vec4 out_Color;
void main()
{
    vec4 raw = texture(u_NormalMap, var_Tex);
    int mode = int(u_SSSParams.w + 0.5);
    if (mode == 2) { out_Color = vec4(vec3(raw.a), 1.0); return; }
    vec4 center = texture(u_ScreenImageMap, var_Tex);
    if (mode == 3 || mode == 4)
    {
        vec3 irradiance = max(mode == 3 ? raw.rgb : center.rgb, vec3(0.0));
        out_Color = vec4(irradiance / (1.0 + irradiance), 1.0);
        return;
    }
    if (mode != 0)
    {
        vec4 scene = texture(u_ScreenDepthMap, var_Tex);
        vec3 change = (center.rgb - raw.rgb) * texture(u_DiffuseMap, var_Tex).rgb * raw.a * u_SSSParams.z;
        vec3 scattered = max(scene.rgb + change, vec3(0.0));
        if (mode == 5)
            out_Color = vec4(clamp(abs(scattered - scene.rgb) * u_SSSParams.x, 0.0, 1.0), 1.0);
        else
            out_Color = vec4(mode == 6 && var_Tex.x < 0.5 ? scene.rgb : scattered, scene.a);
        return;
    }
    if (u_SSSParams.z <= 0.0) { out_Color = center; return; }
    if (center.a < 0.5) { out_Color = vec4(0.0); return; }
    float depth = texture(u_ScreenDepthMap, var_Tex).r;
    float z = u_ViewInfo.y / mix(u_ViewInfo.x, 1.0, depth);
    vec2 stepUV = u_SSSParams.xy * u_SSSParams.z / (2.0 * z * u_ViewInfo.zw);
    vec3 sum = center.rgb;
    vec3 total = vec3(1.0);
    int steps = u_SSSParams.z > 1.0 ? 8 : 4;
    for (int i = -8; i <= 8; ++i)
    {
        if (i == 0 || abs(i) > steps) continue;
        float position = float(i) / float(steps);
        vec2 uv = var_Tex + stepUV * position;
        vec4 tap = texture(u_ScreenImageMap, uv);
        float sampleZ = u_ViewInfo.y / mix(u_ViewInfo.x, 1.0, texture(u_ScreenDepthMap, uv).r);
        if (tap.a < 0.99 || abs(sampleZ - z) > u_SSSParams.z) continue;
        // Wider red diffusion than green/blue. Filter irradiance, not painted albedo.
        vec3 weight = exp(-(position * position * 16.0) / vec3(8.0, 3.0, 1.5));
        sum += tap.rgb * weight;
        total += weight;
    }
    out_Color = vec4(sum / total, center.a);
}
