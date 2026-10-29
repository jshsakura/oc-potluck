import { readFileSync, writeFileSync } from 'node:fs';

/** @type {unknown} */
const source = JSON.parse(readFileSync(new URL('../server/src/Utils/gamelist.json', import.meta.url), 'utf8'));
if (!Array.isArray(source)) throw new TypeError('Game catalog must be an array');
/** @type {Map<string, string>} */
const titles = new Map();
for (const entry of source) {
  if (typeof entry !== 'object' || entry === null ||
      typeof entry.id !== 'string' || typeof entry.name !== 'string' ||
      !/^0x[0-9a-f]{16}$/i.test(entry.id)) throw new TypeError('Invalid game catalog entry');
  titles.set(entry.id.slice(2).toUpperCase(), entry.name);
}
writeFileSync(new URL('../web/game-titles.json', import.meta.url), JSON.stringify(Object.fromEntries([...titles].sort())) + '\n');
console.log(`Generated ${titles.size} game names from the relay catalog`);
