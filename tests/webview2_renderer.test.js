// renderer.js, the webview2 backend's page script, in Node (no browser, no
// dependencies: node --test). Run by ctest as webview2_renderer_js, after
// unit_webview2 has written a frame with the C++ encoder:
//
//   SCRAPS_WEBVIEW2_FIXTURES=<dir with frame.bin, frame.json> node --test tests/webview2_renderer.test.js
//
// It checks that the JS decoder reads the C++ encoder's bytes exactly as the
// C++ decoder does (the shared-buffer format round-trips), that it refuses
// damaged frames without throwing, and the input rules: Esc is never
// forwarded, nothing is forwarded outside "playing" except P in "paused".

'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

const root = path.join(__dirname, '..');
const renderer = require(path.join(root, 'engine', 'backends', 'webview2', 'renderer.js'));
const fixtures = process.env.SCRAPS_WEBVIEW2_FIXTURES;

function frameBuffer() {
  const bytes = fs.readFileSync(path.join(fixtures, 'frame.bin'));
  // Its own ArrayBuffer, as the page gets one from the shared buffer.
  const buffer = new ArrayBuffer(bytes.length);
  new Uint8Array(buffer).set(bytes);
  return buffer;
}

function decode(buffer, base, capacity) {
  const commands = [];
  const visitor = {
    clear(rgba) { commands.push({ op: 1, rgba }); },
    triangles(texture, offset, count) { commands.push({ op: 2, texture, count, offset }); },
    lines(offset, count) { commands.push({ op: 3, count, offset }); },
    texture(id, width, height, offset) { commands.push({ op: 4, texture: id, width, height, offset }); },
    destroy(id) { commands.push({ op: 5, texture: id }); },
  };
  const header = renderer.walkFrame(buffer, base, capacity, visitor, false);
  return { header, commands };
}

test('the C++ encoder\'s frame decodes the same in JS', { skip: !fixtures && 'SCRAPS_WEBVIEW2_FIXTURES not set' }, () => {
  const expected = JSON.parse(fs.readFileSync(path.join(fixtures, 'frame.json'), 'utf8'));
  const buffer = frameBuffer();
  const { header, commands } = decode(buffer, 0, buffer.byteLength);
  assert.ok(header, 'the frame decodes');
  assert.equal(header.seq, expected.seq);
  assert.equal(header.total, expected.total);
  assert.equal(header.width, expected.width);
  assert.equal(header.height, expected.height);
  assert.equal(header.drawOffset, expected.drawOffset);
  assert.equal(commands.length, expected.commands.length);
  expected.commands.forEach((want, i) => {
    const got = commands[i];
    assert.equal(got.op, want.op, `command ${i} op`);
    if (want.op === 1) assert.equal(got.rgba >>> 0, want.rgba, `command ${i} colour`);
    if (want.op === 2 || want.op === 4 || want.op === 5) assert.equal(got.texture, want.texture, `command ${i} texture`);
    if (want.op === 2 || want.op === 3) assert.equal(got.count, want.count, `command ${i} count`);
    if (want.op === 4) {
      assert.equal(got.width, want.width);
      assert.equal(got.height, want.height);
    }
    if (want.op !== 1 && want.op !== 5) assert.equal(got.offset, want.offset, `command ${i} offset`);
  });
  // The first vertex of the first textured batch, read as WebGL reads it.
  const textured = commands.find((c) => c.op === 2 && c.texture !== 0);
  const v = new DataView(buffer, textured.offset, 20);
  const want = expected.firstTexturedVertex;
  assert.equal(v.getFloat32(0, true), want.x);
  assert.equal(v.getFloat32(4, true), want.y);
  assert.deepEqual([v.getUint8(8), v.getUint8(9), v.getUint8(10), v.getUint8(11)], [want.r, want.g, want.b, want.a]);
  assert.equal(v.getFloat32(12, true), want.u);
  assert.equal(v.getFloat32(16, true), want.v);
});

test('a frame in a shared-buffer slot decodes at its offset', { skip: !fixtures && 'SCRAPS_WEBVIEW2_FIXTURES not set' }, () => {
  const frame = new Uint8Array(frameBuffer());
  const capacity = Math.ceil(frame.length / 4) * 4 + 64;
  const shared = new ArrayBuffer(64 + 3 * capacity);
  const header = new DataView(shared);
  header.setUint32(0, renderer.FORMAT.BUFFER_MAGIC, true);
  header.setUint32(4, 1, true);
  header.setUint32(8, 3, true);
  header.setUint32(12, capacity, true);
  header.setUint32(16, 7, true);
  assert.deepEqual(renderer.readBufferHeader(shared), { capacity, generation: 7 });
  new Uint8Array(shared, 64 + 2 * capacity).set(frame);
  const { header: decoded } = decode(shared, 64 + 2 * capacity, capacity);
  assert.ok(decoded);
  // A bad header is no buffer.
  header.setUint32(8, 4, true);
  assert.equal(renderer.readBufferHeader(shared), null);
  assert.equal(renderer.readBufferHeader(new ArrayBuffer(10)), null);
});

test('damaged frames are refused, never thrown on', { skip: !fixtures && 'SCRAPS_WEBVIEW2_FIXTURES not set' }, () => {
  const good = new Uint8Array(frameBuffer());
  const refused = (bytes, base = 0, capacity = bytes.byteLength) =>
    renderer.walkFrame(bytes.buffer, base, capacity, null, true) === null;
  const withU32 = (at, value) => {
    const copy = new Uint8Array(good);
    new DataView(copy.buffer).setUint32(at, value, true);
    return copy;
  };
  assert.ok(!refused(good));
  assert.ok(refused(withU32(0, 0x12345678)));
  assert.ok(refused(withU32(12, good.length + 4)));
  assert.ok(refused(withU32(24, 4)));
  assert.ok(refused(withU32(32, 77)));
  assert.ok(refused(good.slice(0, good.length - 4)));
  assert.ok(refused(good, 2, 64));          // unaligned
  assert.ok(refused(good, 0, good.length + 100));  // past the buffer
  assert.ok(refused(good, -4, 64));
  // Random damage (seeded): every call returns, nothing throws.
  let seed = 12345;
  const random = () => {
    seed = (seed * 1103515245 + 12345) >>> 0;
    return seed;
  };
  for (let round = 0; round < 5000; ++round) {
    const bytes = new Uint8Array(good);
    for (let f = 0; f <= random() % 4; ++f) bytes[random() % bytes.length] = random() & 255;
    const length = random() % 8 === 0 ? random() % (bytes.length + 1) : bytes.length;
    const cut = bytes.slice(0, length - (length % 4));
    renderer.walkFrame(cut.buffer, 0, cut.byteLength, { clear() {}, triangles() {}, lines() {}, texture() {}, destroy() {} });
  }
});

test('frame messages are parsed strictly', () => {
  assert.deepEqual(renderer.parseFrameMessage('\u0001f 12 0 3 4096'), { seq: 12, slot: 0, generation: 3, size: 4096 });
  for (const bad of ['f 12 0 3 4096', '\u0001f 12 3 3 4096', '\u0001f 12 0 3', '\u0001f 1 0 3 4 5', '\u0001f -1 0 3 4',
                     '\u0001f 1.5 0 3 4', '\u0001x 1 0 3 4', '\u0001f  1 0 3 4', null, 7, '\u0001f ' + '9'.repeat(70)]) {
    assert.equal(renderer.parseFrameMessage(bad), null, JSON.stringify(bad));
  }
});

function inputWith(screen) {
  const posted = [];
  const input = renderer.createInput((text) => posted.push(text));
  input.setScreen(screen);
  posted.length = 0;
  return { input, posted };
}

const key = (code, extra = {}) => Object.assign({ code, key: code.replace(/^Key/, '').toLowerCase(), repeat: false }, extra);

test('Esc is never forwarded, on any screen', () => {
  for (const screen of ['main', 'playing', 'paused', 'settings', null]) {
    const { input, posted } = inputWith(screen);
    assert.equal(input.keydown(key('Escape', { key: 'Escape' })), false);
    assert.equal(input.keydown({ code: 'Unidentified', key: 'Escape' }), false);
    assert.equal(input.keyup(key('Escape', { key: 'Escape' })), false);
    assert.deepEqual(posted, [], `screen ${screen}`);
  }
});

test('game keys go to native only while playing; P also in the pause menu', () => {
  let { input, posted } = inputWith('playing');
  assert.equal(input.keydown(key('KeyW')), true);
  assert.equal(input.keydown(key('KeyW', { repeat: true })), true);
  assert.equal(input.keyup(key('KeyW')), true);
  assert.equal(input.keydown(key('KeyR', { ctrlKey: true })), false);  // Ctrl+R is the browser's
  assert.equal(input.keydown(key('F4', { altKey: true, key: 'F4' })), false);
  assert.equal(input.keydown(key('KeyJ')), false);  // not an engine key: text only
  assert.deepEqual(posted, ['kd KeyW 0', 'tx w', 'kd KeyW 1', 'tx w', 'ku KeyW', 'tx j']);

  for (const screen of ['main', 'settings', 'paused']) {
    ({ input, posted } = inputWith(screen));
    assert.equal(input.keydown(key('KeyW')), false);
    assert.equal(input.keydown(key('Space', { key: ' ' })), false);
    assert.equal(input.keyup(key('KeyW')), false);
    assert.equal(input.pointerdown(0, 5, 5, true), false);
    assert.equal(input.wheel(1, true), false);
    input.pointermove(5, 5, true);
    input.flush();
    const p = input.keydown(key('KeyP'));
    assert.equal(p, screen === 'paused', `P on ${screen}`);
    assert.deepEqual(posted, screen === 'paused' ? ['kd KeyP 0'] : [], `screen ${screen}`);
  }

  // A key held while the menu opens still gets its "up".
  ({ input, posted } = inputWith('playing'));
  input.keydown(key('KeyD'));
  input.setScreen('paused');
  input.keyup(key('KeyD'));
  assert.deepEqual(posted, ['kd KeyD 0', 'tx d', 'ku KeyD']);
});

test('the mouse goes to native over the canvas while playing; moves once per frame', () => {
  const { input, posted } = inputWith('playing');
  input.pointermove(10.123, 20, true);
  input.pointermove(11, 21, true);
  assert.deepEqual(posted, []);
  input.flush();
  input.flush();
  assert.deepEqual(posted, ['pm 11 21']);
  posted.length = 0;
  assert.equal(input.pointerdown(0, 12, 22, false), false);  // on a HUD button
  assert.equal(input.pointerdown(2, 12.5, 22, true), true);
  assert.equal(input.pointerup(2, 13, 23), true);
  assert.equal(input.pointerup(2, 13, 23), false);  // not down any more
  assert.equal(input.pointerdown(5, 1, 1, true), false);
  assert.equal(input.pointerdown(0, NaN, 1, true), false);
  assert.equal(input.wheel(1.25, true), true);
  assert.equal(input.wheel(1, false), false);
  assert.equal(input.wheel(500, true), true);  // clamped
  input.pointermove(1, 1, false);  // over the HUD: the pointer left the world
  input.blur();
  assert.deepEqual(posted, ['pd 2 12.5 22', 'pu 2 13 23', 'wh 1.25', 'wh 16', 'pe 0', 'bl']);
  assert.equal(renderer.wheelNotches({ deltaY: -100, deltaMode: 0 }), 1);
  assert.equal(renderer.wheelNotches({ deltaY: 3, deltaMode: 1 }), -1);
});

test('the JS key list matches the native one (page_input.cpp)', () => {
  const source = fs.readFileSync(path.join(root, 'engine', 'backends', 'webview2', 'page_input.cpp'), 'utf8');
  const native = [...source.matchAll(/\{"([A-Za-z0-9]+)", Key::k[A-Za-z0-9]+\}/g)].map((m) => m[1]);
  assert.ok(native.length > 20);
  assert.deepEqual([...renderer.GAME_CODES].sort(), [...native].sort());
  assert.ok(!renderer.GAME_CODES.includes('Escape'));
});
