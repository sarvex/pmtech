// renderer.cpp
// Copyright 2014 - 2023 Alex Dixon.
// License: https://github.com/polymonster/pmtech/blob/master/license.md

#include "renderer.h"
#include "renderer_shared.h"

#include "console.h"
#include "data_struct.h"
#include "memory.h"
#include "pen.h"
#include "pen_string.h"
#include "threads.h"
#include "timer.h"

#include "str/Str.h"

#include <d3d11_1.h>
#include <stdlib.h>
#include <windows.h>

extern pen::window_creation_params pen_window;

// level 0 = no errors, level 1 = print errors, level 2 = assert on error
#define D3D_DEBUG_LEVEL 2
#if D3D_DEBUG_LEVEL > 0
#include <comdef.h>
void check_d3d_error(HRESULT hr)
{
    if (FAILED(hr))
    {
        _com_error err(hr);
        LPCTSTR    errMsg = err.ErrorMessage();
        printf("d3d device error: %ls\n", errMsg);
    }

#if D3D_DEBUG_LEVEL > 1
    PEN_ASSERT(hr == 0);
#endif
}
#define CHECK_CALL(C)                                                                                                        \
    {                                                                                                                        \
        HRESULT ghr = C;                                                                                                     \
        check_d3d_error(ghr);                                                                                                \
    }
#else
#define CHECK_CALL(C) C
#endif

namespace
{
    D3D_DRIVER_TYPE         s_driverType = D3D_DRIVER_TYPE_HARDWARE;
    D3D_FEATURE_LEVEL       s_featureLevel = D3D_FEATURE_LEVEL_11_0;
    ID3D11Device*           s_device = nullptr;
    ID3D11Device1*          s_device_1 = nullptr;
    IDXGISwapChain*         s_swap_chain = nullptr;
    IDXGISwapChain1*        s_swap_chain_1 = nullptr;
    ID3D11RenderTargetView* s_backbuffer_rtv = nullptr;
    ID3D11DeviceContext*    s_immediate_context = nullptr;
    ID3D11DeviceContext1*   s_immediate_context_1 = nullptr;
    u64                     s_frame = 0; // to remove

    D3D11_FILL_MODE to_d3d11_fill_mode(u32 pen_fill_mode)
    {
        switch (pen_fill_mode)
        {
            case PEN_FILL_SOLID:
                return D3D11_FILL_SOLID;
            case PEN_FILL_WIREFRAME:
                return D3D11_FILL_WIREFRAME;
        }
        PEN_ASSERT(0);
        return D3D11_FILL_SOLID;
    }

    D3D11_CULL_MODE to_d3d11_cull_mode(u32 pen_cull_mode)
    {
        switch (pen_cull_mode)
        {
            case PEN_CULL_NONE:
                return D3D11_CULL_NONE;
            case PEN_CULL_FRONT:
                return D3D11_CULL_FRONT;
            case PEN_CULL_BACK:
                return D3D11_CULL_BACK;
        }
        PEN_ASSERT(0);
        return D3D11_CULL_NONE;
    }

    D3D11_USAGE to_d3d11_usage(u32 pen_usage)
    {
        switch (pen_usage)
        {
            case PEN_USAGE_DEFAULT:
                return D3D11_USAGE_DEFAULT;
            case PEN_USAGE_IMMUTABLE:
                return D3D11_USAGE_IMMUTABLE;
            case PEN_USAGE_DYNAMIC:
                return D3D11_USAGE_DYNAMIC;
            case PEN_USAGE_STAGING:
                return D3D11_USAGE_STAGING;
        }
        PEN_ASSERT(0);
        return D3D11_USAGE_DEFAULT;
    }

    UINT to_d3d11_bind_flags(u32 pen_bind_flags)
    {
        u32 bf = 0;
        if (pen_bind_flags & PEN_BIND_SHADER_RESOURCE)
            bf |= D3D11_BIND_SHADER_RESOURCE;
        if (pen_bind_flags & PEN_BIND_VERTEX_BUFFER)
            bf |= D3D11_BIND_VERTEX_BUFFER;
        if (pen_bind_flags & PEN_BIND_INDEX_BUFFER)
            bf |= D3D11_BIND_INDEX_BUFFER;
        if (pen_bind_flags & PEN_BIND_CONSTANT_BUFFER)
            bf |= D3D11_BIND_CONSTANT_BUFFER;
        if (pen_bind_flags & PEN_BIND_RENDER_TARGET)
            bf |= D3D11_BIND_RENDER_TARGET;
        if (pen_bind_flags & PEN_BIND_DEPTH_STENCIL)
            bf |= D3D11_BIND_DEPTH_STENCIL;
        if (pen_bind_flags & PEN_BIND_SHADER_WRITE)
            bf |= D3D11_BIND_UNORDERED_ACCESS;
        if (pen_bind_flags & PEN_STREAM_OUT_VERTEX_BUFFER)
            bf |= (D3D11_BIND_STREAM_OUTPUT | D3D11_BIND_VERTEX_BUFFER);
        return bf;
    }

    UINT to_d3d11_cpu_access_flags(u32 pen_access_flags)
    {
        u32 af = 0;
        if (pen_access_flags & PEN_CPU_ACCESS_WRITE)
            af |= D3D11_CPU_ACCESS_WRITE;
        if (pen_access_flags & PEN_CPU_ACCESS_READ)
            af |= D3D11_CPU_ACCESS_READ;
        return af;
    }

    D3D_PRIMITIVE_TOPOLOGY to_d3d11_primitive_topology(u32 pen_primitive_topology)
    {
        switch (pen_primitive_topology)
        {
            case PEN_PT_POINTLIST:
                return D3D11_PRIMITIVE_TOPOLOGY_POINTLIST;
            case PEN_PT_LINELIST:
                return D3D11_PRIMITIVE_TOPOLOGY_LINELIST;
            case PEN_PT_LINESTRIP:
                return D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP;
            case PEN_PT_TRIANGLELIST:
                return D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
            case PEN_PT_TRIANGLESTRIP:
                return D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
        }
        PEN_ASSERT(0);
        return D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    }

    DXGI_FORMAT to_d3d11_vertex_format(u32 pen_vertex_format)
    {
        switch (pen_vertex_format)
        {
            case PEN_VERTEX_FORMAT_FLOAT1:
                return DXGI_FORMAT_R32_FLOAT;
            case PEN_VERTEX_FORMAT_FLOAT2:
                return DXGI_FORMAT_R32G32_FLOAT;
            case PEN_VERTEX_FORMAT_FLOAT3:
                return DXGI_FORMAT_R32G32B32_FLOAT;
            case PEN_VERTEX_FORMAT_FLOAT4:
                return DXGI_FORMAT_R32G32B32A32_FLOAT;
            case PEN_VERTEX_FORMAT_UNORM1:
                return DXGI_FORMAT_R8_UNORM;
            case PEN_VERTEX_FORMAT_UNORM2:
                return DXGI_FORMAT_R8G8_UNORM;
            case PEN_VERTEX_FORMAT_UNORM4:
                return DXGI_FORMAT_R8G8B8A8_UNORM;
        }
        PEN_ASSERT(0);
        return DXGI_FORMAT_UNKNOWN;
    }

    DXGI_FORMAT to_d3d11_index_format(u32 pen_index_format)
    {
        switch (pen_index_format)
        {
            case PEN_FORMAT_R16_UINT:
                return DXGI_FORMAT_R16_UINT;
            case PEN_FORMAT_R32_UINT:
                return DXGI_FORMAT_R32_UINT;
        }
        PEN_ASSERT(0);
        return DXGI_FORMAT_UNKNOWN;
    }

    DXGI_FORMAT to_d3d11_texture_format(u32 pen_texture_format)
    {
        switch (pen_texture_format)
        {
            case PEN_TEX_FORMAT_BGRA8_UNORM:
                return DXGI_FORMAT_B8G8R8A8_UNORM;
            case PEN_TEX_FORMAT_RGBA8_UNORM:
                return DXGI_FORMAT_R8G8B8A8_UNORM;
            case PEN_TEX_FORMAT_D24_UNORM_S8_UINT:
                return DXGI_FORMAT_R24G8_TYPELESS;
            case PEN_TEX_FORMAT_D32_FLOAT:
                return DXGI_FORMAT_R32_TYPELESS;
            case PEN_TEX_FORMAT_D32_FLOAT_S8_UINT:
                return DXGI_FORMAT_R32G8X24_TYPELESS;
            case PEN_TEX_FORMAT_R32G32B32A32_FLOAT:
                return DXGI_FORMAT_R32G32B32A32_FLOAT;
            case PEN_TEX_FORMAT_R32_FLOAT:
                return DXGI_FORMAT_R32_FLOAT;
            case PEN_TEX_FORMAT_R16G16B16A16_FLOAT:
                return DXGI_FORMAT_R16G16B16A16_FLOAT;
            case PEN_TEX_FORMAT_R16_FLOAT:
                return DXGI_FORMAT_R16_FLOAT;
            case PEN_TEX_FORMAT_R32_UINT:
                return DXGI_FORMAT_R32_UINT;
            case PEN_TEX_FORMAT_R8_UNORM:
                return DXGI_FORMAT_R8_UNORM;
            case PEN_TEX_FORMAT_R32G32_FLOAT:
                return DXGI_FORMAT_R32G32_FLOAT;
            case PEN_TEX_FORMAT_BC1_UNORM:
                return DXGI_FORMAT_BC1_UNORM;
            case PEN_TEX_FORMAT_BC2_UNORM:
                return DXGI_FORMAT_BC2_UNORM;
            case PEN_TEX_FORMAT_BC3_UNORM:
                return DXGI_FORMAT_BC3_UNORM;
            case PEN_TEX_FORMAT_BC4_UNORM:
                return DXGI_FORMAT_BC4_UNORM;
            case PEN_TEX_FORMAT_BC5_UNORM:
                return DXGI_FORMAT_BC5_UNORM;
        }
        // unsupported / unimplemented texture type
        PEN_ASSERT(0);
        return DXGI_FORMAT_UNKNOWN;
    }

    D3D11_FILTER to_d3d11_filter_mode(u32 pen_filter_mode)
    {
        switch (pen_filter_mode)
        {
            case PEN_FILTER_MIN_MAG_MIP_POINT:
            case PEN_FILTER_POINT:
                return D3D11_FILTER_MIN_MAG_MIP_POINT;
            case PEN_FILTER_MIN_MAG_MIP_LINEAR:
            case PEN_FILTER_LINEAR:
                return D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        }
        PEN_ASSERT(0);
        return D3D11_FILTER_MIN_MAG_MIP_POINT;
    }

    D3D11_FILTER to_d3d11_comparison_filter_mode(u32 pen_filter_mode)
    {
        switch (pen_filter_mode)
        {
            case PEN_FILTER_MIN_MAG_MIP_POINT:
                return D3D11_FILTER_COMPARISON_MIN_MAG_MIP_POINT;
            case PEN_FILTER_MIN_MAG_MIP_LINEAR:
                return D3D11_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR;
        }
        PEN_ASSERT(0);
        return D3D11_FILTER_COMPARISON_MIN_MAG_MIP_POINT;
    }

    D3D11_TEXTURE_ADDRESS_MODE to_d3d11_texture_address_mode(u32 pen_texture_address_mode)
    {
        switch (pen_texture_address_mode)
        {
            case PEN_TEXTURE_ADDRESS_WRAP:
                return D3D11_TEXTURE_ADDRESS_WRAP;
            case PEN_TEXTURE_ADDRESS_MIRROR:
                return D3D11_TEXTURE_ADDRESS_MIRROR;
            case PEN_TEXTURE_ADDRESS_CLAMP:
                return D3D11_TEXTURE_ADDRESS_CLAMP;
            case PEN_TEXTURE_ADDRESS_BORDER:
                return D3D11_TEXTURE_ADDRESS_BORDER;
            case PEN_TEXTURE_ADDRESS_MIRROR_ONCE:
                return D3D11_TEXTURE_ADDRESS_MIRROR_ONCE;
        }
        PEN_ASSERT(0);
        return D3D11_TEXTURE_ADDRESS_WRAP;
    }

    D3D11_COMPARISON_FUNC to_d3d11_comparison(u32 pen_comparison)
    {
        switch (pen_comparison)
        {
            case PEN_COMPARISON_NEVER:
                return D3D11_COMPARISON_NEVER;
            case PEN_COMPARISON_LESS:
                return D3D11_COMPARISON_LESS;
            case PEN_COMPARISON_EQUAL:
                return D3D11_COMPARISON_EQUAL;
            case PEN_COMPARISON_LESS_EQUAL:
                return D3D11_COMPARISON_LESS_EQUAL;
            case PEN_COMPARISON_GREATER:
                return D3D11_COMPARISON_GREATER;
            case PEN_COMPARISON_NOT_EQUAL:
                return D3D11_COMPARISON_NOT_EQUAL;
            case PEN_COMPARISON_GREATER_EQUAL:
                return D3D11_COMPARISON_GREATER_EQUAL;
            case PEN_COMPARISON_ALWAYS:
                return D3D11_COMPARISON_ALWAYS;
        }
        PEN_ASSERT(0);
        return D3D11_COMPARISON_NEVER;
    }

    D3D11_STENCIL_OP to_d3d11_stencil_op(u32 pen_stencil_op)
    {
        switch (pen_stencil_op)
        {
            case PEN_STENCIL_OP_KEEP:
                return D3D11_STENCIL_OP_KEEP;
            case PEN_STENCIL_OP_REPLACE:
                return D3D11_STENCIL_OP_REPLACE;
            case PEN_STENCIL_OP_ZERO:
                return D3D11_STENCIL_OP_ZERO;
            case PEN_STENCIL_OP_DECR:
                return D3D11_STENCIL_OP_DECR;
            case PEN_STENCIL_OP_INCR:
                return D3D11_STENCIL_OP_INCR;
            case PEN_STENCIL_OP_DECR_SAT:
                return D3D11_STENCIL_OP_DECR_SAT;
            case PEN_STENCIL_OP_INCR_SAT:
                return D3D11_STENCIL_OP_INCR_SAT;
            case PEN_STENCIL_OP_INVERT:
                return D3D11_STENCIL_OP_INVERT;
        }
        PEN_ASSERT(0);
        return D3D11_STENCIL_OP_REPLACE;
    }

    D3D11_BLEND to_d3d11_blend_factor(u32 pen_blend_factor)
    {
        switch (pen_blend_factor)
        {
            case PEN_BLEND_ZERO:
                return D3D11_BLEND_ZERO;
            case PEN_BLEND_ONE:
                return D3D11_BLEND_ONE;
            case PEN_BLEND_SRC_COLOR:
                return D3D11_BLEND_SRC_COLOR;
            case PEN_BLEND_INV_SRC_COLOR:
                return D3D11_BLEND_INV_SRC_COLOR;
            case PEN_BLEND_SRC_ALPHA:
                return D3D11_BLEND_SRC_ALPHA;
            case PEN_BLEND_INV_SRC_ALPHA:
                return D3D11_BLEND_INV_SRC_ALPHA;
            case PEN_BLEND_DEST_ALPHA:
                return D3D11_BLEND_DEST_ALPHA;
            case PEN_BLEND_INV_DEST_ALPHA:
                return D3D11_BLEND_INV_DEST_ALPHA;
            case PEN_BLEND_INV_DEST_COLOR:
                return D3D11_BLEND_DEST_COLOR;
            case PEN_BLEND_SRC_ALPHA_SAT:
                return D3D11_BLEND_SRC_ALPHA_SAT;
            case PEN_BLEND_SRC1_COLOR:
                return D3D11_BLEND_SRC1_COLOR;
            case PEN_BLEND_INV_SRC1_COLOR:
                return D3D11_BLEND_INV_SRC1_COLOR;
            case PEN_BLEND_SRC1_ALPHA:
                return D3D11_BLEND_SRC1_ALPHA;
            case PEN_BLEND_INV_SRC1_ALPHA:
                return D3D11_BLEND_INV_SRC1_ALPHA;
            case PEN_BLEND_BLEND_FACTOR:
                return D3D11_BLEND_BLEND_FACTOR;
            case PEN_BLEND_INV_BLEND_FACTOR:
                return D3D11_BLEND_INV_BLEND_FACTOR;
        }
        PEN_ASSERT(0);
        return D3D11_BLEND_ZERO;
    }

    D3D11_BLEND_OP to_d3d11_blend_op(u32 pen_blend_op)
    {
        switch (pen_blend_op)
        {
            case PEN_BLEND_OP_ADD:
                return D3D11_BLEND_OP_ADD;
            case PEN_BLEND_OP_SUBTRACT:
                return D3D11_BLEND_OP_SUBTRACT;
            case PEN_BLEND_OP_REV_SUBTRACT:
                return D3D11_BLEND_OP_REV_SUBTRACT;
            case PEN_BLEND_OP_MIN:
                return D3D11_BLEND_OP_MIN;
            case PEN_BLEND_OP_MAX:
                return D3D11_BLEND_OP_MAX;
        }
        PEN_ASSERT(0);
        return D3D11_BLEND_OP_ADD;
    }

    u32 depth_texture_format_to_dsv_format(u32 tex_format)
    {
        switch (tex_format)
        {
            case DXGI_FORMAT_R16_TYPELESS:
                return DXGI_FORMAT_D16_UNORM;
            case DXGI_FORMAT_R32_TYPELESS:
                return DXGI_FORMAT_D32_FLOAT;
            case DXGI_FORMAT_R24G8_TYPELESS:
                return DXGI_FORMAT_D24_UNORM_S8_UINT;
            case DXGI_FORMAT_R32G8X24_TYPELESS:
                return DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
        }
        // unsupported depth texture type
        PEN_ASSERT(0);
        return 0;
    }

    u32 depth_texture_format_to_srv_format(u32 tex_format)
    {
        switch (tex_format)
        {
            case DXGI_FORMAT_R16_TYPELESS:
                return DXGI_FORMAT_R16_FLOAT;
            case DXGI_FORMAT_R32_TYPELESS:
                return DXGI_FORMAT_R32_FLOAT;
            case DXGI_FORMAT_R24G8_TYPELESS:
                return DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
            case DXGI_FORMAT_R32G8X24_TYPELESS:
                return DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
        }
        // unsupported depth texture type
        PEN_ASSERT(0);
        return 0;
    }

    bool is_array(u32 srv)
    {
        return srv == D3D_SRV_DIMENSION_TEXTURE2DARRAY || srv == D3D_SRV_DIMENSION_TEXTURECUBEARRAY ||
               srv == D3D_SRV_DIMENSION_TEXTURECUBE;
    }

    bool is_cube(u32 srv)
    {
        return srv == D3D_SRV_DIMENSION_TEXTURECUBEARRAY || srv == D3D_SRV_DIMENSION_TEXTURECUBE;
    }

    D3D_SRV_DIMENSION to_d3d11_srv_dimension(u32 pen_collection_type, bool ms)
    {
        switch (pen_collection_type)
        {
            case pen::TEXTURE_COLLECTION_CUBE:
                return D3D_SRV_DIMENSION_TEXTURECUBE;
            case pen::TEXTURE_COLLECTION_ARRAY:
                return ms ? D3D_SRV_DIMENSION_TEXTURE2DMSARRAY : D3D_SRV_DIMENSION_TEXTURE2DARRAY;
            case pen::TEXTURE_COLLECTION_CUBE_ARRAY:
                return D3D_SRV_DIMENSION_TEXTURECUBEARRAY;
            case pen::TEXTURE_COLLECTION_VOLUME:
                return D3D_SRV_DIMENSION_TEXTURE3D;
        }
        return ms ? D3D_SRV_DIMENSION_TEXTURE2DMS : D3D_SRV_DIMENSION_TEXTURE2D;
    }

    //
    // perf markers
    //

    struct gpu_perf_result
    {
        u64 elapsed;
        u32 depth;
        u32 frame;
        Str name;
    };
    static gpu_perf_result* s_perf_results = nullptr;

    struct perf_marker
    {
        ID3D11Query* begin = nullptr;
        ID3D11Query* end = nullptr;

        u64       frame = 0;
        const c8* name = nullptr;
        u32       issued = 0;
        u32       depth = 0;
        u64       result = 0;
    };

    class index_stack
    {
      public:
        u32* indices;
        u32  pos = 0;

        void clear()
        {
            pos = 0;
        }

        void push(u32 i)
        {
            if (pos >= sb_count(indices))
            {
                sb_push(indices, i);
            }
            else
            {
                indices[pos] = i;
            }

            ++pos;
        }

        u32 pop()
        {
            --pos;
            return indices[pos];
        }
    };

    struct perf_marker_set
    {
        static const u32 num_marker_buffers = 5;
        perf_marker*     markers[num_marker_buffers] = {0};
        u32              pos[num_marker_buffers] = {0};

        ID3D11Query* disjoint_query[num_marker_buffers];

        index_stack stack;

        u32 buf = 0;
        u32 depth = 0;
    };
    static perf_marker_set s_perf;

    void insert_marker(const c8* name, bool end = false)
    {
        if (s_frame == 0)
            return;

        u32& buf = s_perf.buf;
        u32& pos = s_perf.pos[buf];
        u32& depth = s_perf.depth;

        if (pos >= sb_count(s_perf.markers[buf]))
        {
            // push a new marker
            perf_marker m;

            static D3D11_QUERY_DESC desc = {D3D11_QUERY_TIMESTAMP, 0};
            CHECK_CALL(s_device->CreateQuery(&desc, &m.begin));
            CHECK_CALL(s_device->CreateQuery(&desc, &m.end));

            sb_push(s_perf.markers[buf], m);
        }

        if (end)
        {
            u32 pop_pos = s_perf.stack.pop();

            PEN_ASSERT(s_perf.markers[buf][pop_pos].issued == 1);

            s_immediate_context->End(s_perf.markers[buf][pop_pos].end);
            s_perf.markers[buf][pop_pos].issued++;
        }
        else
        {
            // queries have taken longer than 1 frame to obtain results
            // increase num_marker_buffers to avoid losing data
            PEN_ASSERT(s_perf.markers[buf][pos].issued == 0);

            s_perf.stack.push(pos);

            s_perf.markers[buf][pos].name = name;
            s_perf.markers[buf][pos].depth = depth;
            s_perf.markers[buf][pos].frame = s_frame;

            s_immediate_context->End(s_perf.markers[buf][pos].begin);
            s_perf.markers[buf][pos].issued++;

            ++pos;
        }
    }
} // namespace

namespace pen
{
    a_u64 g_gpu_total;

    void create_rtvs(u32 crtv, u32 dsv, uint32_t w, uint32_t h);
    void release_render_target_internal(u32 render_target);

    void direct::renderer_push_perf_marker(const c8* name)
    {
        if (s_frame == 0)
            return;

        u32& depth = s_perf.depth;

        insert_marker(name);

        ++depth;
    }

    void direct::renderer_pop_perf_marker()
    {
        u32& depth = s_perf.depth;
        --depth;

        insert_marker("end", true);
    }

    void gather_perf_markers()
    {
        if (s_frame == 0)
        {
            // first frame initialise disjoint queries
            for (u32 i = 0; i < perf_marker_set::num_marker_buffers; ++i)
            {
                static D3D11_QUERY_DESC desc = {D3D11_QUERY_TIMESTAMP_DISJOINT, 0};
                CHECK_CALL(s_device->CreateQuery(&desc, &s_perf.disjoint_query[i]));
            }

            s_immediate_context->Begin(s_perf.disjoint_query[s_perf.buf]);

            return;
        }

        s_immediate_context->End(s_perf.disjoint_query[s_perf.buf]);

        // read previous buffers
        for (u32 bb = 0; bb < perf_marker_set::num_marker_buffers; ++bb)
        {
            D3D10_QUERY_DATA_TIMESTAMP_DISJOINT disjoint;
            bool                                frame_ready = false;
            if (s_immediate_context->GetData(s_perf.disjoint_query[bb], &disjoint, sizeof(disjoint), 0) != S_FALSE)
            {
                frame_ready = true;
            }

            if (frame_ready)
            {
                u32 num_complete = 0;
                for (u32 i = 0; i < s_perf.pos[bb]; ++i)
                {
                    perf_marker& m = s_perf.markers[bb][i];
                    if (m.issued == 2)
                    {
                        if (!disjoint.Disjoint)
                        {
                            UINT64  ts_begin;
                            UINT64  ts_end;
                            HRESULT hr = 0;
                            hr = s_immediate_context->GetData(m.begin, &ts_begin, sizeof(UINT64), 0);
                            if (hr == S_OK)
                            {
                                hr = s_immediate_context->GetData(m.end, &ts_end, sizeof(UINT64), 0);
                                if (hr == S_OK)
                                {
                                    UINT64 res = (ts_end - ts_begin);

                                    // PEN_LOG("%s : %llu\n", m.name, res);

                                    if (i == 0)
                                    {
                                        g_gpu_total = res;
                                    }

                                    m.issued = 0;

                                    ++num_complete;
                                }
                            }
                        }
                    }
                }

                if (num_complete == s_perf.pos[bb])
                {
                    s_perf.pos[bb] = 0;
                    s_perf.depth = 0;
                }
            }
        }

        // swap buffers
        s_perf.buf = (s_perf.buf + 1) % s_perf.num_marker_buffers;
        s_immediate_context->Begin(s_perf.disjoint_query[s_perf.buf]);
    }

    //--------------------------------------------------------------------------------------
    //  COMMON API
    //--------------------------------------------------------------------------------------

    struct context_state
    {
        context_state()
        {
            active_query_index = 0;
        }

        u32 backbuffer_colour;
        u32 backbuffer_depth;
        u32 active_colour_target[8] = {0};
        u32 active_depth_target = 0;
        u32 num_active_colour_targets = 1;
        u32 depth_stencil_state = 0;
        u8  stencil_ref = 0;
        u32 active_query_index;
    };

    struct clear_state_internal
    {
        FLOAT     rgba[4];
        f32       depth;
        u8        stencil;
        u32       flags;
        mrt_clear mrt[MAX_MRT];
        u32       num_colour_targets;
    };

    struct texture2d_internal
    {
        ID3D11Texture2D*           texture = nullptr;
        ID3D11ShaderResourceView*  srv = nullptr;
        ID3D11UnorderedAccessView* uav = nullptr;
    };

    struct texture3d_internal
    {
        ID3D11Texture3D*           texture = nullptr;
        ID3D11ShaderResourceView*  srv = nullptr;
        ID3D11UnorderedAccessView* uav = nullptr;
    };

    struct texture_resource
    {
        ID3D11Resource*            texture = nullptr;
        ID3D11ShaderResourceView*  srv = nullptr;
        ID3D11UnorderedAccessView* uav = nullptr;
    };

    struct render_target_internal
    {
        texture_resource         tex;
        ID3D11RenderTargetView** rt;
        texture_resource         tex_msaa;
        ID3D11RenderTargetView** rt_msaa;
        texture_resource         tex_read_back;
        texture2d_internal       tex_resolve;
        bool                     msaa_resolve_readback = false;
        DXGI_FORMAT              format;
        texture_creation_params* tcp;
        u32                      invalidate;
        u32                      num_arrays = 1;
        bool                     has_mips = false;
    };

    struct depth_stencil_target_internal
    {
        texture_resource         tex;
        ID3D11DepthStencilView** ds;
        texture_resource         tex_msaa;
        ID3D11DepthStencilView** ds_msaa;
        texture_resource         tex_read_back;
        DXGI_FORMAT              format;
        texture_creation_params* tcp;
        u32                      invalidate;
        u32                      num_arrays = 1;
        bool                     has_mips = false;
    };

    struct shader_program
    {
        u32 vertex_shader;
        u32 pixel_shader;
        u32 input_layout;
    };

    enum e_resource_type
    {
        RES_NONE,
        RES_BUFFER,
        RES_TEXTURE,
        RES_RENDER_TARGET,
        RES_TEXTURE_3D
    };

    struct stream_out_shader
    {
        ID3D11VertexShader*   vs;
        ID3D11GeometryShader* gs;
    };

    struct ua_buffer
    {
        ID3D11Buffer*              buf;
        ID3D11UnorderedAccessView* uav;
        ID3D11ShaderResourceView*  srv;
    };

    struct resource_allocation
    {
        u32 type = 0;

        union {
            clear_state_internal*          clear_state;
            ID3D11VertexShader*            vertex_shader;
            ID3D11InputLayout*             input_layout;
            ID3D11PixelShader*             pixel_shader;
            ID3D11ComputeShader*           compute_shader;
            ID3D11GeometryShader*          geometry_shader;
            stream_out_shader              stream_out_shader;
            ua_buffer                      generic_buffer;
            texture_resource*              texture_resource;
            texture2d_internal*            texture_2d;
            texture3d_internal*            texture_3d;
            ID3D11SamplerState*            sampler_state;
            ID3D11RasterizerState*         raster_state;
            ID3D11BlendState*              blend_state;
            ID3D11DepthStencilState*       depth_stencil_state;
            render_target_internal*        render_target;
            depth_stencil_target_internal* depth_target;
            shader_program                 shader_program;
        };
    };
    static res_pool<resource_allocation> _res_pool;

    context_state g_context;

    void direct::renderer_create_clear_state(const clear_state& cs, u32 resource_slot)
    {
        _res_pool.grow(resource_slot);

        _res_pool[resource_slot].clear_state = (pen::clear_state_internal*)pen::memory_alloc(sizeof(clear_state_internal));

        _res_pool[resource_slot].clear_state->rgba[0] = cs.r;
        _res_pool[resource_slot].clear_state->rgba[1] = cs.g;
        _res_pool[resource_slot].clear_state->rgba[2] = cs.b;
        _res_pool[resource_slot].clear_state->rgba[3] = cs.a;
        _res_pool[resource_slot].clear_state->depth = cs.depth;
        _res_pool[resource_slot].clear_state->stencil = cs.stencil;
        _res_pool[resource_slot].clear_state->flags = cs.flags;

        _res_pool[resource_slot].clear_state->num_colour_targets = cs.num_colour_targets;

        memcpy(_res_pool[resource_slot].clear_state->mrt, cs.mrt, sizeof(mrt_clear) * cs.num_colour_targets);
        memcpy(_res_pool[resource_slot].clear_state->mrt, cs.mrt, sizeof(mrt_clear) * cs.num_colour_targets);

        mrt_clear* mrt = _res_pool[resource_slot].clear_state->mrt;

        // convert int clears (required on gl) to floats for d3d
        for (s32 i = 0; i < cs.num_colour_targets; ++i)
            if (mrt[i].type == CLEAR_U32)
                for (s32 c = 0; c < 4; ++c)
                    mrt[i].f[c] = (f32)cs.mrt[i].u[c];
    }

    void direct::renderer_sync()
    {
        // unused on this platform
    }

    void direct::renderer_retain()
    {
        // unused on this platform
    }

    void direct::renderer_new_frame()
    {
        // unused on this platform
        shared_flags flags = _renderer_flags();
        if (flags & e_shared_flags::backbuffer_resize)
        {
            s_immediate_context->OMSetRenderTargets(0, 0, 0);

            // Release all outstanding references to the swap chain's buffers.
            if (_res_pool[g_context.backbuffer_depth].depth_target->ds)
            {
                _res_pool[g_context.backbuffer_depth].depth_target->ds[0]->Release();
                _res_pool[g_context.backbuffer_depth].depth_target->tex.texture->Release();
            }

            if (_res_pool[g_context.backbuffer_colour].render_target->rt)
            {
                _res_pool[g_context.backbuffer_colour].render_target->rt[0]->Release();
                _res_pool[g_context.backbuffer_colour].render_target->tex.texture->Release();
            }

            uint32_t w = pen_window.width;
            uint32_t h = pen_window.height;

            s_swap_chain->ResizeBuffers(0, w, h, DXGI_FORMAT_UNKNOWN, 0);

            create_rtvs(g_context.backbuffer_colour, g_context.backbuffer_depth, w, h);
        }

        _renderer_new_frame();
    }

    void direct::renderer_end_frame()
    {
        // unused on this platform
        _renderer_end_frame();
    }

    void direct::renderer_clear_texture(u32 clear_state_index, u32 texture)
    {
        if (!clear_state_index)
            return;

        clear_state_internal* cs = _res_pool[clear_state_index].clear_state;

        if (!(cs->flags & PEN_CLEAR_COLOUR_BUFFER))
            return;

        s_immediate_context->ClearUnorderedAccessViewFloat(_res_pool[texture].texture_3d->uav, &cs->rgba[0]);
    }

    void direct::renderer_clear(u32 clear_state_index, u32 colour_face, u32 depth_face)
    {
        u32 flags = _res_pool[clear_state_index].clear_state->flags;

        clear_state_internal* cs = _res_pool[clear_state_index].clear_state;

        // clear colour
        if (flags & PEN_CLEAR_COLOUR_BUFFER)
        {
            if (cs->num_colour_targets == 0)
            {
                for (s32 i = 0; i < g_context.num_active_colour_targets; ++i)
                {
                    s32                     ct = g_context.active_colour_target[i];
                    ID3D11RenderTargetView* colour_rtv = nullptr;

                    auto rt = _res_pool[ct].render_target;

                    if (rt->rt_msaa && rt->rt_msaa[colour_face])
                        colour_rtv = _res_pool[ct].render_target->rt_msaa[colour_face];
                    else
                        colour_rtv = _res_pool[ct].render_target->rt[colour_face];

                    if (colour_rtv)
                        s_immediate_context->ClearRenderTargetView(colour_rtv,
                                                                   &_res_pool[clear_state_index].clear_state->rgba[0]);
                }
            }
        }

        // MRT clear colour
        for (s32 i = 0; i < cs->num_colour_targets; ++i)
        {
            s32 ct = g_context.active_colour_target[i];

            ID3D11RenderTargetView* colour_rtv = nullptr;

            auto rt = _res_pool[ct].render_target;

            if (rt->rt_msaa && rt->rt_msaa[colour_face])
                colour_rtv = _res_pool[ct].render_target->rt_msaa[colour_face];
            else
                colour_rtv = _res_pool[ct].render_target->rt[colour_face];

            s_immediate_context->ClearRenderTargetView(colour_rtv, cs->mrt[i].f);
        }

        u32 d3d_flags = 0;
        if (flags & PEN_CLEAR_DEPTH_BUFFER)
            d3d_flags |= D3D11_CLEAR_DEPTH;
        if (flags & PEN_CLEAR_STENCIL_BUFFER)
            d3d_flags |= D3D11_CLEAR_STENCIL;

        u8 stencil_val = _res_pool[clear_state_index].clear_state->stencil;

        if (d3d_flags && g_context.active_depth_target)
        {
            ID3D11DepthStencilView* dsv = nullptr;

            auto dt = _res_pool[g_context.active_depth_target].depth_target;
            if (dt->ds_msaa && dt->ds_msaa[depth_face])
                dsv = _res_pool[g_context.active_depth_target].depth_target->ds_msaa[depth_face];
            else
                dsv = _res_pool[g_context.active_depth_target].depth_target->ds[depth_face];

            // clear depth
            s_immediate_context->ClearDepthStencilView(dsv, d3d_flags, _res_pool[clear_state_index].clear_state->depth,
                                                       stencil_val);
        }
    }

    void direct::renderer_present()
    {
        // Just present
        s_swap_chain->Present(0, 0);

        if (s_frame > 0)
            renderer_pop_perf_marker();

        gather_perf_markers();

        s_frame++;

        renderer_push_perf_marker(nullptr);
    }

    void direct::renderer_load_shader(const pen::shader_load_params& params, u32 resource_slot)
    {
        _res_pool.grow(resource_slot);

        HRESULT hr = -1;
        u32     handle_out = (u32)-1;

        u32 resource_index = resource_slot;

        if (params.type == PEN_SHADER_TYPE_VS)
        {
            // Create a vertex shader
            CHECK_CALL(s_device->CreateVertexShader(params.byte_code, params.byte_code_size, nullptr,
                                                    &_res_pool[resource_index].vertex_shader));
        }
        else if (params.type == PEN_SHADER_TYPE_PS)
        {
            // Create a pixel shader
            if (params.byte_code)
            {
                CHECK_CALL(s_device->CreatePixelShader(params.byte_code, params.byte_code_size, nullptr,
                                                       &_res_pool[resource_index].pixel_shader));
            }
            else
            {
                _res_pool[resource_index].pixel_shader = NULL;
                hr = S_OK;
            }
        }
        else if (params.type == PEN_SHADER_TYPE_SO)
        {
            stream_out_shader& sos = _res_pool[resource_index].stream_out_shader;

            u32 resource_index = resource_slot;

            // Create a vertex shader + stream out geometry shader
            CHECK_CALL(s_device->CreateVertexShader(params.byte_code, params.byte_code_size, nullptr, &sos.vs));
            CHECK_CALL(s_device->CreateGeometryShaderWithStreamOutput(
                params.byte_code, params.byte_code_size, (const D3D11_SO_DECLARATION_ENTRY*)params.so_decl_entries,
                params.so_num_entries, NULL, 0, 0, NULL, &sos.gs));
        }
        else if (params.type == PEN_SHADER_TYPE_CS)
        {
            // Create a compute shader
            CHECK_CALL(s_device->CreateComputeShader(params.byte_code, params.byte_code_size, nullptr,
                                                     &_res_pool[resource_index].compute_shader));
        }
    }

    void direct::renderer_set_shader(u32 shader_index, u32 shader_type)
    {
        if (shader_type == PEN_SHADER_TYPE_VS)
        {
            s_immediate_context->VSSetShader(_res_pool[shader_index].vertex_shader, nullptr, 0);
            s_immediate_context->GSSetShader(nullptr, nullptr, 0);
        }
        else if (shader_type == PEN_SHADER_TYPE_PS)
        {
            s_immediate_context->PSSetShader(_res_pool[shader_index].pixel_shader, nullptr, 0);
        }
        else if (shader_type == PEN_SHADER_TYPE_GS)
        {
            s_immediate_context->GSSetShader(_res_pool[shader_index].geometry_shader, nullptr, 0);
        }
        else if (shader_type == PEN_SHADER_TYPE_SO)
        {
            auto& sos = _res_pool[shader_index].stream_out_shader;
            s_immediate_context->VSSetShader(sos.vs, nullptr, 0);
            s_immediate_context->GSSetShader(sos.gs, nullptr, 0);
            s_immediate_context->PSSetShader(nullptr, nullptr, 0);

            // on feature level 10 we cant uses SO_RASTERISER_DISCARD, this prevents the validation layer barking
            static ID3D11DepthStencilState* dss = nullptr;
            if (!dss)
            {
                D3D11_DEPTH_STENCIL_DESC dss_disable = {0};
                dss_disable.DepthEnable = 0;
                dss_disable.StencilEnable = 0;

                CHECK_CALL(s_device->CreateDepthStencilState(&dss_disable, &dss));
            }

            s_immediate_context->OMSetDepthStencilState(dss, 0);
        }
        else if (shader_type == PEN_SHADER_TYPE_CS)
        {
            s_immediate_context->CSSetShader(_res_pool[shader_index].compute_shader, nullptr, 0);
        }
    }

    void direct::renderer_link_shader_program(const shader_link_params& params, u32 resource_slot)
    {
        u32 resource_index = resource_slot;

        auto& sp = _res_pool[resource_index].shader_program;

        // for now d3d just keeps handles to vs, ps and il..
        // the additional requirements of gl and buffer bindings could provide useful reflection info.
        sp.input_layout = params.input_layout;
        sp.pixel_shader = params.pixel_shader;
        sp.vertex_shader = params.vertex_shader;
    }

    void direct::renderer_create_buffer(const buffer_creation_params& params, u32 resource_slot)
    {
        _res_pool.grow(resource_slot);

        u32 resource_index = resource_slot;

        D3D11_BUFFER_DESC bd;
        ZeroMemory(&bd, sizeof(bd));

        bd.Usage = to_d3d11_usage(params.usage_flags);
        bd.BindFlags = to_d3d11_bind_flags(params.bind_flags);
        bd.CPUAccessFlags = to_d3d11_cpu_access_flags(params.cpu_access_flags);
        bd.ByteWidth = params.buffer_size;

        if (bd.BindFlags & PEN_BIND_SHADER_WRITE)
        {
            bd.MiscFlags |= D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
            bd.StructureByteStride = params.stride;
        }

        if (params.data)
        {
            D3D11_SUBRESOURCE_DATA initial_data;
            ZeroMemory(&initial_data, sizeof(initial_data));

            initial_data.pSysMem = params.data;

            CHECK_CALL(s_device->CreateBuffer(&bd, &initial_data, &_res_pool[resource_index].generic_buffer.buf));
        }
        else
        {
            CHECK_CALL(s_device->CreateBuffer(&bd, nullptr, &_res_pool[resource_index].generic_buffer.buf));
        }

        if (bd.BindFlags & PEN_BIND_SHADER_WRITE)
        {
            // uav if we need it
            D3D11_UNORDERED_ACCESS_VIEW_DESC uav_desc = {};
            uav_desc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
            uav_desc.Format = DXGI_FORMAT_UNKNOWN;
            uav_desc.Buffer.FirstElement = 0;
            uav_desc.Buffer.NumElements = params.buffer_size / params.stride;

            CHECK_CALL(s_device->CreateUnorderedAccessView(_res_pool[resource_index].generic_buffer.buf, &uav_desc,
                                                           &_res_pool[resource_index].generic_buffer.uav));

            // srv if we need it
            D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
            srv_desc.ViewDimension = D3D11_SRV_DIMENSION_BUFFEREX;
            srv_desc.Format = DXGI_FORMAT_UNKNOWN;
            srv_desc.BufferEx.FirstElement = 0;
            srv_desc.BufferEx.NumElements = params.buffer_size / params.stride;

            CHECK_CALL(s_device->CreateShaderResourceView(_res_pool[resource_index].generic_buffer.buf, &srv_desc,
                                                          &_res_pool[resource_index].generic_buffer.srv));
        }
    }

    void direct::renderer_create_input_layout(const input_layout_creation_params& params, u32 resource_slot)
    {
        _res_pool.grow(resource_slot);

        u32 resource_index = resource_slot;

        // convert to d3d11 types
        input_layout_creation_params _layout = params;
        D3D11_INPUT_ELEMENT_DESC*    desc = (D3D11_INPUT_ELEMENT_DESC*)params.input_layout;
        for (u32 i = 0; i < _layout.num_elements; ++i)
        {
            desc[i].Format = to_d3d11_vertex_format(params.input_layout[i].format);
        }

        // Create the input layout
        CHECK_CALL(s_device->CreateInputLayout(desc, params.num_elements, params.vs_byte_code, params.vs_byte_code_size,
                                               &_res_pool[resource_index].input_layout));
    }

    void direct::renderer_set_vertex_buffers(u32* buffer_indices, u32 num_buffers, u32 start_slot, const u32* strides,
                                             const u32* offsets)
    {
        ID3D11Buffer* buffers[4];

        for (s32 i = 0; i < num_buffers; ++i)
            buffers[i] = _res_pool[buffer_indices[i]].generic_buffer.buf;

        s_immediate_context->IASetVertexBuffers(start_slot, num_buffers, buffers, strides, offsets);
    }

    void direct::renderer_set_input_layout(u32 layout_index)
    {
        s_immediate_context->IASetInputLayout(_res_pool[layout_index].input_layout);
    }

    void direct::renderer_set_index_buffer(u32 buffer_index, u32 format, u32 offset)
    {
        DXGI_FORMAT d3d11_format = to_d3d11_index_format(format);
        s_immediate_context->IASetIndexBuffer(_res_pool[buffer_index].generic_buffer.buf, d3d11_format, offset);
    }

    void direct::renderer_draw(u32 vertex_count, u32 start_vertex, u32 primitive_topology)
    {
        s_immediate_context->IASetPrimitiveTopology(to_d3d11_primitive_topology(primitive_topology));
        s_immediate_context->Draw(vertex_count, start_vertex);
    }

    void direct::renderer_draw_indexed(u32 index_count, u32 start_index, u32 base_vertex, u32 primitive_topology)
    {
        s_immediate_context->IASetPrimitiveTopology(to_d3d11_primitive_topology(primitive_topology));
        s_immediate_context->DrawIndexed(index_count, start_index, base_vertex);
    }

    void direct::renderer_draw_indexed_instanced(u32 instance_count, u32 start_instance, u32 index_count, u32 start_index,
                                                 u32 base_vertex, u32 primitive_topology)
    {
        s_immediate_context->IASetPrimitiveTopology(to_d3d11_primitive_topology(primitive_topology));
        s_immediate_context->DrawIndexedInstanced(index_count, instance_count, start_index, base_vertex, start_instance);
    }

    void renderer_create_render_target_multi(const texture_creation_params& tcp, texture_resource* texture_container,
                                             ID3D11DepthStencilView*** dsv, ID3D11RenderTargetView*** rtv)
    {
        // create an empty texture
        D3D11_TEXTURE2D_DESC texture_desc;
        memcpy(&texture_desc, (void*)&tcp, sizeof(D3D11_TEXTURE2D_DESC));
        texture_desc.Format = to_d3d11_texture_format(tcp.format);
        texture_desc.Usage = to_d3d11_usage(tcp.usage);
        texture_desc.BindFlags = to_d3d11_bind_flags(tcp.bind_flags);
        texture_desc.CPUAccessFlags = to_d3d11_cpu_access_flags(tcp.cpu_access_flags);

        if (tcp.collection_type == pen::TEXTURE_COLLECTION_CUBE || tcp.collection_type == pen::TEXTURE_COLLECTION_CUBE_ARRAY)
        {
            texture_desc.MiscFlags |= 0x4L; // resource misc texture cube is required
        }

        u32  array_size = texture_desc.ArraySize;
        bool ms = texture_desc.SampleDesc.Count > 1;
        u32  num_mips = tcp.num_mips;

        if (array_size > 1 && ms)
        {
            // arrays and cubes don't support msaa yet
            PEN_ASSERT(0);
        }

        if (tcp.collection_type == pen::TEXTURE_COLLECTION_VOLUME)
        {
            // 3d render targets are writable textures
            PEN_ASSERT(0);
        }
        else
        {
            CHECK_CALL(s_device->CreateTexture2D(&texture_desc, nullptr, (ID3D11Texture2D**)&texture_container->texture));
        }

        D3D11_SHADER_RESOURCE_VIEW_DESC resource_view_desc;
        D3D_SRV_DIMENSION               srv_dimension = ms ? D3D11_SRV_DIMENSION_TEXTURE2DMS : D3D11_SRV_DIMENSION_TEXTURE2D;
        srv_dimension = to_d3d11_srv_dimension(tcp.collection_type, ms);

        if (texture_desc.BindFlags & D3D11_BIND_DEPTH_STENCIL)
        {
            // create shader resource view
            resource_view_desc.Format = (DXGI_FORMAT)depth_texture_format_to_srv_format(texture_desc.Format);
            resource_view_desc.ViewDimension = srv_dimension;
            resource_view_desc.Texture2D.MipLevels = num_mips;
            resource_view_desc.Texture2D.MostDetailedMip = 0;

            // depth target
            D3D11_DEPTH_STENCIL_VIEW_DESC dsv_desc;
            dsv_desc.Format = (DXGI_FORMAT)depth_texture_format_to_dsv_format(texture_desc.Format);
            dsv_desc.Flags = 0;

            *dsv = new ID3D11DepthStencilView*[array_size];
            ID3D11DepthStencilView** _dsv = *dsv;

            // Create the render target view.
            if (!is_array(srv_dimension))
            {
                // single rt
                dsv_desc.ViewDimension = ms ? D3D11_DSV_DIMENSION_TEXTURE2DMS : D3D11_DSV_DIMENSION_TEXTURE2D;
                dsv_desc.Texture2D.MipSlice = 0;

                CHECK_CALL(s_device->CreateDepthStencilView(texture_container->texture, &dsv_desc, &_dsv[0]));
            }
            else
            {
                // array or cubemap rt
                for (u32 a = 0; a < array_size; ++a)
                {
                    dsv_desc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DARRAY;
                    dsv_desc.Texture2DArray.FirstArraySlice = a;
                    dsv_desc.Texture2DArray.MipSlice = 0;
                    dsv_desc.Texture2DArray.ArraySize = 1;

                    CHECK_CALL(s_device->CreateDepthStencilView(texture_container->texture, &dsv_desc, &_dsv[a]));
                }

                if (srv_dimension == D3D_SRV_DIMENSION_TEXTURE2DARRAY)
                {
                    resource_view_desc.Texture2DArray.ArraySize = array_size;
                    resource_view_desc.Texture2DArray.FirstArraySlice = 0;
                    resource_view_desc.Texture2DArray.MipLevels = num_mips;
                    resource_view_desc.Texture2DArray.MostDetailedMip = 0;
                }
                else if (is_cube(srv_dimension))
                {
                    resource_view_desc.Texture2DArray.ArraySize = array_size / 6;
                    resource_view_desc.Texture2DArray.FirstArraySlice = 0;
                    resource_view_desc.Texture2DArray.MipLevels = num_mips;
                    resource_view_desc.Texture2DArray.MostDetailedMip = 0;
                }
            }
        }
        else if (texture_desc.BindFlags & D3D11_BIND_RENDER_TARGET)
        {
            // create shader resource view
            resource_view_desc.Format = texture_desc.Format;
            resource_view_desc.ViewDimension = srv_dimension;
            resource_view_desc.Texture2D.MipLevels = num_mips;
            resource_view_desc.Texture2D.MostDetailedMip = 0;

            // d3d render target
            D3D11_RENDER_TARGET_VIEW_DESC rtv_desc;
            rtv_desc.Format = texture_desc.Format;

            *rtv = new ID3D11RenderTargetView*[array_size];
            ID3D11RenderTargetView** _rtv = *rtv;

            // Create the render target view.
            if (!is_array(srv_dimension))
            {
                // single rt
                rtv_desc.ViewDimension = ms ? D3D11_RTV_DIMENSION_TEXTURE2DMS : D3D11_RTV_DIMENSION_TEXTURE2D;
                rtv_desc.Texture2D.MipSlice = 0;

                CHECK_CALL(s_device->CreateRenderTargetView(texture_container->texture, &rtv_desc, &_rtv[0]));
            }
            else
            {
                // array or cubemap rt
                for (u32 a = 0; a < array_size; ++a)
                {
                    rtv_desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
                    rtv_desc.Texture2DArray.FirstArraySlice = a;
                    rtv_desc.Texture2DArray.MipSlice = 0;
                    rtv_desc.Texture2DArray.ArraySize = 1;
                    CHECK_CALL(s_device->CreateRenderTargetView(texture_container->texture, &rtv_desc, &_rtv[a]));
                }

                if (srv_dimension == D3D_SRV_DIMENSION_TEXTURE2DARRAY)
                {
                    resource_view_desc.Texture2DArray.ArraySize = array_size;
                    resource_view_desc.Texture2DArray.FirstArraySlice = 0;
                    resource_view_desc.Texture2DArray.MipLevels = num_mips;
                    resource_view_desc.Texture2DArray.MostDetailedMip = 0;
                }
                else if (is_cube(srv_dimension))
                {
                    resource_view_desc.Texture2DArray.ArraySize = array_size / 6;
                    resource_view_desc.Texture2DArray.FirstArraySlice = 0;
                    resource_view_desc.Texture2DArray.MipLevels = num_mips;
                    resource_view_desc.Texture2DArray.MostDetailedMip = 0;
                }
            }
        }
        else
        {
            // m8 this is not a render target
            PEN_ASSERT(0);
        }

        CHECK_CALL(
            s_device->CreateShaderResourceView(texture_container->texture, &resource_view_desc, &texture_container->srv));
    }

    void direct::renderer_create_render_target(const texture_creation_params& tcp, u32 resource_slot, bool track)
    {
        if (tcp.collection_type == pen::TEXTURE_COLLECTION_VOLUME)
        {
            renderer_create_texture(tcp, resource_slot);
            return;
        }

        _res_pool.grow(resource_slot);

        u32 resource_index = resource_slot;

        _res_pool[resource_index].type = RES_RENDER_TARGET;

        // alloc rt
        _res_pool[resource_index].render_target = (render_target_internal*)pen::memory_alloc(sizeof(render_target_internal));
        pen::memory_zero(_res_pool[resource_index].render_target, sizeof(render_target_internal));

        // format required for resolve
        _res_pool[resource_index].render_target->format = (DXGI_FORMAT)tcp.format;
        _res_pool[resource_index].render_target->tcp = nullptr;

        texture_creation_params _tcp = _renderer_tcp_resolve_ratio(tcp);
        if (track)
        {
            _renderer_track_managed_render_target(tcp, resource_index);
        }

        // rt mip maps
        _tcp.num_mips = tcp.num_mips;
        if (_tcp.num_mips == -1)
            _tcp.num_mips = calc_num_mips(_tcp.width, _tcp.height);

        if (_tcp.num_mips > 1)
        {
            _tcp.flags |= D3D11_RESOURCE_MISC_GENERATE_MIPS;
            _res_pool[resource_index].depth_target->has_mips = true;
            _res_pool[resource_index].render_target->has_mips = true;
        }

        _res_pool[resource_index].render_target->num_arrays = _tcp.num_arrays;

        if (_tcp.cpu_access_flags != 0)
        {
            texture_creation_params read_back_tcp = _tcp;
            read_back_tcp.bind_flags = 0;
            read_back_tcp.usage = D3D11_USAGE_STAGING;
            read_back_tcp.cpu_access_flags = to_d3d11_cpu_access_flags(_tcp.cpu_access_flags);
            read_back_tcp.format = to_d3d11_texture_format(_tcp.format);

            D3D11_TEXTURE2D_DESC texture_desc;
            memcpy(&texture_desc, (void*)&read_back_tcp, sizeof(D3D11_TEXTURE2D_DESC));

            CHECK_CALL(s_device->CreateTexture2D(
                &texture_desc, NULL, (ID3D11Texture2D**)&_res_pool[resource_index].render_target->tex_read_back.texture));

            _tcp.cpu_access_flags = 0;
        }

        if (tcp.sample_count > 1)
        {
            // create msaa
            renderer_create_render_target_multi(_tcp, &_res_pool[resource_index].render_target->tex_msaa,
                                                &_res_pool[resource_index].depth_target->ds_msaa,
                                                &_res_pool[resource_index].render_target->rt_msaa);

            // for resolve later
            _res_pool[resource_index].render_target->tcp = new texture_creation_params;
            *_res_pool[resource_index].render_target->tcp = tcp;
        }
        else
        {
            texture_creation_params resolve_tcp = _tcp;
            resolve_tcp.sample_count = 1;
            renderer_create_render_target_multi(resolve_tcp, &_res_pool[resource_index].render_target->tex,
                                                &_res_pool[resource_index].depth_target->ds,
                                                &_res_pool[resource_index].render_target->rt);
        }
    }

    void direct::renderer_set_resolve_targets(u32 colour_target, u32 depth_target)
    {
        ID3D11RenderTargetView* colour_rtv[MAX_MRT] = {0};

        if (colour_target > 0)
            colour_rtv[0] = _res_pool[colour_target].render_target->rt[0];

        ID3D11DepthStencilView* dsv = nullptr;
        if (depth_target > 0)
            dsv = _res_pool[depth_target].depth_target->ds[0];

        s_immediate_context->OMSetRenderTargets(1, colour_rtv, dsv);
    }

    void direct::renderer_set_targets(const u32* const colour_targets, u32 num_colour_targets, u32 depth_target,
                                      u32 colour_face, u32 depth_face)
    {
        g_context.active_depth_target = depth_target;
        g_context.num_active_colour_targets = num_colour_targets;

        u32                     num_views = num_colour_targets;
        ID3D11RenderTargetView* colour_rtv[MAX_MRT] = {0};

        for (s32 i = 0; i < num_colour_targets; ++i)
        {
            u32 colour_target = colour_targets[i];
            g_context.active_colour_target[i] = colour_target;

            if (colour_target != 0 && colour_target != PEN_INVALID_HANDLE)
            {
                auto rt = _res_pool[colour_target].render_target;

                if (rt->rt_msaa && rt->rt_msaa[colour_face])
                    colour_rtv[i] = _res_pool[colour_target].render_target->rt_msaa[colour_face];
                else
                    colour_rtv[i] = _res_pool[colour_target].render_target->rt[colour_face];

                if (_res_pool[colour_target].render_target->has_mips)
                    _res_pool[colour_target].render_target->invalidate = 1;
            }
            else
            {
                g_context.active_colour_target[i] = 0;
            }
        }

        ID3D11DepthStencilView* dsv = nullptr;
        if (depth_target != 0 && depth_target != PEN_INVALID_HANDLE)
        {
            auto dt = _res_pool[depth_target].depth_target;

            if (dt->ds_msaa && dt->ds_msaa[depth_face])
                dsv = _res_pool[depth_target].depth_target->ds_msaa[depth_face];
            else
                dsv = _res_pool[depth_target].depth_target->ds[depth_face];

            if (_res_pool[depth_target].depth_target->has_mips)
                _res_pool[depth_target].depth_target->invalidate = 1;
        }
        else
        {
            g_context.active_depth_target = 0;
        }

        s_immediate_context->OMSetRenderTargets(num_views, colour_rtv, dsv);
    }

    void direct::renderer_create_texture(const texture_creation_params& tcp, u32 resource_slot)
    {
        _res_pool.grow(resource_slot);

        u32 resource_index = resource_slot;

        D3D_SRV_DIMENSION view_dimension = to_d3d11_srv_dimension(tcp.collection_type, tcp.sample_count > 1);

        u32 num_slices = 1;
        u32 num_arrays = tcp.num_arrays;

        if (tcp.collection_type == TEXTURE_COLLECTION_VOLUME)
        {
            // texture 3d
            view_dimension = D3D10_SRV_DIMENSION_TEXTURE3D;

            num_slices = tcp.num_arrays;
            num_arrays = 1;

            u32 extra_flags = 0;
            if (tcp.num_mips > 1)
            {
                extra_flags |= D3D11_RESOURCE_MISC_GENERATE_MIPS;
            }

            D3D11_TEXTURE3D_DESC texture_desc = {tcp.width,
                                                 tcp.height,
                                                 tcp.num_arrays,
                                                 (u32)tcp.num_mips,
                                                 to_d3d11_texture_format(tcp.format),
                                                 to_d3d11_usage(tcp.usage),
                                                 to_d3d11_bind_flags(tcp.bind_flags),
                                                 to_d3d11_cpu_access_flags(tcp.cpu_access_flags),
                                                 tcp.flags | extra_flags};

            _res_pool[resource_index].type = RES_TEXTURE_3D;
            _res_pool[resource_index].texture_3d = (texture3d_internal*)memory_alloc(sizeof(texture3d_internal));
            CHECK_CALL(s_device->CreateTexture3D(&texture_desc, nullptr, &(_res_pool[resource_index].texture_3d->texture)));
        }
        else
        {
            // texture 2d, arrays, cubemaps, cubemap arrays
            D3D11_TEXTURE2D_DESC texture_desc;
            memcpy(&texture_desc, (void*)&tcp, sizeof(D3D11_TEXTURE2D_DESC));
            texture_desc.Format = to_d3d11_texture_format(tcp.format);
            texture_desc.Usage = to_d3d11_usage(tcp.usage);
            texture_desc.BindFlags = to_d3d11_bind_flags(tcp.bind_flags);
            texture_desc.CPUAccessFlags = to_d3d11_cpu_access_flags(tcp.cpu_access_flags);

            if (tcp.collection_type == TEXTURE_COLLECTION_CUBE)
            {
                view_dimension = D3D10_SRV_DIMENSION_TEXTURECUBE;
                texture_desc.MiscFlags |= D3D11_RESOURCE_MISC_TEXTURECUBE;
            }

            _res_pool[resource_index].type = RES_TEXTURE;
            _res_pool[resource_index].texture_2d = (texture2d_internal*)memory_alloc(sizeof(texture2d_internal));
            CHECK_CALL(s_device->CreateTexture2D(&texture_desc, nullptr, &(_res_pool[resource_index].texture_2d->texture)));
        }

        texture_resource* tex_res = _res_pool[resource_index].texture_resource;

        // fill with data
        if (tcp.data)
        {
            u8* image_data = (u8*)tcp.data;

            // for arrays, slices, faces
            for (s32 a = 0; a < num_arrays; ++a)
            {
                u32 current_width = tcp.width / tcp.pixels_per_block;
                u32 current_height = tcp.height / tcp.pixels_per_block;
                u32 current_depth = num_slices / tcp.pixels_per_block;
                u32 block_size = tcp.block_size;

                // for mips
                for (s32 i = 0; i < tcp.num_mips; ++i)
                {
                    u32 row_pitch = current_width * block_size;
                    u32 slice_pitch = current_height * row_pitch;
                    u32 depth_pitch = slice_pitch * current_depth;

                    u32 sub = D3D11CalcSubresource(i, a, tcp.num_mips);

                    s_immediate_context->UpdateSubresource(tex_res->texture, sub, nullptr, image_data, row_pitch,
                                                           slice_pitch);

                    image_data += depth_pitch;

                    current_width = max<u32>(current_width / 2, 1);
                    current_height = max<u32>(current_height / 2, 1);
                    current_depth = max<u32>(current_depth / 2, 1);
                }
            }
        }

        // create shader resource view
        D3D11_SHADER_RESOURCE_VIEW_DESC resource_view_desc;
        ZeroMemory(&resource_view_desc, sizeof(resource_view_desc));
        resource_view_desc.Format = to_d3d11_texture_format(tcp.format);
        resource_view_desc.ViewDimension = view_dimension;
        resource_view_desc.Texture2D.MipLevels = -1;
        resource_view_desc.Texture2D.MostDetailedMip = 0;

        if (tcp.collection_type == TEXTURE_COLLECTION_ARRAY)
        {
            resource_view_desc.Texture2DArray.ArraySize = tcp.num_arrays;
            resource_view_desc.Texture2DArray.FirstArraySlice = 0;
        }

        CHECK_CALL(s_device->CreateShaderResourceView(tex_res->texture, &resource_view_desc, &tex_res->srv));

        tex_res->uav = nullptr;

        if (tcp.bind_flags & PEN_BIND_SHADER_WRITE)
        {
            D3D11_UNORDERED_ACCESS_VIEW_DESC uav_desc;
            ZeroMemory(&uav_desc, sizeof(uav_desc));

            if (tcp.collection_type == pen::TEXTURE_COLLECTION_VOLUME)
            {
                uav_desc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE3D;
                uav_desc.Format = DXGI_FORMAT_UNKNOWN;
                uav_desc.Texture3D.WSize = -1;
                uav_desc.Texture3D.FirstWSlice = 0;
                uav_desc.Texture3D.MipSlice = 0;
            }
            else
            {
                uav_desc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
                uav_desc.Format = DXGI_FORMAT_UNKNOWN;
                uav_desc.Texture2D.MipSlice = 0;
            }

            CHECK_CALL(s_device->CreateUnorderedAccessView(tex_res->texture, &uav_desc, &tex_res->uav));
        }
    }

    void direct::renderer_create_sampler(const sampler_creation_params& scp, u32 resource_slot)
    {
        _res_pool.grow(resource_slot);

        u32                resource_index = resource_slot;
        D3D11_SAMPLER_DESC desc = {};
        memcpy(&desc, &scp, sizeof(desc));
        desc.Filter = to_d3d11_filter_mode(scp.filter);
        desc.AddressU = to_d3d11_texture_address_mode(scp.address_u);
        desc.AddressV = to_d3d11_texture_address_mode(scp.address_v);
        desc.AddressW = to_d3d11_texture_address_mode(scp.address_w);
        if (scp.comparison_func != PEN_COMPARISON_DISABLED)
        {
            desc.ComparisonFunc = to_d3d11_comparison(scp.comparison_func);
            desc.Filter = to_d3d11_comparison_filter_mode(scp.filter);
        }

        CHECK_CALL(s_device->CreateSamplerState(&desc, &_res_pool[resource_index].sampler_state));
    }

    void direct::renderer_set_texture(u32 texture_index, u32 sampler_index, u32 unit, u32 bind_flags)
    {
        static ID3D11SamplerState*        null_sampler = nullptr;
        static ID3D11ShaderResourceView*  null_srv = nullptr;
        static ID3D11UnorderedAccessView* null_uav = nullptr;

        ID3D11SamplerState**        sampler = &null_sampler;
        ID3D11ShaderResourceView**  srv = &null_srv;
        ID3D11UnorderedAccessView** uav = &null_uav;

        if (sampler_index > 0)
        {
            sampler = &_res_pool[sampler_index].sampler_state;
        }

        if (texture_index > 0)
        {
            // auto gen mip maps
            bool gen_mips = false;
            if (_res_pool[texture_index].type == RES_RENDER_TARGET)
            {
                if (_res_pool[texture_index].render_target->has_mips)
                    if (_res_pool[texture_index].render_target->invalidate)
                        gen_mips = true;
            }

            if (_res_pool[texture_index].type == RES_RENDER_TARGET && bind_flags & TEXTURE_BIND_MSAA)
            {
                render_target_internal* rt = _res_pool[texture_index].render_target;
                srv = &rt->tex_msaa.srv;
            }
            else
            {
                srv = &_res_pool[texture_index].texture_resource->srv;
            }

            if (gen_mips)
            {
                s_immediate_context->GenerateMips(*srv);
                _res_pool[texture_index].render_target->invalidate = 0;
            }

            auto* tex_res = _res_pool[texture_index].texture_resource;
            if (tex_res->uav)
            {
                uav = &tex_res->uav;
            }
        }

        if (bind_flags & TEXTURE_BIND_PS)
        {
            s_immediate_context->PSSetSamplers(unit, 1, sampler);
            s_immediate_context->PSSetShaderResources(unit, 1, srv);
        }

        if (bind_flags & TEXTURE_BIND_VS)
        {
            s_immediate_context->VSSetSamplers(unit, 1, sampler);
            s_immediate_context->VSSetShaderResources(unit, 1, srv);
        }

        if (bind_flags & TEXTURE_BIND_CS)
        {
            s_immediate_context->CSSetUnorderedAccessViews(unit, 1, uav, nullptr);

            if (!(*uav))
                s_immediate_context->CSSetShaderResources(unit, 1, srv);
        }
    }

    void direct::renderer_create_raster_state(const raster_state_creation_params& rscp, u32 resource_slot)
    {
        _res_pool.grow(resource_slot);

        u32 resource_index = resource_slot;

        // convert
        D3D11_RASTERIZER_DESC rd;
        memcpy(&rd, &rscp, sizeof(D3D11_RASTERIZER_DESC));
        rd.FillMode = to_d3d11_fill_mode(rd.FillMode);
        rd.CullMode = to_d3d11_cull_mode(rd.CullMode);

        CHECK_CALL(s_device->CreateRasterizerState(&rd, &_res_pool[resource_index].raster_state));
    }

    void direct::renderer_set_raster_state(u32 rasterizer_state_index)
    {
        s_immediate_context->RSSetState(_res_pool[rasterizer_state_index].raster_state);
    }

    void direct::renderer_set_viewport(const viewport& vp)
    {
        viewport _vp = _renderer_resolve_viewport_ratio(vp);
        s_immediate_context->RSSetViewports(1, (D3D11_VIEWPORT*)&_vp);
    }

    void direct::renderer_create_blend_state(const blend_creation_params& bcp, u32 resource_slot)
    {
        _res_pool.grow(resource_slot);

        u32 resource_index = resource_slot;

        D3D11_BLEND_DESC bd;
        pen::memory_zero(&bd, sizeof(D3D11_BLEND_DESC));

        bd.AlphaToCoverageEnable = bcp.alpha_to_coverage_enable;
        bd.IndependentBlendEnable = bcp.independent_blend_enable;

        for (u32 i = 0; i < bcp.num_render_targets; ++i)
        {
            memcpy(&bd.RenderTarget[i], (void*)&(bcp.render_targets[i]), sizeof(render_target_blend));
            bd.RenderTarget[i].RenderTargetWriteMask = min<u8>(bcp.render_targets[i].render_target_write_mask, 0xf);
            bd.RenderTarget[i].BlendOp = to_d3d11_blend_op(bcp.render_targets[i].blend_op);
            bd.RenderTarget[i].BlendOpAlpha = to_d3d11_blend_op(bcp.render_targets[i].blend_op_alpha);
            bd.RenderTarget[i].SrcBlend = to_d3d11_blend_factor(bcp.render_targets[i].src_blend);
            bd.RenderTarget[i].DestBlend = to_d3d11_blend_factor(bcp.render_targets[i].dest_blend);
            bd.RenderTarget[i].SrcBlendAlpha = to_d3d11_blend_factor(bcp.render_targets[i].src_blend_alpha);
            bd.RenderTarget[i].DestBlendAlpha = to_d3d11_blend_factor(bcp.render_targets[i].dest_blend_alpha);
        }

        CHECK_CALL(s_device->CreateBlendState(&bd, &_res_pool[resource_index].blend_state));
    }

    void direct::renderer_set_blend_state(u32 blend_state_index)
    {
        s_immediate_context->OMSetBlendState(_res_pool[blend_state_index].blend_state, NULL, 0xffffffff);
    }

    void direct::renderer_set_constant_buffer(u32 buffer_index, u32 unit, u32 flags)
    {
        if (flags & pen::CBUFFER_BIND_PS)
        {
            s_immediate_context->PSSetConstantBuffers(unit, 1, &_res_pool[buffer_index].generic_buffer.buf);
        }

        if (flags & pen::CBUFFER_BIND_VS)
        {
            s_immediate_context->VSSetConstantBuffers(unit, 1, &_res_pool[buffer_index].generic_buffer.buf);
        }

        if (flags & pen::CBUFFER_BIND_CS)
        {
            s_immediate_context->CSSetConstantBuffers(unit, 1, &_res_pool[buffer_index].generic_buffer.buf);
        }
    }

    void direct::renderer_set_structured_buffer(u32 buffer_index, u32 unit, u32 flags)
    {
        static ID3D11Buffer*              null_buffer = nullptr;
        static ID3D11ShaderResourceView*  null_srv = nullptr;
        static ID3D11UnorderedAccessView* null_uav = nullptr;

        ID3D11Buffer**              buffer = &null_buffer;
        ID3D11ShaderResourceView**  srv = &null_srv;
        ID3D11UnorderedAccessView** uav = &null_uav;

        if (buffer_index > 0)
        {
            buffer = &_res_pool[buffer_index].generic_buffer.buf;
            srv = &_res_pool[buffer_index].generic_buffer.srv;
            uav = &_res_pool[buffer_index].generic_buffer.uav;
        }

        if (flags & pen::SBUFFER_BIND_CS)
        {
            if (flags & SBUFFER_BIND_WRITE)
            {
                // rw
                s_immediate_context->CSSetUnorderedAccessViews(unit, 1, uav, nullptr);
            }
            else
            {
                // r
                s_immediate_context->CSSetShaderResources(unit, 1, srv);
            }
        }

        if (flags & pen::SBUFFER_BIND_VS)
        {
            s_immediate_context->VSSetShaderResources(unit, 1, srv);
        }

        if (flags & pen::SBUFFER_BIND_PS)
        {
            s_immediate_context->PSSetShaderResources(unit, 1, srv);
        }
    }

    void direct::renderer_update_buffer(u32 buffer_index, const void* data, u32 data_size, u32 offset)
    {
        D3D11_MAPPED_SUBRESOURCE mapped_res = {0};

        s_immediate_context->Map(_res_pool[buffer_index].generic_buffer.buf, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped_res);

        void* p_data = (void*)((size_t)mapped_res.pData + offset);
        memcpy(p_data, data, data_size);

        s_immediate_context->Unmap(_res_pool[buffer_index].generic_buffer.buf, 0);
    }

    void direct::renderer_read_back_resource(const resource_read_back_params& rrbp)
    {
        D3D11_MAPPED_SUBRESOURCE mapped_res = {0};

        if (_res_pool[rrbp.resource_index].type == RES_RENDER_TARGET)
        {
            render_target_internal* rt = _res_pool[rrbp.resource_index].render_target;

            if (rt->msaa_resolve_readback)
            {
                // resolve and copy into staging
                s_immediate_context->ResolveSubresource(rt->tex_resolve.texture, 0, rt->tex.texture, 0, rt->format);
                s_immediate_context->CopyResource(rt->tex_read_back.texture, rt->tex_resolve.texture);
            }
            else
            {
                s_immediate_context->CopyResource(rt->tex_read_back.texture, rt->tex.texture);
            }

            CHECK_CALL(s_immediate_context->Map(rt->tex_read_back.texture, 0, D3D11_MAP_READ, 0, &mapped_res));

            rrbp.call_back_function((void*)mapped_res.pData, mapped_res.RowPitch, mapped_res.DepthPitch, rrbp.block_size);

            s_immediate_context->Unmap(rt->tex_read_back.texture, 0);
        }
        else if (_res_pool[rrbp.resource_index].type == RES_TEXTURE)
        {
            texture2d_internal* tex = _res_pool[rrbp.resource_index].texture_2d;

            s_immediate_context->Map(tex->texture, 0, D3D11_MAP_READ, 0, &mapped_res);

            rrbp.call_back_function((void*)mapped_res.pData, mapped_res.RowPitch, mapped_res.DepthPitch, rrbp.block_size);

            s_immediate_context->Unmap(tex->texture, 0);
        }
    }

    void direct::renderer_create_depth_stencil_state(const depth_stencil_creation_params& dscp, u32 resource_slot)
    {
        _res_pool.grow(resource_slot);

        u32 resource_index = resource_slot;

        D3D11_DEPTH_STENCIL_DESC desc = {};
        memcpy(&desc, &dscp, sizeof(desc));

        if (dscp.stencil_enable)
        {
            desc.BackFace.StencilFailOp = to_d3d11_stencil_op(dscp.back_face.stencil_failop);
            desc.BackFace.StencilDepthFailOp = to_d3d11_stencil_op(dscp.back_face.stencil_depth_failop);
            desc.BackFace.StencilPassOp = to_d3d11_stencil_op(dscp.back_face.stencil_passop);
            desc.BackFace.StencilFunc = to_d3d11_comparison(dscp.back_face.stencil_func);
            desc.FrontFace.StencilFailOp = to_d3d11_stencil_op(dscp.front_face.stencil_failop);
            desc.FrontFace.StencilDepthFailOp = to_d3d11_stencil_op(dscp.front_face.stencil_depth_failop);
            desc.FrontFace.StencilPassOp = to_d3d11_stencil_op(dscp.front_face.stencil_passop);
            desc.FrontFace.StencilFunc = to_d3d11_comparison(dscp.front_face.stencil_func);
        }

        desc.DepthFunc = to_d3d11_comparison(dscp.depth_func);

        CHECK_CALL(s_device->CreateDepthStencilState(&desc, &_res_pool[resource_index].depth_stencil_state));
    }

    void direct::renderer_set_depth_stencil_state(u32 depth_stencil_state)
    {
        g_context.depth_stencil_state = depth_stencil_state;
        s_immediate_context->OMSetDepthStencilState(_res_pool[g_context.depth_stencil_state].depth_stencil_state,
                                                    g_context.stencil_ref);
    }

    void direct::renderer_set_stencil_ref(u8 ref)
    {
        g_context.stencil_ref = ref;
        if (g_context.depth_stencil_state)
            s_immediate_context->OMSetDepthStencilState(_res_pool[g_context.depth_stencil_state].depth_stencil_state,
                                                        g_context.stencil_ref);
    }

    void direct::renderer_release_shader(u32 shader_index, u32 shader_type)
    {
        if (shader_type == PEN_SHADER_TYPE_PS)
        {
            if (_res_pool[shader_index].pixel_shader)
                _res_pool[shader_index].pixel_shader->Release();
        }
        else if (shader_type == PEN_SHADER_TYPE_VS)
        {
            if (_res_pool[shader_index].vertex_shader)
                _res_pool[shader_index].vertex_shader->Release();
        }
        else if (shader_type == PEN_SHADER_TYPE_GS)
        {
            if (_res_pool[shader_index].geometry_shader)
                _res_pool[shader_index].geometry_shader->Release();
        }
    }

    void direct::renderer_release_buffer(u32 buffer_index)
    {
        _res_pool[buffer_index].generic_buffer.buf->Release();
    }

    void direct::renderer_release_texture(u32 texture_index)
    {
        _res_pool[texture_index].texture_2d->texture->Release();
        _res_pool[texture_index].texture_2d->srv->Release();
    }

    void direct::renderer_release_raster_state(u32 raster_state_index)
    {
        _res_pool[raster_state_index].raster_state->Release();
    }

    void direct::renderer_release_blend_state(u32 blend_state)
    {
        _res_pool[blend_state].blend_state->Release();
    }

    void release_render_target_internal(u32 render_target)
    {
        _renderer_untrack_managed_render_target(render_target);

        render_target_internal* rt = _res_pool[render_target].render_target;

        for (s32 i = 0; i < rt->num_arrays; ++i)
        {
            if (rt->rt && rt->rt[i])
            {
                rt->rt[i]->Release();
                rt->rt[i] = nullptr;
            }

            if (rt->rt_msaa && rt->rt_msaa[i])
            {
                rt->rt_msaa[i]->Release();
                rt->rt_msaa[i] = nullptr;
            }
        }

        if (rt->tex.texture)
            rt->tex.texture->Release();

        if (rt->tex.srv)
            rt->tex.srv->Release();

        if (rt->tex_msaa.texture)
            rt->tex_msaa.texture->Release();

        if (rt->tex_msaa.srv)
            rt->tex_msaa.srv->Release();

        if (rt->tex_read_back.texture)
            rt->tex_read_back.texture->Release();

        rt->tex_msaa.srv = nullptr;
        rt->tex_msaa.texture = nullptr;
        rt->tex.srv = nullptr;
        rt->tex.texture = nullptr;

        delete rt->tcp;
    }

    void direct::renderer_release_render_target(u32 render_target)
    {
        release_render_target_internal(render_target);
    }

    void direct::renderer_release_input_layout(u32 input_layout)
    {
        _res_pool[input_layout].input_layout->Release();
    }

    void direct::renderer_release_sampler(u32 sampler)
    {
        _res_pool[sampler].sampler_state->Release();
    }

    void direct::renderer_release_depth_stencil_state(u32 depth_stencil_state)
    {
        _res_pool[depth_stencil_state].depth_stencil_state->Release();
    }

    void direct::renderer_release_clear_state(u32 clear_state)
    {
        memory_free(_res_pool[clear_state].clear_state);
    }

    void direct::renderer_set_stream_out_target(u32 buffer_index)
    {
        if (buffer_index == 0)
        {
            s_immediate_context->SOSetTargets(0, NULL, 0);
        }
        else
        {
            ID3D11Buffer* buffers[] = {_res_pool[buffer_index].generic_buffer.buf};
            UINT          offsets[] = {0};

            s_immediate_context->SOSetTargets(1, buffers, offsets);
        }
    }

    void direct::renderer_resolve_target(u32 target, e_msaa_resolve_type type, resolve_resources res)
    {
        if (type == RESOLVE_GENERATE_MIPS)
        {
            // 3d texture render targets are just 3d textures
            if (_res_pool[target].type == RES_TEXTURE_3D)
                s_immediate_context->GenerateMips(_res_pool[target].texture_3d->srv);

            return;
        }

        render_target_internal* rti = _res_pool[target].render_target;

        if (!rti->tcp)
            return;

        // get dimensions for shader
        f32 w = rti->tcp->width;
        f32 h = rti->tcp->height;

        if (rti->tcp->width == -1)
        {
            w = pen_window.width / h;
            h = pen_window.height / h;
        }

        // create resolve surface if required
        if (!rti->tex.texture)
        {
            // create a resolve surface
            texture_creation_params resolve_tcp = *rti->tcp;
            resolve_tcp.sample_count = 1;

            texture_creation_params& _tcp = resolve_tcp;
            _tcp.width = w;
            _tcp.height = h;

            // depth gets resolved into colour textures
            if (rti->format == PEN_TEX_FORMAT_D24_UNORM_S8_UINT)
            {
                resolve_tcp.bind_flags &= ~PEN_BIND_DEPTH_STENCIL;
                resolve_tcp.bind_flags |= PEN_BIND_RENDER_TARGET;
                resolve_tcp.format = PEN_TEX_FORMAT_R32_FLOAT;
            }

            renderer_create_render_target_multi(_tcp, &rti->tex, &_res_pool[target].depth_target->ds, &rti->rt);
        }

        if (!rti->tex_msaa.texture)
        {
            PEN_LOG("[error] renderer : render target %i is not an msaa target\n", target);
            return;
        }

        if (type == RESOLVE_CUSTOM)
        {
            resolve_cbuffer cbuf = {w, h, 0.0f, 0.0f};

            direct::renderer_set_resolve_targets(target, 0);

            direct::renderer_update_buffer(res.constant_buffer, &cbuf, sizeof(cbuf), 0);
            direct::renderer_set_constant_buffer(res.constant_buffer, 0, pen::CBUFFER_BIND_PS);

            pen::viewport vp = {0.0f, 0.0f, w, h, 0.0f, 1.0f};
            direct::renderer_set_viewport(vp);

            u32 stride = 24;
            u32 offset = 0;
            direct::renderer_set_vertex_buffers(&res.vertex_buffer, 1, 0, &stride, &offset);
            direct::renderer_set_index_buffer(res.index_buffer, PEN_FORMAT_R16_UINT, 0);

            direct::renderer_set_texture(target, 0, 0, pen::TEXTURE_BIND_MSAA | pen::TEXTURE_BIND_PS);

            direct::renderer_draw_indexed(6, 0, 0, PEN_PT_TRIANGLELIST);
        }
        else
        {
            if (rti->format == PEN_TEX_FORMAT_D24_UNORM_S8_UINT)
            {
                PEN_LOG("[error] renderer : render target %i cannot be resolved as it is a depth target\n", target);
                return;
            }

            s_immediate_context->ResolveSubresource(rti->tex.texture, 0, rti->tex_msaa.texture, 0, rti->format);
        }
    }

    void direct::renderer_draw_auto()
    {
        s_immediate_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);
        s_immediate_context->DrawAuto();
    }

    void direct::renderer_dispatch_compute(uint3 grid, uint3 num_threads)
    {
        static ID3D11UnorderedAccessView* uav_null[8] = {nullptr};

        s_immediate_context->Dispatch(grid.x, grid.y, grid.z);
    }

    void direct::renderer_set_scissor_rect(const rect& r)
    {
        rect             _r = _renderer_resolve_scissor_ratio(r);
        const D3D11_RECT rd3d = {(LONG)_r.left, (LONG)_r.top, (LONG)_r.right, (LONG)_r.bottom};
        s_immediate_context->RSSetScissorRects(1, &rd3d);
    }

    void direct::renderer_replace_resource(u32 dest, u32 src, e_renderer_resource type)
    {
        switch (type)
        {
            case RESOURCE_TEXTURE:
                direct::renderer_release_texture(dest);
                break;
            case RESOURCE_BUFFER:
                direct::renderer_release_buffer(dest);
                break;
            case RESOURCE_VERTEX_SHADER:
                direct::renderer_release_shader(dest, PEN_SHADER_TYPE_VS);
                break;
            case RESOURCE_PIXEL_SHADER:
                direct::renderer_release_shader(dest, PEN_SHADER_TYPE_PS);
                break;
            case RESOURCE_RENDER_TARGET:
                direct::renderer_release_render_target(dest);
                break;
            default:
                break;
        }

        _res_pool[dest] = _res_pool[src];
    }

    //--------------------------------------------------------------------------------------
    // D3D Device Creation
    //--------------------------------------------------------------------------------------
    void create_rtvs(u32 crtv, u32 dsv, uint32_t w, uint32_t h)
    {
        PEN_ASSERT(crtv == PEN_BACK_BUFFER_COLOUR);
        PEN_ASSERT(dsv == PEN_BACK_BUFFER_DEPTH);

        _res_pool[crtv].type = RES_RENDER_TARGET;

        if (!_res_pool[crtv].render_target)
            _res_pool[crtv].render_target = (render_target_internal*)pen::memory_alloc(sizeof(render_target_internal));

        pen::memory_zero(_res_pool[crtv].render_target, sizeof(render_target_internal));

        if (!_res_pool[dsv].depth_target)
            _res_pool[dsv].depth_target =
                (depth_stencil_target_internal*)pen::memory_alloc(sizeof(depth_stencil_target_internal));

        pen::memory_zero(_res_pool[dsv].depth_target, sizeof(depth_stencil_target_internal));

        // alloc arrays
        _res_pool[crtv].render_target->rt = new ID3D11RenderTargetView*[1];
        _res_pool[dsv].depth_target->ds = new ID3D11DepthStencilView*[1];
        _res_pool[crtv].render_target->rt[0] = nullptr;
        _res_pool[dsv].depth_target->ds[0] = nullptr;

        // Create a render target view
        CHECK_CALL(s_swap_chain->GetBuffer(0, __uuidof(ID3D11Texture2D),
                                           reinterpret_cast<void**>(&_res_pool[crtv].render_target->tex.texture)));

        CHECK_CALL(s_device->CreateRenderTargetView(_res_pool[crtv].render_target->tex.texture, nullptr,
                                                    &_res_pool[crtv].render_target->rt[0]));

        // for readback and resolve
        DXGI_SWAP_CHAIN_DESC p_desc;
        s_swap_chain->GetDesc(&p_desc);

        _res_pool[crtv].render_target->format = p_desc.BufferDesc.Format;
        _res_pool[crtv].render_target->msaa_resolve_readback = true;

        D3D11_TEXTURE2D_DESC rb_desc;
        rb_desc.CPUAccessFlags = 0;
        rb_desc.ArraySize = 1;
        rb_desc.Format = p_desc.BufferDesc.Format;
        rb_desc.MipLevels = 1;
        rb_desc.MiscFlags = 0;
        rb_desc.Width = p_desc.BufferDesc.Width;
        rb_desc.Height = p_desc.BufferDesc.Height;
        rb_desc.SampleDesc = {1, 0};
        rb_desc.BindFlags = 0;
        rb_desc.Usage = D3D11_USAGE_DEFAULT;

        CHECK_CALL(s_device->CreateTexture2D(&rb_desc, nullptr,
                                             (ID3D11Texture2D**)&_res_pool[crtv].render_target->tex_resolve.texture));

        rb_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        rb_desc.Usage = D3D11_USAGE_STAGING;
        CHECK_CALL(s_device->CreateTexture2D(&rb_desc, nullptr,
                                             (ID3D11Texture2D**)&_res_pool[crtv].render_target->tex_read_back.texture));

        g_context.active_depth_target = PEN_BACK_BUFFER_DEPTH;
        g_context.active_colour_target[0] = PEN_BACK_BUFFER_COLOUR;
        g_context.num_active_colour_targets = 1;
        g_context.backbuffer_colour = crtv;
        g_context.backbuffer_depth = dsv;

        // Create depth stencil texture
        D3D11_TEXTURE2D_DESC descDepth;
        ZeroMemory(&descDepth, sizeof(descDepth));
        descDepth.Width = w;
        descDepth.Height = h;
        descDepth.MipLevels = 1;
        descDepth.ArraySize = 1;
        descDepth.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
        descDepth.SampleDesc.Count = pen_window.sample_count;
        descDepth.SampleDesc.Quality = 0;
        descDepth.Usage = D3D11_USAGE_DEFAULT;
        descDepth.BindFlags = D3D11_BIND_DEPTH_STENCIL;
        descDepth.CPUAccessFlags = 0;
        descDepth.MiscFlags = 0;

        CHECK_CALL(
            s_device->CreateTexture2D(&descDepth, nullptr, (ID3D11Texture2D**)&_res_pool[dsv].depth_target->tex.texture));

        // Create the depth stencil view
        D3D11_DEPTH_STENCIL_VIEW_DESC descDSV;
        ZeroMemory(&descDSV, sizeof(descDSV));
        descDSV.Format = descDepth.Format;
        descDSV.ViewDimension = pen_window.sample_count > 1 ? D3D11_DSV_DIMENSION_TEXTURE2DMS : D3D11_DSV_DIMENSION_TEXTURE2D;
        descDSV.Texture2D.MipSlice = 0;
        CHECK_CALL(s_device->CreateDepthStencilView(_res_pool[dsv].depth_target->tex.texture, &descDSV,
                                                    &_res_pool[dsv].depth_target->ds[0]));

        s_immediate_context->OMSetRenderTargets(1, &_res_pool[crtv].render_target->rt[0], NULL);
    }

    void caps_init();
    u32  direct::renderer_initialise(void* params, u32 bb_res, u32 bb_depth_res)
    {
        _res_pool.init(2048);

        HWND* hwnd = (HWND*)params;

        HRESULT hr = S_OK;

        RECT rc;
        GetClientRect(*hwnd, &rc);
        UINT width = rc.right - rc.left;
        UINT height = rc.bottom - rc.top;

        UINT createDeviceFlags = 0;

#ifdef _DEBUG
        createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

        D3D_DRIVER_TYPE driverTypes[] = {
            D3D_DRIVER_TYPE_HARDWARE,
            D3D_DRIVER_TYPE_WARP,
            D3D_DRIVER_TYPE_REFERENCE,
        };
        UINT numDriverTypes = ARRAYSIZE(driverTypes);

        D3D_FEATURE_LEVEL featureLevels[] = {
            D3D_FEATURE_LEVEL_11_1,
            D3D_FEATURE_LEVEL_11_0,
            D3D_FEATURE_LEVEL_10_1,
            D3D_FEATURE_LEVEL_10_0,
        };
        UINT numFeatureLevels = ARRAYSIZE(featureLevels);

        for (UINT driverTypeIndex = 0; driverTypeIndex < numDriverTypes; driverTypeIndex++)
        {
            s_driverType = driverTypes[driverTypeIndex];
            hr = D3D11CreateDevice(nullptr, s_driverType, nullptr, createDeviceFlags, featureLevels, numFeatureLevels,
                                   D3D11_SDK_VERSION, &s_device, &s_featureLevel, &s_immediate_context);

            if (hr == E_INVALIDARG)
            {
                // DirectX 11.0 platforms will not recognize D3D_FEATURE_LEVEL_11_1 so we need to retry without it
                hr = D3D11CreateDevice(nullptr, s_driverType, nullptr, createDeviceFlags, &featureLevels[1],
                                       numFeatureLevels - 1, D3D11_SDK_VERSION, &s_device, &s_featureLevel,
                                       &s_immediate_context);
            }

            if (SUCCEEDED(hr))
                break;
        }
        if (FAILED(hr))
            return hr;

        // Obtain DXGI factory from device (since we used nullptr for pAdapter above)
        IDXGIFactory1* dxgiFactory = nullptr;
        {
            IDXGIDevice* dxgiDevice = nullptr;
            hr = s_device->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(&dxgiDevice));
            if (SUCCEEDED(hr))
            {
                IDXGIAdapter* adapter = nullptr;
                hr = dxgiDevice->GetAdapter(&adapter);
                if (SUCCEEDED(hr))
                {
                    hr = adapter->GetParent(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&dxgiFactory));
                    adapter->Release();
                }
                dxgiDevice->Release();
            }
        }
        if (FAILED(hr))
            return hr;

        // Create swap chain
        IDXGIFactory2* dxgiFactory2 = nullptr;
        hr = dxgiFactory->QueryInterface(__uuidof(IDXGIFactory2), reinterpret_cast<void**>(&dxgiFactory2));
        if (dxgiFactory2)
        {
            // DirectX 11.1 or later
            hr = s_device->QueryInterface(__uuidof(ID3D11Device1), reinterpret_cast<void**>(&s_device_1));
            if (SUCCEEDED(hr))
            {
                (void)s_immediate_context->QueryInterface(__uuidof(ID3D11DeviceContext1),
                                                          reinterpret_cast<void**>(&s_immediate_context_1));
            }

            DXGI_SWAP_CHAIN_DESC1 sd;
            ZeroMemory(&sd, sizeof(sd));
            sd.Width = width;
            sd.Height = height;
            sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            sd.SampleDesc.Count = pen_window.sample_count;
            sd.SampleDesc.Quality = 0;
            sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
            sd.BufferCount = 1;

            hr = dxgiFactory2->CreateSwapChainForHwnd(s_device, *hwnd, &sd, nullptr, nullptr, &s_swap_chain_1);
            if (SUCCEEDED(hr))
            {
                hr = s_swap_chain_1->QueryInterface(__uuidof(IDXGISwapChain), reinterpret_cast<void**>(&s_swap_chain));
            }

            dxgiFactory2->Release();
        }
        else
        {
            // DirectX 11.0 systems
            DXGI_SWAP_CHAIN_DESC sd;
            ZeroMemory(&sd, sizeof(sd));
            sd.BufferCount = 1;
            sd.BufferDesc.Width = width;
            sd.BufferDesc.Height = height;
            sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            sd.BufferDesc.RefreshRate.Numerator = 60;
            sd.BufferDesc.RefreshRate.Denominator = 1;
            sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
            sd.OutputWindow = *hwnd;
            sd.SampleDesc.Count = pen_window.sample_count;
            sd.SampleDesc.Quality = 0;
            sd.Windowed = TRUE;
            sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

            hr = dxgiFactory->CreateSwapChain(s_device, &sd, &s_swap_chain);
        }

        dxgiFactory->Release();

        if (FAILED(hr))
            return hr;

        create_rtvs(bb_res, bb_depth_res, width, height);

        caps_init();

        return S_OK;
    }

    //--------------------------------------------------------------------------------------
    // Base renderer cleanup
    //--------------------------------------------------------------------------------------
    void renderer_destroy()
    {
        if (s_immediate_context)
            s_immediate_context->ClearState();
        if (s_backbuffer_rtv)
            s_backbuffer_rtv->Release();

        if (s_swap_chain)
            s_swap_chain->Release();
        if (s_swap_chain_1)
            s_swap_chain_1->Release();

        if (s_immediate_context)
            s_immediate_context->Release();
        if (s_immediate_context_1)
            s_immediate_context_1->Release();

        if (s_device)
            s_device->Release();
        if (s_device_1)
            s_device_1->Release();
    }

    static renderer_info s_renderer_info;
    static Str           str_hlsl_version;
    static Str           str_d3d_version;
    static Str           str_d3d_renderer;
    static Str           str_d3d_vendor;

    void caps_init()
    {
        // todo renderer caps

        s_renderer_info.shader_version = str_hlsl_version.c_str();
        s_renderer_info.api_version = str_d3d_version.c_str();
        s_renderer_info.renderer = str_d3d_renderer.c_str();
        s_renderer_info.vendor = str_d3d_vendor.c_str();
        s_renderer_info.renderer_cmd = " -renderer dx11";

        s_renderer_info.caps |= PEN_CAPS_TEX_FORMAT_BC1;
        s_renderer_info.caps |= PEN_CAPS_TEX_FORMAT_BC2;
        s_renderer_info.caps |= PEN_CAPS_TEX_FORMAT_BC3;
        s_renderer_info.caps |= PEN_CAPS_TEX_FORMAT_BC4;
        s_renderer_info.caps |= PEN_CAPS_TEX_FORMAT_BC5;
        s_renderer_info.caps |= PEN_CAPS_TEX_FORMAT_BC6;
        s_renderer_info.caps |= PEN_CAPS_TEX_FORMAT_BC7;
        s_renderer_info.caps |= PEN_CAPS_GPU_TIMER;
        s_renderer_info.caps |= PEN_CAPS_DEPTH_CLAMP;
        s_renderer_info.caps |= PEN_CAPS_COMPUTE;
        s_renderer_info.caps |= PEN_CAPS_TEXTURE_CUBE_ARRAY;
    }

    const renderer_info& renderer_get_info()
    {
        return s_renderer_info;
    }

    const c8* renderer_get_shader_platform()
    {
        return "hlsl";
    }

    bool renderer_viewport_vup()
    {
        return false;
    }

    bool renderer_depth_0_to_1()
    {
        return true;
    }
} // namespace pen
