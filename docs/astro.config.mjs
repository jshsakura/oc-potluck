import { copyFileSync, mkdirSync } from 'node:fs';
import { defineConfig } from 'astro/config';
import starlight from '@astrojs/starlight';

// Serve the same Compose source in development and production.
const downloads = new URL('./public/downloads/', import.meta.url);
mkdirSync(downloads, { recursive: true });
copyFileSync(new URL('../deploy/compose.nas.yml', import.meta.url), new URL('compose.nas.yml', downloads));

export default defineConfig({
  site: 'https://jshsakura.github.io',
  base: '/oc-potluck',
  markdown: { gfm: true },
  integrations: [starlight({
    title: 'potluck.',
    description: 'Local multiplayer with friends through LDN MITM, nearby or far away.',
    favicon: '/favicon.svg',
    head: [
      { tag: 'meta', attrs: { property: 'og:image', content: 'https://jshsakura.github.io/oc-potluck/og-image.png' } },
      { tag: 'meta', attrs: { property: 'og:image:width', content: '1200' } },
      { tag: 'meta', attrs: { property: 'og:image:height', content: '630' } },
      { tag: 'meta', attrs: { property: 'og:image:alt', content: 'potluck. Local wireless play, over the internet.' } },
      { tag: 'meta', attrs: { name: 'twitter:image', content: 'https://jshsakura.github.io/oc-potluck/og-image.png' } },
    ],
    components: { Hero: './src/components/Hero.astro', SiteTitle: './src/components/SiteTitle.astro' },
    customCss: ['./src/styles/custom.css'],
    defaultLocale: 'root',
    locales: { root: { label: 'English', lang: 'en' }, ko: { label: '한국어', lang: 'ko' } },
    editLink: { baseUrl: 'https://github.com/jshsakura/oc-potluck/edit/main/docs/src/content/docs/' },
    social: [{ icon: 'github', label: 'GitHub', href: 'https://github.com/jshsakura/oc-potluck' }],
    sidebar: [
      { label: 'Start playing', translations: { ko: '플레이 시작' }, items: [
        { slug: 'guides/introduction' }, { slug: 'guides/installation' },
        { slug: 'guides/overlay' }, { slug: 'guides/configuration' },
        { slug: 'guides/maintenance' }, { slug: 'guides/troubleshooting' },
      ] },
      { label: 'Host a relay', translations: { ko: '중계 서버 운영' }, items: [
        { slug: 'guides/relay-server' }, { slug: 'guides/relay-native' }, { slug: 'guides/synology' },
      ] },
      { label: 'Development', translations: { ko: '개발' }, items: [
        { slug: 'guides/building' }, { slug: 'guides/architecture' },
        { slug: 'guides/contributing' }, { slug: 'api' },
        { slug: 'reference/ipc-commands' }, { slug: 'reference/protocol' },
      ] },
    ],
  })],
});
