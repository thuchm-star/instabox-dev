idf_component_register(SRCS "main.c"
                    "../drivers/radar_ld2420.c"
                    "../drivers/led.c"
                    "../tasks/task_radar.c"
                    INCLUDE_DIRS "." "..")
