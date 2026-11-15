// A tiny offline-mode Minecraft 26.3 (protocol 777) server for looking at how Falcon draws other players without a real server: no world, a grass floor, and scripted players
// (scenarios.mjs: walk, elytra, poses, ride, switch). It reads the game's registries from the horizon-launcher data folder and blocks.json from a vanilla server's --reports output
// (FALCON_REPORTS, default %TEMP%/falcon-gen/out/reports).
// usage: node fake_server.mjs <port> <scenario> [seconds]      (it stops by itself); then: Falcon.exe --connect 127.0.0.1:<port> --username Viewer --game-dir <scratch> ...
// tools/fake_server/capture.ps1 does both with a hidden window and takes a picture.
import net from 'node:net';
import fs from 'node:fs';

const port = Number(process.argv[2] || 25598);
const scenario = process.argv[3] || 'walk';
const lifetime = Number(process.argv[4] || 60);

const versions = process.env.APPDATA + '/horizon-launcher/versions';
const dataDir = fs.readdirSync(versions).map((d) => versions + '/' + d + '/.local/horizon-client/26.3').filter((d) => fs.existsSync(d + '/registries.json')).pop();
const reg = JSON.parse(fs.readFileSync(dataDir + '/registries.json', 'utf8'));
const componentId = (n) => reg['minecraft:data_component_type'].entries['minecraft:' + n].protocol_id;
const itemId = (n) => reg['minecraft:item'].entries['minecraft:' + n].protocol_id;
const typeId = (n) => reg['minecraft:entity_type'].entries['minecraft:' + n].protocol_id;
const blocks = JSON.parse(fs.readFileSync((process.env.FALCON_REPORTS || process.env.TEMP + '/falcon-gen/out/reports') + '/blocks.json', 'utf8'));
const stateOf = (name) => {
  const b = blocks['minecraft:' + name];
  const s = b.states.find((x) => x.default) || b.states[0];
  return s.id;
};

// ---- bytes
class W {
  constructor() { this.b = []; }
  u8(v) { this.b.push(v & 0xff); return this; }
  i8(v) { return this.u8(v); }
  i16(v) { this.b.push((v >> 8) & 0xff, v & 0xff); return this; }
  i32(v) { this.b.push((v >>> 24) & 0xff, (v >>> 16) & 0xff, (v >>> 8) & 0xff, v & 0xff); return this; }
  i64(v) { const x = BigInt.asUintN(64, BigInt(v)); for (let i = 7; i >= 0; i--) this.b.push(Number((x >> BigInt(i * 8)) & 0xffn)); return this; }
  f32(v) { const b = Buffer.alloc(4); b.writeFloatBE(v); this.b.push(...b); return this; }
  f64(v) { const b = Buffer.alloc(8); b.writeDoubleBE(v); this.b.push(...b); return this; }
  varint(v) { v >>>= 0; do { let x = v & 0x7f; v >>>= 7; if (v) x |= 0x80; this.b.push(x); } while (v); return this; }
  bool(v) { return this.u8(v ? 1 : 0); }
  str(s) { const buf = Buffer.from(s, 'utf8'); this.varint(buf.length); this.b.push(...buf); return this; }
  raw(arr) { this.b.push(...arr); return this; }
  uuid(n) { for (let i = 0; i < 16; i++) this.u8(i === 15 ? n : i === 0 ? 0x10 + n : 0); return this; }
  buf() { return Buffer.from(this.b); }
}
const frame = (id, body) => { const inner = Buffer.concat([new W().varint(id).buf(), body || Buffer.alloc(0)]); return Buffer.concat([new W().varint(inner.length).buf(), inner]); };

// ---- the world: a grass floor
function chunkPacket(cx, cz, block = 'grass_block') {
  const data = new W();
  const grass = stateOf(block);
  for (let s = 0; s < 24; s++) {
    const solid = s <= 7;
    data.i16(solid ? 4096 : 0).i16(0);
    data.u8(0).varint(solid ? grass : 0);  // blocks: a single value
    data.u8(0).varint(0);                   // biomes: a single value
  }
  const body = new W().i32(cx).i32(cz).varint(0).varint(data.b.length).raw(data.b);
  return frame(46, body.buf());
}

// ---- the players
const players = [];
function addPlayer(p) {
  Object.assign(p, { qx: Math.round(p.x * 4096), qy: Math.round(p.y * 4096), qz: Math.round(p.z * 4096), lastYaw: -999, lastPitch: -999, lastHead: -999, data: {}, ticks: 0 });
  players.push(p);
  return p;
}
const angle = (deg) => Math.round(((deg % 360) + 360) % 360 / 360 * 256) & 0xff;

function spawn(sock, p) {
  sock.write(frame(71, new W().u8(0x01 | 0x04 | 0x08 | 0x10).varint(1).uuid(p.id).str(p.name).varint(0).varint(p.gameMode ?? 0).bool(true).varint(40 + p.id * 15).buf()));
  const w = new W().varint(100 + p.id).uuid(p.id).varint(typeId('player')).f64(p.x).f64(p.y).f64(p.z).u8(0).i8(angle(p.pitch)).i8(angle(p.yaw)).i8(angle(p.head)).varint(0);
  sock.write(frame(1, w.buf()));
}
function entityData(sock, p, entries) {
  const w = new W().varint(100 + p.id);
  for (const [index, type, value] of entries) {
    w.u8(index).varint(type);
    if (type === 0) w.u8(value);       // byte
    else if (type === 1) w.varint(value);  // int
    else if (type === 20 || type === 42) w.varint(value); // pose, main arm
    else if (type === 3) w.f32(value);
    else if (type === 8) w.bool(value);
    else if (type === 11) { w.bool(true); w.i64(value); }  // an optional block position (already packed)
  }
  w.u8(0xff);
  sock.write(frame(101, w.buf()));
}
function equipment(sock, p, slots) {  // [slot, itemName]
  const w = new W().varint(100 + p.id);
  slots.forEach(([slot, name], i) => {
    w.u8(slot | (i < slots.length - 1 ? 0x80 : 0));
    const [base, flag] = (name || '').split('+');
    if (flag === 'charged') w.varint(1).varint(itemId(base)).varint(1).varint(0).varint(componentId('charged_projectiles')).varint(1).varint(itemId('arrow')).varint(1).varint(0).varint(0);  // a loaded crossbow
    else if (base) w.varint(1).varint(itemId(base)).varint(0).varint(0);
    else w.varint(0);
  });
  sock.write(frame(104, w.buf()));
}
function moveTick(sock, p) {
  const qx = Math.round(p.x * 4096), qy = Math.round(p.y * 4096), qz = Math.round(p.z * 4096);
  const dx = qx - p.qx, dy = qy - p.qy, dz = qz - p.qz;
  const yawB = angle(p.yaw), pitchB = angle(p.pitch);
  const rotated = yawB !== p.lastYaw || pitchB !== p.lastPitch;
  const moved = dx !== 0 || dy !== 0 || dz !== 0;
  if (moved && Math.abs(dx) < 32000 && Math.abs(dy) < 32000 && Math.abs(dz) < 32000) {
    if (rotated) sock.write(frame(55, new W().varint(100 + p.id).i16(dx).i16(dy).i16(dz).i8(yawB).i8(pitchB).bool(p.onGround ?? true).buf()));
    else sock.write(frame(54, new W().varint(100 + p.id).i16(dx).i16(dy).i16(dz).bool(p.onGround ?? true).buf()));
    p.qx = qx; p.qy = qy; p.qz = qz;
  } else if (rotated) {
    sock.write(frame(57, new W().varint(100 + p.id).i8(yawB).i8(pitchB).bool(p.onGround ?? true).buf()));
  }
  p.lastYaw = yawB; p.lastPitch = pitchB;
  const headB = angle(p.head);
  if (headB !== p.lastHead) { sock.write(frame(85, new W().varint(100 + p.id).i8(headB).buf())); p.lastHead = headB; }
  p.ticks++;
  if (p.ticks % 60 === 0) {  // entity_position_sync: where it really is
    sock.write(frame(35, new W().varint(100 + p.id).f64(p.x).f64(p.y).f64(p.z).f64(0).f64(0).f64(0).f32(p.yaw).f32(p.pitch).bool(p.onGround ?? true).buf()));
    p.qx = qx; p.qy = qy; p.qz = qz;
  }
}

// ---- scenarios
import { scenarios } from './scenarios.mjs';
// BlockPos.asLong: x and z in 26 bits, y in 12
const packPos = (x, y, z) => ((BigInt(x) & 0x3FFFFFFn) << 38n) | ((BigInt(z) & 0x3FFFFFFn) << 12n) | (BigInt(y) & 0xFFFn);
// the state number of a block with the properties given ({facing: 'east'}); the default state for the rest
const blockState = (name, props = {}) => {
  const b = blocks['minecraft:' + name];
  const s = b.states.find((x) => Object.entries(props).every(([k, v]) => x.properties[k] === v) && (x.default || Object.keys(props).length)) || b.states.find((x) => Object.entries(props).every(([k, v]) => x.properties[k] === v));
  return s.id;
};
const table = scenarios({ addPlayer, spawn, equipment, entityData, frame, W, typeId, angle, players, chunkPacket, packPos, blockState });

// ---- the connection
const server = net.createServer((sock) => {
  let state = 'handshake';
  let buf = Buffer.alloc(0);
  let tickTimer = null;
  let t = 0;
  const readVar = (b, o) => { let v = 0, s = 0, c; do { if (o >= b.length) return null; c = b[o++]; v |= (c & 0x7f) << s; s += 7; } while (c & 0x80); return [v, o]; };
  sock.on('data', (d) => {
    buf = Buffer.concat([buf, d]);
    for (;;) {
      const l = readVar(buf, 0);
      if (!l || buf.length < l[1] + l[0]) return;
      const payload = buf.subarray(l[1], l[1] + l[0]);
      buf = buf.subarray(l[1] + l[0]);
      const idr = readVar(payload, 0);
      const id = idr[0];
      if (state === 'handshake') {
        state = 'login';
      } else if (state === 'login' && id === 0) {
        sock.write(frame(2, new W().uuid(200).str('Viewer').varint(0).buf()));  // login_finished
      } else if (state === 'login' && id === 3) {
        state = 'config';
        sock.write(frame(3));  // finish_configuration
      } else if (state === 'config' && id === 3) {
        state = 'play';
        play(sock);
      }
    }
  });
  const play = (s) => {
    s.write(frame(50, new W().i32(200).raw(new Array(40).fill(0)).buf()));  // login: our entity
    s.write(frame(96, new W().varint(0).varint(0).buf()));
    s.write(frame(12));
    for (let cx = -8; cx <= 8; cx++) for (let cz = -8; cz <= 8; cz++) s.write(chunkPacket(cx, cz));
    s.write(frame(11, new W().varint(289).buf()));
    const sc = (table[scenario] || (() => ({ view: { x: 0.5, y: 64, z: 0.5, yaw: 0, pitch: 0 }, tick: () => {} })))(s);
    const v = sc.view;
    s.write(frame(73, new W().varint(1).f64(v.x).f64(v.y).f64(v.z).f64(0).f64(0).f64(0).f32(v.yaw).f32(v.pitch).i32(0).buf()));
    const step = sc.tick;
    tickTimer = setInterval(() => {
      t++;
      step(t);
      for (const p of players) moveTick(s, p);
    }, 50);
  };
  sock.on('close', () => { if (tickTimer) clearInterval(tickTimer); });
  sock.on('error', () => {});
});
server.listen(port, '127.0.0.1', () => console.log('listening', port, scenario));
setTimeout(() => { console.log('done'); process.exit(0); }, lifetime * 1000);
