//TEST:SIMPLE: -target dxil -entry main -stage compute -profile sm_6_0
//TEST:SIMPLE: -target dxil-assembly -entry main -stage compute -profile sm_6_0
//TEST:SIMPLE(filecheck=HLSL): -target hlsl -entry main -stage compute -profile sm_6_0

struct Parameters
{
    float2 offset;
};

StructuredBuffer<float2> input;
RWStructuredBuffer<float2> output;

// HLSL: [noinline]
[noinline]
float2 displace(Parameters params, float2 point)
{
    return point + params.offset;
}

[shader("compute")]
[numthreads(1, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    Parameters params;
    params.offset = input[id.x];
    output[id.x] = displace(params, input[id.x + 1]);
}
