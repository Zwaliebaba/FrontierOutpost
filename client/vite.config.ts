import { defineConfig } from "vite";

export default defineConfig({
  build: {
    target: "es2023",
    sourcemap: true,
    rolldownOptions: {
      // greybox.html is the client-only readability test harness (design §5, §9).
      input: { main: "index.html", greybox: "greybox.html" },
    },
  },
});
