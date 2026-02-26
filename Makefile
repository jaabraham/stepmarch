CC = gcc
CXX = g++
CFLAGS = -Wall -O3 -std=c11 -I/opt/cuda/targets/x86_64-linux/include
CXXFLAGS = -Wall -O3 -std=c++11 -I/opt/cuda/targets/x86_64-linux/include -I/usr/include/SDL2
LDFLAGS = -L/opt/cuda/targets/x86_64-linux/lib -lOpenCL -lpng -lm

# GUI libraries
GUI_LDFLAGS = $(LDFLAGS) -lSDL2 -lGL -ldl

TARGET = stepmarch
GUITARGET = stepmarch_gui
SRCDIR = src
OBJDIR = obj
BINDIR = .

SOURCES = $(SRCDIR)/main.c $(SRCDIR)/png_writer.c
OBJECTS = $(SOURCES:$(SRCDIR)/%.c=$(OBJDIR)/%.o)

# ImGui sources
IMGUI_SOURCES = $(SRCDIR)/imgui/imgui.cpp \
                $(SRCDIR)/imgui/imgui_draw.cpp \
                $(SRCDIR)/imgui/imgui_widgets.cpp \
                $(SRCDIR)/imgui/imgui_tables.cpp \
                $(SRCDIR)/imgui/imgui_impl_sdl2.cpp \
                $(SRCDIR)/imgui/imgui_impl_opengl3.cpp

IMGUI_OBJECTS = $(IMGUI_SOURCES:$(SRCDIR)/%.cpp=$(OBJDIR)/%.o)

.PHONY: all clean run test gui anim multi

all: $(BINDIR)/$(TARGET)

gui: $(BINDIR)/$(GUITARGET)

anim: $(BINDIR)/stepmarch_anim

multi: $(BINDIR)/stepmarch_multi

$(OBJDIR):
	mkdir -p $(OBJDIR)/imgui

# CLI build
$(BINDIR)/$(TARGET): $(OBJECTS) | $(OBJDIR)
	$(CC) $(OBJECTS) -o $@ $(LDFLAGS)

$(OBJDIR)/%.o: $(SRCDIR)/%.c | $(OBJDIR)
	$(CC) $(CFLAGS) -c $< -o $@

# ImGui object files
$(OBJDIR)/imgui/%.o: $(SRCDIR)/imgui/%.cpp | $(OBJDIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# GUI builds
$(BINDIR)/$(GUITARGET): $(OBJDIR)/gui.o $(OBJDIR)/png_writer.o $(IMGUI_OBJECTS) | $(OBJDIR)
	$(CXX) $^ -o $@ $(GUI_LDFLAGS)

$(BINDIR)/stepmarch_anim: $(OBJDIR)/gui_animation.o $(OBJDIR)/png_writer.o $(IMGUI_OBJECTS) | $(OBJDIR)
	$(CXX) $^ -o $@ $(GUI_LDFLAGS)

$(BINDIR)/stepmarch_multi: $(OBJDIR)/gui_multi_fractal.o $(OBJDIR)/png_writer.o $(IMGUI_OBJECTS) | $(OBJDIR)
	$(CXX) $^ -o $@ $(GUI_LDFLAGS)

$(OBJDIR)/gui.o: $(SRCDIR)/gui.cpp | $(OBJDIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(OBJDIR)/gui_animation.o: $(SRCDIR)/gui_animation.cpp | $(OBJDIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(OBJDIR)/gui_multi_fractal.o: $(SRCDIR)/gui_multi_fractal.cpp | $(OBJDIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -rf $(OBJDIR) $(BINDIR)/$(TARGET) $(BINDIR)/$(GUITARGET) output/*.png

run: $(BINDIR)/$(TARGET)
	./$(BINDIR)/$(TARGET)

run_gui: $(BINDIR)/$(GUITARGET)
	./$(BINDIR)/$(GUITARGET)

test: $(BINDIR)/$(TARGET)
	./$(BINDIR)/$(TARGET) -o output/test_render.png -w 800 -h 600

# Debug build with symbols
debug: CFLAGS = -Wall -g -O0 -std=c11 -DDEBUG
debug: $(BINDIR)/$(TARGET)
