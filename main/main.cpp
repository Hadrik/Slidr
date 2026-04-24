#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "app/Application.h"

extern "C" void app_main(void) {
    static slidr::app::Application application;

    application.begin();

    while (true) {
        application.tick();
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
