importScripts('https://cdn.jsdelivr.net/npm/pako@2.0.3/dist/pako.min.js');

self.addEventListener('message', ({ data: { id, buffer, operation } }) => {
  buffer = pako[operation](buffer);
  self.postMessage({ id, buffer }, [buffer.buffer]);
});
