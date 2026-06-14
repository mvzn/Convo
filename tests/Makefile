# Headless DSP test runner for Convo. Builds standalone — does not touch the plugin build.
# Run: make -C tests && ./tests/runtests
#
# JUCE is taken from the plugin's FetchContent checkout by default. Override with
#   make JUCE_MODULES=/path/to/JUCE/modules

JUCE_MODULES ?= ../build/_deps/juce-src/modules

CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wno-unused-parameter -Wno-deprecated-declarations \
            -pthread -fvisibility=hidden

# auto-generated header dependencies: editing a .h rebuilds every .o that includes it
# (without this, changing e.g. IRBakeParams leaves stale objects with a mismatched layout)
DEPFLAGS := -MMD -MP

# Headless module set: tests/JuceHeader.h matches this exact list and is found first via -I.
DEFS := \
    -DLINUX=1 -DNDEBUG=1 \
    -DJUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1 \
    -DJUCE_STANDALONE_APPLICATION=0 \
    -DJUCE_WEB_BROWSER=0 \
    -DJUCE_USE_CURL=0 \
    -DJUCE_MODULE_AVAILABLE_juce_core=1 \
    -DJUCE_MODULE_AVAILABLE_juce_events=1 \
    -DJUCE_MODULE_AVAILABLE_juce_data_structures=1 \
    -DJUCE_MODULE_AVAILABLE_juce_audio_basics=1 \
    -DJUCE_MODULE_AVAILABLE_juce_audio_formats=1 \
    -DJUCE_MODULE_AVAILABLE_juce_dsp=1

INCLUDES := -I. -I$(JUCE_MODULES) -I../Source

LDLIBS := -lpthread -ldl -lrt

OBJDIR := build
OBJS := \
    $(OBJDIR)/juce_core.o \
    $(OBJDIR)/juce_events.o \
    $(OBJDIR)/juce_data_structures.o \
    $(OBJDIR)/juce_audio_basics.o \
    $(OBJDIR)/juce_audio_formats.o \
    $(OBJDIR)/juce_dsp.o \
    $(OBJDIR)/juce_core_CompilationTime.o \
    $(OBJDIR)/ConvolutionEngine.o \
    $(OBJDIR)/IRLibrary.o \
    $(OBJDIR)/runtests.o

DEPS := $(OBJS:.o=.d)

runtests: $(OBJS)
	$(CXX) $(CXXFLAGS) $(OBJS) $(LDLIBS) -o $@

$(OBJDIR):
	mkdir -p $(OBJDIR)

$(OBJDIR)/runtests.o: runtests.cpp | $(OBJDIR)
	$(CXX) $(CXXFLAGS) $(DEPFLAGS) $(DEFS) $(INCLUDES) -c $< -o $@

$(OBJDIR)/ConvolutionEngine.o: ../Source/ConvolutionEngine.cpp | $(OBJDIR)
	$(CXX) $(CXXFLAGS) $(DEPFLAGS) $(DEFS) $(INCLUDES) -c $< -o $@

$(OBJDIR)/IRLibrary.o: ../Source/IRLibrary.cpp | $(OBJDIR)
	$(CXX) $(CXXFLAGS) $(DEPFLAGS) $(DEFS) $(INCLUDES) -c $< -o $@

$(OBJDIR)/juce_core.o: | $(OBJDIR)
	$(CXX) $(CXXFLAGS) $(DEPFLAGS) $(DEFS) $(INCLUDES) -x c++ -c $(JUCE_MODULES)/juce_core/juce_core.cpp -o $@

$(OBJDIR)/juce_events.o: | $(OBJDIR)
	$(CXX) $(CXXFLAGS) $(DEPFLAGS) $(DEFS) $(INCLUDES) -x c++ -c $(JUCE_MODULES)/juce_events/juce_events.cpp -o $@

$(OBJDIR)/juce_data_structures.o: | $(OBJDIR)
	$(CXX) $(CXXFLAGS) $(DEPFLAGS) $(DEFS) $(INCLUDES) -x c++ -c $(JUCE_MODULES)/juce_data_structures/juce_data_structures.cpp -o $@

$(OBJDIR)/juce_audio_basics.o: | $(OBJDIR)
	$(CXX) $(CXXFLAGS) $(DEPFLAGS) $(DEFS) $(INCLUDES) -x c++ -c $(JUCE_MODULES)/juce_audio_basics/juce_audio_basics.cpp -o $@

$(OBJDIR)/juce_audio_formats.o: | $(OBJDIR)
	$(CXX) $(CXXFLAGS) $(DEPFLAGS) $(DEFS) $(INCLUDES) -x c++ -c $(JUCE_MODULES)/juce_audio_formats/juce_audio_formats.cpp -o $@

$(OBJDIR)/juce_dsp.o: | $(OBJDIR)
	$(CXX) $(CXXFLAGS) $(DEPFLAGS) $(DEFS) $(INCLUDES) -x c++ -c $(JUCE_MODULES)/juce_dsp/juce_dsp.cpp -o $@

$(OBJDIR)/juce_core_CompilationTime.o: | $(OBJDIR)
	$(CXX) $(CXXFLAGS) $(DEPFLAGS) $(DEFS) $(INCLUDES) -x c++ -c $(JUCE_MODULES)/juce_core/juce_core_CompilationTime.cpp -o $@

clean:
	rm -rf $(OBJDIR) runtests

-include $(DEPS)

.PHONY: clean
