# Build dentro do Alpine armv7 (musl estático): ver build.sh
SRCS := main.c home_ui.c sidebar.c settings.c font_acentos_28.c font_relogio_84.c hw_config.c hw_mixer.c icons.c $(shell find third_party/lvgl/src -name '*.c')
OBJS := $(SRCS:%.c=build/%.o)
CFLAGS := -Os -I. -Ithird_party -DLV_CONF_INCLUDE_SIMPLE -ffunction-sections -fdata-sections

h805d-ui: $(OBJS)
	$(CC) -static -Wl,--gc-sections -s -o $@ $^

build/%.o: %.c lv_conf.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# so o settings.o depende do hash (nao recompila o LVGL)
build/settings.o: CFLAGS += -DGIT_REV=\"$(GIT_REV)\"
