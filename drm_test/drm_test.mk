################################################################################
# Files
# ----------
LD_OBJ   = EXE
LD_BIN   = ./drm_test
PATH_TMP = $(DIR_TMP)/drm_test

CC_SRC = $(wildcard drm_test.c)

CC_INC = -Iinclude

USR_LIB = /home/fanchenxin/j7/demo/drm_test/lib

LD_INC = -L$(DIR_LIB) -L$(USR_LIB)
LD_LIB = -ldrm -ldrm_omap -pthread 


################################################################################
# Build
# ----------
include $(MAKE_GO)
################################################################################
