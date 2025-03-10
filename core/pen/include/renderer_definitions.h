// renderer_definitions.h
// Copyright 2014 - 2023 Alex Dixon.
// License: https://github.com/polymonster/pmtech/blob/master/license.md

#pragma once

enum null_values
{
    PEN_NULL_DEPTH_BUFFER = -1,
    PEN_NULL_COLOUR_BUFFER = -1,
    PEN_NULL_PIXEL_SHADER = -1,
};

enum shader_type
{
    PEN_SHADER_TYPE_VS,
    PEN_SHADER_TYPE_PS,
    PEN_SHADER_TYPE_GS,
    PEN_SHADER_TYPE_SO,
    PEN_SHADER_TYPE_CS
};

enum fill_mode
{
    PEN_FILL_SOLID,
    PEN_FILL_WIREFRAME
};

enum cull_mode
{
    PEN_CULL_NONE,
    PEN_CULL_FRONT,
    PEN_CULL_BACK
};

enum default_targets
{
    PEN_BACK_BUFFER_COLOUR = 1,
    PEN_BACK_BUFFER_DEPTH = 2
};

enum texture_format
{
    // integer
    PEN_TEX_FORMAT_BGRA8_UNORM,
    PEN_TEX_FORMAT_RGBA8_UNORM,

    // depth formats
    PEN_TEX_FORMAT_D24_UNORM_S8_UINT,
    PEN_TEX_FORMAT_D32_FLOAT,
    PEN_TEX_FORMAT_D32_FLOAT_S8_UINT,

    // floating point
    PEN_TEX_FORMAT_R32G32B32A32_FLOAT,
    PEN_TEX_FORMAT_R32_FLOAT,
    PEN_TEX_FORMAT_R16G16B16A16_FLOAT,
    PEN_TEX_FORMAT_R16_FLOAT,
    PEN_TEX_FORMAT_R32_UINT,
    PEN_TEX_FORMAT_R8_UNORM,
    PEN_TEX_FORMAT_R32G32_FLOAT,

    // bc compressed
    PEN_TEX_FORMAT_BC1_UNORM,
    PEN_TEX_FORMAT_BC2_UNORM,
    PEN_TEX_FORMAT_BC3_UNORM,
    PEN_TEX_FORMAT_BC4_UNORM,
    PEN_TEX_FORMAT_BC5_UNORM
};

enum clear_bits
{
    PEN_CLEAR_COLOUR_BUFFER = 1 << 0,
    PEN_CLEAR_DEPTH_BUFFER = 1 << 1,
    PEN_CLEAR_STENCIL_BUFFER = 1 << 2,
};

enum input_classification
{
    PEN_INPUT_PER_VERTEX,
    PEN_INPUT_PER_INSTANCE
};

enum primitive_topology
{
    PEN_PT_POINTLIST,
    PEN_PT_LINELIST,
    PEN_PT_LINESTRIP,
    PEN_PT_TRIANGLELIST,
    PEN_PT_TRIANGLESTRIP
};

enum vertex_format
{
    PEN_VERTEX_FORMAT_FLOAT1,
    PEN_VERTEX_FORMAT_FLOAT2,
    PEN_VERTEX_FORMAT_FLOAT3,
    PEN_VERTEX_FORMAT_FLOAT4,
    PEN_VERTEX_FORMAT_UNORM4,
    PEN_VERTEX_FORMAT_UNORM2,
    PEN_VERTEX_FORMAT_UNORM1
};

enum index_buffer_format
{
    PEN_FORMAT_R16_UINT,
    PEN_FORMAT_R32_UINT
};

enum usage
{
    PEN_USAGE_DEFAULT,   // gpu read and write, d3d can updatesubresource with usage default
    PEN_USAGE_IMMUTABLE, // gpu read only
    PEN_USAGE_DYNAMIC,   // dynamic
    PEN_USAGE_STAGING    // cpu access
};

enum bind_flags
{
    PEN_BIND_SHADER_RESOURCE = 1 << 0,
    PEN_BIND_VERTEX_BUFFER = 1 << 1,
    PEN_BIND_INDEX_BUFFER = 1 << 2,
    PEN_BIND_CONSTANT_BUFFER = 1 << 3,
    PEN_BIND_RENDER_TARGET = 1 << 5,
    PEN_BIND_DEPTH_STENCIL = 1 << 6,
    PEN_BIND_SHADER_WRITE = 1 << 7,
    PEN_STREAM_OUT_VERTEX_BUFFER = 1 << 8 // needs renaming
};

enum cpu_access_flags
{
    PEN_CPU_ACCESS_WRITE = 1 << 0,
    PEN_CPU_ACCESS_READ = 1 << 1
};

enum texture_address_mode
{
    PEN_TEXTURE_ADDRESS_WRAP,
    PEN_TEXTURE_ADDRESS_MIRROR,
    PEN_TEXTURE_ADDRESS_CLAMP,
    PEN_TEXTURE_ADDRESS_BORDER,
    PEN_TEXTURE_ADDRESS_MIRROR_ONCE
};

enum filter_mode
{
    PEN_FILTER_MIN_MAG_MIP_LINEAR,
    PEN_FILTER_MIN_MAG_MIP_POINT,
    PEN_FILTER_LINEAR,
    PEN_FILTER_POINT
};

enum comparison
{
    PEN_COMPARISON_DISABLED,
    PEN_COMPARISON_NEVER,
    PEN_COMPARISON_LESS,
    PEN_COMPARISON_EQUAL,
    PEN_COMPARISON_LESS_EQUAL,
    PEN_COMPARISON_GREATER,
    PEN_COMPARISON_NOT_EQUAL,
    PEN_COMPARISON_GREATER_EQUAL,
    PEN_COMPARISON_ALWAYS
};

enum stencil_op
{
    PEN_STENCIL_OP_KEEP,
    PEN_STENCIL_OP_REPLACE,
    PEN_STENCIL_OP_ZERO,
    PEN_STENCIL_OP_INCR_SAT,
    PEN_STENCIL_OP_DECR_SAT,
    PEN_STENCIL_OP_INVERT,
    PEN_STENCIL_OP_INCR,
    PEN_STENCIL_OP_DECR
};

enum blending_factor
{
    PEN_BLEND_ZERO,
    PEN_BLEND_ONE,
    PEN_BLEND_SRC_COLOR,
    PEN_BLEND_INV_SRC_COLOR,
    PEN_BLEND_SRC_ALPHA,
    PEN_BLEND_INV_SRC_ALPHA,
    PEN_BLEND_DEST_ALPHA,
    PEN_BLEND_INV_DEST_ALPHA,
    PEN_BLEND_DEST_COLOR,
    PEN_BLEND_INV_DEST_COLOR,
    PEN_BLEND_SRC_ALPHA_SAT,
    PEN_BLEND_BLEND_FACTOR,
    PEN_BLEND_INV_BLEND_FACTOR,
    PEN_BLEND_SRC1_COLOR,
    PEN_BLEND_INV_SRC1_COLOR,
    PEN_BLEND_SRC1_ALPHA,
    PEN_BLEND_INV_SRC1_ALPHA
};

enum blend_op
{
    PEN_BLEND_OP_ADD,
    PEN_BLEND_OP_SUBTRACT,
    PEN_BLEND_OP_REV_SUBTRACT,
    PEN_BLEND_OP_MIN,
    PEN_BLEND_OP_MAX
};
