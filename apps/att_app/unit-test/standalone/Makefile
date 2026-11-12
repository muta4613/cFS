#
# Standalone build of the ATT_APP NMEA parser, with sanitizers.
#
# Needs only a C compiler and the cFS source tree (for common_types.h);
# no cFS build tree has to exist. From this directory:
#
#     make check
#
APP_DIR  ?= ../..
OSAL_INC ?= $(APP_DIR)/../../osal/src/os/inc
LOG      ?= $(APP_DIR)/data/gnss_sample.nmea
BUILD    ?= build

# What the committed sample log contains (see ground/make_nmea_log.py)
EXPECTED_EPOCHS ?= 300
EXPECTED_NOFIX  ?= 10

CC     ?= gcc
CFLAGS ?= -std=c99 -g -O1 -Wall -Wextra -Wpedantic -Wconversion -Werror \
          -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer

INCLUDES := -I. -I$(APP_DIR)/config -I$(APP_DIR)/fsw/inc -I$(APP_DIR)/fsw/src -I$(OSAL_INC)

.PHONY: check clean

check: $(BUILD)/nmea_log_check
	$(BUILD)/nmea_log_check $(LOG) $(EXPECTED_EPOCHS) $(EXPECTED_NOFIX)

$(BUILD)/nmea_log_check: nmea_log_check.c $(APP_DIR)/fsw/src/att_app_nmea.c $(APP_DIR)/fsw/src/att_app_nmea.h
	mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ nmea_log_check.c $(APP_DIR)/fsw/src/att_app_nmea.c

clean:
	rm -rf $(BUILD)
