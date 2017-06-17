NULL =

# Configuration
OBJ_DIR = .
SRC_DIR = .

# Program
PRG_OBJS = $(OBJ_DIR)/autohintexe.o
PRG_TARGET = $(OBJ_DIR)/autohintexe$(EXE)
PRG_LIBS = -L. -lpsautohint

# Library
LIB_TARGET = $(OBJ_DIR)/libpsautohint.a

CFLAGS = \
	-I$(SRC_DIR)/include \
	-I$(SRC_DIR)/src \
	-Wall \
	-Wextra \
	-Wpedantic \
	-Wdeclaration-after-statement \
	-Wstrict-prototypes \
	-Wunused-macros \
	-Wmissing-prototypes \
	-O3 \
	$(NULL)

ifeq ($(firstword $(shell $(CC) --version)),clang)
CFLAGS += \
	-Wunreachable-code-break \
	$(NULL)
endif

ifeq ($(OS),Windows_NT)
EXE = .exe
else
PRG_LIBS += -lm
endif
					 
LIB_OBJS = \
	$(OBJ_DIR)/src/acfixed.o \
	$(OBJ_DIR)/src/ac.o \
	$(OBJ_DIR)/src/auto.o \
	$(OBJ_DIR)/src/bbox.o \
	$(OBJ_DIR)/src/charpath.o \
	$(OBJ_DIR)/src/charprop.o \
	$(OBJ_DIR)/src/check.o \
	$(OBJ_DIR)/src/control.o \
	$(OBJ_DIR)/src/eval.o \
	$(OBJ_DIR)/src/fix.o \
	$(OBJ_DIR)/src/flat.o \
	$(OBJ_DIR)/src/fontinfo.o \
	$(OBJ_DIR)/src/gen.o \
	$(OBJ_DIR)/src/head.o \
	$(OBJ_DIR)/src/logging.o \
	$(OBJ_DIR)/src/memory.o \
	$(OBJ_DIR)/src/merge.o \
	$(OBJ_DIR)/src/misc.o \
	$(OBJ_DIR)/src/pick.o \
	$(OBJ_DIR)/src/psautohint.o \
	$(OBJ_DIR)/src/read.o \
	$(OBJ_DIR)/src/report.o \
	$(OBJ_DIR)/src/shuffle.o \
	$(OBJ_DIR)/src/stemreport.o \
	$(OBJ_DIR)/src/write.o \
	$(NULL)

default: $(PRG_TARGET)

clean:
	rm -f $(PRG_OBJS)
	rm -f $(LIB_OBJS)
	rm -f $(PRG_TARGET)
	rm -f $(LIB_TARGET)

COMPILE = $(if $(filter $V,1),,@echo "  CC $< ";)$(CC)
LINK    = $(if $(filter $V,1),,@echo "  LD $@ ";)$(CC)
ARCHIVE = $(if $(filter $V,1),,@echo "  AR $@ ";)$(AR)

# Program
$(PRG_TARGET): $(PRG_OBJS) $(LIB_TARGET)
	$(LINK) $(CFLAGS) -o $@ $(PRG_OBJS) $(PRG_LIBS)

# Library
$(LIB_TARGET): $(LIB_OBJS)
	$(ARCHIVE) -rs $@ $?

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	$(COMPILE) $(CFLAGS) -c $< -o $@
