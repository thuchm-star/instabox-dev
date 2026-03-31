idf_component_register(SRCS "main.c"
                    "../drivers/radar_ld2420.c"
                    "../drivers/led.c"
                    "../drivers/sensor_i2c.c"
                    "../drivers/sht30.c"
                    "../tasks/task_radar.c"
                    "../tasks/task_env.c"
                    INCLUDE_DIRS "." "..")
