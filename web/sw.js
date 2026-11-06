// Makes Teevee work offline once it has been opened: files are fetched from the network when it is reachable (so an update shows up
// on the next visit) and from the cache when it is not.
const CACHE = 'teevee-v2';  // (v2: build-stamped engine files)
const FILES = ['./', 'index.html', 'teevee.js', 'teevee.wasm', 'manifest.webmanifest', 'icons/icon-192.png', 'icons/icon-512.png'];

self.addEventListener('install', e => { e.waitUntil(caches.open(CACHE).then(c => c.addAll(FILES)).then(() => self.skipWaiting())); });
self.addEventListener('activate', e => {
  e.waitUntil(caches.keys().then(keys => Promise.all(keys.filter(k => k !== CACHE).map(k => caches.delete(k)))).then(() => self.clients.claim()));
});
self.addEventListener('fetch', e => {
  if (e.request.method !== 'GET') return;
  // always ask the server (a 304 when nothing changed): a file the browser cached by itself is never used unchecked
  e.respondWith(fetch(e.request, { cache: 'no-cache' }).then(res => {
    if (res.ok && new URL(e.request.url).origin === location.origin) { const copy = res.clone(); caches.open(CACHE).then(c => c.put(e.request, copy)); }
    return res;
  }).catch(() => caches.match(e.request, { ignoreSearch: true })));  // offline: the copy kept, whatever build stamp the page asks for
});
