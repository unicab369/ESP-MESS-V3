idf_component_register(SRCS "mod_nvs.c"
                    INCLUDE_DIRS "."
                    REQUIRES 
                        nvs_flash
                    )