import { defineConfig } from "@playwright/test";
import config from "./playwright.config";

// CPU-only hosts keep the same CSS viewport and gameplay, with cheaper backing buffers.
export default defineConfig({ ...config, use: { ...config.use, deviceScaleFactor: 0.5 } });
