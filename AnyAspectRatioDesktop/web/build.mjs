import { build } from "esbuild";
import { createRequire } from "node:module";
import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const here = dirname(fileURLToPath(import.meta.url));
const require = createRequire(import.meta.url);

const desktopPlatform = {
  name: "desktop-platform",
  setup(builder) {
    builder.onResolve({ filter: /\/services\/platform$/ }, () => ({ path: resolve(here, "src/platform.ts") }));
    builder.onResolve({ filter: /\/launch_options\/steamLaunchOptions$/ }, () => ({
      path: resolve(here, "src/launchOptions.ts"),
    }));
  },
};

// The Decky sources are bundled unchanged; Steam-only modules are swapped for desktop versions,
// and every package (React included) resolves from this project's node_modules.
await build({
  entryPoints: [resolve(here, "src/main.tsx")],
  outfile: resolve(here, "dist/app.js"),
  bundle: true,
  format: "iife",
  target: "es2020",
  jsx: "automatic",
  minify: process.argv.includes("--minify"),
  sourcemap: !process.argv.includes("--minify"),
  plugins: [desktopPlatform],
  alias: {
    "@decky/ui": resolve(here, "src/decky-ui.tsx"),
    "@decky/api": resolve(here, "src/decky-api.ts"),
    "react": dirname(require.resolve("react/package.json")),
    "react-dom": dirname(require.resolve("react-dom/package.json")),
    "react-markdown": resolve(here, "node_modules/react-markdown"),
  },
  define: { "process.env.NODE_ENV": '"production"' },
  logLevel: "info",
});
