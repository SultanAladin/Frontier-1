CXX := g++
CXXFLAGS := -O3 -std=c++20 -Wall -Wextra -pthread
HEADERS := MathTypes.hpp SceneBvh.hpp ShadowLab.hpp RendererEngines.hpp

all: sdf_demo

sdf_demo: main.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS) -o $@ $<

verify: sdf_demo
	./sdf_demo output_engines --res 480x270 --pt-spp 24
	for f in output_engines/*.ppm; do convert "$$f" "$${f%.ppm}.png" && rm -f "$$f"; done
	ls -lh output_engines/*.png

render: sdf_demo
	./sdf_demo output_engines --res 800x450 --pt-spp 32
	for f in output_engines/*.ppm; do convert "$$f" "$${f%.ppm}.png" && rm -f "$$f"; done
	ls -lh output_engines/*.png

clean:
	rm -f sdf_demo output_engines/*.ppm output_engines/*.png
