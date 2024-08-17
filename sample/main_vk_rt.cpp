#include <application.h>
#include <camera.h>
#include <material.h>
#include <mesh.h>
#include <vk.h>
#include <profiler.h>
#include <assimp/scene.h>
#include <vk_mem_alloc.h>
#include <ray_traced_scene.h>

// Uniform buffer data structure.
struct Transforms
{
    DW_ALIGNED(16)
    glm::mat4 view_inverse;
    DW_ALIGNED(16)
    glm::mat4 proj_inverse;
};

class Sample : public dw::Application
{
protected:
    // -----------------------------------------------------------------------------------------------------------------------------------

    bool init(int argc, const char* argv[]) override
    {
        // Create GPU resources.
        if (!create_shaders())
            return false;

        if (!create_uniform_buffer())
            return false;

        // Load mesh.
        if (!load_mesh())
            return false;

        create_output_image();
        create_descriptor_set_layout();
        create_descriptor_set();
        write_descriptor_set();
        create_copy_pipeline();
        create_ray_tracing_pipeline();

        // Create camera.
        create_camera();

        return true;
    }

    // -----------------------------------------------------------------------------------------------------------------------------------

    void update(double delta) override
    {
        dw::vk::CommandBuffer::Ptr cmd_buf = m_vk_backend->allocate_graphics_command_buffer(true);

        {
            DW_SCOPED_SAMPLE("update", cmd_buf);

            m_scene->build_tlas(cmd_buf);

            // Render profiler.
#if defined(DWSF_IMGUI)
            dw::profiler::ui();
#endif

            // Update camera.
            m_main_camera->update();

            // Update uniforms.
            update_uniforms(cmd_buf);

            // Render.
            trace_scene(cmd_buf);

            render(cmd_buf);
        }

        vkEndCommandBuffer(cmd_buf->handle());

        submit_and_present({ cmd_buf });
    }

    // -----------------------------------------------------------------------------------------------------------------------------------

    void shutdown() override
    {
        m_raytracing_pipeline.reset();
        m_copy_pipeline.reset();
        m_copy_ds.reset();
        m_ray_tracing_ds.reset();
        m_ray_tracing_layout.reset();
        m_copy_layout.reset();
        m_raytracing_pipeline_layout.reset();
        m_copy_pipeline_layout.reset();
        m_sampler.reset();
        m_ubo.reset();
        m_output_view.reset();
        m_output_image.reset();
        m_sbt.reset();

        // Unload assets.
        m_scene.reset();
        m_mesh.reset();
    }

    // -----------------------------------------------------------------------------------------------------------------------------------

    dw::AppSettings intial_app_settings() override
    {
        // Set custom settings here...
        dw::AppSettings settings;

        settings.width       = 1280;
        settings.height      = 720;
        settings.title       = "Hello dwSampleFramework (Vulkan Ray-Tracing)";
        settings.ray_tracing = true;

        return settings;
    }

    // -----------------------------------------------------------------------------------------------------------------------------------

    void window_resized(int width, int height) override
    {
        // Override window resized method to update camera projection.
        m_main_camera->update_projection(60.0f, 0.1f, 1000.0f, float(m_width) / float(m_height));
        m_vk_backend->wait_idle();
        create_output_image();
        write_descriptor_set();
    }

    // -----------------------------------------------------------------------------------------------------------------------------------

private:
    // -----------------------------------------------------------------------------------------------------------------------------------

    bool create_shaders()
    {
        return true;
    }

    // -----------------------------------------------------------------------------------------------------------------------------------

    void create_output_image()
    {
        m_output_image = dw::vk::Image::create(m_vk_backend, VK_IMAGE_TYPE_2D, m_width, m_height, 1, 1, 1, m_vk_backend->swap_chain_image_format(), VMA_MEMORY_USAGE_GPU_ONLY, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_SAMPLE_COUNT_1_BIT);
        m_output_view  = dw::vk::ImageView::create(m_vk_backend, m_output_image, VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_ASPECT_COLOR_BIT);
    }

    // -----------------------------------------------------------------------------------------------------------------------------------

    bool create_uniform_buffer()
    {
        m_ubo_size = m_vk_backend->aligned_dynamic_ubo_size(sizeof(Transforms));
        m_ubo      = dw::vk::Buffer::create(m_vk_backend, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, m_ubo_size * dw::vk::Backend::kMaxFramesInFlight, VMA_MEMORY_USAGE_CPU_TO_GPU, VMA_ALLOCATION_CREATE_MAPPED_BIT);

        return true;
    }

    // -----------------------------------------------------------------------------------------------------------------------------------

    void create_descriptor_set_layout()
    {
        {
            dw::vk::DescriptorSetLayout::Desc desc;

            desc.add_binding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT);

            m_copy_layout = dw::vk::DescriptorSetLayout::create(m_vk_backend, desc);
        }

        {
            dw::vk::DescriptorSetLayout::Desc desc;

            desc.add_binding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_RAYGEN_BIT_NV);
            desc.add_binding(1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1, VK_SHADER_STAGE_RAYGEN_BIT_NV);

            m_ray_tracing_layout = dw::vk::DescriptorSetLayout::create(m_vk_backend, desc);
        }
    }

    // -----------------------------------------------------------------------------------------------------------------------------------

    void create_descriptor_set()
    {
        m_copy_ds        = m_vk_backend->allocate_descriptor_set(m_copy_layout);
        m_ray_tracing_ds = m_vk_backend->allocate_descriptor_set(m_ray_tracing_layout);
    }

    // -----------------------------------------------------------------------------------------------------------------------------------

    void write_descriptor_set()
    {
        {
            VkDescriptorImageInfo image_info;

            image_info.sampler     = dw::Material::common_sampler()->handle();
            image_info.imageView   = m_output_view->handle();
            image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            VkWriteDescriptorSet write_data;
            DW_ZERO_MEMORY(write_data);

            write_data.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write_data.descriptorCount = 1;
            write_data.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            write_data.pImageInfo      = &image_info;
            write_data.dstBinding      = 0;
            write_data.dstSet          = m_copy_ds->handle();

            vkUpdateDescriptorSets(m_vk_backend->device(), 1, &write_data, 0, nullptr);
        }

        {
            VkWriteDescriptorSet write_data[2];
            DW_ZERO_MEMORY(write_data[0]);
            DW_ZERO_MEMORY(write_data[1]);

            VkDescriptorImageInfo output_image;
            output_image.sampler     = VK_NULL_HANDLE;
            output_image.imageView   = m_output_view->handle();
            output_image.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

            write_data[0].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write_data[0].descriptorCount = 1;
            write_data[0].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            write_data[0].pImageInfo      = &output_image;
            write_data[0].dstBinding      = 0;
            write_data[0].dstSet          = m_ray_tracing_ds->handle();

            VkDescriptorBufferInfo buffer_info;

            buffer_info.buffer = m_ubo->handle();
            buffer_info.offset = 0;
            buffer_info.range  = sizeof(Transforms);

            write_data[1].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write_data[1].descriptorCount = 1;
            write_data[1].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
            write_data[1].pBufferInfo     = &buffer_info;
            write_data[1].dstBinding      = 1;
            write_data[1].dstSet          = m_ray_tracing_ds->handle();

            vkUpdateDescriptorSets(m_vk_backend->device(), 2, &write_data[0], 0, nullptr);
        }
    }

    // -----------------------------------------------------------------------------------------------------------------------------------

    void create_copy_pipeline()
    {
        dw::vk::PipelineLayout::Desc desc;

        desc.add_descriptor_set_layout(m_copy_layout);

        m_copy_pipeline_layout = dw::vk::PipelineLayout::create(m_vk_backend, desc);
        m_copy_pipeline        = dw::vk::GraphicsPipeline::create_for_post_process(m_vk_backend, "shaders/triangle.vert.spv", "shaders/copy.frag.spv", m_copy_pipeline_layout, m_vk_backend->swapchain_render_pass());
    }

    // -----------------------------------------------------------------------------------------------------------------------------------

    void create_ray_tracing_pipeline()
    {
        // ---------------------------------------------------------------------------
        // Create shader modules
        // ---------------------------------------------------------------------------

        dw::vk::ShaderModule::Ptr rgen  = dw::vk::ShaderModule::create_from_file(m_vk_backend, "shaders/mesh.rgen.spv");
        dw::vk::ShaderModule::Ptr rchit = dw::vk::ShaderModule::create_from_file(m_vk_backend, "shaders/mesh.rchit.spv");
        dw::vk::ShaderModule::Ptr rmiss = dw::vk::ShaderModule::create_from_file(m_vk_backend, "shaders/mesh.rmiss.spv");

        dw::vk::ShaderBindingTable::Desc sbt_desc;

        sbt_desc.add_ray_gen_group(rgen, "main");
        sbt_desc.add_hit_group(rchit, "main");
        sbt_desc.add_miss_group(rmiss, "main");

        m_sbt = dw::vk::ShaderBindingTable::create(m_vk_backend, sbt_desc);

        dw::vk::RayTracingPipeline::Desc desc;

        desc.set_max_pipeline_ray_recursion_depth(8);
        desc.set_shader_binding_table(m_sbt);

        // ---------------------------------------------------------------------------
        // Create pipeline layout
        // ---------------------------------------------------------------------------

        dw::vk::PipelineLayout::Desc pl_desc;

        pl_desc.add_descriptor_set_layout(m_scene->descriptor_set_layout());
        pl_desc.add_descriptor_set_layout(m_ray_tracing_layout);

        m_raytracing_pipeline_layout = dw::vk::PipelineLayout::create(m_vk_backend, pl_desc);

        desc.set_pipeline_layout(m_raytracing_pipeline_layout);

        m_raytracing_pipeline = dw::vk::RayTracingPipeline::create(m_vk_backend, desc);
    }

    // -----------------------------------------------------------------------------------------------------------------------------------

    bool load_mesh()
    {
        m_mesh = dw::Mesh::load(m_vk_backend, "teapot.obj");
        m_mesh->initialize_for_ray_tracing(m_vk_backend);

        dw::RayTracedScene::Instance instance;

        instance.mesh      = m_mesh;
        instance.transform = glm::mat4(1.0f);

        m_scene = dw::RayTracedScene::create(m_vk_backend, { instance });

        return m_mesh != nullptr;
    }

    // -----------------------------------------------------------------------------------------------------------------------------------

    void create_camera()
    {
        m_main_camera = std::make_unique<dw::Camera>(
            60.0f, 0.1f, 1000.0f, float(m_width) / float(m_height), glm::vec3(0.0f, 0.0f, 100.0f), glm::vec3(0.0f, 0.0, -1.0f));
    }

    // -----------------------------------------------------------------------------------------------------------------------------------

    void trace_scene(dw::vk::CommandBuffer::Ptr cmd_buf)
    {
        DW_SCOPED_SAMPLE("ray-tracing", cmd_buf);

        VkImageSubresourceRange subresource_range = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

        // Transition ray tracing output image back to general layout
        dw::vk::utilities::set_image_layout(
            cmd_buf->handle(),
            m_output_image->handle(),
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_GENERAL,
            subresource_range);

        auto& rt_pipeline_props = m_vk_backend->ray_tracing_pipeline_properties();

        vkCmdBindPipeline(cmd_buf->handle(), VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, m_raytracing_pipeline->handle());

        const uint32_t dynamic_offset = m_ubo_size * m_vk_backend->current_frame_idx();

        VkDescriptorSet descriptor_sets[] = {
            m_scene->descriptor_set()->handle(),
            m_ray_tracing_ds->handle(),
        };

        vkCmdBindDescriptorSets(cmd_buf->handle(), VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, m_raytracing_pipeline_layout->handle(), 0, 2, descriptor_sets, 1, &dynamic_offset);

        VkDeviceSize group_size   = dw::vk::utilities::aligned_size(rt_pipeline_props.shaderGroupHandleSize, rt_pipeline_props.shaderGroupBaseAlignment);
        VkDeviceSize group_stride = group_size;

        const VkStridedDeviceAddressRegionKHR raygen_sbt   = { m_raytracing_pipeline->shader_binding_table_buffer()->device_address(), group_stride, group_size };
        const VkStridedDeviceAddressRegionKHR miss_sbt     = { m_raytracing_pipeline->shader_binding_table_buffer()->device_address() + m_sbt->miss_group_offset(), group_stride, group_size * 2 };
        const VkStridedDeviceAddressRegionKHR hit_sbt      = { m_raytracing_pipeline->shader_binding_table_buffer()->device_address() + m_sbt->hit_group_offset(), group_stride, group_size * 2 };
        const VkStridedDeviceAddressRegionKHR callable_sbt = { 0, 0, 0 };

        vkCmdTraceRaysKHR(cmd_buf->handle(), &raygen_sbt, &miss_sbt, &hit_sbt, &callable_sbt, m_width, m_height, 1);

        // Prepare ray tracing output image as transfer source
        dw::vk::utilities::set_image_layout(
            cmd_buf->handle(),
            m_output_image->handle(),
            VK_IMAGE_LAYOUT_GENERAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            subresource_range);
    }

    // -----------------------------------------------------------------------------------------------------------------------------------

    void render(dw::vk::CommandBuffer::Ptr cmd_buf)
    {
        DW_SCOPED_SAMPLE("render", cmd_buf);

        VkClearValue clear_values[2];

        clear_values[0].color.float32[0] = 0.0f;
        clear_values[0].color.float32[1] = 0.0f;
        clear_values[0].color.float32[2] = 0.0f;
        clear_values[0].color.float32[3] = 1.0f;

        clear_values[1].color.float32[0] = 1.0f;
        clear_values[1].color.float32[1] = 1.0f;
        clear_values[1].color.float32[2] = 1.0f;
        clear_values[1].color.float32[3] = 1.0f;

        VkRenderPassBeginInfo info    = {};
        info.sType                    = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        info.renderPass               = m_vk_backend->swapchain_render_pass()->handle();
        info.framebuffer              = m_vk_backend->swapchain_framebuffer()->handle();
        info.renderArea.extent.width  = m_width;
        info.renderArea.extent.height = m_height;
        info.clearValueCount          = 2;
        info.pClearValues             = &clear_values[0];

        vkCmdBeginRenderPass(cmd_buf->handle(), &info, VK_SUBPASS_CONTENTS_INLINE);

        VkViewport vp;

        vp.x        = 0.0f;
        vp.y        = (float)m_height;
        vp.width    = (float)m_width;
        vp.height   = -(float)m_height;
        vp.minDepth = 0.0f;
        vp.maxDepth = 1.0f;

        vkCmdSetViewport(cmd_buf->handle(), 0, 1, &vp);

        VkRect2D scissor_rect;

        scissor_rect.extent.width  = m_width;
        scissor_rect.extent.height = m_height;
        scissor_rect.offset.x      = 0;
        scissor_rect.offset.y      = 0;

        vkCmdSetScissor(cmd_buf->handle(), 0, 1, &scissor_rect);

        vkCmdBindPipeline(cmd_buf->handle(), VK_PIPELINE_BIND_POINT_GRAPHICS, m_copy_pipeline->handle());
        vkCmdBindDescriptorSets(cmd_buf->handle(), VK_PIPELINE_BIND_POINT_GRAPHICS, m_copy_pipeline_layout->handle(), 0, 1, &m_copy_ds->handle(), 0, nullptr);

        vkCmdDraw(cmd_buf->handle(), 3, 1, 0, 0);

#if defined(DWSF_IMGUI)
        render_gui(cmd_buf);
#endif

        vkCmdEndRenderPass(cmd_buf->handle());
    }

    // -----------------------------------------------------------------------------------------------------------------------------------

    void update_uniforms(dw::vk::CommandBuffer::Ptr cmd_buf)
    {
        DW_SCOPED_SAMPLE("update_uniforms", cmd_buf);

        m_transforms.proj_inverse = glm::inverse(m_main_camera->m_projection);
        m_transforms.view_inverse = glm::inverse(m_main_camera->m_view);

        dw::RayTracedScene::Instance& instance = m_scene->fetch_instance(0);

        glm::mat4 model = glm::mat4(1.0f);
        model           = glm::translate(model, glm::vec3(0.0f, -20.0f, 0.0f));
        model           = glm::rotate(model, (float)glfwGetTime(), glm::vec3(0.0f, 1.0f, 0.0f));
        model           = glm::scale(model, glm::vec3(0.6f));

        instance.transform = model;

        uint8_t* ptr = (uint8_t*)m_ubo->mapped_ptr();
        memcpy(ptr + m_ubo_size * m_vk_backend->current_frame_idx(), &m_transforms, sizeof(Transforms));
    }

    // -----------------------------------------------------------------------------------------------------------------------------------

private:
    // GPU resources.
    size_t m_ubo_size;

    dw::vk::RayTracingPipeline::Ptr  m_raytracing_pipeline;
    dw::vk::PipelineLayout::Ptr      m_raytracing_pipeline_layout;
    dw::vk::DescriptorSet::Ptr       m_ray_tracing_ds;
    dw::vk::DescriptorSetLayout::Ptr m_ray_tracing_layout;
    dw::vk::GraphicsPipeline::Ptr    m_copy_pipeline;
    dw::vk::PipelineLayout::Ptr      m_copy_pipeline_layout;
    dw::vk::DescriptorSet::Ptr       m_copy_ds;
    dw::vk::DescriptorSetLayout::Ptr m_copy_layout;
    dw::vk::Sampler::Ptr             m_sampler;
    dw::vk::Buffer::Ptr              m_ubo;
    dw::vk::Image::Ptr               m_output_image;
    dw::vk::ImageView::Ptr           m_output_view;
    dw::vk::ShaderBindingTable::Ptr  m_sbt;

    // Camera.
    std::unique_ptr<dw::Camera> m_main_camera;

    // Assets.
    dw::Mesh::Ptr           m_mesh;
    dw::RayTracedScene::Ptr m_scene;

    // Uniforms.
    Transforms m_transforms;
};

DW_DECLARE_MAIN(Sample)
