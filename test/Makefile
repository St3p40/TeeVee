# Host tests for the pure-logic headers (no board needed). Run: make -C test
CXX ?= g++
CXXFLAGS = -O1 -std=c++17 -Wall -Wextra -Wno-unused-parameter -I../src/core -I../src

TESTS = proto_test portal_test settings_test style_test custom_test gesture_test motion_test mind_test look_net_test app_test pet_test turnmap_test appraisal_test pack_test mind_senses_test hearing_test voice_test

all: $(TESTS)
	@for t in $(TESTS); do ./$$t || exit 1; done
	@echo "all host tests passed"

%: %.cpp $(wildcard ../src/core/*.h) $(wildcard ../src/*.h) $(wildcard ../desktop/pet.h)
	$(CXX) $(CXXFLAGS) $< -o $@

clean:
	rm -f $(TESTS)

# Renderer pixel regression (see render_golden.cpp). Compiles every src/render*.cpp.
render_golden: render_golden.cpp $(wildcard ../src/core/render*.cpp) ../src/core/font_data.cpp $(wildcard ../src/core/*.h)
	$(CXX) -O1 -std=c++17 -Wall -Wextra -Wno-unused-parameter -Istub -I../src/core render_golden.cpp platform_host.cpp $(wildcard ../src/core/render*.cpp) ../src/core/font_data.cpp -o $@

# The touch mapping through the head turn: renders frames, so it is built with the renderer like render_golden.
turnmap_test: turnmap_test.cpp $(wildcard ../src/core/render*.cpp) ../src/core/font_data.cpp $(wildcard ../src/core/*.h)
	$(CXX) -O1 -std=c++17 -Wall -Wextra -Wno-unused-parameter -Istub -I../src/core turnmap_test.cpp platform_host.cpp $(wildcard ../src/core/render*.cpp) ../src/core/font_data.cpp -o $@

golden-save: render_golden
	./render_golden > render.golden
	@echo "recorded $$(wc -l < render.golden) frame hashes"

golden-check: render_golden
	./render_golden | diff - render.golden > /dev/null && echo "renderer output identical ($$(wc -l < render.golden) frames)" || { echo "RENDERER OUTPUT CHANGED:"; ./render_golden | diff - render.golden | head -20; exit 1; }

# Lua app VM: Lua is compiled here from lib/lua with the firmware's flags (LUA_32BITS, bounded C stack).
LUA_SRC = $(filter-out ../lib/lua/lua.c,$(wildcard ../lib/lua/*.c))
LUA_OBJ = $(patsubst ../lib/lua/%.c,lua_obj/%.o,$(LUA_SRC))
lua_obj/%.o: ../lib/lua/%.c
	@mkdir -p lua_obj
	$(CC) -O1 -w -DLUA_32BITS -DLUAI_MAXCCALLS=60 -c $< -o $@

app_test: app_test.cpp ../src/core/app_vm.cpp ../src/core/font_data.cpp $(LUA_OBJ) $(wildcard ../src/core/*.h)
	$(CXX) -O1 -std=c++17 -Wall -Wextra -Wno-unused-parameter -DLUA_32BITS -DLUAI_MAXCCALLS=60 -I../src/core -I../lib/lua app_test.cpp platform_host.cpp ../src/core/app_vm.cpp ../src/core/font_data.cpp $(LUA_OBJ) -o $@

# The mind net at the desktop and web size (128 inner units), to check it behaves like the board's 32.
mind-big: mind_test.cpp $(wildcard ../src/core/*.h)
	$(CXX) $(CXXFLAGS) -DTEEVEE_MIND_N=128 mind_test.cpp -o mind_test_big && ./mind_test_big
