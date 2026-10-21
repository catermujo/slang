//TEST:SIMPLE:-target dxbc -profile sm_5_0 -entry main -stage compute -o fxc-short-circuit.dxbc.actual
//TEST:SIMPLE:-target dxil -profile sm_6_0 -entry main -stage compute -o fxc-short-circuit.dxil.actual
//TEST:SIMPLE:-target spirv -profile spirv_1_5 -entry main -stage compute -o fxc-short-circuit.spv.actual
//TEST:SIMPLE(filecheck=SM5):-target hlsl -profile sm_5_0 -entry main -stage compute
//TEST:SIMPLE(filecheck=SM6):-target hlsl -profile sm_6_0 -entry main -stage compute

// SM5-COUNT-2: [branch]
// SM6-NOT: [branch]
// SM6: void main(
// SM6-NOT: [branch]

[[vk::binding(0, 1)]] RWStructuredBuffer<uint> inputBuffer : register(u0);
[[vk::binding(1, 1)]] RWStructuredBuffer<uint> outputBuffer : register(u1);
[[vk::binding(0, 0)]] cbuffer Params : register(b0)
{
    uint mode;
    uint index;
    uint count;
};

bool enabled()
{
    if (mode == 0xffffffffu)
        return true;
    return inputBuffer[index] != 0u;
}

uint blockCount()
{
    return (count + 63u) / 64u;
}

[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (mode == 1u && !enabled())
        return;
    if (id.x < 256u)
        outputBuffer[id.x] = 0u;
    else if (mode != 2u && id.x < 256u * (1u + blockCount()))
        outputBuffer[id.x] = 1u;
}

