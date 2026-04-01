idf_component_register(SRCS "main.c"
                    "../drivers/radar_ld2420.c"
                    "../drivers/led.c"
                    "../tasks/task_radar.c"
                    "../tests/test_ld2420_commands.c"
                    INCLUDE_DIRS "." "..")
