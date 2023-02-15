SRC_DIR = src
BUILD_DIR = build
INC_DIR = include
OUT = $(BUILD_DIR)/apple2

C_FLAGS = -I$(INC_DIR) -fPIE

C_FILES = $(shell find $(SRC_DIR)/ -type f -name '*.c')
OBJ_FILES += $(patsubst $(SRC_DIR)/%.c, $(BUILD_DIR)/%.o, $(C_FILES))

all: build run

.PHONY: build
build: $(OUT)

run:
	build/apple2

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(@D)
	gcc -c $(C_FLAGS) -o $@ $<

$(OUT): $(OBJ_FILES)
	gcc -pthread $(OBJ_FILES) -o $(OUT) -lglfw -lGL -lX11 -lXi -lXrandr -lXxf86vm -lXinerama -lXcursor -lrt -lm -ldl

clean:
	rm -rf build