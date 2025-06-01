# Compiler
CC = gcc

# Compiler flags
# -Wall: Enable all warnings
# -g: Add debugging information
CFLAGS = -Wall -g

# Source files
SERVER_SRC = server.c
SENSOR_SRC = sensor.c
# Uncomment common_obj if you create common.c
# COMMON_SRC = common.c

# Object files
SERVER_OBJ = $(SERVER_SRC:.c=.o)
SENSOR_OBJ = $(SENSOR_SRC:.c=.o)
# Uncomment common_obj if you create common.c
# COMMON_OBJ = $(COMMON_SRC:.c=.o)

# Executable names
SERVER_EXE = server
SENSOR_EXE = sensor

# Default target: build all
all: $(SERVER_EXE) $(SENSOR_EXE)

# Rule to build the server
$(SERVER_EXE): $(SERVER_OBJ) # $(COMMON_OBJ) # Uncomment COMMON_OBJ if common.c is used
	$(CC) $(CFLAGS) -o $(SERVER_EXE) $(SERVER_OBJ) # $(COMMON_OBJ)

# Rule to build the sensor (client)
$(SENSOR_EXE): $(SENSOR_OBJ) # $(COMMON_OBJ) # Uncomment COMMON_OBJ if common.c is used
	$(CC) $(CFLAGS) -o $(SENSOR_EXE) $(SENSOR_OBJ) # $(COMMON_OBJ)

# Generic rule for .c to .o
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Clean target to remove executables and object files
clean:
	rm -f $(SERVER_EXE) $(SENSOR_EXE) *.o

# Phony targets
.PHONY: all clean