CC = gcc
# Turn off error on incompatible pointer types
# so we can cast Obj* into whichever object
# pointer we need.
# Turn off error on unused parameters as well
# since many parsing functions have an unused
# (bool canAssign) parameter.
CFLAGS = -Wall -Wextra -Wno-incompatible-pointer-types -Wno-unused-parameter -Werror
NAME = clox

ifeq ($(OS), Windows_NT)
    PLATFORM = windows
else
    UNAME_S := $(shell uname -s)
    ifeq ($(UNAME_S), Linux)
        PLATFORM = linux
    endif
    ifeq ($(UNAME_S), Darwin)
        PLATFORM = macos
    endif
endif

SRC_DIR = src
OBJ_DIR = build

SRCS = $(wildcard $(SRC_DIR)/*.c)
OBJS = $(patsubst $(SRC_DIR)/%.c, $(OBJ_DIR)/%.o, $(SRCS))

all: $(NAME)

$(NAME): $(OBJS)
	@$(CC) $(CFLAGS) $^ -o $(NAME)

$(OBJ_DIR):
ifeq ($(PLATFORM), windows)
	@powershell -Command "if (-not (Test-Path $(OBJ_DIR))) \
	{[void](New-Item -ItemType Directory -Path $(OBJ_DIR))}"
else
	@mkdir -p $(OBJ_DIR)
endif

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	@$(CC) $(CFLAGS) -c $< -o $@

clean:
ifeq ($(PLATFORM), windows)
	@powershell -Command "if (Test-Path $(OBJ_DIR)) \
	{Remove-Item -Path $(OBJ_DIR) -Recurse -Force}"
else
	@rm -rf $(OBJ_DIR)
endif

fclean: clean
	@rm -f $(NAME)

re: fclean all

.PHONY = all clean fclean re