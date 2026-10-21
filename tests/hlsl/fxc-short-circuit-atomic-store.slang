//TEST:SIMPLE:-target dxbc -profile sm_5_0 -entry main -stage compute -o fxc-conditional-atomic.dxbc.actual
//TEST:SIMPLE:-target dxil -profile sm_6_0 -entry main -stage compute -o fxc-conditional-atomic.dxil.actual
//TEST:SIMPLE:-target spirv -profile spirv_1_5 -entry main -stage compute -fvk-u-shift 1 0 -fvk-b-shift 0 0 -o fxc-conditional-atomic.spv.actual

RWStructuredBuffer<uint> input_buffer : register(u0);
RWStructuredBuffer<Atomic<uint>> output_buffer : register(u1);
cbuffer Params : register(b0)
{
    uint mode;
    uint index;
};

// DUMBAI: Same-line statements reproduce FXC's line-sensitive flattening failure.
bool enabled()
{
    if (mode == 0xffffffffu) return true;
    return input_buffer[index] != 0u;
}

[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (mode == 1u && !enabled()) return;
    if (id.x < 256u) output_buffer[id.x].store(0u, MemoryOrder.Relaxed);
}
