################################################################################
# Files
# ----------
LD_OBJ   = EXE
LD_BIN   = ./modeset-vsync
PATH_TMP = $(DIR_TMP)/modeset-vsync

CC_SRC = $(wildcard modeset-vsync.c)

CC_INC = -Iinclude

USR_LIB = /home/fanchenxin/j7/demo/drm_test/lib

LD_INC = -L$(DIR_LIB) -L$(USR_LIB)
LD_LIB = -ldrm -ldrm_omap -pthread 


################################################################################
# Build
# ----------
include $(MAKE_GO)
################################################################################
