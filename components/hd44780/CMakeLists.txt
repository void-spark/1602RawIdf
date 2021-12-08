set(req driver freertos)

idf_component_register(
    SRCS hd44780.c
    INCLUDE_DIRS .
    REQUIRES ${req}
)
