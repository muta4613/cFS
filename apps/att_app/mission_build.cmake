###########################################################
#
# ATT_APP mission build setup
#
# This file is evaluated as part of the "prepare" stage
# and can be used to set up prerequisites for the build,
# such as generating header files
#
###########################################################

# The list of header files that control the ATT_APP configuration
set(ATT_APP_MISSION_CONFIG_FILE_LIST
  att_app_fcncode_values.h
  att_app_interface_cfg_values.h
  att_app_mission_cfg.h
  att_app_perfids.h
  att_app_msg.h
  att_app_msgdefs.h
  att_app_msgstruct.h
  att_app_tbl.h
  att_app_tbldefs.h
  att_app_tblstruct.h
  att_app_topicid_values.h
)

generate_configfile_set(${ATT_APP_MISSION_CONFIG_FILE_LIST})

