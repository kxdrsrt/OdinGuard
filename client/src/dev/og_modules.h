/* Default module selection of a development build: every module. The Studio generates this file per profile
   (modules/modules.toml); a module set to 0 is also left out of the source list, so none of its code is linked. */
#ifndef OG_MODULES_H
#define OG_MODULES_H
#define OG_MOD_CORE 1
#define OG_MOD_HWID 1
#define OG_MOD_ENV 1
#define OG_MOD_HEARTBEAT 1
#define OG_MOD_INTEGRITY 1
#define OG_MOD_DETECT 1
#define OG_MOD_ASSETS 1
#define OG_MOD_INPUT 1
#define OG_MOD_CRASH 1
#define OG_MOD_CMDS 1
#define OG_MOD_COLORS 1
#define OG_MOD_SPLASH 1
#define OG_MODCH_CODE 1
#define OG_MODCH_TIME 1
#define OG_MODCH_FILE 1
#endif
