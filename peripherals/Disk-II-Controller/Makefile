SRC_DIR = src
BUILD_DIR = build
INC_DIR = include
OUT = $(BUILD_DIR)/testCard.so

C_FLAGS = -I$(INC_DIR) -fpic 

C_FILES = $(shell find $(SRC_DIR)/ -type f -name '*.c')
OBJ_FILES += $(patsubst $(SRC_DIR)/%.c, $(BUILD_DIR)/%.o, $(C_FILES))

all: build

build: $(OUT)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(@D)
	gcc -c $(C_FLAGS) -o $@ $<

$(OUT): $(OBJ_FILES)
	gcc -shared $(OBJ_FILES) -o $(OUT)
