// the scenarios of the fake server (imported by fakeserver.mjs): each sets players up and returns a function that moves them every tick (t counts ticks)
export function scenarios(h) {
  const { addPlayer, spawn, equipment, entityData, frame, W, typeId, angle, players, chunkPacket, packPos, blockState } = h;
  const table = {};

  // the viewer stands at the back, looking at where things happen
  table.walk = (sock) => {
    const a = addPlayer({ id: 1, name: 'Walker', x: 4.5, y: 64, z: 6.5, yaw: 0, pitch: 0, head: 0 });
    const b = addPlayer({ id: 2, name: 'Looker', x: -2.5, y: 64, z: 5, yaw: 0, pitch: 0, head: 0 });
    const c = addPlayer({ id: 3, name: 'Sneaker', x: 0.5, y: 64, z: 8, yaw: 90, pitch: 0, head: 90 });
    for (const p of players) spawn(sock, p);
    equipment(sock, a, [[0, 'diamond_sword'], [5, 'iron_helmet'], [4, 'iron_chestplate'], [3, 'iron_leggings'], [2, 'iron_boots']]);
    equipment(sock, b, [[0, 'golden_apple'], [1, 'shield']]);
    entityData(sock, c, [[0, 0, 0x02], [6, 20, 5]]);
    return {
      view: { x: 0.5, y: 64, z: -2, yaw: 0, pitch: 5 },
      tick: (t) => {
        const ang = t / 30;
        a.x = 0.5 + 4 * Math.cos(ang); a.z = 6.5 + 2.5 * Math.sin(ang);
        a.yaw = (Math.atan2(4 * Math.sin(ang), 2.5 * Math.cos(ang)) * 180) / Math.PI;
        a.head = a.yaw - 25 * Math.sin(t / 10); a.pitch = 10;
        b.head = 70 * Math.sin(t / 30); b.yaw = b.head * 0.5; b.pitch = 30 * Math.sin(t / 20);
        c.x = 0.5 + 3 * Math.sin(t / 40); c.yaw = 90; c.head = 90 + 40 * Math.sin(t / 25);
        if (t % 40 === 10) sock.write(frame(2, new W().varint(100 + a.id).u8(0).buf()));
        if (t % 40 === 30) sock.write(frame(34, new W().i32(100 + b.id).i8(2).buf()));  // a hit
      },
    };
  };

  table.elytra = (sock) => {
    const a = addPlayer({ id: 1, name: 'Glider', x: 3.5, y: 67, z: 3.5, yaw: 0, pitch: 35, head: 0 });
    const b = addPlayer({ id: 2, name: 'Wearer', x: 0.5, y: 64, z: 6, yaw: 180, pitch: 0, head: 180 });
    const c = addPlayer({ id: 3, name: 'Tucked', x: 3.5, y: 64, z: 6, yaw: 180, pitch: 0, head: 180 });
    for (const p of players) spawn(sock, p);
    equipment(sock, a, [[4, 'elytra']]);
    equipment(sock, b, [[4, 'elytra'], [0, 'firework_rocket']]);
    equipment(sock, c, [[4, 'elytra']]);
    entityData(sock, a, [[0, 0, 0x80], [6, 20, 1]]);  // gliding
    entityData(sock, c, [[0, 0, 0x02], [6, 20, 5]]);  // crouching
    return {
      view: { x: 0.5, y: 64, z: -2, yaw: 0, pitch: 8 },
      tick: (t) => { const g = t / 45; a.x = 0.5 + 3 * Math.cos(g); a.z = 3.5 + 1.5 * Math.sin(g); a.y = 67 - 1.2 * Math.sin(t / 25); a.yaw = (Math.atan2(3 * Math.sin(g), 1.5 * Math.cos(g)) * 180) / Math.PI; a.head = a.yaw; a.pitch = 35; },
    };
  };

  table.poses = (sock) => {
    const swim = addPlayer({ id: 1, name: 'Swimmer', x: -4.5, y: 64, z: 6, yaw: 180, pitch: 40, head: 180 });
    const bow = addPlayer({ id: 2, name: 'Archer', x: -1.5, y: 64, z: 6, yaw: 180, pitch: -10, head: 180 });
    const lefty = addPlayer({ id: 3, name: 'Lefty', x: 1.5, y: 64, z: 6, yaw: 180, pitch: 0, head: 180 });
    const block = addPlayer({ id: 4, name: 'Shield', x: 4.5, y: 64, z: 6, yaw: 180, pitch: 0, head: 180 });
    for (const p of players) spawn(sock, p);
    entityData(sock, swim, [[0, 0, 0x10], [6, 20, 3]]);
    equipment(sock, bow, [[0, 'bow']]);
    entityData(sock, bow, [[8, 0, 0x01]]);              // using the bow
    equipment(sock, lefty, [[0, 'diamond_sword'], [1, 'shield']]);
    entityData(sock, lefty, [[15, 42, 0]]);             // left-handed
    equipment(sock, block, [[0, 'iron_sword'], [1, 'shield']]);
    entityData(sock, block, [[8, 0, 0x03]]);            // using the off hand (the shield)
    return {
      view: { x: 0.5, y: 64, z: -1, yaw: 0, pitch: 5 },
      tick: (t) => { if (t % 30 === 5) sock.write(frame(2, new W().varint(100 + lefty.id).u8(0).buf())); },
    };
  };

  // The server turns the viewer (player_rotation, 74): an absolute one at t=20, so it looks north at the player that was behind it, and a relative one at t=60 (+90 degrees,
  // which the client has to add to its own), so it looks east at the other one. The fake server logs the angles the client says it looks along, so the handling is seen and
  // not only compiled.
  table.turn = (sock) => {
    const behind = addPlayer({ id: 1, name: 'Behind', x: 0.5, y: 64, z: -6, yaw: 180, pitch: 0, head: 180 });
    const east = addPlayer({ id: 2, name: 'East', x: 6, y: 64, z: 0.5, yaw: 270, pitch: 0, head: 270 });
    for (const p of players) spawn(sock, p);
    return {
      view: { x: 0.5, y: 64, z: 0.5, yaw: 0, pitch: 5 },
      tick: (t) => {
        // ClientboundPlayerRotationPacket: the yaw, whether it is relative, the pitch, whether it is relative
        if (t === 20) sock.write(frame(74, new W().f32(180).bool(false).f32(10).bool(false).buf()));
        if (t === 60) sock.write(frame(74, new W().f32(90).bool(true).f32(-5).bool(true).buf()));
      },
    };
  };

  // after a few seconds the server moves the viewer to another dimension: the old chunks and players must go and the new floor (stone) must show
  table.switch = (sock) => {
    const a = addPlayer({ id: 1, name: 'Stays', x: 0.5, y: 64, z: 5, yaw: 180, pitch: 0, head: 180 });
    spawn(sock, a);
    return {
      view: { x: 0.5, y: 64, z: -1, yaw: 0, pitch: 5 },
      tick: (t) => {
        if (t === 100) {
          sock.write(frame(84, new W().varint(0).str('minecraft:the_nether').i64(0).u8(0).i8(-1).bool(false).bool(false).bool(false).varint(0).varint(63).u8(0).buf()));
          sock.write(frame(12));
          for (let cx = -8; cx <= 8; cx++) for (let cz = -8; cz <= 8; cz++) sock.write(chunkPacket(cx, cz, 'stone'));
          sock.write(frame(11, new W().varint(289).buf()));
          sock.write(frame(73, new W().varint(2).f64(0.5).f64(64).f64(0.5).f64(0).f64(0).f64(0).f32(0).f32(5).i32(0).buf()));
          players.length = 0;  // (the server no longer talks of them)
        }
      },
    };
  };

  // a crossbow drawn (the string hand pulls back), one held loaded, and a loaded one in the left hand
  table.crossbow = (sock) => {
    const draw = addPlayer({ id: 1, name: 'Drawing', x: -3, y: 64, z: 6, yaw: 180, pitch: 0, head: 180 });
    const loaded = addPlayer({ id: 2, name: 'Loaded', x: 0, y: 64, z: 6, yaw: 180, pitch: 0, head: 180 });
    const lefty = addPlayer({ id: 3, name: 'LeftLoaded', x: 3, y: 64, z: 6, yaw: 180, pitch: 0, head: 180 });
    for (const p of players) spawn(sock, p);
    equipment(sock, draw, [[0, 'crossbow']]);
    entityData(sock, draw, [[8, 0, 0x01]]);              // drawing it
    equipment(sock, loaded, [[0, 'crossbow+charged']]);
    equipment(sock, lefty, [[0, 'crossbow+charged']]);
    entityData(sock, lefty, [[15, 42, 0]]);              // left-handed
    return { view: { x: 0.5, y: 64, z: -1, yaw: 0, pitch: 5 }, tick: () => {} };
  };

  // two players asleep in beds that face different ways (the body lies along the bed, the head at the pillow)
  table.sleep = (sock) => {
    const east = addPlayer({ id: 1, name: 'SleepsEast', x: 3.5, y: 64.6875, z: 6.5, yaw: 90, pitch: 0, head: 90 });
    const north = addPlayer({ id: 2, name: 'SleepsNorth', x: -2.5, y: 64.6875, z: 6.5, yaw: 180, pitch: 0, head: 180 });
    for (const p of players) spawn(sock, p);
    const bed = (x, y, z, facing, part) => sock.write(frame(8, new W().i64(packPos(x, y, z)).varint(blockState('red_bed', { facing, part, occupied: 'true' })).buf()));
    entityData(sock, east, [[6, 20, 2], [14, 11, packPos(3, 64, 6)]]);
    entityData(sock, north, [[6, 20, 2], [14, 11, packPos(-3, 64, 6)]]);
    return {
      view: { x: 0.5, y: 64, z: -1, yaw: 0, pitch: 12 },
      tick: (tick) => {
        if (tick === 40) {  // (the beds go in once the chunks are there)
          bed(2, 64, 6, 'east', 'foot');  // the foot, and the head one block further east
          bed(3, 64, 6, 'east', 'head');
          bed(-3, 64, 7, 'north', 'foot');
          bed(-3, 64, 6, 'north', 'head');
        }
      },
    };
  };

  table.ride = (sock) => {
    const rider = addPlayer({ id: 1, name: 'BoatRider', x: 3, y: 64, z: 6, yaw: 90, pitch: 0, head: 90 });
    const horseman = addPlayer({ id: 2, name: 'Horseman', x: -3, y: 64, z: 6, yaw: 0, pitch: 0, head: 0 });
    spawn(sock, rider);
    spawn(sock, horseman);
    const boat = { x: 3, y: 64.2, z: 6, yaw: 90, q: { x: Math.round(3 * 4096), y: Math.round(64.2 * 4096), z: Math.round(6 * 4096) } };
    sock.write(frame(1, new W().varint(109).uuid(99).varint(typeId('oak_boat')).f64(boat.x).f64(boat.y).f64(boat.z).u8(0).i8(0).i8(angle(boat.yaw)).i8(0).varint(0).buf()));
    sock.write(frame(1, new W().varint(110).uuid(98).varint(typeId('horse')).f64(-3).f64(64).f64(6).u8(0).i8(0).i8(angle(0)).i8(0).varint(0).buf()));
    sock.write(frame(109, new W().varint(109).varint(1).varint(101).buf()));
    sock.write(frame(109, new W().varint(110).varint(1).varint(102).buf()));
    return {
      view: { x: 0.5, y: 64, z: -1, yaw: 0, pitch: 5 },
      tick: (t) => {
        const x = 3 + 1.5 * Math.sin(t / 50), qx = Math.round(x * 4096);
        // (26.3: a varint of the ticks the move covers, bit 0 for standing on the ground, and then the move itself)
        sock.write(frame(54, new W().varint(109).varint(1).i16(qx - boat.q.x).i16(0).i16(0).buf()));
        boat.q.x = qx;
      },
    };
  };
  return table;
}
