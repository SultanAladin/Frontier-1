#include "RendererEngines.hpp"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace fs = std::filesystem;
using namespace sdf_demo;

static void writePpm(const std::string& path, const std::vector<Vec3>& hdr,
                     int w, int h, float exposure, bool isLinearSrgb = false) {
  std::ofstream f(path, std::ios::binary);
  f << "P6\n" << w << " " << h << "\n255\n";
  std::vector<uint8_t> bytes(size_t(w) * h * 3);
  for (size_t i = 0; i < size_t(w) * h; ++i) {
    Vec3 c = hdr[i] * exposure;
    Vec3 srgb = isLinearSrgb ? linearToSrgb(clamp3(c, 0.0f, 1.0f))
                             : linearToSrgb(acesToneMap(c));
    bytes[i * 3 + 0] = uint8_t(clampf(srgb.x * 255.0f + 0.5f, 0.0f, 255.0f));
    bytes[i * 3 + 1] = uint8_t(clampf(srgb.y * 255.0f + 0.5f, 0.0f, 255.0f));
    bytes[i * 3 + 2] = uint8_t(clampf(srgb.z * 255.0f + 0.5f, 0.0f, 255.0f));
  }
  f.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
}

int main(int argc, char** argv) {
  std::string outDir = (argc > 1) ? argv[1] : "output_engines";
  int width = 480;
  int height = 270;
  int ptSpp = 24;

  for (int i = 2; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--res" && i + 1 < argc) {
      int w = 0, h = 0;
      if (std::sscanf(argv[++i], "%dx%d", &w, &h) == 2 && w >= 64 && h >= 64) {
        width = w;
        height = h;
      }
    } else if (arg == "--pt-spp" && i + 1 < argc) {
      ptSpp = clampi(std::atoi(argv[++i]), 1, 256);
    }
  }

  fs::create_directories(outDir);

  std::cout << "Building BVH showcase scene (Boxes + Spheres + Capsules + Tessellated Torus)...\n";
  Scene dayScene = buildShowcaseScene(true, true, true);
  Scene nightScene = buildShowcaseScene(false, true, true);

  std::cout << "Day Scene primitives: " << dayScene.prims.size()
            << ", BVH nodes: " << dayScene.bvhNodes.size() << "\n";

  MultiRenderer dayRenderer(dayScene);
  MultiRenderer nightRenderer(nightScene);

  struct Job {
    std::string filename;
    MultiRenderer* renderer;
    RenderConfig cfg;
    float exposure;
    bool isMatCap;
  };

  std::vector<Job> jobs;

  // 1. MatCap
  {
    RenderConfig c{width, height, RenderEngine::MatCap, ShadowAlg::ContactPcss, ReflAlg::Bvh, 2, 16, ptSpp, 6, true, true, 1.0f};
    jobs.push_back({"01_matcap.ppm", &dayRenderer, c, 1.0f, true});
  }
  // 2. PBR without GI (Shadow Lab Contact-PCSS + BVH Reflections)
  {
    RenderConfig c{width, height, RenderEngine::PbrDirect, ShadowAlg::ContactPcss, ReflAlg::Bvh, 2, 16, ptSpp, 6, true, true, 0.85f};
    jobs.push_back({"02_pbr_no_gi.ppm", &dayRenderer, c, 0.85f, false});
  }
  // 3. DDGI Raytracing (Daylight + Emissive Night)
  {
    RenderConfig c{width, height, RenderEngine::Ddgi, ShadowAlg::ContactPcss, ReflAlg::Bvh, 2, 16, ptSpp, 6, true, true, 0.85f};
    jobs.push_back({"03_ddgi_day.ppm", &dayRenderer, c, 0.85f, false});
    jobs.push_back({"06_ddgi_night_emissive.ppm", &nightRenderer, c, 1.10f, false});
  }
  // 4. SDF GI only (Shadow Lab Contact-PCSS shadows + BVH reflections; SDF strictly for GI)
  {
    RenderConfig c{width, height, RenderEngine::SdfGi, ShadowAlg::ContactPcss, ReflAlg::Bvh, 2, 16, ptSpp, 6, true, true, 0.85f};
    jobs.push_back({"04_sdf_gi_day.ppm", &dayRenderer, c, 0.85f, false});
    jobs.push_back({"07_sdf_gi_night_emissive.ppm", &nightRenderer, c, 1.10f, false});
  }
  // 5. Path Tracer (MIS + NEE + A-Trous denoiser)
  {
    RenderConfig c{width, height, RenderEngine::PathTracer, ShadowAlg::Bvh, ReflAlg::Bvh, 2, 16, ptSpp, 6, true, true, 0.85f};
    jobs.push_back({"05_path_tracer_day.ppm", &dayRenderer, c, 0.85f, false});
    jobs.push_back({"08_path_tracer_night.ppm", &nightRenderer, c, 1.10f, false});
  }

  for (const auto& job : jobs) {
    double ms = 0.0;
    auto hdr = job.renderer->render(job.cfg, ms);
    std::string path = (fs::path(outDir) / job.filename).string();
    writePpm(path, hdr, job.cfg.width, job.cfg.height, job.exposure, job.isMatCap);
    std::cout << "  [" << engineName(job.cfg.engine) << "] -> " << path
              << " (" << job.cfg.width << "x" << job.cfg.height << ", "
              << int(ms) << " ms)\n";
  }

  std::cout << "Done rendering all 5 engines.\n";
  return 0;
}
