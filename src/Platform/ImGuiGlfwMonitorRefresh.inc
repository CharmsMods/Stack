// Adaptation of the pinned imgui_impl_glfw monitor function. The upstream file's
// MIT copyright/license remain in the generated translation unit. CMake checks
// every integration anchor; fetched dependency sources are never modified.
static void ImGui_ImplGlfw_UpdateMonitors()
{
    ImGui_ImplGlfw_Data* bd = ImGui_ImplGlfw_GetBackendData();
    const auto refresh = Stack::Platform::BeginGlfwMonitorRefresh(bd->MonitorRefresh, glfwGetTime());
    if (refresh == Stack::Platform::GlfwMonitorRefresh::Cached)
        return;
    const double queryStarted = glfwGetTime();

    ImGuiPlatformIO& platform_io = ImGui::GetPlatformIO();
    int monitors_count = 0;
    GLFWmonitor** glfw_monitors = glfwGetMonitors(&monitors_count);
    if (refresh == Stack::Platform::GlfwMonitorRefresh::WorkArea)
    {
        for (ImGuiPlatformMonitor& monitor : platform_io.Monitors)
        {
            // GLFW invalidates handles on topology changes. Check membership
            // even if another caller replaced the GLFW monitor callback.
            GLFWmonitor* handle = (GLFWmonitor*)monitor.PlatformHandle;
            bool present = false;
            for (int n = 0; n < monitors_count; ++n)
                present |= glfw_monitors[n] == handle;
            if (!present)
            {
                bd->MonitorRefresh.initialized = false;
                continue;
            }
#if GLFW_HAS_MONITOR_WORK_AREA
            int x, y, w, h;
            glfwGetMonitorWorkarea(handle, &x, &y, &w, &h);
            if (w > 0 && h > 0)
            {
                monitor.WorkPos = ImVec2((float)x, (float)y);
                monitor.WorkSize = ImVec2((float)w, (float)h);
            }
#endif
            float scale = ImGui_ImplGlfw_GetContentScaleForMonitor(handle);
            if (scale > 0.0f)
                monitor.DpiScale = scale;
        }
        Stack::Platform::ObserveGlfwMonitorRefresh(refresh, (glfwGetTime()-queryStarted)*1000.0);
        return;
    }

    bool updated_monitors = false;
    for (int n = 0; n < monitors_count; n++)
    {
        ImGuiPlatformMonitor monitor;
        int x, y;
        glfwGetMonitorPos(glfw_monitors[n], &x, &y);
        const GLFWvidmode* vid_mode = glfwGetVideoMode(glfw_monitors[n]);
        if (vid_mode == nullptr || vid_mode->width <= 0 || vid_mode->height <= 0)
            continue;
        monitor.MainPos = monitor.WorkPos = ImVec2((float)x, (float)y);
        monitor.MainSize = monitor.WorkSize = ImVec2((float)vid_mode->width, (float)vid_mode->height);
#if GLFW_HAS_MONITOR_WORK_AREA
        int w, h;
        glfwGetMonitorWorkarea(glfw_monitors[n], &x, &y, &w, &h);
        if (w > 0 && h > 0)
        {
            monitor.WorkPos = ImVec2((float)x, (float)y);
            monitor.WorkSize = ImVec2((float)w, (float)h);
        }
#endif
        float scale = ImGui_ImplGlfw_GetContentScaleForMonitor(glfw_monitors[n]);
        if (scale == 0.0f)
            continue;
        monitor.DpiScale = scale;
        monitor.PlatformHandle = (void*)glfw_monitors[n];
        if (!updated_monitors)
            platform_io.Monitors.resize(0);
        updated_monitors = true;
        platform_io.Monitors.push_back(monitor);
    }
    // Preserve upstream's last usable list across sleep/temporary query failure,
    // but retry instead of treating a failed enumeration as an indefinitely valid cache.
    if (!updated_monitors)
        bd->MonitorRefresh.initialized = false;
    Stack::Platform::ObserveGlfwMonitorRefresh(refresh, (glfwGetTime()-queryStarted)*1000.0);
}
