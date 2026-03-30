idf_component_register(SRCS "main.c"
                    "../drivers/sensor_i2c.c"
                    "../drivers/sht30.c"
                    "../drivers/radar_ld2420.c"
                    "../drivers/reed_switch.c"
                    "../drivers/led.c"
                    INCLUDE_DIRS "." "..")
