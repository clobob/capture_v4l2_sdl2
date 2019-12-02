CROSS_COMPILER = /home/joe/worksource/J721E_EVM/ti-processor-sdk-linux-automotive-j7-evm-06_01_00_05/linux-devkit/sysroots/x86_64-arago-linux/usr/bin/aarch64-linux-gnu
CC = $(CROSS_COMPILER)-gcc
CFLAGS = -g -O0
CLIBS = -L/home/joe/worksource/J721E_EVM/linux-devkit/sysroots/aarch64-linux/usr/lib/ -lwayland-server -lwayland-client -lglapi -ldrm -ldrm_omap -lEGL -lGLESv2 -lgbm -pthread -lrt -lm

USR_INC = 
INCLUDE_DIRS = -I/home/joe/worksource/J721E_EVM/linux-devkit/sysroots/aarch64-linux/usr/include/ \
	-I/home/joe/worksource/J721E_EVM/linux-devkit/sysroots/aarch64-linux/usr/include/EGL \
	-I/home/joe/worksource/J721E_EVM/linux-devkit/sysroots/aarch64-linux/usr/include/GLES2 \
	-I/home/joe/worksource/J721E_EVM/linux-devkit/sysroots/aarch64-linux/usr/include/drm

SOURCES = $(wildcard ./*.c)

TARGET = kms_cube
OBJECTS = $(patsubst %.c,%.o,$(SOURCES))

$(TARGET) : $(OBJECTS)
	$(CC) $^ -o $@ $(CLIBS)
	
$(OBJECTS) : %.o : %.c 
	$(CC) -c $(CFLAGS) $< -o $@ $(INCLUDE_DIRS)

.PHONY : clean
clean:
	rm -rf $(TARGET) $(OBJECTS)