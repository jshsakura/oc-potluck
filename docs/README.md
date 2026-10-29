# Documentation site

Canonical English and Korean guides live in `src/content/docs/`. README links into those files for GitHub readers. Astro + Starlight builds the same content for `/oc-potluck/` and `/oc-potluck/ko/`.

```sh
npm ci --prefix docs
bash scripts/ci/run-limited.sh npm run build --prefix docs
bash scripts/ci/run-limited.sh npm run dev --prefix docs -- --host 127.0.0.1
```

Build/QA process trees on this machine must remain in the shared verified 4 GiB cgroup. Stop previews after QA. Standard builds need only Node. Astro copies the canonical `deploy/compose.nas.yml` to the downloadable public asset; do not edit that generated copy. For developer API pages, generate XML with the repository Doxyfile, then run `npm run build:api --prefix docs` under the same limiter.

The documentation workflow validates PRs and publishes main changes to GitHub Pages. Select GitHub Actions as the repository Pages source. Feature branches and PRs do not deploy. Intended URL: `https://jshsakura.github.io/oc-potluck/`; a local build alone does not mean it is published.

Design: `DESIGN.md`. Font license: `public/fonts/OFL-Baloo2.txt`.
