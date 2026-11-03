#include "pd_platform.h"
#include "pd_ui.h"

#include <string.h>

int main(int argc, char **argv) {
    bool one_shot = false;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--once") == 0) {
            one_shot = true;
        }
    }

    if (!pd_platform_init()) {
        return 1;
    }

    pd_app_t app;
    pd_app_init(&app, pd_platform_default_save_path());

    while (app.running && pd_platform_main_loop()) {
        pd_input_t input;
        pd_platform_poll_input(&input);
        pd_app_update(&app, &input);
        pd_ui_render(&app, input.slider_3d);
        if (one_shot) {
            break;
        }
    }

    pd_app_shutdown(&app);
    pd_platform_shutdown();
    return 0;
}
