###########################################################
#
# ATT_APP platform build setup
#
# This file is evaluated as part of the "prepare" stage
# and can be used to set up prerequisites for the build,
# such as generating header files
#
###########################################################

# The list of header files that control the ATT_APP configuration
set(ATT_APP_PLATFORM_CONFIG_FILE_LIST
  att_app_internal_cfg_values.h
  att_app_platform_cfg.h
  att_app_perfids.h
  att_app_msgids.h
  att_app_msgid_values.h
)

generate_configfile_set(${ATT_APP_PLATFORM_CONFIG_FILE_LIST})

