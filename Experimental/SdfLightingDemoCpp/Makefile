CXX := g++
CXXFLAGS := -O3 -std=c++20 -Wall -Wextra -pthread

all: sdf_demo

sdf_demo: main.cpp
	$(CXX) $(CXXFLAGS) -o $@ $<

render: sdf_demo
	./sdf_demo output
	for f in output/*.ppm; do convert "$$f" "$${f%.ppm}.png" && rm -f "$$f"; done
	ls -lh output/*.png

clean:
	rm -f sdf_demo output/*.ppm output/*.png
