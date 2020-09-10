#include "pmfx.h"
#include "loader.h"

#include "pen.h"
#include "os.h"
#include "memory.h"
#include "file_system.h"
#include "pen_string.h"
#include "renderer.h"
#include "threads.h"
#include "timer.h"

using namespace put;
using namespace pen;

namespace
{
    void*   user_setup(void* params);
    loop_t  user_update();
    void    user_shutdown();
}

namespace pen
{
    pen_creation_params pen_entry(int argc, char** argv)
    {
        pen::pen_creation_params p;
        p.window_width = 1280;
        p.window_height = 720;
        p.window_title = "basic_texture";
        p.window_sample_count = 4;
        p.user_thread_function = user_setup;
        p.flags = pen::e_pen_create_flags::renderer;
        return p;
    }
} // namespace pen

namespace
{
    struct vertex
    {
        f32 x, y, z, w;
    };

    struct textured_vertex
    {
        f32 x, y, z, w;
        f32 u, v;
    };

    job_thread_params*  s_job_params = nullptr;
    job*                s_thread_info = nullptr;
    u32                 s_clear_state = 0;
    u32                 s_raster_state = 0;
    u32                 s_textured_shader = 0;
    u32                 s_test_texture = 0;
    u32                 s_quad_vertex_buffer = 0;
    u32                 s_quad_index_buffer = 0;
    u32                 s_linear_sampler = 0;
    
    void* user_setup(void* params)
    {
        // unpack the params passed to the thread and signal to the engine it ok to proceed
        s_job_params = (pen::job_thread_params*)params;
        s_thread_info = s_job_params->job_info;
        pen::semaphore_post(s_thread_info->p_sem_continue, 1);

        static pen::clear_state cs = {
            0.0f, 0.0f, 0.5f, 1.0f, 1.0f, 0x00, PEN_CLEAR_COLOUR_BUFFER | PEN_CLEAR_DEPTH_BUFFER,
        };

        s_clear_state = pen::renderer_create_clear_state(cs);

        // raster state
        pen::rasteriser_state_creation_params rcp;
        pen::memory_zero(&rcp, sizeof(rasteriser_state_creation_params));
        rcp.fill_mode = PEN_FILL_SOLID;
        rcp.cull_mode = PEN_CULL_NONE;
        rcp.depth_bias_clamp = 0.0f;
        rcp.sloped_scale_depth_bias = 0.0f;

        s_raster_state = pen::renderer_create_rasterizer_state(rcp);

        s_textured_shader = pmfx::load_shader("textured");

        s_test_texture = put::load_texture("data/textures/formats/texfmt_rgba8.dds");

        // manually scale 16:9 to 1:1
        f32 x_size = 0.5f / pen::window_get_aspect();

        f32 uv_y_a = 1.0f;
        f32 uv_y_b = 0.0f;

        // create vertex buffer for a quad
        textured_vertex quad_vertices[] = {
            -x_size, -0.5f,  0.5f, 1.0f, // p1
            0.0f,    uv_y_a,             // uv1

            -x_size, 0.5f,   0.5f, 1.0f, // p2
            0.0f,    uv_y_b,             // uv2

            x_size,  0.5f,   0.5f, 1.0f, // p3
            1.0f,    uv_y_b,             // uv3

            x_size,  -0.5f,  0.5f, 1.0f, // p4
            1.0f,    uv_y_a,             // uv4
        };

        pen::buffer_creation_params bcp;
        bcp.usage_flags = PEN_USAGE_DEFAULT;
        bcp.bind_flags = PEN_BIND_VERTEX_BUFFER;
        bcp.cpu_access_flags = 0;

        bcp.buffer_size = sizeof(textured_vertex) * 4;
        bcp.data = (void*)&quad_vertices[0];

        s_quad_vertex_buffer = pen::renderer_create_buffer(bcp);

        // create index buffer
        u16 indices[] = {0, 1, 2, 2, 3, 0};

        bcp.usage_flags = PEN_USAGE_IMMUTABLE;
        bcp.bind_flags = PEN_BIND_INDEX_BUFFER;
        bcp.cpu_access_flags = 0;
        bcp.buffer_size = sizeof(u16) * 6;
        bcp.data = (void*)&indices[0];

        s_quad_index_buffer = pen::renderer_create_buffer(bcp);

        // create a sampler object so we can sample a texture
        pen::sampler_creation_params scp;
        pen::memory_zero(&scp, sizeof(pen::sampler_creation_params));
        scp.filter = PEN_FILTER_MIN_MAG_MIP_LINEAR;
        scp.address_u = PEN_TEXTURE_ADDRESS_CLAMP;
        scp.address_v = PEN_TEXTURE_ADDRESS_CLAMP;
        scp.address_w = PEN_TEXTURE_ADDRESS_CLAMP;
        scp.comparison_func = PEN_COMPARISON_DISABLED;
        scp.min_lod = 0.0f;
        scp.max_lod = 4.0f;

        s_linear_sampler = pen::renderer_create_sampler(scp);

        pen_main_loop(user_update);
        return PEN_THREAD_OK;
    }
    
    void user_shutdown()
    {
        // clean up mem
        pen::renderer_new_frame();
        pen::renderer_release_clear_state(s_clear_state);
        pen::renderer_release_raster_state(s_raster_state);
        pen::renderer_release_buffer(s_quad_vertex_buffer);
        pen::renderer_release_buffer(s_quad_index_buffer);
        pen::renderer_release_sampler(s_linear_sampler);
        pen::renderer_release_texture(s_test_texture);
        pmfx::release_shader(s_textured_shader);
        pen::renderer_present();
        pen::renderer_consume_cmd_buffer();

        // signal to the engine the thread has finished
        pen::semaphore_post(s_thread_info->p_sem_terminated, 1);
    }

    loop_t user_update()
    {
        pen::renderer_new_frame();

        pen::renderer_set_rasterizer_state(s_raster_state);

        // bind back buffer and clear
        pen::viewport vp = {0.0f, 0.0f, PEN_BACK_BUFFER_RATIO, 1, 0.0f, 1.0f};
        pen::renderer_set_viewport(vp);
        pen::renderer_set_scissor_rect(rect{vp.x, vp.y, vp.width, vp.height});

        pen::renderer_set_targets(PEN_BACK_BUFFER_COLOUR, PEN_BACK_BUFFER_DEPTH);
        pen::renderer_clear(s_clear_state);

        // draw quad
        {
            // bind vertex layout and shaders
            pmfx::set_technique(s_textured_shader, 0);

            // bind vertex buffer
            u32 stride = sizeof(textured_vertex);
            pen::renderer_set_vertex_buffer(s_quad_vertex_buffer, 0, stride, 0);
            pen::renderer_set_index_buffer(s_quad_index_buffer, PEN_FORMAT_R16_UINT, 0);

            // bind render target as texture on sampler 0
            pen::renderer_set_texture(s_test_texture, s_linear_sampler, 0, pen::TEXTURE_BIND_PS);

            // draw
            pen::renderer_draw_indexed(6, 0, 0, PEN_PT_TRIANGLELIST);
        }

        // present
        pen::renderer_present();
        pen::renderer_consume_cmd_buffer();
            
        if (pen::semaphore_try_wait(s_thread_info->p_sem_exit))
        {
            user_shutdown();
            pen_main_loop_exit();
        }
        
        pen_main_loop_continue();
    }
}
