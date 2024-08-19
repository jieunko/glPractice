#include <application.h>
#include <fstream>
#include <json.hpp>
#include <backends/imgui_impl_glfw.h>

#if defined(DWSF_VULKAN)
#    include <backends/imgui_impl_vulkan.h>
#else
#    include <backends/imgui_impl_opengl3.h>
#endif
#include <profiler.h>
#include <iostream>

#if defined(__EMSCRIPTEN__)
#    include <emscripten/emscripten.h>
#endif

#include "material.h"
#include "mesh.h"
#include "utility.h"

namespace dw
{
// -----------------------------------------------------------------------------------------------------------------------------------

#if !defined(DWSF_VULKAN)

static void APIENTRY glDebugCallback(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar* message, const void* user_param)
{
    std::string msg_source;

    switch (source)
    {
        case GL_DEBUG_SOURCE_API:
            msg_source = "API";
            break;
        case GL_DEBUG_SOURCE_SHADER_COMPILER:
            msg_source = "SHADER_COMPILER";
            break;
        case GL_DEBUG_SOURCE_THIRD_PARTY:
            msg_source = "THIRD_PARTY";
            break;
        case GL_DEBUG_SOURCE_APPLICATION:
            msg_source = "APPLICATION";
            break;
        case GL_DEBUG_SOURCE_OTHER:
            msg_source = "OTHER";
            break;
    }

    std::string msg_type;

    switch (type)
    {
        case GL_DEBUG_TYPE_ERROR:
            msg_type = "ERROR";
            break;
        case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR:
            msg_type = "DEPRECATED_BEHAVIOR";
            break;
        case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR:
            msg_type = "UNDEFINED_BEHAVIOR";
            break;
        case GL_DEBUG_TYPE_PORTABILITY:
            msg_type = "PORTABILITY";
            break;
        case GL_DEBUG_TYPE_PERFORMANCE:
            msg_type = "PERFORMANCE";
            break;
        case GL_DEBUG_TYPE_OTHER:
            msg_type = "OTHER";
            break;
    }

    std::string msg_severity = "DEFAULT";

    switch (severity)
    {
        case GL_DEBUG_SEVERITY_LOW:
            msg_severity = "LOW";
            break;
        case GL_DEBUG_SEVERITY_MEDIUM:
            msg_severity = "MEDIUM";
            break;
        case GL_DEBUG_SEVERITY_HIGH:
            msg_severity = "HIGH";
            break;
    }

    std::string log_msg = "glDebugMessage: " + std::string(message) + ", type = " + msg_type + ", source = " + msg_source + ", severity = " + msg_severity;

    if (type == GL_DEBUG_TYPE_ERROR)
        DW_LOG_ERROR(log_msg);
    else
        DW_LOG_WARNING(log_msg);
}

#endif

#if defined(__EMSCRIPTEN__)
void Application::run_frame(void* arg)
{
    dw::Application* app = (dw::Application*)arg;
    app->update_base(app->m_delta);
}
#endif

// -----------------------------------------------------------------------------------------------------------------------------------

Application::Application() :
    m_mouse_x(0.0), m_mouse_y(0.0), m_last_mouse_x(0.0), m_last_mouse_y(0.0),
    m_mouse_delta_x(0.0), m_mouse_delta_y(0.0), m_delta(0.0),
    m_delta_seconds(0.0), m_window(nullptr) {}

// -----------------------------------------------------------------------------------------------------------------------------------

Application::~Application() {}

// -----------------------------------------------------------------------------------------------------------------------------------

int Application::run(int argc, const char* argv[])
{
    if (!init_base(argc, argv))
        return 1;

#if defined(__EMSCRIPTEN__)
    emscripten_set_main_loop_arg(run_frame, this, 0, 1);
#else
    while (!exit_requested())
        update_base(m_delta);
#endif

#if defined(DWSF_VULKAN)
    m_vk_backend->wait_idle();
#endif

    shutdown_base();

    return 0;
}

// -----------------------------------------------------------------------------------------------------------------------------------

bool Application::init(int argc, const char* argv[]) { return true; }

// -----------------------------------------------------------------------------------------------------------------------------------

void Application::update(double delta) {}

// -----------------------------------------------------------------------------------------------------------------------------------

void Application::shutdown() {}

// -----------------------------------------------------------------------------------------------------------------------------------

bool Application::init_base(int argc, const char* argv[])
{
    logger::initialize();
    logger::open_console_stream();
    logger::open_file_stream();

    // Defaults
    AppSettings settings = intial_app_settings();

    load_initial_settings_from_file(settings);

    bool maximized  = settings.maximized;
    bool fullscreen = settings.fullscreen;
    m_vsync         = settings.vsync;
    m_width         = settings.width;
    m_height        = settings.height;
    m_title         = settings.title;
    std::cout << m_width << std::endl;
    std::cout << m_height << std::endl;
    std::cout << m_title << std::endl;
    int major_ver = 4;
#if defined(__APPLE__)
    int         minor_ver          = 1;
    const char* imgui_glsl_version = "#version 150";
#elif defined(__EMSCRIPTEN__)
    major_ver                      = 3;
    int         minor_ver          = 0;
    const char* imgui_glsl_version = "#version 150";
#else
    int         minor_ver          = 0;
    const char* imgui_glsl_version = "#version 150";
#endif

    if (glfwInit() != GLFW_TRUE)
    {
        DW_LOG_FATAL("Failed to initialize GLFW");
        return false;
    }

#if defined(DWSF_VULKAN)
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
#else
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_API);

#    if !defined(__EMSCRIPTEN__)
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_SAMPLES, 8);
#    endif

#    if __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#    endif

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, major_ver);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, minor_ver);
    glfwSwapInterval(m_vsync ? 1 : 0);
#endif
    glfwWindowHint(GLFW_RESIZABLE, false);
    glfwWindowHint(GLFW_MAXIMIZED, maximized);
   std::cout << "test1";
    m_window = glfwCreateWindow(m_width, m_height, m_title.c_str(), fullscreen ? glfwGetPrimaryMonitor() : nullptr, nullptr);
    //m_window = glfwCreateWindow(m_width, m_height, m_title.c_str(),nullptr, nullptr);
    if (!m_window)
    {
        DW_LOG_FATAL("Failed to create GLFW window!");
        return false;
    }

    glfwSetKeyCallback(m_window, key_callback_glfw);
    glfwSetCursorPosCallback(m_window, mouse_callback_glfw);
    glfwSetScrollCallback(m_window, scroll_callback_glfw);
    glfwSetMouseButtonCallback(m_window, mouse_button_callback_glfw);
    glfwSetCharCallback(m_window, char_callback_glfw);
    glfwSetWindowSizeCallback(m_window, window_size_callback_glfw);
    glfwSetWindowUserPointer(m_window, this);

    glfwMakeContextCurrent(m_window);

    DW_LOG_INFO("Successfully initialized platform!");

#if defined(DWSF_VULKAN)
    m_vk_backend = vk::Backend::create(m_window,
                                       m_vsync,
                                       settings.srgb,
#    if defined(_DEBUG)
                                       true
#    else
                                       false
#    endif
                                       ,
                                       settings.ray_tracing,
                                       settings.device_extensions);

    m_present_complete_semaphore = vk::Semaphore::create(m_vk_backend);
    m_render_complete_semaphore = vk::Semaphore::create(m_vk_backend);

    Material::initialize_common_resources(m_vk_backend);
#else
#    if !defined(__EMSCRIPTEN__)
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
        return false;
#    endif

    if (settings.enable_debug_callback)
    {
        glDebugMessageCallback(glDebugCallback, NULL);
        glEnable(GL_DEBUG_OUTPUT);
    }

    glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);

#endif

#if defined(DWSF_IMGUI)
    ImGui::CreateContext();

#    if defined(DWSF_VULKAN)
    ImGui_ImplGlfw_InitForVulkan(m_window, false);

    ImGui_ImplVulkan_InitInfo init_info = {};

    init_info.Instance        = m_vk_backend->instance();
    init_info.PhysicalDevice  = m_vk_backend->physical_device();
    init_info.Device          = m_vk_backend->device();
    init_info.QueueFamily     = m_vk_backend->queue_infos().graphics_queue_index;
    init_info.Queue           = m_vk_backend->graphics_queue();
    init_info.PipelineCache   = nullptr;
    init_info.DescriptorPool  = m_vk_backend->thread_local_descriptor_pool()->handle();
    init_info.Allocator       = nullptr;
    init_info.MinImageCount   = 2;
    init_info.ImageCount      = m_vk_backend->swap_image_count();
    init_info.CheckVkResultFn = nullptr;
    init_info.RenderPass =  m_vk_backend->swapchain_render_pass()->handle();

    //ImGui_ImplVulkan_Init(&init_info, m_vk_backend->swapchain_render_pass()->handle());
    ImGui_ImplVulkan_Init(&init_info);
    vk::CommandBuffer::Ptr cmd_buf = m_vk_backend->allocate_graphics_command_buffer();

    VkCommandBufferBeginInfo begin_info;
    DW_ZERO_MEMORY(begin_info);

    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

    vkBeginCommandBuffer(cmd_buf->handle(), &begin_info);

    //ImGui_ImplVulkan_CreateFontsTexture(cmd_buf->handle());

    vkEndCommandBuffer(cmd_buf->handle());

    m_vk_backend->flush_graphics({ cmd_buf });

    //ImGui_ImplVulkan_DestroyFontUploadObjects();
#    else
    ImGui_ImplGlfw_InitForOpenGL(m_window, false);
    ImGui_ImplOpenGL3_Init(imgui_glsl_version);
#    endif

    ImGui::StyleColorsDark();
#endif

    GLFWmonitor* primary = glfwGetPrimaryMonitor();

    float xscale, yscale;
    glfwGetMonitorContentScale(primary, &xscale, &yscale);

#if defined(DWSF_IMGUI) && !defined(__APPLE__)
    ImGuiStyle* style = &ImGui::GetStyle();

    style->ScaleAllSizes(xscale > yscale ? xscale : yscale);

    ImGuiIO& io        = ImGui::GetIO();
    io.FontGlobalScale = xscale > yscale ? xscale : yscale;
#endif

    int display_w, display_h;
    glfwGetFramebufferSize(m_window, &display_w, &display_h);
    m_width  = display_w;
    m_height = display_h;

    if (!m_debug_draw.init(
#if defined(DWSF_VULKAN)
            m_vk_backend, m_vk_backend->swapchain_render_pass()
#endif
                ))
        return false;

    profiler::initialize(
#if defined(DWSF_VULKAN)
        m_vk_backend
#endif
    );
    
    if (!init(argc, argv))
        return false;
    std::cout <<"here" << std::endl;
    return true;
}

// -----------------------------------------------------------------------------------------------------------------------------------

void Application::update_base(double delta)
{
    begin_frame();
    update(delta);
    end_frame();
}

// -----------------------------------------------------------------------------------------------------------------------------------

void Application::shutdown_base()
{
    // Execute user-side shutdown method.
    shutdown();

#if defined(DWSF_VULKAN)
    // Shutdown debug draw.

    // Shutdown profiler.
    profiler::shutdown();

    m_debug_draw.shutdown();
    Material::shutdown_common_resources();

    // Shutdown ImGui.
#    if defined(DWSF_IMGUI)
    ImGui_ImplVulkan_Shutdown();
#    endif

    m_present_complete_semaphore.reset();
    m_render_complete_semaphore.reset();

    m_vk_backend->~Backend();
#else
    // Shutdown debug draw.
    m_debug_draw.shutdown();

#    if defined(DWSF_IMGUI)
    ImGui_ImplOpenGL3_Shutdown();
#    endif
#endif

#if defined(DWSF_IMGUI)
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
#endif

    // Shutdown GLFW.
    glfwDestroyWindow(m_window);
    glfwTerminate();

    // Close logger streams.
    logger::close_file_stream();
    logger::close_console_stream();
}

// -----------------------------------------------------------------------------------------------------------------------------------

#if defined(DWSF_VULKAN)
#    if defined(DWSF_IMGUI)
void Application::render_gui(vk::CommandBuffer::Ptr cmd_buf)
{
    ImGui::Render();
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd_buf->handle());
}
#    endif

// -----------------------------------------------------------------------------------------------------------------------------------

void Application::submit_and_present(const std::vector<vk::CommandBuffer::Ptr>& cmd_bufs)
{
    m_vk_backend->submit_graphics(cmd_bufs,
                                  { m_present_complete_semaphore },
                                  { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT },
                                  { m_render_complete_semaphore });

    m_vk_backend->present({ m_render_complete_semaphore });
}

#endif

// -----------------------------------------------------------------------------------------------------------------------------------

void Application::begin_frame()
{
    m_timer.start();

    glfwPollEvents();

#if defined(DWSF_VULKAN)
    if (m_should_recreate_swap_chain)
    {
        m_vk_backend->recreate_swapchain(m_vsync);
        m_should_recreate_swap_chain = false;
    }

    m_vk_backend->acquire_next_swap_chain_image(m_present_complete_semaphore);

#    if defined(DWSF_IMGUI)
    ImGui_ImplVulkan_NewFrame();
#    endif
#else
#    if defined(DWSF_IMGUI)
    ImGui_ImplOpenGL3_NewFrame();
#    endif
#endif

#if defined(DWSF_IMGUI)
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
#endif

    m_mouse_delta_x = m_mouse_x - m_last_mouse_x;
    m_mouse_delta_y = m_mouse_y - m_last_mouse_y;

    m_last_mouse_x = m_mouse_x;
    m_last_mouse_y = m_mouse_y;

    profiler::begin_frame();
}

// -----------------------------------------------------------------------------------------------------------------------------------

void Application::end_frame()
{
    profiler::end_frame();

#if !defined(DWSF_VULKAN)
#    if defined(DWSF_IMGUI)
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
#    endif
    glfwSwapBuffers(m_window);
#endif

    m_timer.stop();
    m_delta         = m_timer.elapsed_time_milisec();
    m_delta_seconds = m_timer.elapsed_time_sec();
}

// -----------------------------------------------------------------------------------------------------------------------------------

void Application::request_exit() const
{
    glfwSetWindowShouldClose(m_window, true);
}

// -----------------------------------------------------------------------------------------------------------------------------------

bool Application::exit_requested() const
{
    return glfwWindowShouldClose(m_window);
}

// -----------------------------------------------------------------------------------------------------------------------------------

AppSettings Application::intial_app_settings() { return AppSettings(); }

// -----------------------------------------------------------------------------------------------------------------------------------

void Application::load_initial_settings_from_file(AppSettings& settings)
{
    std::ifstream i("config.json");

    if (!i.is_open())
        return;

    nlohmann::json j;
    i >> j;

    if (j.find("width") != j.end())
        settings.width = j["width"];

    if (j.find("height") != j.end())
        settings.height = j["height"];

    if (j.find("maximized") != j.end())
        settings.maximized = j["maximized"];

    if (j.find("fullscreen") != j.end())
        settings.fullscreen = j["fullscreen"];

    if (j.find("vsync") != j.end())
        settings.vsync = j["vsync"];
}

// -----------------------------------------------------------------------------------------------------------------------------------

void Application::window_resized(int width, int height) {}

// -----------------------------------------------------------------------------------------------------------------------------------

void Application::key_pressed(int code) {}

// -----------------------------------------------------------------------------------------------------------------------------------

void Application::key_released(int code) {}

// -----------------------------------------------------------------------------------------------------------------------------------

void Application::mouse_scrolled(double xoffset, double yoffset) {}

// -----------------------------------------------------------------------------------------------------------------------------------

void Application::mouse_pressed(int code) {}

// -----------------------------------------------------------------------------------------------------------------------------------

void Application::mouse_released(int code) {}

// -----------------------------------------------------------------------------------------------------------------------------------

void Application::mouse_move(double x, double y, double deltaX, double deltaY) {}

// -----------------------------------------------------------------------------------------------------------------------------------

void Application::key_callback(GLFWwindow* window, int key, int scancode, int action, int mode)
{
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
        glfwSetWindowShouldClose(window, GL_TRUE);
    if (key >= 0 && key < MAX_KEYS)
    {
        if (action == GLFW_PRESS)
        {
            key_pressed(key);
            m_keys[key] = true;
        }
        else if (action == GLFW_RELEASE)
        {
            key_released(key);
            m_keys[key] = false;
        }
    }
}

// -----------------------------------------------------------------------------------------------------------------------------------

void Application::mouse_callback(GLFWwindow* window, double xpos, double ypos)
{
    m_mouse_x = xpos;
    m_mouse_y = ypos;
    mouse_move(xpos, ypos, m_mouse_delta_x, m_mouse_delta_y);
}

// -----------------------------------------------------------------------------------------------------------------------------------

void Application::scroll_callback(GLFWwindow* window, double xoffset, double yoffset)
{
    mouse_scrolled(xoffset, yoffset);
}

// -----------------------------------------------------------------------------------------------------------------------------------

void Application::mouse_button_callback(GLFWwindow* window, int button, int action, int mods)
{
    if (button >= 0 && button < MAX_MOUSE_BUTTONS)
    {
        if (action == GLFW_PRESS)
        {
            mouse_pressed(button);
            m_mouse_buttons[button] = true;
        }
        else if (action == GLFW_RELEASE)
        {
            mouse_released(button);
            m_mouse_buttons[button] = false;
        }
    }
}

// -----------------------------------------------------------------------------------------------------------------------------------

void Application::window_size_callback(GLFWwindow* window, int width, int height)
{
    m_width  = width;
    m_height = height;

#if defined(DWSF_VULKAN)
    m_should_recreate_swap_chain = true;
#endif

    window_resized(width, height);
}

// -----------------------------------------------------------------------------------------------------------------------------------

void Application::key_callback_glfw(GLFWwindow* window, int key, int scancode, int action, int mode)
{
#if defined(DWSF_IMGUI)
    ImGui_ImplGlfw_KeyCallback(window, key, scancode, action, mode);
#endif
    Application* app = (Application*)glfwGetWindowUserPointer(window);
    app->key_callback(window, key, scancode, action, mode);
}

// -----------------------------------------------------------------------------------------------------------------------------------

void Application::mouse_callback_glfw(GLFWwindow* window, double xpos, double ypos)
{
    Application* app = (Application*)glfwGetWindowUserPointer(window);
    app->mouse_callback(window, xpos, ypos);
}

// -----------------------------------------------------------------------------------------------------------------------------------

void Application::scroll_callback_glfw(GLFWwindow* window, double xoffset, double yoffset)
{
#if defined(DWSF_IMGUI)
    ImGui_ImplGlfw_ScrollCallback(window, xoffset, yoffset);
#endif
    Application* app = (Application*)glfwGetWindowUserPointer(window);
    app->scroll_callback(window, xoffset, yoffset);
}

// -----------------------------------------------------------------------------------------------------------------------------------

void Application::mouse_button_callback_glfw(GLFWwindow* window, int button, int action, int mods)
{
#if defined(DWSF_IMGUI)
    ImGui_ImplGlfw_MouseButtonCallback(window, button, action, mods);
#endif
    Application* app = (Application*)glfwGetWindowUserPointer(window);
    app->mouse_button_callback(window, button, action, mods);
}

// -----------------------------------------------------------------------------------------------------------------------------------

void Application::char_callback_glfw(GLFWwindow* window, unsigned int c)
{
#if defined(DWSF_IMGUI)
    ImGui_ImplGlfw_CharCallback(window, c);
#endif
}

// -----------------------------------------------------------------------------------------------------------------------------------

void Application::window_size_callback_glfw(GLFWwindow* window, int width, int height)
{
    Application* app = (Application*)glfwGetWindowUserPointer(window);
    app->window_size_callback(window, width, height);
}

// -----------------------------------------------------------------------------------------------------------------------------------
} // namespace dw
