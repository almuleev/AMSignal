# Build the LVM Reader CLI with g++ (MSYS2/MinGW or any C++17 compiler).
CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -finput-charset=UTF-8
WINDRES  ?= windres
# Static linking keeps the binary self-contained (no libstdc++/libgcc DLLs).
LDFLAGS  ?= -static
TARGET   := lvm_reader
# Identify the actual checkout, including uncommitted source changes.
VERSION  := $(shell git describe --tags --always --dirty 2>/dev/null || echo dev)
RELEASE_VERSION := $(patsubst v%,%,$(VERSION))
CPPFLAGS += -DAPP_VERSION=\"$(VERSION)\"

# Parser/analysis library shared by the CLI and the tests.
LIB_SRC  := lvm_parser.cpp fft.cpp analysis.cpp data_io.cpp filter_engine.cpp spectrum_worker.cpp frf_analysis.cpp frf_worker.cpp
APP_SRC  := main.cpp $(LIB_SRC)
APP_OBJ  := $(APP_SRC:.cpp=.o)
HDRS     := $(wildcard *.hpp)
GUI_PARTS := $(wildcard gui_*.cpp)
GUI_SOURCES := $(GUI_PARTS) $(LIB_SRC) export_helpers.cpp formula_engine.cpp gap_details.cpp
GUI_OBJECTS := $(patsubst %.cpp,.build/make_gui/%.o,$(GUI_SOURCES))
GUI_TEST_OBJECT := .build/make_gui/gui_regression.o
BENCH_BIN := tests/perf_benchmark.exe
GUI_FLAGS := $(CPPFLAGS) $(CXXFLAGS) -DAPP_VERSION_W=L\"$(VERSION)\" -I.
GUI_RES  := AM_logo.o

ifeq ($(OS),Windows_NT)
    BIN      := $(TARGET).exe
    TEST_BIN := tests/run_tests.exe
    GUI_BIN  := AMSignal-$(RELEASE_VERSION)-x64.exe
else
    BIN      := $(TARGET)
    TEST_BIN := tests/run_tests
    GUI_BIN  := AMSignal-$(RELEASE_VERSION)
endif

.PHONY: all clean run test gui test-gui bench FORCE

all: $(BIN)

# Refresh the CLI version even when only the checkout/dirty state changed.
main.o: FORCE
FORCE:

$(BIN): $(APP_OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $(APP_OBJ) $(LDFLAGS)

%.o: %.cpp $(HDRS)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

run: $(BIN)
	./$(BIN) lvm_files_for_tests/test.lvm

test: $(TEST_BIN)
	./$(TEST_BIN)

test-gui: tests/gui_regression.exe
	./tests/gui_regression.exe

# Manual timing tool; it reports local measurements and is intentionally not a CI gate.
bench: $(BENCH_BIN)
	./$(BENCH_BIN) --input lvm_files_for_tests/test.lvm

$(BENCH_BIN): tests/perf_benchmark.cpp $(LIB_SRC) $(HDRS)
	$(CXX) $(CXXFLAGS) -I. -o $@ tests/perf_benchmark.cpp $(LIB_SRC) $(LDFLAGS)

tests/gui_regression.exe: $(GUI_TEST_OBJECT) $(GUI_OBJECTS)
	$(CXX) -o $@ $^ $(LDFLAGS) -lcomdlg32 -lgdi32 -luser32 -lgdiplus -lcomctl32 -luxtheme -ladvapi32 -lshell32

.build/make_gui:
	mkdir -p $@

.build/make_gui/flags.txt: FORCE | .build/make_gui
	@printf '%s\n' '$(CXX) $(GUI_FLAGS)' | cmp -s - $@ || printf '%s\n' '$(CXX) $(GUI_FLAGS)' > $@

.build/make_gui/%.o: %.cpp .build/make_gui/flags.txt | .build/make_gui
	$(CXX) $(GUI_FLAGS) -MMD -MP -c $< -o $@

$(GUI_TEST_OBJECT): tests/gui_regression.cpp .build/make_gui/flags.txt | .build/make_gui
	$(CXX) $(GUI_FLAGS) -MMD -MP -c $< -o $@

-include $(GUI_OBJECTS:.o=.d) $(GUI_TEST_OBJECT:.o=.d)

$(TEST_BIN): tests/run_tests.cpp $(LIB_SRC) export_helpers.cpp formula_engine.cpp gap_details.cpp $(HDRS)
	$(CXX) $(CXXFLAGS) -I. -o $@ tests/run_tests.cpp $(LIB_SRC) export_helpers.cpp formula_engine.cpp gap_details.cpp $(LDFLAGS)

# Native Win32 GUI viewer (Windows only). Needs -municode for wWinMain and the
# Win32 import libraries. On Windows you can also run: powershell ./build_gui.ps1
gui: $(GUI_BIN)

$(GUI_RES): AM_logo.rc AM_logo.ico
	$(WINDRES) -O coff -i $< -o $@

$(GUI_BIN): $(GUI_OBJECTS) $(GUI_RES)
	$(CXX) -municode -mwindows -o $@ $^ $(LDFLAGS) -lcomdlg32 -lgdi32 -luser32 -lgdiplus -lcomctl32 -luxtheme -ladvapi32 -lshell32

clean:
	rm -f $(APP_OBJ) $(BIN) $(TEST_BIN) $(GUI_BIN) $(GUI_RES) $(GUI_OBJECTS) $(GUI_OBJECTS:.o=.d) $(GUI_TEST_OBJECT) $(GUI_TEST_OBJECT:.o=.d) $(BENCH_BIN) tests/gui_regression.exe
