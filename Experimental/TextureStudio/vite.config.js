import { defineConfig } from "vite";

// React hooks and their renderer must come from one dependency generation. A
// late optimizer discovery used to change ?v= URLs while an open preview still
// held the previous modules, creating two React dispatchers (and two Three runtimes).
const studioDependencies = [
  "react",
  "react-dom",
  "react-dom/client",
  "react/jsx-runtime",
  "react/jsx-dev-runtime",
  "lucide-react",
  "three",
  "three-mesh-bvh",
  "three/addons/controls/OrbitControls.js",
  "three/addons/controls/TransformControls.js",
  "three/addons/geometries/DecalGeometry.js",
  "three/addons/geometries/RoundedBoxGeometry.js",
  "three/addons/geometries/TeapotGeometry.js",
  "three/addons/math/MeshSurfaceSampler.js",
  "three/addons/loaders/OBJLoader.js",
  "three/addons/loaders/GLTFLoader.js",
  "three/addons/loaders/PLYLoader.js",
  "three/addons/loaders/STLLoader.js",
  "fflate",
  "opentype.js",
  "paper",
];

export default defineConfig({
  // Isolate the repaired runtime from the old immutable /node_modules/.vite URLs.
  cacheDir: "node_modules/.vite-studio",
  resolve: { dedupe: ["react", "react-dom", "three"] },
  optimizeDeps: {
    entries: ["index.html"],
    include: studioDependencies,
    noDiscovery: true,
    force: true,
  },
  server: {
    host: "0.0.0.0",
    allowedHosts: [".e2b.app"],
    // Dev source and optimized modules must not survive as mismatched immutable
    // browser/proxy cache entries after a dependency restart.
    headers: { "Cache-Control": "no-store" },
    watch: { ignored: ["**/.playwright/**", "**/site/**"] },
  },
  preview: {
    host: "0.0.0.0",
    allowedHosts: [".e2b.app"],
    headers: { "Cache-Control": "no-store" },
  },
  build: {
    rollupOptions: {
      output: {
        manualChunks: { three: ["three"], react: ["react", "react-dom"] },
      },
    },
  },
});
