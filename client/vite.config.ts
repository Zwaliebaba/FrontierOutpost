import { defineConfig } from "vite";

export default defineConfig({
  // wasm-bindgen loads the simulation with `new URL(..., import.meta.url)`, an asset URL, so the
  // module is an asset everywhere. That also lets tests import its bytes with `?inline`.
  assetsInclude: ["**/*.wasm"],
  server: {
    // `npm run dev` serves the client; the game server answers the API (`cargo run -p frontier-server`).
    proxy: { "/api": "http://127.0.0.1:8080" },
  },
  build: {
    target: "es2023",
    sourcemap: true,
    rolldownOptions: {
      // greybox.html is the client-only readability test harness (design §5, §9).
      input: { main: "index.html", greybox: "greybox.html" },
    },
  },
});
