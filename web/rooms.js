// Game metadata stays on this origin. Room and player data are always rendered as text.
(() => {
  /** @typedef {Readonly<{game:string,title_id:string,players:number,max_players:number,is_public:boolean,is_joinable:boolean,names:readonly string[],created_at:number}>} Room */
  /** @typedef {Readonly<{updated_at:number,total_rooms:number,total_players:number,rooms:readonly Room[]}>} Status */
  /** @typedef {{titleId:string,name:string,players:number,rooms:Room[]}} Group */
  const root = document.documentElement;
  const REFRESH_MS = 10000;
  const koreanNames = new Map([
    ['0100C2500FC20000', '스플래툰 3'],
    ['01003BC0000A0000', '스플래툰 2'],
    ['01003C700009C000', '스플래툰 2'],
  ]);
  const text = {
    ko: {
      open:'참가 가능',busy:'게임 중',locked:'채널 ***',public:'채널 없음',unknown:'이름을 찾지 못한 게임',refresh:'새로 고침',refreshing:'새로 고치는 중',
      info:'게임 정보',titleId:'타이틀 ID',player:'플레이어',privateNames:'참가자 이름이 비공개인 방입니다.',
      room:(/** @type {number} */ n)=>`방 ${String(n).padStart(2,'0')}`,
      capacity:(/** @type {number} */ n,/** @type {number} */ max)=>`${n}명 / 정원 ${max}명`,
      summary:(/** @type {number} */ n,/** @type {number} */ rooms)=>`${n}명 · ${rooms}개 방`,
      age:(/** @type {number} */ n)=>n<1?'방금 생성':n<60?`${n}분 전 생성`:`${Math.floor(n/60)}시간 전 생성`,
      updated:(/** @type {string} */ time)=>`마지막 갱신 ${time} · 10초마다 자동 갱신`,
    },
    en: {
      open:'Joinable',busy:'In game',locked:'Channel ***',public:'No channel',unknown:'Unidentified game',refresh:'Refresh',refreshing:'Refreshing',
      info:'Game details',titleId:'Title ID',player:'Player',privateNames:'Player names are private in this room.',
      room:(/** @type {number} */ n)=>`Room ${String(n).padStart(2,'0')}`,
      capacity:(/** @type {number} */ n,/** @type {number} */ max)=>`${n} of ${max} players`,
      summary:(/** @type {number} */ n,/** @type {number} */ rooms)=>`${n} ${n===1?'player':'players'} · ${rooms} ${rooms===1?'room':'rooms'}`,
      age:(/** @type {number} */ n)=>n<1?'Created just now':n<60?`Created ${n} min ago`:`Created ${Math.floor(n/60)} hr ago`,
      updated:(/** @type {string} */ time)=>`Last updated ${time} · Refreshes every 10 seconds`,
    },
  };
  const t = () => root.dataset.lang === 'ko' ? text.ko : text.en;
  /** @param {string} id */
  const $ = (id) => {
    const node = document.getElementById(id);
    if (!(node instanceof HTMLElement)) throw new TypeError(`Missing board element: ${id}`);
    return node;
  };
  /** @template {keyof HTMLElementTagNameMap} K
   * @param {K} tag @param {string} [className] @param {string} [content]
   * @returns {HTMLElementTagNameMap[K]} */
  const el = (tag, className = '', content) => {
    const node = document.createElement(tag);
    node.className = className;
    if (content !== undefined) node.textContent = content;
    return node;
  };
  /** @param {unknown} value @returns {value is Record<string, unknown>} */
  const record = (value) => value !== null && typeof value === 'object' && !Array.isArray(value);
  /** @param {unknown} value @returns {value is number} */
  const nonnegative = (value) => typeof value === 'number' && Number.isSafeInteger(value) && value >= 0;
  /** @param {unknown} value @returns {Room|null} */
  const parseRoom = (value) => {
    if (!record(value) || typeof value.game !== 'string' || typeof value.title_id !== 'string' ||
        !/^(0x)?[0-9a-f]{16}$/i.test(value.title_id) || !nonnegative(value.players) ||
        !nonnegative(value.max_players) || value.max_players < 1 || value.max_players > 16 ||
        value.players > value.max_players || typeof value.is_public !== 'boolean' ||
        typeof value.is_joinable !== 'boolean' || !nonnegative(value.created_at) ||
        !Array.isArray(value.names) || !value.names.every((name) => typeof name === 'string')) return null;
    return {game:value.game,title_id:value.title_id.replace(/^0x/i,'').toUpperCase(),players:value.players,
      max_players:value.max_players,is_public:value.is_public,is_joinable:value.is_joinable,
      names:value.names.slice(0,value.players),created_at:value.created_at};
  };
  /** @param {unknown} value @returns {Status|null} */
  const parseStatus = (value) => {
    if (!record(value) || !nonnegative(value.updated_at) || !nonnegative(value.total_rooms) ||
        !nonnegative(value.total_players) || !Array.isArray(value.rooms)) return null;
    /** @type {Room[]} */
    const rooms = [];
    for (const item of value.rooms) {
      const room = parseRoom(item);
      if (room === null) return null;
      rooms.push(room);
    }
    return {updated_at:value.updated_at,total_rooms:value.total_rooms,total_players:value.total_players,rooms};
  };
  /** @type {Map<string,string>} */
  const catalog = new Map();
  let catalogAttempted = false;
  /** @param {string} name */
  const knownName = (name) => name.trim() !== '' && !/^unknown(?: game| title)?$/i.test(name.trim()) && !/^(0x)?[0-9a-f]{16}$/i.test(name.trim());
  /** @param {Room} room */
  const gameName = (room) => {
    const local = root.dataset.lang === 'ko' ? koreanNames.get(room.title_id) : undefined;
    return (local ?? catalog.get(room.title_id) ?? (knownName(room.game) ? room.game : t().unknown)).replace(/[™®]/g,'').trim();
  };
  /** @type {Status|null} */
  let lastStatus = null;
  let lastRender = '';
  let pending = false;
  let manualPending = false;
  const updateRefreshButton = () => {
    const button=$('refresh-btn');
    const busy=pending || manualPending;
    button.toggleAttribute('disabled',busy);
    button.setAttribute('aria-busy',String(busy));
    button.setAttribute('aria-label',busy?t().refreshing:t().refresh);
    button.title=busy?t().refreshing:t().refresh;
  };

  /** @param {Room} room @param {number} index @param {number} nowSec */
  const roomItem = (room, index, nowSec) => {
    const item = el('article','room');
    item.setAttribute('aria-label',`${t().room(index+1)}, ${t().capacity(room.players,room.max_players)}`);
    const body = el('div','room-body');
    const meta = el('div','room-meta');
    meta.append(el('span','room-label',t().room(index+1)),
      el('span',room.is_joinable?'room-status available':'room-status busy',room.is_joinable?t().open:t().busy),
      el('span','room-privacy',room.is_public?t().public:t().locked));
    body.append(meta);
    if (room.is_public && room.names.length) {
      const names = el('ul','names');
      for (const name of room.names) {
        const person = el('li','person');
        const mark = el('span','person-mark',Array.from(name.trim())[0]?.toLocaleUpperCase(root.lang) ?? '·');
        mark.setAttribute('aria-hidden','true');
        person.append(mark,el('span','person-name',name || t().player));
        names.append(person);
      }
      body.append(names);
    } else body.append(el('p','room-note',room.is_public?t().capacity(room.players,room.max_players):t().privateNames));
    const age = el('time','room-age',t().age(Math.floor(Math.max(0,nowSec-room.created_at/1000)/60)));
    age.dateTime = new Date(room.created_at).toISOString();
    age.dataset.createdAt=String(room.created_at);
    body.append(age);
    const occupancy = el('div','occupancy');
    const number = el('strong','occupancy-number',String(room.players));
    number.append(el('span','occupancy-max',`/ ${room.max_players}`));
    number.setAttribute('aria-hidden','true');
    const meter = el('meter','occupancy-meter');
    meter.min=0;meter.max=room.max_players;meter.value=room.players;
    meter.setAttribute('aria-label',t().capacity(room.players,room.max_players));
    occupancy.append(number,meter);
    item.append(body,occupancy);
    return item;
  };
  const render = () => {
    updateRefreshButton();
    if (lastStatus === null) return;
    $('total-rooms').textContent=String(lastStatus.total_rooms);
    $('total-players').textContent=String(lastStatus.total_players);
    $('total-games').textContent=String(new Set(lastStatus.rooms.map((room)=>room.title_id)).size);
    const nowSec=Math.floor(Date.now()/1000);
    const key=JSON.stringify([root.dataset.lang,lastStatus.rooms,catalog.size]);
    if (key !== lastRender) {
      const expanded=new Set([...document.querySelectorAll('details.game-info[open]')].map((node)=>node.getAttribute('data-title-id')));
      const active=document.activeElement instanceof HTMLElement ? document.activeElement.dataset.gameInfo : undefined;
      /** @type {Map<string,Group>} */
      const groups=new Map();
      for (const room of lastStatus.rooms) {
        const group=groups.get(room.title_id);
        if (group) {group.players+=room.players;group.rooms.push(room);}
        else groups.set(room.title_id,{titleId:room.title_id,name:gameName(room),players:room.players,rooms:[room]});
      }
      const games=[...groups.values()].sort((a,b)=>b.players-a.players || a.titleId.localeCompare(b.titleId)).map((game)=>{
        const group=el('section','game');
        const header=el('div','game-header');
        const title=el('h2','game-name',game.name);
        title.id=`game-${game.titleId}`;
        group.setAttribute('aria-labelledby',title.id);
        header.append(title,el('span','game-summary',t().summary(game.players,game.rooms.length)));
        const info=el('details','game-info');
        info.dataset.titleId=game.titleId;info.open=expanded.has(game.titleId);
        const summary=el('summary','',t().info);summary.dataset.gameInfo=game.titleId;
        info.append(summary,el('span','game-id-label',t().titleId),el('code','',game.titleId));
        const rooms=el('div','game-rooms');
        game.rooms.forEach((room,index)=>rooms.append(roomItem(room,index,nowSec)));
        group.append(header,info,rooms);
        return group;
      });
      $('games').replaceChildren(...games);
      if (active) {
        const summary=document.querySelector(`summary[data-game-info="${active}"]`);
        if (summary instanceof HTMLElement) summary.focus({preventScroll:true});
      }
      lastRender=key;
    }
    for (const age of document.querySelectorAll('time.room-age')) {
      if (age instanceof HTMLElement) age.textContent=t().age(Math.floor(Math.max(0,nowSec-Number(age.dataset.createdAt)/1000)/60));
    }
    $('empty').hidden=lastStatus.rooms.length>0;
    $('updated').textContent=t().updated(new Date(lastStatus.updated_at*1000).toLocaleTimeString(root.lang));
  };
  const loadCatalog = async () => {
    catalogAttempted=true;
    try {
      const response=await fetch('game-titles.json',{signal:AbortSignal.timeout(8000)});
      if (!response.ok) return;
      /** @type {unknown} */
      const value=await response.json();
      if (!record(value)) return;
      for (const [id,name] of Object.entries(value)) if (/^[0-9A-F]{16}$/.test(id) && typeof name==='string') catalog.set(id,name);
      render();
    } catch (error) {
      if (!(error instanceof Error)) throw error;
      // Names already supplied by the relay remain usable if this optional catalog is unavailable.
      console.warn('Game catalog unavailable:',error.message);
    }
  };
  const refresh = async () => {
    if (pending) return;
    pending=true;
    updateRefreshButton();
    $('games').setAttribute('aria-busy','true');
    try {
      const response=await fetch('api/rooms',{cache:'no-store',signal:AbortSignal.timeout(8000)});
      if (!response.ok) throw new TypeError(`Room status HTTP ${response.status}`);
      /** @type {unknown} */
      const value=await response.json();
      const status=parseStatus(value);
      if (status===null) throw new TypeError('Invalid room status');
      lastStatus=status;
      $('offline').hidden=true;
      render();
      if (!catalogAttempted && status.rooms.some((room)=>!knownName(room.game))) void loadCatalog();
    } catch (error) {
      if (!(error instanceof Error)) throw error;
      $('offline').hidden=false;
      console.warn('Room status unavailable:',error.message);
    } finally {
      pending=false;
      $('loading').hidden=true;
      updateRefreshButton();
      $('games').setAttribute('aria-busy','false');
    }
  };
  const manualRefresh = async () => {
    if (pending || manualPending) return;
    manualPending=true;
    $('refresh-btn').classList.add('is-refreshing');
    updateRefreshButton();
    // A fast response still leaves one visible turn; the list itself updates immediately.
    const visibleTurn=matchMedia('(prefers-reduced-motion: reduce)').matches
      ? Promise.resolve()
      : new Promise((resolve)=>setTimeout(()=>resolve(undefined),650));
    try {
      await Promise.all([refresh(),visibleTurn]);
    } finally {
      manualPending=false;
      $('refresh-btn').classList.remove('is-refreshing');
      updateRefreshButton();
    }
  };
  addEventListener('DOMContentLoaded',()=>{
    updateRefreshButton();
    new MutationObserver(render).observe(root,{attributes:true,attributeFilter:['data-lang']});
    const watching=()=>!document.hidden;
    $('refresh-btn').addEventListener('click',()=>{void manualRefresh();});
    if (watching()) void refresh();
    setInterval(()=>{if(watching()) void refresh();},REFRESH_MS);
    document.addEventListener('visibilitychange',()=>{if(watching()) void refresh();});
  });
})();
