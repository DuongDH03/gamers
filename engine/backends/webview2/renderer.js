// Scraps webview2 backend: the page side. It belongs to the backend
// (engine/backends/webview2), not to the React app: the backend compiles it
// in and injects it with AddScriptToExecuteOnDocumentCreated, so it runs in
// every document before the page's own scripts.
//
// It does three things:
//   1. Draws the world. Each frame's Renderer2D calls arrive as bytes in a
//      WebView2 shared buffer (layout: frame_format.h); a tiny text message
//      "\x01f <seq> <slot> <generation> <size>" says a frame is ready. It is
//      drawn with WebGL2 into <canvas id="world"> at the next
//      requestAnimationFrame, exactly as the sdl3 renderer draws it:
//      painter's order, the same colours and alpha blending, nearest-
//      neighbour texture sampling, no antialiasing.
//   2. Paces the game: after each animation frame it asks native for the
//      next frame ("rf <generation> <last drawn> <width> <height>"), which
//      also tells native the canvas size in device pixels.
//   3. Forwards input: keys, mouse, wheel and text, as short text messages,
//      only while the game has the input (the view's screen is "playing";
//      P also in "paused", where it closes the pause menu natively). Never
//      Esc: the React app owns it (pause, resume, back).
//
// Messages from native that start with \x01 are the backend's own: this
// script listens before React does and stops them, so the UI bridge only
// ever sees view messages.
//
// The decoder and the input rules are exported for Node tests
// (tests/webview2_renderer.test.js); nothing starts outside WebView2.

(function (root) {
  'use strict';

  // ------------------------------------------------------------------ format

  var FORMAT = {
    BUFFER_MAGIC: 0x42524353,
    FRAME_MAGIC: 0x46524353,
    VERSION: 1,
    SLOTS: 3,
    BUFFER_HEADER: 64,
    FRAME_HEADER: 32,
    VERTEX: 20,
    MAX_TEXTURE_SIDE: 16384,
    OP_CLEAR: 1,
    OP_TRIANGLES: 2,
    OP_LINES: 3,
    OP_TEXTURE: 4,
    OP_DESTROY: 5,
  };

  // Walks the frame at `base` in `buffer` (an ArrayBuffer), at most
  // `capacity` bytes, calling `visitor` for each command (any method may be
  // missing). Returns the frame header, or null if anything is malformed:
  // the bytes are treated as untrusted, and every count is bounded by what
  // is left. With `validateOnly`, nothing is called.
  function walkFrame(buffer, base, capacity, visitor, validateOnly) {
    if (!buffer || base < 0 || capacity < FORMAT.FRAME_HEADER || base % 4 !== 0) return null;
    if (base + capacity > buffer.byteLength) return null;
    var dv = new DataView(buffer, base, capacity);
    if (dv.getUint32(0, true) !== FORMAT.FRAME_MAGIC || dv.getUint32(4, true) !== FORMAT.VERSION) return null;
    var header = {
      seq: dv.getUint32(8, true),
      total: dv.getUint32(12, true),
      width: dv.getUint32(16, true),
      height: dv.getUint32(20, true),
      drawOffset: dv.getUint32(24, true),
    };
    var total = header.total;
    var drawOffset = header.drawOffset;
    if (total > capacity || total < FORMAT.FRAME_HEADER || total % 4 !== 0) return null;
    if (drawOffset < FORMAT.FRAME_HEADER || drawOffset > total || drawOffset % 4 !== 0) return null;
    var call = visitor && !validateOnly;
    var at = FORMAT.FRAME_HEADER;
    while (at < total) {
      var left = total - at;
      if (left < 4) return null;
      var op = dv.getUint32(at, true);
      var resources = at < drawOffset;
      var length;
      if (op === FORMAT.OP_CLEAR) {
        if (resources || left < 8) return null;
        if (call && visitor.clear) visitor.clear(dv.getUint32(at + 4, true));
        length = 8;
      } else if (op === FORMAT.OP_TRIANGLES) {
        if (resources || left < 12) return null;
        var texture = dv.getUint32(at + 4, true);
        var count = dv.getUint32(at + 8, true);
        if (count % 3 !== 0 || count > Math.floor((left - 12) / FORMAT.VERTEX)) return null;
        if (call && visitor.triangles) visitor.triangles(texture, base + at + 12, count);
        length = 12 + count * FORMAT.VERTEX;
      } else if (op === FORMAT.OP_LINES) {
        if (resources || left < 8) return null;
        var lines = dv.getUint32(at + 4, true);
        if (lines % 2 !== 0 || lines > Math.floor((left - 8) / FORMAT.VERTEX)) return null;
        if (call && visitor.lines) visitor.lines(base + at + 8, lines);
        length = 8 + lines * FORMAT.VERTEX;
      } else if (op === FORMAT.OP_TEXTURE) {
        if (!resources || left < 16) return null;
        var id = dv.getUint32(at + 4, true);
        var width = dv.getUint32(at + 8, true);
        var height = dv.getUint32(at + 12, true);
        if (id === 0 || width === 0 || height === 0 || width > FORMAT.MAX_TEXTURE_SIDE ||
            height > FORMAT.MAX_TEXTURE_SIDE) {
          return null;
        }
        if (width * height > Math.floor((left - 16) / 4)) return null;
        if (call && visitor.texture) visitor.texture(id, width, height, base + at + 16);
        length = 16 + width * height * 4;
      } else if (op === FORMAT.OP_DESTROY) {
        if (!resources || left < 8) return null;
        if (call && visitor.destroy) visitor.destroy(dv.getUint32(at + 4, true));
        length = 8;
      } else {
        return null;
      }
      if (resources && at + length > drawOffset) return null;
      at += length;
    }
    return header;
  }

  // Reads the 64-byte header of a posted shared buffer: its slot size and
  // generation, or null if it is not one.
  function readBufferHeader(buffer) {
    if (!buffer || buffer.byteLength < FORMAT.BUFFER_HEADER) return null;
    var dv = new DataView(buffer, 0, FORMAT.BUFFER_HEADER);
    if (dv.getUint32(0, true) !== FORMAT.BUFFER_MAGIC || dv.getUint32(4, true) !== FORMAT.VERSION) return null;
    var slots = dv.getUint32(8, true);
    var capacity = dv.getUint32(12, true);
    if (slots !== FORMAT.SLOTS || capacity < FORMAT.FRAME_HEADER || capacity % 4 !== 0) return null;
    if (FORMAT.BUFFER_HEADER + slots * capacity > buffer.byteLength) return null;
    return { capacity: capacity, generation: dv.getUint32(16, true) };
  }

  // "\x01f <seq> <slot> <generation> <size>" -> {seq, slot, generation, size}, or null.
  function parseFrameMessage(text) {
    if (typeof text !== 'string' || text.length > 64 || text.charCodeAt(0) !== 1 || text.charAt(1) !== 'f') return null;
    var parts = text.slice(3).split(' ');
    if (text.charAt(2) !== ' ' || parts.length !== 4) return null;
    var values = [];
    for (var i = 0; i < 4; ++i) {
      if (!/^[0-9]{1,10}$/.test(parts[i])) return null;
      values.push(Number(parts[i]));
    }
    if (values[1] >= FORMAT.SLOTS) return null;
    return { seq: values[0], slot: values[1], generation: values[2], size: values[3] };
  }

  // ------------------------------------------------------------------ input

  // The DOM KeyboardEvent.code of every key the engine knows (it must match
  // kKeyCodes in page_input.cpp; a Node test compares them). No "Escape".
  var GAME_CODES = [
    'KeyW', 'KeyA', 'KeyS', 'KeyD', 'KeyE', 'KeyQ', 'KeyR', 'KeyV', 'KeyX', 'KeyZ',
    'ArrowUp', 'ArrowDown', 'ArrowLeft', 'ArrowRight', 'Space', 'Enter', 'NumpadEnter', 'Tab',
    'Minus', 'Equal', 'Digit1', 'Digit2', 'Digit3', 'F1', 'F2', 'F3', 'KeyP', 'F5', 'F9',
    'Backspace', 'Delete', 'Home', 'End',
  ];

  function round2(value) {
    return Math.round(value * 100) / 100;
  }

  // The input rules, without the DOM: `post(text)` sends a message; the
  // handlers take event-like objects. Returns true when an event was
  // forwarded (the caller then stops the browser's own action for it).
  function createInput(post) {
    var codes = Object.create(null);
    GAME_CODES.forEach(function (code) { codes[code] = true; });
    var screen = null;
    var keysDown = Object.create(null);  // codes whose "down" went to native
    var buttonsDown = [false, false, false];
    var inside = false;
    var move = null;  // the latest pointer move, sent once per animation frame

    function gameHasInput() {
      return screen === 'playing';
    }

    return {
      setScreen: function (next) {
        screen = typeof next === 'string' ? next : null;
        if (!gameHasInput() && inside) {
          inside = false;
          move = null;
          post('pe 0');
        }
      },
      screen: function () { return screen; },
      keydown: function (e) {
        if (!e || typeof e.code !== 'string') return false;
        // Esc is the page's: it opens and closes the menus (React).
        if (e.code === 'Escape' || e.key === 'Escape') return false;
        // Browser and system shortcuts (Ctrl+R, Alt+F4...) are not the game's.
        if (e.ctrlKey || e.altKey || e.metaKey) return false;
        // P toggles the pause menu natively, so it also counts in "paused".
        if (!gameHasInput() && !(screen === 'paused' && e.code === 'KeyP')) return false;
        var known = codes[e.code] === true;
        if (known) {
          keysDown[e.code] = true;
          post('kd ' + e.code + ' ' + (e.repeat ? '1' : '0'));
        }
        // Typed text, for a text field the game may have (native keeps it
        // only while its text input is on).
        if (gameHasInput() && typeof e.key === 'string' && e.key.length > 0 && e.key.length <= 2 &&
            Array.from(e.key).length === 1 && e.key.charCodeAt(0) >= 32) {
          post('tx ' + e.key);
        }
        return known;
      },
      keyup: function (e) {
        if (!e || typeof e.code !== 'string' || !keysDown[e.code]) return false;
        delete keysDown[e.code];
        post('ku ' + e.code);
        return true;
      },
      // A position over the canvas (x, y in canvas pixels).
      pointermove: function (x, y, overCanvas) {
        if (!gameHasInput()) return false;
        if (!overCanvas) {
          if (inside) {
            inside = false;
            move = null;
            post('pe 0');
          }
          return false;
        }
        if (!isFinite(x) || !isFinite(y)) return false;
        inside = true;
        move = 'pm ' + round2(x) + ' ' + round2(y);
        return true;
      },
      pointerdown: function (button, x, y, overCanvas) {
        if (!gameHasInput() || !overCanvas || button < 0 || button > 2 || !isFinite(x) || !isFinite(y)) return false;
        if (move !== null) {
          post(move);
          move = null;
        }
        inside = true;
        buttonsDown[button] = true;
        post('pd ' + button + ' ' + round2(x) + ' ' + round2(y));
        return true;
      },
      pointerup: function (button, x, y) {
        if (button < 0 || button > 2 || !buttonsDown[button] || !isFinite(x) || !isFinite(y)) return false;
        buttonsDown[button] = false;
        post('pu ' + button + ' ' + round2(x) + ' ' + round2(y));
        return true;
      },
      // `notches`: positive = away from the user.
      wheel: function (notches, overCanvas) {
        if (!gameHasInput() || !overCanvas || !isFinite(notches) || notches === 0) return false;
        post('wh ' + round2(Math.max(-16, Math.min(16, notches))));
        return true;
      },
      leave: function () {
        if (inside) {
          inside = false;
          move = null;
          post('pe 0');
        }
      },
      // The page lost focus: native lets go of everything.
      blur: function () {
        keysDown = Object.create(null);
        buttonsDown = [false, false, false];
        inside = false;
        move = null;
        post('bl');
      },
      // Called once per animation frame: sends the last pointer move.
      flush: function () {
        if (move !== null) {
          post(move);
          move = null;
        }
      },
    };
  }

  // DOM WheelEvent -> notches, positive away from the user. Chromium on
  // Windows reports 100 pixels per notch at 100 % scale.
  function wheelNotches(e) {
    var delta = -e.deltaY;
    if (e.deltaMode === 1) return delta / 3;   // lines
    if (e.deltaMode === 2) return delta;       // pages
    return delta / 100;                        // pixels
  }

  // ------------------------------------------------------------------ WebGL2

  var VERTEX_SHADER = [
    '#version 300 es',
    'in vec2 a_position;',
    'in vec4 a_color;',
    'in vec2 a_uv;',
    'uniform vec2 u_size;',
    'uniform float u_offset;',
    'out vec4 v_color;',
    'out vec2 v_uv;',
    'void main() {',
    '  vec2 p = a_position + vec2(u_offset);',
    // Top row of the frame at clip y = -1: drawn upside down into `target`
    // and flipped by the blit (see draw()).
    '  gl_Position = vec4(p.x / u_size.x * 2.0 - 1.0, p.y / u_size.y * 2.0 - 1.0, 0.0, 1.0);',
    '  v_color = a_color;',
    '  v_uv = a_uv;',
    '}',
  ].join('\n');

  var FRAGMENT_SHADER = [
    '#version 300 es',
    'precision highp float;',
    'uniform sampler2D u_texture;',
    'in vec4 v_color;',
    'in vec2 v_uv;',
    'out vec4 o_color;',
    'void main() {',
    '  o_color = texture(u_texture, v_uv) * v_color;',
    '}',
  ].join('\n');

  function compile(gl, type, source) {
    var shader = gl.createShader(type);
    gl.shaderSource(shader, source);
    gl.compileShader(shader);
    if (!gl.getShaderParameter(shader, gl.COMPILE_STATUS)) {
      throw new Error('scraps renderer: shader: ' + gl.getShaderInfoLog(shader));
    }
    return shader;
  }

  function createRenderer(canvas) {
    // As SDL's renderer: no antialiasing, an opaque surface (the sky covers it).
    var gl = canvas.getContext('webgl2', {
      alpha: false, antialias: false, depth: false, stencil: false,
      premultipliedAlpha: false, preserveDrawingBuffer: false,
    });
    if (!gl) return null;
    var program = gl.createProgram();
    gl.attachShader(program, compile(gl, gl.VERTEX_SHADER, VERTEX_SHADER));
    gl.attachShader(program, compile(gl, gl.FRAGMENT_SHADER, FRAGMENT_SHADER));
    gl.bindAttribLocation(program, 0, 'a_position');
    gl.bindAttribLocation(program, 1, 'a_color');
    gl.bindAttribLocation(program, 2, 'a_uv');
    gl.linkProgram(program);
    if (!gl.getProgramParameter(program, gl.LINK_STATUS)) {
      throw new Error('scraps renderer: program: ' + gl.getProgramInfoLog(program));
    }
    var uSize = gl.getUniformLocation(program, 'u_size');
    var uOffset = gl.getUniformLocation(program, 'u_offset');
    var uTexture = gl.getUniformLocation(program, 'u_texture');
    var vao = gl.createVertexArray();
    var vbo = gl.createBuffer();
    gl.bindVertexArray(vao);
    gl.bindBuffer(gl.ARRAY_BUFFER, vbo);
    gl.enableVertexAttribArray(0);
    gl.enableVertexAttribArray(1);
    gl.enableVertexAttribArray(2);

    function makeTexture(width, height, pixels) {
      var texture = gl.createTexture();
      gl.bindTexture(gl.TEXTURE_2D, texture);
      gl.pixelStorei(gl.UNPACK_ALIGNMENT, 4);
      gl.pixelStorei(gl.UNPACK_PREMULTIPLY_ALPHA_WEBGL, false);
      gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL, false);
      gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, width, height, 0, gl.RGBA, gl.UNSIGNED_BYTE, pixels);
      // SDL_SCALEMODE_NEAREST: crisp pixel art, as the sdl3 backend.
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
      return texture;
    }

    // Flat colour = a white texel tinted by the vertex colour: one program.
    var white = makeTexture(1, 1, new Uint8Array([255, 255, 255, 255]));
    var textures = new Map();
    var drawBase = 0;
    var lastClear = 0;

    // Where frames are drawn, then copied to the canvas flipped. WebGL's
    // canvas is stored bottom row first, so Direct3D (under ANGLE) would
    // apply its top-left fill rule to the bottom edges of shapes, and an
    // edge that falls exactly on a pixel centre would cover the row below
    // the one SDL's renderer covers. Drawn top row first into a texture of
    // our own, rows and texel sampling come out exactly as in the sdl3 backend.
    var target = { framebuffer: gl.createFramebuffer(), texture: null, width: 0, height: 0 };
    function fitTarget(width, height) {
      if (target.texture && target.width === width && target.height === height) return;
      if (target.texture) gl.deleteTexture(target.texture);
      target.texture = makeTexture(width, height, null);
      target.width = width;
      target.height = height;
      gl.bindFramebuffer(gl.FRAMEBUFFER, target.framebuffer);
      gl.framebufferTexture2D(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, gl.TEXTURE_2D, target.texture, 0);
    }

    function pointAttributes(offset) {
      var at = offset - drawBase;
      gl.vertexAttribPointer(0, 2, gl.FLOAT, false, FORMAT.VERTEX, at);
      gl.vertexAttribPointer(1, 4, gl.UNSIGNED_BYTE, true, FORMAT.VERTEX, at + 8);
      gl.vertexAttribPointer(2, 2, gl.FLOAT, false, FORMAT.VERTEX, at + 12);
    }

    var resources = {
      buffer: null,
      texture: function (id, width, height, offset) {
        var old = textures.get(id);
        if (old) gl.deleteTexture(old);
        textures.set(id, makeTexture(width, height, new Uint8Array(resources.buffer, offset, width * height * 4)));
      },
      destroy: function (id) {
        var old = textures.get(id);
        if (old) gl.deleteTexture(old);
        textures.delete(id);
      },
    };

    var draws = {
      clear: function (rgba) {
        lastClear = rgba >>> 0;
        gl.clearColor((rgba & 255) / 255, ((rgba >>> 8) & 255) / 255, ((rgba >>> 16) & 255) / 255,
                      ((rgba >>> 24) & 255) / 255);
        gl.clear(gl.COLOR_BUFFER_BIT);
      },
      triangles: function (texture, offset, count) {
        gl.bindTexture(gl.TEXTURE_2D, (texture !== 0 && textures.get(texture)) || white);
        gl.uniform1f(uOffset, 0);
        pointAttributes(offset);
        gl.drawArrays(gl.TRIANGLES, 0, count);
      },
      lines: function (offset, count) {
        gl.bindTexture(gl.TEXTURE_2D, white);
        gl.uniform1f(uOffset, 0.5);  // through pixel centres, as SDL draws lines
        pointAttributes(offset);
        gl.drawArrays(gl.LINES, 0, count);
      },
    };

    return {
      gl: gl,
      // Uploads a frame's textures (before it is drawn, even if it never is).
      applyResources: function (buffer, base, capacity) {
        resources.buffer = buffer;
        var header = walkFrame(buffer, base, capacity, resources, false);
        resources.buffer = null;
        return header;
      },
      // Draws a frame (already checked by applyResources).
      draw: function (buffer, base, capacity) {
        var header = walkFrame(buffer, base, capacity, null, true);
        if (!header) return null;
        var width = canvas.width;
        var height = canvas.height;
        fitTarget(width, height);
        gl.bindFramebuffer(gl.FRAMEBUFFER, target.framebuffer);
        gl.viewport(0, 0, width, height);
        gl.useProgram(program);
        gl.bindVertexArray(vao);
        gl.bindBuffer(gl.ARRAY_BUFFER, vbo);
        gl.bufferData(gl.ARRAY_BUFFER, new Uint8Array(buffer, base + header.drawOffset,
                                                      header.total - header.drawOffset), gl.STREAM_DRAW);
        drawBase = base + header.drawOffset;
        gl.uniform2f(uSize, Math.max(1, header.width), Math.max(1, header.height));
        gl.uniform1i(uTexture, 0);
        gl.activeTexture(gl.TEXTURE0);
        // SDL_BLENDMODE_BLEND: rgb = src * a + dst * (1 - a); a = src + dst * (1 - a).
        gl.enable(gl.BLEND);
        gl.blendFuncSeparate(gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA, gl.ONE, gl.ONE_MINUS_SRC_ALPHA);
        walkFrame(buffer, base, capacity, draws, false);
        // To the canvas, top row up.
        gl.bindFramebuffer(gl.READ_FRAMEBUFFER, target.framebuffer);
        gl.bindFramebuffer(gl.DRAW_FRAMEBUFFER, null);
        gl.blitFramebuffer(0, 0, width, height, 0, height, width, 0, gl.COLOR_BUFFER_BIT, gl.NEAREST);
        gl.bindFramebuffer(gl.FRAMEBUFFER, null);
        return header;
      },
      lastClear: function () { return lastClear; },
    };
  }

  // ------------------------------------------------------------------ the page

  function start(win) {
    var webview = win.chrome.webview;
    var doc = win.document;
    var post = function (text) { webview.postMessage(text); };
    var input = createInput(post);
    var canvas = null;
    var renderer = null;
    var shared = null;    // {buffer, capacity, generation}
    var pending = null;   // the newest frame announced: {seq, base}
    var lastDrawn = 0;
    var views = 0;
    var clicked = 0;
    var playWait = -1;    // animation frames left to find the Play button (-1: not asked)
    var probeWait = -1;   // frames until the probe answers (-1: not asked)
    var leaked = 0;
    var wantShot = false;

    function onInternal(text) {
      var tag = text.charAt(1);
      if (tag === 'f') {
        var frame = parseFrameMessage(text);
        if (!frame || !shared || !renderer || frame.generation !== shared.generation) return;
        var base = FORMAT.BUFFER_HEADER + frame.slot * shared.capacity;
        // Textures now, so a frame that is never drawn still delivers them.
        if (!renderer.applyResources(shared.buffer, base, shared.capacity)) return;
        if (!pending || frame.seq > pending.seq) pending = { seq: frame.seq, base: base };
      } else if (tag === 'p') {
        playWait = 300;
      } else if (tag === 'q') {
        // A listener added now comes after React's: it must never see these.
        webview.addEventListener('message', function (event) {
          if (typeof event.data === 'string' && event.data.charCodeAt(0) === 1) leaked += 1;
        });
        probeWait = 3;
      } else if (tag === 's') {
        wantShot = true;
      }
    }

    // Registered before the page's scripts run, so before React's bridge.
    webview.addEventListener('message', function (event) {
      var data = event.data;
      if (typeof data === 'string' && data.charCodeAt(0) === 1) {
        event.stopImmediatePropagation();
        onInternal(data);
        return;
      }
      if (data && typeof data === 'object' && data.type === 'view' && typeof data.screen === 'string') {
        views += 1;
        input.setScreen(data.screen);
      }
    });

    webview.addEventListener('sharedbufferreceived', function (event) {
      var buffer = event.getBuffer();
      var header = readBufferHeader(buffer);
      if (shared && shared.buffer !== buffer) webview.releaseBuffer(shared.buffer);
      if (!header) {
        shared = null;
        return;
      }
      shared = { buffer: buffer, capacity: header.capacity, generation: header.generation };
      pending = null;
      lastDrawn = 0;
    });

    function canvasPoint(e) {
      var rect = canvas.getBoundingClientRect();
      var sx = rect.width > 0 ? canvas.width / rect.width : 1;
      var sy = rect.height > 0 ? canvas.height / rect.height : 1;
      return { x: (e.clientX - rect.left) * sx, y: (e.clientY - rect.top) * sy };
    }

    function listenForInput() {
      win.addEventListener('keydown', function (e) {
        if (input.keydown(e)) e.preventDefault();
      }, true);
      win.addEventListener('keyup', function (e) {
        if (input.keyup(e)) e.preventDefault();
      }, true);
      win.addEventListener('blur', function () { input.blur(); });
      win.addEventListener('pointermove', function (e) {
        var p = canvasPoint(e);
        input.pointermove(p.x, p.y, e.target === canvas);
      });
      win.addEventListener('pointerdown', function (e) {
        var p = canvasPoint(e);
        if (input.pointerdown(e.button, p.x, p.y, e.target === canvas)) {
          try { canvas.setPointerCapture(e.pointerId); } catch (ignored) { /* already released */ }
          e.preventDefault();
        }
      });
      win.addEventListener('pointerup', function (e) {
        var p = canvasPoint(e);
        input.pointerup(e.button, p.x, p.y);
      });
      win.addEventListener('pointercancel', function (e) {
        var p = canvasPoint(e);
        input.pointerup(e.button, p.x, p.y);
      });
      doc.addEventListener('pointerleave', function () { input.leave(); });
      win.addEventListener('wheel', function (e) {
        if (input.wheel(wheelNotches(e), e.target === canvas)) e.preventDefault();
      }, { passive: false });
    }

    // The canvas's drawing buffer: its CSS size times devicePixelRatio,
    // exactly when the browser can say (device-pixel-content-box).
    var deviceSize = null;
    function watchSize() {
      if (typeof win.ResizeObserver !== 'function') return;
      try {
        new win.ResizeObserver(function (entries) {
          var entry = entries[entries.length - 1];
          var box = entry.devicePixelContentBoxSize && entry.devicePixelContentBoxSize[0];
          if (box) deviceSize = { width: box.inlineSize, height: box.blockSize };
        }).observe(canvas, { box: 'device-pixel-content-box' });
      } catch (ignored) {
        deviceSize = null;
      }
    }
    function fitCanvas() {
      var ratio = win.devicePixelRatio || 1;
      var width = deviceSize ? deviceSize.width : Math.round(canvas.clientWidth * ratio);
      var height = deviceSize ? deviceSize.height : Math.round(canvas.clientHeight * ratio);
      width = Math.max(1, Math.min(16384, width | 0));
      height = Math.max(1, Math.min(16384, height | 0));
      if (canvas.width !== width) canvas.width = width;
      if (canvas.height !== height) canvas.height = height;
    }

    function findPlayButton() {
      var buttons = doc.querySelectorAll('button');
      for (var i = 0; i < buttons.length; ++i) {
        if (/^\s*play\s*$/i.test(buttons[i].textContent || '') && !buttons[i].disabled) return buttons[i];
      }
      return null;
    }

    function probe() {
      var gl = renderer.gl;
      var width = canvas.width;
      var height = canvas.height;
      var pixels = new Uint8Array(width * height * 4);
      gl.readPixels(0, 0, width, height, gl.RGBA, gl.UNSIGNED_BYTE, pixels);
      var clear = renderer.lastClear();
      var colors = new Set();
      var sampled = 0;
      var notClear = 0;
      for (var i = 0; i < pixels.length; i += 4 * 7) {
        var rgb = pixels[i] | (pixels[i + 1] << 8) | (pixels[i + 2] << 16);
        colors.add(rgb);
        sampled += 1;
        if (rgb !== (clear & 0xffffff)) notClear += 1;
      }
      var perMille = sampled > 0 ? Math.round((notClear * 1000) / sampled) : 0;
      post('pr ' + views + ' ' + clicked + ' ' + width + ' ' + height + ' ' + colors.size + ' ' + perMille + ' ' +
           leaked);
    }

    function animationFrame() {
      fitCanvas();
      if (pending && shared && pending.seq > lastDrawn) {
        renderer.draw(shared.buffer, pending.base, shared.capacity);
        lastDrawn = pending.seq;
        // Right after drawing, while the drawing buffer still holds the frame.
        if (probeWait > 0) probeWait -= 1;
        if (probeWait === 0) {
          probeWait = -1;
          probe();
        }
        if (wantShot) {
          wantShot = false;
          post('sc ' + canvas.toDataURL('image/png'));
        }
      }
      if (playWait >= 0) {
        var button = findPlayButton();
        if (button) {
          button.click();
          clicked = 1;
          playWait = -1;
        } else if (--playWait < 0) {
          webview.postMessage({ type: 'action', name: 'play' });
          clicked = 2;
        }
      }
      input.flush();
      post('rf ' + (shared ? shared.generation : 0) + ' ' + lastDrawn + ' ' + canvas.width + ' ' + canvas.height);
      win.requestAnimationFrame(animationFrame);
    }

    function attach(found) {
      if (canvas || !found || typeof found.getContext !== 'function') return;
      canvas = found;
      renderer = createRenderer(canvas);
      if (!renderer) {
        win.console.error('scraps: WebGL2 is not available; the world cannot be drawn');
        return;
      }
      watchSize();
      fitCanvas();
      listenForInput();
      post('hi');
      win.requestAnimationFrame(animationFrame);
    }

    // The React page sets window.scrapsCanvas and fires this before React
    // mounts; a page without that event still has #world once parsed.
    win.addEventListener('scraps:canvas-ready', function (e) {
      attach((e && e.detail) || win.scrapsCanvas);
    });
    doc.addEventListener('DOMContentLoaded', function () {
      attach(win.scrapsCanvas || doc.getElementById('world'));
    });
  }

  var api = {
    FORMAT: FORMAT,
    GAME_CODES: GAME_CODES,
    walkFrame: walkFrame,
    readBufferHeader: readBufferHeader,
    parseFrameMessage: parseFrameMessage,
    createInput: createInput,
    wheelNotches: wheelNotches,
  };
  if (typeof module === 'object' && module && module.exports) module.exports = api;
  if (root && root.chrome && root.chrome.webview && root.document && !root.__scrapsBackend) {
    root.__scrapsBackend = api;
    start(root);
  }
})(typeof window !== 'undefined' ? window : null);
