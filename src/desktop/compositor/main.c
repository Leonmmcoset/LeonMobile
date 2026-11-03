/* SPDX-License-Identifier: CC0-1.0
 * Based on wlroots 0.20 tinywl; mobile policy additions by LeonMobile. */
#include "server.h"
static int terminate_display(int signal_number, void *data) {
    (void)signal_number;
    wl_display_terminate(data);
    return 0;
}

int main(int argc, char *argv[]) {
    wlr_log_init(WLR_DEBUG, NULL);
    char *startup_cmd = NULL;

    int c;
    while ((c = getopt(argc, argv, "s:h")) != -1) {
        switch (c) {
        case 's':
            startup_cmd = optarg;
            break;
        default:
            printf("Usage: %s [-s startup command]\n", argv[0]);
            return 0;
        }
    }
    if (optind < argc) {
        printf("Usage: %s [-s startup command]\n", argv[0]);
        return 0;
    }

    struct leon_server server = {0};
    server.width = 480;
    server.height = 960;
    /* The Wayland display is managed by libwayland. It handles accepting
     * clients from the Unix socket, managing Wayland globals, and so on. */
    server.wl_display = wl_display_create();
    /* The backend is a wlroots feature which abstracts the underlying input and
     * output hardware. The autocreate option will choose the most suitable
     * backend based on the current environment, such as opening an X11 window
     * if an X11 server is running. */
    server.backend = wlr_backend_autocreate(wl_display_get_event_loop(server.wl_display), NULL);
    if (server.backend == NULL) {
        wlr_log(WLR_ERROR, "failed to create wlr_backend");
        return 1;
    }

    /* Autocreates a renderer, either Pixman, GLES2 or Vulkan for us. The user
     * can also specify a renderer using the WLR_RENDERER env var.
     * The renderer is responsible for defining the various pixel formats it
     * supports for shared memory, this configures that for clients. */
    server.renderer = wlr_renderer_autocreate(server.backend);
    if (server.renderer == NULL) {
        wlr_log(WLR_ERROR, "failed to create wlr_renderer");
        return 1;
    }

    wlr_renderer_init_wl_display(server.renderer, server.wl_display);

    /* Autocreates an allocator for us.
     * The allocator is the bridge between the renderer and the backend. It
     * handles the buffer creation, allowing wlroots to render onto the
     * screen */
    server.allocator = wlr_allocator_autocreate(server.backend, server.renderer);
    if (server.allocator == NULL) {
        wlr_log(WLR_ERROR, "failed to create wlr_allocator");
        return 1;
    }

    /* This creates some hands-off wlroots interfaces. The compositor is
     * necessary for clients to allocate surfaces, the subcompositor allows to
     * assign the role of subsurfaces to surfaces and the data device manager
     * handles the clipboard. Each of these wlroots interfaces has room for you
     * to dig your fingers in and play with their behavior if you want. Note that
     * the clients cannot set the selection directly without compositor approval,
     * see the handling of the request_set_selection event below.*/
    wlr_compositor_create(server.wl_display, 5, server.renderer);
    wlr_subcompositor_create(server.wl_display);
    wlr_data_device_manager_create(server.wl_display);

    /* Creates an output layout, which a wlroots utility for working with an
     * arrangement of screens in a physical layout. */
    server.output_layout = wlr_output_layout_create(server.wl_display);

    /* Configure a listener to be notified when new outputs are available on the
     * backend. */
    leon_outputs_init(&server);

    /* Create a scene graph. This is a wlroots abstraction that handles all
     * rendering and damage tracking. All the compositor author needs to do
     * is add things that should be rendered to the scene graph at the proper
     * positions and then call wlr_scene_output_commit() to render a frame if
     * necessary.
     */
    server.scene = wlr_scene_create();
    server.scene_layout = wlr_scene_attach_output_layout(server.scene, server.output_layout);

    /* Set up xdg-shell version 3. The xdg-shell is a Wayland protocol which is
     * used for application windows. For more detail on shells, refer to
     * https://drewdevault.com/2018/07/29/Wayland-shells.html.
     */
    leon_windows_init(&server);
    if (!leon_input_init(&server))
        return 1;

    /* Add a Unix socket to the Wayland display. */
    const char *socket_name = wl_display_add_socket_auto(server.wl_display);
    if (!socket_name) {
        wlr_backend_destroy(server.backend);
        return 1;
    }
    const char *runtime = getenv("XDG_RUNTIME_DIR");
    char wayland_path[256];
    snprintf(wayland_path, sizeof(wayland_path), "%s/%s", runtime, socket_name);
    /* Separate UIDs may connect; Wayland never grants filesystem privileges. */
    chmod(wayland_path, 0666);
    if (!leon_control_init(&server))
        return 1;
    wl_event_loop_add_signal(wl_display_get_event_loop(server.wl_display), SIGTERM,
                             terminate_display, server.wl_display);
    wl_event_loop_add_signal(wl_display_get_event_loop(server.wl_display), SIGINT,
                             terminate_display, server.wl_display);

    /* Start the backend. This will enumerate outputs and inputs, become the DRM
     * master, etc */
    if (!wlr_backend_start(server.backend)) {
        wlr_backend_destroy(server.backend);
        wl_display_destroy(server.wl_display);
        return 1;
    }

    /* Set the WAYLAND_DISPLAY environment variable to our socket and run the
     * startup command if requested. */
    setenv("WAYLAND_DISPLAY", socket_name, true);
    if (startup_cmd) {
        if (fork() == 0) {
            execl("/bin/sh", "/bin/sh", "-c", startup_cmd, (void *)NULL);
        }
    }
    /* Run the Wayland event loop. This does not return until you exit the
     * compositor. Starting the backend rigged up all of the necessary event
     * loop configuration to listen to libinput events, DRM events, generate
     * frame events at the refresh rate, and so on. */
    wlr_log(WLR_INFO, "Running Wayland compositor on WAYLAND_DISPLAY=%s", socket_name);
    wl_display_run(server.wl_display);

    /* Once wl_display_run returns, we destroy all clients then shut down the
     * server. */
    wl_display_destroy_clients(server.wl_display);
    leon_control_finish(&server);
    leon_input_detach(&server);
    leon_windows_finish(&server);
    leon_outputs_finish(&server);

    wlr_scene_node_destroy(&server.scene->tree.node);
    leon_input_destroy(&server);
    wlr_allocator_destroy(server.allocator);
    wlr_renderer_destroy(server.renderer);
    wlr_backend_destroy(server.backend);
    wl_display_destroy(server.wl_display);
    return 0;
}
