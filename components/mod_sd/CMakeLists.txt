idf_component_register(SRCS "mod_sd.c"
                        INCLUDE_DIRS "."
                        REQUIRES fatfs sd_card
                    )