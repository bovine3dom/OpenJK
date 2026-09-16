/*[Vertex]*/
in vec3 attr_Position;
in uvec4 attr_BoneIndexes;
in vec4 attr_BoneWeights;
layout(std140) uniform Bones
{
    mat3x4 u_BoneMatrices[MAX_G2_BONES];
};
out vec3 var_Position;
mat4x3 GetBoneMatrix(uint index)
{
    mat3x4 bone = u_BoneMatrices[index];
    return mat4x3(bone[0].x, bone[1].x, bone[2].x,
                 bone[0].y, bone[1].y, bone[2].y,
                 bone[0].z, bone[1].z, bone[2].z,
                 bone[0].w, bone[1].w, bone[2].w);
}
void main()
{
    mat4x3 influence = GetBoneMatrix(attr_BoneIndexes[0]) * attr_BoneWeights[0]
                    + GetBoneMatrix(attr_BoneIndexes[1]) * attr_BoneWeights[1]
                    + GetBoneMatrix(attr_BoneIndexes[2]) * attr_BoneWeights[2]
                    + GetBoneMatrix(attr_BoneIndexes[3]) * attr_BoneWeights[3];
    var_Position = influence * vec4(attr_Position, 1.0);
    gl_Position = vec4(var_Position, 1.0);
}
