import { defineConfig } from "vite";
export default defineConfig({
  root: "web",
  base: "/",
  build: {
    assetsInlineLimit: 0,
    outDir: "../dist",
    emptyOutDir: true,
    target: "es2022",
  },
  server: { host: "127.0.0.1" },
});
