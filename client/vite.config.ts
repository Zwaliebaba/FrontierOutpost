import { defineConfig } from "vite";

export default defineConfig({
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
