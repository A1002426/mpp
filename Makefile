CC = /opt/atk-dlrk356x-toolchain/bin/aarch64-buildroot-linux-gnu-gcc

TARGET = v4l2


EXCLUDE_C = 
ALL_SRC = main.c $(wildcard src/*.c)
SRCS =  $(filter-out $(EXCLUDE_C), $(ALL_SRC))

OBJS = $(SRCS:.c=.o)


INC_DIR = ./inc


ION_UAPI = /home/alientek/rk3568_sdk/kernel/drivers/staging/android/uapi
MPP_INC = /home/alientek/rk3568_sdk/external/mpp/inc

MPP_LIB=/home/alientek/rk3568_sdk/external/mpp/build/linux/aarch64/mpp 



CFLAGS = -Wall -O2 -g
CFLAGS += -I$(INC_DIR)  -I$(ION_UAPI) -I$(MPP_INC)
CFLAGS += -D_GNU_SOURCE   

LDFLAGS += -L$(MPP_LIB) 
LDLIBS  += -lrockchip_mpp -lpthread

# 默认目标
all: $(TARGET)


$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) $(OBJS) $(LDLIBS) -o $@ 

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# 清理
clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all clean