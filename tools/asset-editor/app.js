(function () {
  const CHAR_COUNT = 256;
  const CHARSET_BANKS = 2;
  const TILE_COUNT = 256;
  const CHAR_BYTES = 8;
  const STORAGE_KEY = "c64-asset-editor-state-v1";
  const STORAGE_VERSION = 1;

  const C64_COLORS = [
    "#000000", "#ffffff", "#813338", "#75cec8",
    "#8e3c97", "#56ac4d", "#2e2c9b", "#edf171",
    "#8e5029", "#553800", "#c46c71", "#4a4a4a",
    "#7b7b7b", "#a9ff9f", "#706deb", "#b2b2b2"
  ];

  const state = {
    mode: "char",
    charset: new Uint8Array(CHARSET_BANKS * CHAR_COUNT * CHAR_BYTES),
    activeCharsetBank: 0,
    tiles: Array.from({ length: TILE_COUNT }, () => ({
      chars: [0, 0, 0, 0],
      colors: [1, 1, 1, 1]
    })),
    tileProps: new Uint8Array(TILE_COUNT),
    selectedChar: 0,
    selectedTile: 0,
    charPreviewColor: 1,
    trialText: "HELLO C64",
    trialMapping: "petscii-screen",
    trialColor: 1,
    charClipboard: null,
    tileClipboard: null,
    showGrid: true,
    drawing: false,
    drawValue: 1,
    map: {
      width: 16,
      height: 16,
      id: 0,
      reserved: 0,
      data: new Uint8Array(16 * 16)
    },
    test: {
      width: 16,
      height: 12,
      data: new Uint8Array(16 * 12)
    }
  };

  const ui = {
    modeButtons: {
      char: document.getElementById("mode-char"),
      tile: document.getElementById("mode-tile"),
      map: document.getElementById("mode-map")
    },
    modePanels: {
      char: document.getElementById("char-mode"),
      tile: document.getElementById("tile-mode"),
      map: document.getElementById("map-mode")
    },
    sidePanels: {
      char: document.getElementById("side-char"),
      tile: document.getElementById("side-tile"),
      map: document.getElementById("side-map")
    },
    charsetActiveBank: document.getElementById("charset-active-bank"),
    charsetImportBank: document.getElementById("charset-import-bank"),
    selectedBank: document.getElementById("selected-bank"),
    selectedChar: document.getElementById("selected-char"),
    selectedCharTile: document.getElementById("selected-char-tile"),
    selectedTile: document.getElementById("selected-tile"),
    selectedTileMap: document.getElementById("selected-tile-map"),
    showGrid: document.getElementById("show-grid"),
    helpOpen: document.getElementById("help-open"),
    helpDialog: document.getElementById("help-dialog"),
    helpClose: document.getElementById("help-close"),
    status: document.getElementById("status"),

    charCanvas: document.getElementById("char-canvas"),
    charPicker: document.getElementById("char-picker"),
    charPreviewColor: document.getElementById("char-preview-color"),
    charRollUp: document.getElementById("char-roll-up"),
    charRollDown: document.getElementById("char-roll-down"),
    charRollLeft: document.getElementById("char-roll-left"),
    charRollRight: document.getElementById("char-roll-right"),
    charCopy: document.getElementById("char-copy"),
    charPaste: document.getElementById("char-paste"),
    charClear: document.getElementById("char-clear"),
    trialText: document.getElementById("trial-text"),
    trialMapping: document.getElementById("trial-mapping"),
    trialColor: document.getElementById("trial-color"),
    trialCanvas: document.getElementById("trial-canvas"),

    tileCanvas: document.getElementById("tile-canvas"),
    tilePicker: document.getElementById("tile-picker"),
    tileDefGrid: document.getElementById("tile-def-grid"),

    propsBoxes: document.querySelectorAll("#tile-props-boxes input[type=checkbox]"),

    testCanvas: document.getElementById("test-canvas"),
    testWidth: document.getElementById("test-width"),
    testHeight: document.getElementById("test-height"),
    resizeTest: document.getElementById("resize-test"),

    mapCanvas: document.getElementById("map-canvas"),
    mapWidth: document.getElementById("map-width"),
    mapHeight: document.getElementById("map-height"),
    mapId: document.getElementById("map-id"),
    mapReserved: document.getElementById("map-reserved"),
    resizeMap: document.getElementById("resize-map"),

    exportChars: document.getElementById("export-chars"),
    importChars: document.getElementById("import-chars"),
    exportTiles: document.getElementById("export-tiles"),
    importTiles: document.getElementById("import-tiles"),
    exportMap: document.getElementById("export-map"),
    importMap: document.getElementById("import-map"),
    charsFile: document.getElementById("chars-file"),
    tilesFile: document.getElementById("tiles-file"),
    mapFile: document.getElementById("map-file"),
    assetServerStatus: document.getElementById("asset-server-status"),
    assetFileLists: {
      charset: document.getElementById("asset-file-list-charset"),
      tiles: document.getElementById("asset-file-list-tiles"),
      map: document.getElementById("asset-file-list-map")
    },
    assetRefreshButtons: {
      charset: document.getElementById("asset-refresh-charset"),
      tiles: document.getElementById("asset-refresh-tiles"),
      map: document.getElementById("asset-refresh-map")
    },
    assetOpenButtons: {
      charset: document.getElementById("asset-open-charset"),
      tiles: document.getElementById("asset-open-tiles"),
      map: document.getElementById("asset-open-map")
    },
    assetSavePaths: {
      charset: document.getElementById("asset-save-path-charset"),
      tiles: document.getElementById("asset-save-path-tiles"),
      map: document.getElementById("asset-save-path-map")
    },
    assetSaveChars: document.getElementById("asset-save-chars"),
    assetSaveTiles: document.getElementById("asset-save-tiles"),
    assetSaveMap: document.getElementById("asset-save-map")
  };

  const ctx = {
    char: ui.charCanvas.getContext("2d"),
    charPicker: ui.charPicker.getContext("2d"),
    tile: ui.tileCanvas.getContext("2d"),
    tilePicker: ui.tilePicker.getContext("2d"),
    trial: ui.trialCanvas.getContext("2d"),
    test: ui.testCanvas.getContext("2d"),
    map: ui.mapCanvas.getContext("2d")
  };
  const tileAtlases = new Map();
  let persistTimer = null;
  let statusTimer = null;
  let assetFiles = [];

  function setStatus(msg, isError) {
    if (statusTimer !== null) {
      window.clearTimeout(statusTimer);
      statusTimer = null;
    }
    ui.status.textContent = msg;
    ui.status.style.color = isError ? "#ffd0c9" : "#f8f5ef";
    ui.status.style.borderColor = isError ? "#ff6f61" : "#3f637e";
    ui.status.classList.toggle("hidden", !msg);
    if (msg) {
      statusTimer = window.setTimeout(() => {
        ui.status.textContent = "";
        ui.status.classList.add("hidden");
        statusTimer = null;
      }, 3500);
    }
  }

  function persistNow() {
    try {
      const snapshot = {
        version: STORAGE_VERSION,
        mode: state.mode,
        activeCharsetBank: state.activeCharsetBank,
        selectedChar: state.selectedChar,
        selectedTile: state.selectedTile,
        charPreviewColor: state.charPreviewColor,
        trialText: state.trialText,
        trialMapping: state.trialMapping,
        trialColor: state.trialColor,
        showGrid: state.showGrid,
        charset: Array.from(state.charset),
        tiles: state.tiles.map((t) => ({
          chars: t.chars.slice(0, 4),
          colors: t.colors.slice(0, 4)
        })),
        tileProps: Array.from(state.tileProps),
        map: {
          width: state.map.width,
          height: state.map.height,
          id: state.map.id,
          reserved: state.map.reserved,
          data: Array.from(state.map.data)
        },
        test: {
          width: state.test.width,
          height: state.test.height,
          data: Array.from(state.test.data)
        }
      };
      localStorage.setItem(STORAGE_KEY, JSON.stringify(snapshot));
    } catch (err) {
      setStatus(`Failed to save editor state: ${String(err)}`, true);
    }
  }

  function schedulePersist() {
    if (persistTimer !== null) return;
    persistTimer = window.setTimeout(() => {
      persistTimer = null;
      persistNow();
    }, 200);
  }

  function loadPersistedState() {
    try {
      const raw = localStorage.getItem(STORAGE_KEY);
      if (!raw) return false;
      const parsed = JSON.parse(raw);
      if (!parsed || parsed.version !== STORAGE_VERSION) return false;

      if (Array.isArray(parsed.charset) && parsed.charset.length === state.charset.length) {
        state.charset.set(parsed.charset.map(clampByte));
      } else {
        return false;
      }

      if (Array.isArray(parsed.tiles) && parsed.tiles.length === TILE_COUNT) {
        for (let i = 0; i < TILE_COUNT; i += 1) {
          const src = parsed.tiles[i] || {};
          const chars = Array.isArray(src.chars) ? src.chars : [0, 0, 0, 0];
          const colors = Array.isArray(src.colors) ? src.colors : [1, 1, 1, 1];
          state.tiles[i].chars = [
            clampCharIndex(chars[0] ?? 0),
            clampCharIndex(chars[1] ?? 0),
            clampCharIndex(chars[2] ?? 0),
            clampCharIndex(chars[3] ?? 0)
          ];
          state.tiles[i].colors = [
            clampByte(colors[0] ?? 1) & 0x0f,
            clampByte(colors[1] ?? 1) & 0x0f,
            clampByte(colors[2] ?? 1) & 0x0f,
            clampByte(colors[3] ?? 1) & 0x0f
          ];
        }
      } else {
        return false;
      }

      if (Array.isArray(parsed.tileProps) && parsed.tileProps.length === TILE_COUNT) {
        state.tileProps.set(parsed.tileProps.map(clampByte));
      } else {
        return false;
      }

      if (parsed.map && Array.isArray(parsed.map.data)) {
        const w = Math.max(1, Math.min(255, parsed.map.width | 0));
        const h = Math.max(1, Math.min(255, parsed.map.height | 0));
        if (parsed.map.data.length === w * h) {
          state.map.width = w;
          state.map.height = h;
          state.map.id = clampByte(parsed.map.id);
          state.map.reserved = clampByte(parsed.map.reserved);
          state.map.data = Uint8Array.from(parsed.map.data.map(clampByte));
        }
      }

      if (parsed.test && Array.isArray(parsed.test.data)) {
        const w = Math.max(1, Math.min(64, parsed.test.width | 0));
        const h = Math.max(1, Math.min(64, parsed.test.height | 0));
        if (parsed.test.data.length === w * h) {
          state.test.width = w;
          state.test.height = h;
          state.test.data = Uint8Array.from(parsed.test.data.map(clampByte));
        }
      }

      if (parsed.mode === "char" || parsed.mode === "tile" || parsed.mode === "map") {
        state.mode = parsed.mode;
      }
      state.activeCharsetBank = clampCharsetBank(parsed.activeCharsetBank ?? 0);
      state.selectedChar = clampCharIndex(parsed.selectedChar ?? 0);
      state.selectedTile = clampTileIndex(parsed.selectedTile ?? 0);
      state.charPreviewColor = clampByte(parsed.charPreviewColor ?? 1) & 0x0f;
      state.trialText = typeof parsed.trialText === "string" ? parsed.trialText : "HELLO C64";
      state.trialMapping =
        parsed.trialMapping === "direct" || parsed.trialMapping === "petscii-screen"
          ? parsed.trialMapping
          : "petscii-screen";
      state.trialColor = clampByte(parsed.trialColor ?? 1) & 0x0f;
      state.showGrid = parsed.showGrid !== false;
      return true;
    } catch (err) {
      setStatus(`Failed to load saved state: ${String(err)}`, true);
      return false;
    }
  }

  function clampByte(n) {
    if (!Number.isFinite(n)) return 0;
    return Math.max(0, Math.min(255, n | 0));
  }

  function clampCharIndex(n) {
    return Math.max(0, Math.min(CHAR_COUNT - 1, n | 0));
  }

  function clampCharsetBank(n) {
    return Math.max(0, Math.min(CHARSET_BANKS - 1, n | 0));
  }

  function clampTileIndex(n) {
    return Math.max(0, Math.min(TILE_COUNT - 1, n | 0));
  }

  function charsetOffset(bank, charIndex, row) {
    return (clampCharsetBank(bank) * CHAR_COUNT + clampCharIndex(charIndex)) * CHAR_BYTES + row;
  }

  function setCharPixel(charIndex, x, y, value) {
    const ci = clampCharIndex(charIndex);
    if (x < 0 || x > 7 || y < 0 || y > 7) return;
    const offset = charsetOffset(state.activeCharsetBank, ci, y);
    const mask = 1 << (7 - x);
    if (value) {
      state.charset[offset] |= mask;
    } else {
      state.charset[offset] &= ~mask;
    }
  }

  function drawChar(targetCtx, bank, charIndex, dx, dy, scale, fg, bg) {
    targetCtx.fillStyle = bg;
    targetCtx.fillRect(dx, dy, 8 * scale, 8 * scale);
    targetCtx.fillStyle = fg;
    for (let y = 0; y < 8; y += 1) {
      const row = state.charset[charsetOffset(bank, charIndex, y)];
      for (let x = 0; x < 8; x += 1) {
        if ((row >> (7 - x)) & 1) {
          targetCtx.fillRect(dx + x * scale, dy + y * scale, scale, scale);
        }
      }
    }
  }

  function drawTile(targetCtx, tileIndex, dx, dy, scale) {
    const t = state.tiles[clampTileIndex(tileIndex)];
    targetCtx.fillStyle = "#000";
    targetCtx.fillRect(dx, dy, 16 * scale, 16 * scale);
    const positions = [
      [0, 0], [8, 0], [0, 8], [8, 8]
    ];
    for (let q = 0; q < 4; q += 1) {
      const [px, py] = positions[q];
      const charIndex = clampCharIndex(t.chars[q]);
      const color = C64_COLORS[t.colors[q] & 0x0f];
      drawChar(targetCtx, state.activeCharsetBank, charIndex, dx + px * scale, dy + py * scale, scale, color, "#000");
    }
  }

  function getTileAtlas(scale) {
    let atlas = tileAtlases.get(scale);
    const tileSize = 16 * scale;
    const width = 16 * tileSize;
    const height = 16 * tileSize;
    if (!atlas || atlas.canvas.width !== width || atlas.canvas.height !== height) {
      const canvas = document.createElement("canvas");
      canvas.width = width;
      canvas.height = height;
      atlas = {
        canvas,
        ctx: canvas.getContext("2d"),
        dirty: true,
        scale
      };
      tileAtlases.set(scale, atlas);
    }
    return atlas;
  }

  function invalidateTileAtlases() {
    tileAtlases.forEach((atlas) => {
      atlas.dirty = true;
    });
  }

  function rebuildTileAtlas(scale) {
    const atlas = getTileAtlas(scale);
    const tileSize = 16 * scale;
    atlas.ctx.clearRect(0, 0, atlas.canvas.width, atlas.canvas.height);
    for (let i = 0; i < TILE_COUNT; i += 1) {
      const x = (i % 16) * tileSize;
      const y = Math.floor(i / 16) * tileSize;
      drawTile(atlas.ctx, i, x, y, scale);
    }
    atlas.dirty = false;
    return atlas;
  }

  function blitTile(targetCtx, tileIndex, dx, dy, scale) {
    let atlas = getTileAtlas(scale);
    if (atlas.dirty) {
      atlas = rebuildTileAtlas(scale);
    }
    const ti = clampTileIndex(tileIndex);
    const tileSize = 16 * scale;
    const sx = (ti % 16) * tileSize;
    const sy = Math.floor(ti / 16) * tileSize;
    targetCtx.drawImage(atlas.canvas, sx, sy, tileSize, tileSize, dx, dy, tileSize, tileSize);
  }

  function shouldDrawGrid() {
    return state.showGrid;
  }

  function drawGridLine(ctxTarget, x1, y1, x2, y2, color) {
    ctxTarget.strokeStyle = color;
    ctxTarget.beginPath();
    ctxTarget.moveTo(x1, y1);
    ctxTarget.lineTo(x2, y2);
    ctxTarget.stroke();
  }

  function drawMapCellGrid(tx, ty) {
    if (!shouldDrawGrid(state.map.width, state.map.height)) return;
    const scale = 2;
    const cell = 16 * scale;
    const left = tx * cell + 0.5;
    const top = ty * cell + 0.5;
    const right = (tx + 1) * cell + 0.5;
    const bottom = (ty + 1) * cell + 0.5;
    const color = "#303942";

    drawGridLine(ctx.map, left, top, right, top, color);
    drawGridLine(ctx.map, left, bottom, right, bottom, color);
    drawGridLine(ctx.map, left, top, left, bottom, color);
    drawGridLine(ctx.map, right, top, right, bottom, color);
  }

  function rollCurrentChar(dx, dy) {
    const base = charsetOffset(state.activeCharsetBank, state.selectedChar, 0);
    const rows = new Uint8Array(CHAR_BYTES);
    for (let i = 0; i < CHAR_BYTES; i += 1) {
      rows[i] = state.charset[base + i];
    }

    if (dy !== 0) {
      const shifted = new Uint8Array(CHAR_BYTES);
      for (let y = 0; y < CHAR_BYTES; y += 1) {
        const src = (y - dy + CHAR_BYTES) % CHAR_BYTES;
        shifted[y] = rows[src];
      }
      for (let y = 0; y < CHAR_BYTES; y += 1) {
        rows[y] = shifted[y];
      }
    }

    if (dx !== 0) {
      for (let y = 0; y < CHAR_BYTES; y += 1) {
        const row = rows[y];
        rows[y] = dx < 0
          ? (((row << 1) & 0xff) | (row >> 7))
          : ((row >> 1) | ((row & 1) << 7));
      }
    }

    for (let i = 0; i < CHAR_BYTES; i += 1) {
      state.charset[base + i] = rows[i];
    }
    invalidateTileAtlases();
    renderAll();
    schedulePersist();
  }

  function asciiToPetscii(code) {
    // Mapping tuned for charset indexing convention:
    // lowercase a-z -> PETSCII 0x41-0x5A (screen codes 1-26),
    // uppercase A-Z -> PETSCII 0xC1-0xDA (screen codes 65-90).
    if (code >= 0x61 && code <= 0x7a) return code - 0x20;
    if (code >= 0x41 && code <= 0x5a) return code + 0x80;
    return code & 0xff;
  }

  function petsciiToScreen(code) {
    const c = code & 0xff;
    if (c === 0x0d || c === 0x0a) return -1;
    // In this block PETSCII matches screen code directly:
    // space (32), punctuation, digits (48-57), etc.
    if (c >= 0x20 && c <= 0x3f) return c;
    // 0x41..0x5A letters become 1..26.
    if (c >= 0x40 && c <= 0x5f) return c - 0x40;
    if (c >= 0x60 && c <= 0x7f) return c - 0x20;
    if (c >= 0xa0 && c <= 0xbf) return c - 0x80;
    // 0xC1..0xDA letters become 65..90.
    if (c >= 0xc0 && c <= 0xfe) return c - 0x80;
    return c;
  }

  function trialCharToIndex(ch) {
    const code = ch.charCodeAt(0) & 0xff;
    if (state.trialMapping === "direct") {
      if (code === 0x0d || code === 0x0a) return -1;
      return code;
    }
    const petscii = asciiToPetscii(code);
    return petsciiToScreen(petscii);
  }

  function renderTrialCanvas() {
    const scale = 2;
    const cellW = 8 * scale;
    const cellH = 8 * scale;
    const lines = state.trialText.split(/\r?\n/);
    const longest = lines.reduce((m, line) => Math.max(m, line.length), 1);
    const cols = Math.max(1, longest);
    const rows = Math.max(1, lines.length);
    ui.trialCanvas.width = cols * cellW;
    ui.trialCanvas.height = rows * cellH;

    ctx.trial.fillStyle = "#000";
    ctx.trial.fillRect(0, 0, ui.trialCanvas.width, ui.trialCanvas.height);

    const fg = C64_COLORS[state.trialColor & 0x0f];
    for (let y = 0; y < lines.length; y += 1) {
      const line = lines[y];
      for (let x = 0; x < line.length; x += 1) {
        const idx = trialCharToIndex(line[x]);
        if (idx < 0) continue;
        drawChar(ctx.trial, state.activeCharsetBank, idx, x * cellW, y * cellH, scale, fg, "#000");
      }
    }
  }

  function renderCharCanvas() {
    const scale = 32;
    const charIndex = state.selectedChar;
    drawChar(ctx.char, state.activeCharsetBank, charIndex, 0, 0, scale, C64_COLORS[state.charPreviewColor], "#101010");

    ctx.char.strokeStyle = "rgba(255,255,255,0.16)";
    for (let i = 0; i <= 8; i += 1) {
      const p = i * scale + 0.5;
      ctx.char.beginPath();
      ctx.char.moveTo(p, 0);
      ctx.char.lineTo(p, 256);
      ctx.char.stroke();
      ctx.char.beginPath();
      ctx.char.moveTo(0, p);
      ctx.char.lineTo(256, p);
      ctx.char.stroke();
    }
  }

  function renderCharPicker() {
    const cols = 16;
    const cell = 24;
    ctx.charPicker.fillStyle = "#06121c";
    ctx.charPicker.fillRect(0, 0, ui.charPicker.width, ui.charPicker.height);

    for (let i = 0; i < 256; i += 1) {
      const x = (i % cols) * cell;
      const y = Math.floor(i / cols) * cell;
      ctx.charPicker.strokeStyle = i === state.selectedChar ? "#ffb347" : "#305066";
      ctx.charPicker.strokeRect(x + 0.5, y + 0.5, cell - 1, cell - 1);
      drawChar(ctx.charPicker, state.activeCharsetBank, i, x + 4, y + 4, 2, "#d8f4ff", "#06121c");
    }
  }

  function renderTileCanvas() {
    drawTile(ctx.tile, state.selectedTile, 0, 0, 16);

    ctx.tile.strokeStyle = "rgba(255,255,255,0.25)";
    ctx.tile.lineWidth = 1;
    for (let i = 0; i <= 16; i += 1) {
      const p = i * 16 + 0.5;
      ctx.tile.beginPath();
      ctx.tile.moveTo(p, 0);
      ctx.tile.lineTo(p, 256);
      ctx.tile.stroke();
      ctx.tile.beginPath();
      ctx.tile.moveTo(0, p);
      ctx.tile.lineTo(256, p);
      ctx.tile.stroke();
    }

    ctx.tile.strokeStyle = "rgba(255, 179, 71, 0.8)";
    ctx.tile.strokeRect(0.5, 0.5, 255, 255);
    ctx.tile.beginPath();
    ctx.tile.moveTo(128.5, 0);
    ctx.tile.lineTo(128.5, 256);
    ctx.tile.moveTo(0, 128.5);
    ctx.tile.lineTo(256, 128.5);
    ctx.tile.stroke();
  }

  function renderTilePicker() {
    const cols = 16;
    const cell = 32;
    ctx.tilePicker.fillStyle = "#06121c";
    ctx.tilePicker.fillRect(0, 0, ui.tilePicker.width, ui.tilePicker.height);

    for (let i = 0; i < TILE_COUNT; i += 1) {
      const x = (i % cols) * cell;
      const y = Math.floor(i / cols) * cell;
      blitTile(ctx.tilePicker, i, x, y, 2);
      ctx.tilePicker.strokeStyle = i === state.selectedTile ? "#ffb347" : "#2b4a61";
      ctx.tilePicker.strokeRect(x + 0.5, y + 0.5, cell - 1, cell - 1);
    }
  }

  function drawMapCell(tx, ty) {
    if (tx < 0 || ty < 0 || tx >= state.map.width || ty >= state.map.height) return;
    const scale = 2;
    const px = tx * 16 * scale;
    const py = ty * 16 * scale;
    const tile = state.map.data[ty * state.map.width + tx];
    blitTile(ctx.map, tile, px, py, scale);
    drawMapCellGrid(tx, ty);
  }

  function renderMapCanvas() {
    const scale = 2;
    ui.mapCanvas.width = state.map.width * 16 * scale;
    ui.mapCanvas.height = state.map.height * 16 * scale;

    for (let y = 0; y < state.map.height; y += 1) {
      for (let x = 0; x < state.map.width; x += 1) {
        const tile = state.map.data[y * state.map.width + x];
        blitTile(ctx.map, tile, x * 16 * scale, y * 16 * scale, scale);
      }
    }

    if (!shouldDrawGrid(state.map.width, state.map.height)) {
      return;
    }
    ctx.map.strokeStyle = "#303942";
    for (let x = 0; x <= state.map.width; x += 1) {
      const px = x * 16 * scale + 0.5;
      ctx.map.beginPath();
      ctx.map.moveTo(px, 0);
      ctx.map.lineTo(px, ui.mapCanvas.height);
      ctx.map.stroke();
    }
    for (let y = 0; y <= state.map.height; y += 1) {
      const py = y * 16 * scale + 0.5;
      ctx.map.beginPath();
      ctx.map.moveTo(0, py);
      ctx.map.lineTo(ui.mapCanvas.width, py);
      ctx.map.stroke();
    }
  }

  function renderTestCanvas() {
    const scale = 2;
    ui.testCanvas.width = state.test.width * 16 * scale;
    ui.testCanvas.height = state.test.height * 16 * scale;

    for (let y = 0; y < state.test.height; y += 1) {
      for (let x = 0; x < state.test.width; x += 1) {
        const tile = state.test.data[y * state.test.width + x];
        blitTile(ctx.test, tile, x * 16 * scale, y * 16 * scale, scale);
      }
    }

    if (!shouldDrawGrid(state.test.width, state.test.height)) {
      return;
    }
    ctx.test.strokeStyle = "#303942";
    for (let x = 0; x <= state.test.width; x += 1) {
      const px = x * 16 * scale + 0.5;
      ctx.test.beginPath();
      ctx.test.moveTo(px, 0);
      ctx.test.lineTo(px, ui.testCanvas.height);
      ctx.test.stroke();
    }
    for (let y = 0; y <= state.test.height; y += 1) {
      const py = y * 16 * scale + 0.5;
      ctx.test.beginPath();
      ctx.test.moveTo(0, py);
      ctx.test.lineTo(ui.testCanvas.width, py);
      ctx.test.stroke();
    }
  }

  function renderTileDefinitions() {
    ui.tileDefGrid.innerHTML = "";
    const names = ["Top-Left", "Top-Right", "Bottom-Left", "Bottom-Right"];
    const tile = state.tiles[state.selectedTile];

    for (let i = 0; i < 4; i += 1) {
      const row = document.createElement("div");
      row.className = "tile-def-row";

      const charLabel = document.createElement("label");
      charLabel.textContent = `${names[i]} char`;
      const charInput = document.createElement("input");
      charInput.type = "number";
      charInput.min = "0";
      charInput.max = String(CHAR_COUNT - 1);
      charInput.value = String(tile.chars[i]);
      charInput.addEventListener("change", () => {
        tile.chars[i] = clampCharIndex(Number(charInput.value));
        charInput.value = String(tile.chars[i]);
        invalidateTileAtlases();
        renderAll();
        schedulePersist();
      });
      charLabel.appendChild(charInput);

      const colorSelect = document.createElement("select");
      for (let c = 0; c < 16; c += 1) {
        const opt = document.createElement("option");
        opt.value = String(c);
        opt.textContent = `${c}`;
        opt.style.background = C64_COLORS[c];
        opt.style.color = c === 0 ? "#fff" : "#000";
        colorSelect.appendChild(opt);
      }
      colorSelect.value = String(tile.colors[i]);
      colorSelect.addEventListener("change", () => {
        tile.colors[i] = clampByte(Number(colorSelect.value)) & 0x0f;
        invalidateTileAtlases();
        renderAll();
        schedulePersist();
      });

      row.appendChild(charLabel);
      row.appendChild(colorSelect);
      ui.tileDefGrid.appendChild(row);
    }
  }

  function renderTileProperties() {
    const props = state.tileProps[state.selectedTile];
    ui.propsBoxes.forEach((box) => {
      const bit = Number(box.dataset.bit);
      box.checked = ((props >> bit) & 1) === 1;
    });
  }

  function renderMode() {
    ["char", "tile", "map"].forEach((m) => {
      ui.modePanels[m].classList.toggle("hidden", m !== state.mode);
      ui.sidePanels[m].classList.toggle("hidden", m !== state.mode);
      ui.modeButtons[m].classList.toggle("active", m === state.mode);
    });
  }

  function renderSelection() {
    ui.selectedBank.textContent = String(state.activeCharsetBank);
    ui.charsetActiveBank.value = String(state.activeCharsetBank);
    ui.selectedChar.textContent = String(state.selectedChar);
    ui.selectedCharTile.textContent = String(state.selectedChar);
    ui.selectedTile.textContent = String(state.selectedTile);
    ui.selectedTileMap.textContent = String(state.selectedTile);
  }

  function renderAll() {
    renderMode();
    renderSelection();
    renderCharCanvas();
    renderCharPicker();
    renderTileCanvas();
    renderTilePicker();
    renderTileDefinitions();
    renderTileProperties();
    renderTrialCanvas();
    if (state.mode === "tile") {
      renderTestCanvas();
    }
    if (state.mode === "map") {
      renderMapCanvas();
    }
    ui.charPaste.disabled = !state.charClipboard;
  }

  function syncUiFromState() {
    ui.charPreviewColor.value = String(state.charPreviewColor);
    ui.trialText.value = state.trialText;
    ui.trialMapping.value = state.trialMapping;
    ui.trialColor.value = String(state.trialColor);
    ui.mapWidth.value = String(state.map.width);
    ui.mapHeight.value = String(state.map.height);
    ui.mapId.value = String(state.map.id);
    ui.mapReserved.value = String(state.map.reserved);
    ui.testWidth.value = String(state.test.width);
    ui.testHeight.value = String(state.test.height);
    ui.showGrid.checked = state.showGrid;
  }

  function copyCurrentChar() {
    const base = charsetOffset(state.activeCharsetBank, state.selectedChar, 0);
    state.charClipboard = Array.from(state.charset.subarray(base, base + CHAR_BYTES));
    setStatus(`Copied char ${state.selectedChar} from bank ${state.activeCharsetBank}.`);
    renderAll();
  }

  function pasteCurrentChar() {
    if (!state.charClipboard || state.charClipboard.length !== CHAR_BYTES) {
      setStatus("Character clipboard is empty.", true);
      return;
    }
    const base = charsetOffset(state.activeCharsetBank, state.selectedChar, 0);
    for (let i = 0; i < CHAR_BYTES; i += 1) {
      state.charset[base + i] = clampByte(state.charClipboard[i]);
    }
    invalidateTileAtlases();
    renderAll();
    schedulePersist();
    setStatus(`Pasted into char ${state.selectedChar} on bank ${state.activeCharsetBank}.`);
  }

  function copyCurrentTile() {
    const tile = state.tiles[state.selectedTile];
    state.tileClipboard = {
      chars: tile.chars.slice(0, 4),
      colors: tile.colors.slice(0, 4),
      props: state.tileProps[state.selectedTile]
    };
    setStatus(`Copied tile ${state.selectedTile}.`);
  }

  function pasteCurrentTile() {
    if (!state.tileClipboard) {
      setStatus("Tile clipboard is empty.", true);
      return;
    }
    state.tiles[state.selectedTile].chars = state.tileClipboard.chars.map(clampCharIndex);
    state.tiles[state.selectedTile].colors = state.tileClipboard.colors.map((c) => clampByte(c) & 0x0f);
    state.tileProps[state.selectedTile] = clampByte(state.tileClipboard.props);
    invalidateTileAtlases();
    renderAll();
    schedulePersist();
    setStatus(`Pasted into tile ${state.selectedTile}.`);
  }

  function canvasPos(canvas, event) {
    const rect = canvas.getBoundingClientRect();
    const x = Math.floor((event.clientX - rect.left) * canvas.width / rect.width);
    const y = Math.floor((event.clientY - rect.top) * canvas.height / rect.height);
    return { x, y };
  }

  function tileCanvasCell(event) {
    const { x, y } = canvasPos(ui.tileCanvas, event);
    const px = Math.floor(x / 16);
    const py = Math.floor(y / 16);
    if (px < 0 || px > 15 || py < 0 || py > 15) return null;
    const qx = px >= 8 ? 1 : 0;
    const qy = py >= 8 ? 1 : 0;
    return {
      quadrant: qx + qy * 2,
      localX: px % 8,
      localY: py % 8
    };
  }

  function applyCharDraw(event) {
    const { x, y } = canvasPos(ui.charCanvas, event);
    const px = Math.floor(x / 32);
    const py = Math.floor(y / 32);
    setCharPixel(state.selectedChar, px, py, state.drawValue);
    invalidateTileAtlases();
    renderCharCanvas();
    renderCharPicker();
    renderTileCanvas();
    renderTilePicker();
    if (state.mode === "tile") {
      renderTestCanvas();
    }
    if (state.mode === "map") {
      renderMapCanvas();
    }
    schedulePersist();
  }

  function applyTileDraw(event) {
    const cell = tileCanvasCell(event);
    if (!cell) return;

    const tile = state.tiles[state.selectedTile];
    const targetChar = tile.chars[cell.quadrant];
    setCharPixel(targetChar, cell.localX, cell.localY, state.drawValue);
    invalidateTileAtlases();
    renderCharCanvas();
    renderCharPicker();
    renderTileCanvas();
    renderTilePicker();
    if (state.mode === "tile") {
      renderTestCanvas();
    }
    if (state.mode === "map") {
      renderMapCanvas();
    }
    schedulePersist();
  }

  function assignSelectedCharToTileQuadrant(quadrant) {
    const q = Math.max(0, Math.min(3, quadrant | 0));
    state.tiles[state.selectedTile].chars[q] = state.selectedChar;
    invalidateTileAtlases();
    renderAll();
    schedulePersist();
    setStatus(`Tile ${state.selectedTile}: set quadrant ${q + 1} to char ${state.selectedChar}.`);
  }

  function openHelpDialog() {
    if (ui.helpDialog.open) return;
    if (typeof ui.helpDialog.showModal === "function") {
      ui.helpDialog.showModal();
      return;
    }
    ui.helpDialog.setAttribute("open", "open");
  }

  function closeHelpDialog() {
    if (typeof ui.helpDialog.close === "function") {
      ui.helpDialog.close();
      return;
    }
    ui.helpDialog.removeAttribute("open");
  }

  function applyTilePaintOnCanvas(canvasKey, event) {
    const canvas = canvasKey === "map" ? ui.mapCanvas : ui.testCanvas;
    const data = canvasKey === "map" ? state.map.data : state.test.data;
    const width = canvasKey === "map" ? state.map.width : state.test.width;
    const height = canvasKey === "map" ? state.map.height : state.test.height;

    const { x, y } = canvasPos(canvas, event);
    const tx = Math.floor(x / (16 * 2));
    const ty = Math.floor(y / (16 * 2));
    if (tx < 0 || tx >= width || ty < 0 || ty >= height) return;

    data[ty * width + tx] = state.drawValue ? state.selectedTile : 0;
    if (canvasKey === "map") {
      drawMapCell(tx, ty);
    } else {
      blitTile(ctx.test, data[ty * width + tx], tx * 32, ty * 32, 2);
      if (shouldDrawGrid(width, height)) {
        ctx.test.strokeStyle = "#303942";
        ctx.test.strokeRect(tx * 32 + 0.5, ty * 32 + 0.5, 32, 32);
      }
    }
    schedulePersist();
  }

  function resizeMap(newW, newH) {
    const w = Math.max(1, Math.min(255, newW | 0));
    const h = Math.max(1, Math.min(255, newH | 0));
    const next = new Uint8Array(w * h);

    for (let y = 0; y < Math.min(h, state.map.height); y += 1) {
      for (let x = 0; x < Math.min(w, state.map.width); x += 1) {
        next[y * w + x] = state.map.data[y * state.map.width + x];
      }
    }

    state.map.width = w;
    state.map.height = h;
    state.map.data = next;
    ui.mapWidth.value = String(w);
    ui.mapHeight.value = String(h);
    renderMapCanvas();
  }

  function resizeTest(newW, newH) {
    const w = Math.max(1, Math.min(64, newW | 0));
    const h = Math.max(1, Math.min(64, newH | 0));
    const next = new Uint8Array(w * h);

    for (let y = 0; y < Math.min(h, state.test.height); y += 1) {
      for (let x = 0; x < Math.min(w, state.test.width); x += 1) {
        next[y * w + x] = state.test.data[y * state.test.width + x];
      }
    }

    state.test.width = w;
    state.test.height = h;
    state.test.data = next;
    ui.testWidth.value = String(w);
    ui.testHeight.value = String(h);
    renderTestCanvas();
  }

  function buildCharsetBytes() {
    const out = new Uint8Array(8 + CHARSET_BANKS * CHAR_COUNT * CHAR_BYTES);
    out[0] = 0x43; // C
    out[1] = 0x43; // C
    out[2] = 0x48; // H
    out[3] = 0x52; // R
    out[4] = 2;
    out[5] = CHARSET_BANKS;
    out[6] = 0;
    out[7] = 0;
    out.set(state.charset, 8);
    return out;
  }

  function buildTilesBytes() {
    const out = new Uint8Array(8 + TILE_COUNT * 8 + TILE_COUNT);
    out[0] = 0x43; // C
    out[1] = 0x54; // T
    out[2] = 0x49; // I
    out[3] = 0x4c; // L
    out[4] = 1;
    out[5] = 0;
    out[6] = TILE_COUNT & 0xff;
    out[7] = (TILE_COUNT >> 8) & 0xff;

    let p = 8;
    for (let i = 0; i < TILE_COUNT; i += 1) {
      const t = state.tiles[i];
      for (let q = 0; q < 4; q += 1) {
        out[p++] = clampCharIndex(t.chars[q]);
        out[p++] = t.colors[q] & 0x0f;
      }
    }
    out.set(state.tileProps, p);
    return out;
  }

  function buildMapBytes() {
    const size = state.map.width * state.map.height;
    const out = new Uint8Array(4 + size);
    out[0] = state.map.width & 0xff;
    out[1] = state.map.height & 0xff;
    out[2] = state.map.id & 0xff;
    out[3] = state.map.reserved & 0xff;
    out.set(state.map.data, 4);
    return out;
  }

  function downloadBinary(name, bytes) {
    const blob = new Blob([bytes], { type: "application/octet-stream" });
    const a = document.createElement("a");
    a.href = URL.createObjectURL(blob);
    a.download = name;
    a.click();
    URL.revokeObjectURL(a.href);
  }

  function exportCharset() {
    downloadBinary("charset.cchr", buildCharsetBytes());
    setStatus("Exported charset.cchr");
  }

  function importCharset(buffer) {
    const data = new Uint8Array(buffer);
    const hasCchrHeader =
      data.length >= 8 &&
      data[0] === 0x43 &&
      data[1] === 0x43 &&
      data[2] === 0x48 &&
      data[3] === 0x52;

    if (hasCchrHeader) {
      const version = data[4];
      if (version === 1) {
        const count = data[5] === 0 ? 256 : data[5];
        const needed = 8 + count * CHAR_BYTES;
        if (data.length < needed) {
          setStatus("Charset payload truncated.", true);
          return;
        }

        const targetBank = clampCharsetBank(Number(ui.charsetImportBank.value));
        const copyCount = Math.min(count, CHAR_COUNT);
        for (let c = 0; c < copyCount; c += 1) {
          for (let r = 0; r < CHAR_BYTES; r += 1) {
            state.charset[charsetOffset(targetBank, c, r)] = data[8 + c * CHAR_BYTES + r];
          }
        }
        setStatus(`Imported CCHR v1 into bank ${targetBank} (${copyCount} chars).`);
        invalidateTileAtlases();
        renderAll();
        schedulePersist();
        return;
      }

      if (version === 2) {
        const bankCount = Math.max(1, data[5] | 0);
        const needed = 8 + bankCount * CHAR_COUNT * CHAR_BYTES;
        if (data.length < needed) {
          setStatus("Charset payload truncated.", true);
          return;
        }
        const copyBanks = Math.min(bankCount, CHARSET_BANKS);
        for (let b = 0; b < copyBanks; b += 1) {
          const srcBase = 8 + b * CHAR_COUNT * CHAR_BYTES;
          const dstBase = b * CHAR_COUNT * CHAR_BYTES;
          state.charset.set(data.subarray(srcBase, srcBase + CHAR_COUNT * CHAR_BYTES), dstBase);
        }
        setStatus(`Imported CCHR v2 (${copyBanks} bank(s)).`);
        invalidateTileAtlases();
        renderAll();
        schedulePersist();
        return;
      }

      setStatus(`Unsupported charset version ${version}.`, true);
      return;
    }

    if (data.length === 256 * CHAR_BYTES) {
      const targetBank = clampCharsetBank(Number(ui.charsetImportBank.value));
      const dstBase = targetBank * CHAR_COUNT * CHAR_BYTES;
      state.charset.set(data, dstBase);
      setStatus(`Imported raw ROM charset into bank ${targetBank} (256 chars).`);
      invalidateTileAtlases();
      renderAll();
      schedulePersist();
      return;
    }

    if (data.length === 512 * CHAR_BYTES) {
      state.charset.set(data.subarray(0, CHAR_COUNT * CHAR_BYTES), 0);
      state.charset.set(
        data.subarray(CHAR_COUNT * CHAR_BYTES, 2 * CHAR_COUNT * CHAR_BYTES),
        CHAR_COUNT * CHAR_BYTES
      );
      setStatus("Imported raw dual-bank ROM charset (banks 0 and 1).");
      invalidateTileAtlases();
      renderAll();
      schedulePersist();
      return;
    }

    setStatus("Unsupported charset file. Use CCHR v1/v2, 2048-byte raw ROM, or 4096-byte dual-bank ROM.", true);
  }

  function exportTiles() {
    downloadBinary("tiles.ctil", buildTilesBytes());
    setStatus("Exported tiles.ctil");
  }

  function importTiles(buffer) {
    const data = new Uint8Array(buffer);
    if (data.length < 8) {
      setStatus("Tile file too small.", true);
      return;
    }
    if (data[0] !== 0x43 || data[1] !== 0x54 || data[2] !== 0x49 || data[3] !== 0x4c) {
      setStatus("Invalid tile magic. Expected CTIL.", true);
      return;
    }

    const version = data[4];
    const tileCount = data[6] | (data[7] << 8);
    if (version !== 1) {
      setStatus(`Unsupported tile version ${version}.`, true);
      return;
    }

    const needed = 8 + tileCount * 8 + tileCount;
    if (data.length < needed) {
      setStatus("Tile payload truncated.", true);
      return;
    }

    for (let i = 0; i < TILE_COUNT; i += 1) {
      state.tiles[i].chars = [0, 0, 0, 0];
      state.tiles[i].colors = [1, 1, 1, 1];
      state.tileProps[i] = 0;
    }

    let p = 8;
    const copyCount = Math.min(tileCount, TILE_COUNT);
    for (let i = 0; i < copyCount; i += 1) {
      const chars = [];
      const colors = [];
      for (let q = 0; q < 4; q += 1) {
        chars.push(clampCharIndex(data[p++]));
        colors.push(data[p++] & 0x0f);
      }
      state.tiles[i].chars = chars;
      state.tiles[i].colors = colors;
    }

    const propsStart = 8 + tileCount * 8;
    for (let i = 0; i < copyCount; i += 1) {
      state.tileProps[i] = data[propsStart + i];
    }

    setStatus(`Imported tiles (${copyCount} tiles from file).`);
    invalidateTileAtlases();
    renderAll();
    schedulePersist();
  }

  function exportMap() {
    downloadBinary("map.bin", buildMapBytes());
    setStatus("Exported map.bin");
  }

  function importMap(buffer) {
    const data = new Uint8Array(buffer);
    if (data.length < 4) {
      setStatus("Map file too small.", true);
      return;
    }
    const w = data[0];
    const h = data[1];
    const id = data[2];
    const reserved = data[3];

    if (w === 0 || h === 0) {
      setStatus("Map width/height cannot be zero.", true);
      return;
    }

    const needed = 4 + w * h;
    if (data.length < needed) {
      setStatus("Map payload truncated.", true);
      return;
    }

    state.map.width = w;
    state.map.height = h;
    state.map.id = id;
    state.map.reserved = reserved;
    state.map.data = data.slice(4, needed);

    ui.mapWidth.value = String(w);
    ui.mapHeight.value = String(h);
    ui.mapId.value = String(id);
    ui.mapReserved.value = String(reserved);

    setStatus(`Imported map (${w}x${h}, id=${id}).`);
    renderMapCanvas();
    schedulePersist();
  }

  function normalizeAssetPath(path) {
    return path.trim().replace(/\\/g, "/").replace(/^\/+/, "");
  }

  function assetApiUrl(path) {
    return `/api/assets/${normalizeAssetPath(path).split("/").map(encodeURIComponent).join("/")}`;
  }

  function setAssetServerAvailable(available, message) {
    ui.assetServerStatus.textContent = message;
    [
      ...Object.values(ui.assetFileLists),
      ...Object.values(ui.assetOpenButtons),
      ...Object.values(ui.assetSavePaths),
      ui.assetSaveChars,
      ui.assetSaveTiles,
      ui.assetSaveMap
    ].forEach((el) => {
      el.disabled = !available;
    });
  }

  function detectAssetKinds(path, bytes) {
    const kinds = new Set();
    const lower = path.toLowerCase();
    if (bytes.length >= 4) {
      const magic = String.fromCharCode(bytes[0], bytes[1], bytes[2], bytes[3]);
      if (magic === "CCHR") kinds.add("charset");
      if (magic === "CTIL") kinds.add("tiles");
    }
    if (bytes.length === 256 * CHAR_BYTES || bytes.length === 512 * CHAR_BYTES) kinds.add("charset");
    if (bytes.length >= 4 && bytes[0] > 0 && bytes[1] > 0 && bytes[0] * bytes[1] === bytes.length - 4) {
      kinds.add("map");
    }
    if (kinds.size > 0) return Array.from(kinds);
    if (lower.endsWith(".cchr") || lower.endsWith(".rom") || lower.endsWith(".chr")) kinds.add("charset");
    if (lower.endsWith(".ctil") || lower.endsWith(".til") || lower.endsWith(".tiles")) kinds.add("tiles");
    if (lower.endsWith(".map") || lower.endsWith(".cmap")) kinds.add("map");
    return Array.from(kinds);
  }

  function importAssetBytes(path, buffer, kind) {
    if (kind === "charset") {
      importCharset(buffer);
      return true;
    }
    if (kind === "tiles") {
      importTiles(buffer);
      return true;
    }
    if (kind === "map") {
      importMap(buffer);
      return true;
    }
    setStatus(`Unknown asset type for ${path}.`, true);
    return false;
  }

  function assetKindLabel(kind) {
    if (kind === "charset") return "charset";
    if (kind === "tiles") return "tile";
    return "map";
  }

  function assetExtensionLooksCompatible(kind, path) {
    const lower = path.toLowerCase();
    if (lower.endsWith(".bin")) return kind === "charset" || kind === "map";
    if (kind === "charset") return /\.(cchr|rom|chr)$/.test(lower);
    if (kind === "tiles") return /\.(ctil|til|tiles)$/.test(lower);
    if (kind === "map") return /\.(map|cmap)$/.test(lower);
    return false;
  }

  function canSaveAssetAs(kind, path) {
    const existing = assetFiles.find((file) => file.path === path);
    if (existing && Array.isArray(existing.kinds) && existing.kinds.length > 0) {
      if (existing.kinds.length !== 1 || existing.kinds[0] !== kind) {
        setStatus(`Refusing to overwrite ${path}: existing file is ${existing.kinds.join("/")} data.`, true);
        return false;
      }
    }
    if (!assetExtensionLooksCompatible(kind, path)) {
      setStatus(`Refusing to save ${assetKindLabel(kind)} data with incompatible filename: ${path}.`, true);
      return false;
    }
    return true;
  }

  function renderAssetFileLists() {
    ["charset", "tiles", "map"].forEach((kind) => {
      const select = ui.assetFileLists[kind];
      const current = select.value;
      const files = assetFiles.filter((file) => Array.isArray(file.kinds) && file.kinds.includes(kind));
      select.innerHTML = "";
      if (files.length === 0) {
        const opt = document.createElement("option");
        opt.value = "";
        opt.textContent = `(no ${assetKindLabel(kind)} assets)`;
        select.appendChild(opt);
        return;
      }

      files.forEach((file) => {
        const opt = document.createElement("option");
        opt.value = file.path;
        opt.textContent = `${file.path} (${file.size} bytes)`;
        select.appendChild(opt);
      });
      if (files.some((file) => file.path === current)) {
        select.value = current;
      }
    });
  }

  async function refreshAssetFiles() {
    if (window.location.protocol === "file:") {
      setAssetServerAvailable(false, "Run: python3 tools/asset-editor/server.py");
      return;
    }

    try {
      const response = await fetch("/api/assets");
      if (!response.ok) {
        throw new Error(`HTTP ${response.status}`);
      }
      const payload = await response.json();
      const files = Array.isArray(payload.files) ? payload.files : [];
      assetFiles = files.map((file) => ({
        ...file,
        kinds: Array.isArray(file.kinds) ? file.kinds : []
      }));
      renderAssetFileLists();
      setAssetServerAvailable(true, `Connected to assets/ (${files.length} files).`);
    } catch (err) {
      setAssetServerAvailable(false, `Asset server unavailable: ${String(err)}`);
    }
  }

  async function openAssetFromServer(kind) {
    const path = normalizeAssetPath(ui.assetFileLists[kind].value);
    if (!path) {
      setStatus(`Select a ${assetKindLabel(kind)} asset first.`, true);
      return;
    }
    try {
      const response = await fetch(assetApiUrl(path));
      if (!response.ok) {
        throw new Error(`HTTP ${response.status}`);
      }
      const buffer = await response.arrayBuffer();
      const detectedKinds = detectAssetKinds(path, new Uint8Array(buffer));
      if (detectedKinds.length > 0 && !detectedKinds.includes(kind)) {
        setStatus(`Refusing to open ${path} as ${assetKindLabel(kind)} data; detected ${detectedKinds.join("/")}.`, true);
        return;
      }
      if (importAssetBytes(path, buffer, kind)) {
        ui.assetSavePaths[kind].value = path;
        setStatus(`Opened assets/${path}.`);
      }
    } catch (err) {
      setStatus(`Failed to open assets/${path}: ${String(err)}`, true);
    }
  }

  async function saveAssetToServer(kind) {
    const path = normalizeAssetPath(ui.assetSavePaths[kind].value);
    if (!path) {
      setStatus("Enter a save path under assets/.", true);
      return;
    }
    if (!canSaveAssetAs(kind, path)) {
      return;
    }

    const bytes = kind === "charset"
      ? buildCharsetBytes()
      : kind === "tiles"
        ? buildTilesBytes()
        : buildMapBytes();

    try {
      const response = await fetch(assetApiUrl(path), {
        method: "PUT",
        headers: { "Content-Type": "application/octet-stream" },
        body: bytes
      });
      if (!response.ok) {
        const message = await response.text();
        throw new Error(message || `HTTP ${response.status}`);
      }
      await refreshAssetFiles();
      ui.assetSavePaths[kind].value = path;
      setStatus(`Saved ${bytes.length} bytes to assets/${path}.`);
    } catch (err) {
      setStatus(`Failed to save assets/${path}: ${String(err)}`, true);
    }
  }

  function bindEvents() {
    Object.entries(ui.modeButtons).forEach(([mode, btn]) => {
      btn.addEventListener("click", () => {
        state.mode = mode;
        renderAll();
        schedulePersist();
      });
    });

    ui.showGrid.addEventListener("change", () => {
      state.showGrid = ui.showGrid.checked;
      if (state.mode === "tile") {
        renderTestCanvas();
      }
      if (state.mode === "map") {
        renderMapCanvas();
      }
      schedulePersist();
    });

    ui.helpOpen.addEventListener("click", openHelpDialog);
    ui.helpClose.addEventListener("click", closeHelpDialog);
    ui.helpDialog.addEventListener("click", (event) => {
      if (event.target === ui.helpDialog) {
        closeHelpDialog();
      }
    });

    for (let c = 0; c < 16; c += 1) {
      const opt = document.createElement("option");
      opt.value = String(c);
      opt.textContent = `${c}`;
      ui.charPreviewColor.appendChild(opt);

      const trialOpt = document.createElement("option");
      trialOpt.value = String(c);
      trialOpt.textContent = `${c}`;
      ui.trialColor.appendChild(trialOpt);
    }
    ui.charPreviewColor.value = String(state.charPreviewColor);
    ui.charPreviewColor.addEventListener("change", () => {
      state.charPreviewColor = clampByte(Number(ui.charPreviewColor.value)) & 0x0f;
      renderCharCanvas();
      schedulePersist();
    });

    const onPrimaryButton = (event) => event.button === undefined || event.button === 0;
    ui.charCopy.addEventListener("click", (event) => {
      if (!onPrimaryButton(event)) return;
      copyCurrentChar();
    });
    ui.charCopy.addEventListener("pointerdown", (event) => {
      if (!onPrimaryButton(event)) return;
      event.preventDefault();
      copyCurrentChar();
    });

    ui.charPaste.addEventListener("click", (event) => {
      if (!onPrimaryButton(event)) return;
      pasteCurrentChar();
    });
    ui.charPaste.addEventListener("pointerdown", (event) => {
      if (!onPrimaryButton(event)) return;
      event.preventDefault();
      pasteCurrentChar();
    });

    ui.charRollUp.addEventListener("click", () => {
      rollCurrentChar(0, -1);
      setStatus(`Rolled char ${state.selectedChar} up.`);
    });
    ui.charRollDown.addEventListener("click", () => {
      rollCurrentChar(0, 1);
      setStatus(`Rolled char ${state.selectedChar} down.`);
    });
    ui.charRollLeft.addEventListener("click", () => {
      rollCurrentChar(-1, 0);
      setStatus(`Rolled char ${state.selectedChar} left.`);
    });
    ui.charRollRight.addEventListener("click", () => {
      rollCurrentChar(1, 0);
      setStatus(`Rolled char ${state.selectedChar} right.`);
    });

    ui.trialText.addEventListener("input", () => {
      state.trialText = ui.trialText.value;
      renderTrialCanvas();
      schedulePersist();
    });
    ui.trialMapping.addEventListener("change", () => {
      state.trialMapping = ui.trialMapping.value === "direct" ? "direct" : "petscii-screen";
      renderTrialCanvas();
      schedulePersist();
    });
    ui.trialColor.addEventListener("change", () => {
      state.trialColor = clampByte(Number(ui.trialColor.value)) & 0x0f;
      renderTrialCanvas();
      schedulePersist();
    });

    ui.charsetActiveBank.value = String(state.activeCharsetBank);
    ui.charsetActiveBank.addEventListener("change", () => {
      state.activeCharsetBank = clampCharsetBank(Number(ui.charsetActiveBank.value));
      invalidateTileAtlases();
      renderAll();
      schedulePersist();
    });

    ui.charClear.addEventListener("click", () => {
      const base = charsetOffset(state.activeCharsetBank, state.selectedChar, 0);
      state.charset.fill(0, base, base + CHAR_BYTES);
      invalidateTileAtlases();
      renderAll();
      schedulePersist();
    });

    ui.charPicker.addEventListener("click", (event) => {
      const { x, y } = canvasPos(ui.charPicker, event);
      const cell = 24;
      const ix = Math.floor(x / cell);
      const iy = Math.floor(y / cell);
      const index = iy * 16 + ix;
      if (index >= 0 && index < CHAR_COUNT) {
        state.selectedChar = index;
        renderSelection();
        renderCharCanvas();
        renderCharPicker();
        schedulePersist();
      }
    });

    ui.tilePicker.addEventListener("click", (event) => {
      const { x, y } = canvasPos(ui.tilePicker, event);
      const cell = 32;
      const ix = Math.floor(x / cell);
      const iy = Math.floor(y / cell);
      const index = iy * 16 + ix;
      if (index >= 0 && index < TILE_COUNT) {
        state.selectedTile = index;
        renderSelection();
        renderTileCanvas();
        renderTilePicker();
        renderTileDefinitions();
        renderTileProperties();
        schedulePersist();
      }
    });

    [ui.charCanvas, ui.tileCanvas, ui.mapCanvas, ui.testCanvas].forEach((canvas) => {
      canvas.addEventListener("contextmenu", (e) => e.preventDefault());
    });

    ui.charCanvas.addEventListener("mousedown", (event) => {
      state.drawing = true;
      state.drawValue = event.button === 2 ? 0 : 1;
      applyCharDraw(event);
    });
    ui.charCanvas.addEventListener("mousemove", (event) => {
      if (state.drawing) applyCharDraw(event);
    });

    ui.tileCanvas.addEventListener("mousedown", (event) => {
      if (event.shiftKey) {
        state.drawing = false;
        if (event.button !== 0) return;
        const cell = tileCanvasCell(event);
        if (cell) {
          assignSelectedCharToTileQuadrant(cell.quadrant);
        }
        return;
      }

      state.drawing = true;
      state.drawValue = event.button === 2 ? 0 : 1;
      applyTileDraw(event);
    });
    ui.tileCanvas.addEventListener("mousemove", (event) => {
      if (!state.drawing) return;
      if (event.shiftKey) {
        state.drawing = false;
        return;
      }
      applyTileDraw(event);
    });

    ui.mapCanvas.addEventListener("mousedown", (event) => {
      state.drawing = true;
      state.drawValue = event.button === 2 ? 0 : 1;
      applyTilePaintOnCanvas("map", event);
    });
    ui.mapCanvas.addEventListener("mousemove", (event) => {
      if (state.drawing) applyTilePaintOnCanvas("map", event);
    });

    ui.testCanvas.addEventListener("mousedown", (event) => {
      state.drawing = true;
      state.drawValue = event.button === 2 ? 0 : 1;
      applyTilePaintOnCanvas("test", event);
    });
    ui.testCanvas.addEventListener("mousemove", (event) => {
      if (state.drawing) applyTilePaintOnCanvas("test", event);
    });

    window.addEventListener("mouseup", () => {
      state.drawing = false;
    });

    window.addEventListener("keydown", (event) => {
      const target = event.target;
      const onInput =
        target instanceof HTMLInputElement ||
        target instanceof HTMLSelectElement ||
        target instanceof HTMLTextAreaElement ||
        target instanceof HTMLButtonElement;

      if (!onInput && state.mode === "tile" && ["1", "2", "3", "4"].includes(event.key)) {
        event.preventDefault();
        const quadrant = Number(event.key) - 1;
        assignSelectedCharToTileQuadrant(quadrant);
        return;
      }

      if ((event.ctrlKey || event.metaKey) && !event.shiftKey && !event.altKey && !onInput) {
        if (event.key === "c" || event.key === "C") {
          event.preventDefault();
          if (state.mode === "tile") {
            copyCurrentTile();
          } else if (state.mode === "char") {
            copyCurrentChar();
          }
          return;
        }
        if (event.key === "v" || event.key === "V") {
          event.preventDefault();
          if (state.mode === "tile") {
            pasteCurrentTile();
          } else if (state.mode === "char") {
            pasteCurrentChar();
          }
          return;
        }
      }

      if (event.key !== "Tab") return;
      if (onInput) {
        return;
      }
      event.preventDefault();
      state.activeCharsetBank = state.activeCharsetBank === 0 ? 1 : 0;
      invalidateTileAtlases();
      renderAll();
      setStatus(`Active charset bank: ${state.activeCharsetBank}.`);
      schedulePersist();
    });

    ui.propsBoxes.forEach((box) => {
      box.addEventListener("change", () => {
        const bit = Number(box.dataset.bit);
        let props = state.tileProps[state.selectedTile];
        if (box.checked) {
          props |= (1 << bit);
        } else {
          props &= ~(1 << bit);
        }
        state.tileProps[state.selectedTile] = props;
        schedulePersist();
      });
    });

    ui.resizeMap.addEventListener("click", () => {
      resizeMap(Number(ui.mapWidth.value), Number(ui.mapHeight.value));
      state.map.id = clampByte(Number(ui.mapId.value));
      state.map.reserved = clampByte(Number(ui.mapReserved.value));
      ui.mapId.value = String(state.map.id);
      ui.mapReserved.value = String(state.map.reserved);
      setStatus(`Map resized to ${state.map.width}x${state.map.height}.`);
      schedulePersist();
    });

    ui.mapId.addEventListener("change", () => {
      state.map.id = clampByte(Number(ui.mapId.value));
      ui.mapId.value = String(state.map.id);
      schedulePersist();
    });

    ui.mapReserved.addEventListener("change", () => {
      state.map.reserved = clampByte(Number(ui.mapReserved.value));
      ui.mapReserved.value = String(state.map.reserved);
      schedulePersist();
    });

    ui.resizeTest.addEventListener("click", () => {
      resizeTest(Number(ui.testWidth.value), Number(ui.testHeight.value));
      setStatus(`Test canvas resized to ${state.test.width}x${state.test.height}.`);
      schedulePersist();
    });

    ui.exportChars.addEventListener("click", exportCharset);
    ui.importChars.addEventListener("click", () => ui.charsFile.click());
    ui.exportTiles.addEventListener("click", exportTiles);
    ui.importTiles.addEventListener("click", () => ui.tilesFile.click());
    ui.exportMap.addEventListener("click", exportMap);
    ui.importMap.addEventListener("click", () => ui.mapFile.click());
    Object.values(ui.assetRefreshButtons).forEach((btn) => {
      btn.addEventListener("click", refreshAssetFiles);
    });
    ui.assetOpenButtons.charset.addEventListener("click", () => openAssetFromServer("charset"));
    ui.assetOpenButtons.tiles.addEventListener("click", () => openAssetFromServer("tiles"));
    ui.assetOpenButtons.map.addEventListener("click", () => openAssetFromServer("map"));
    ui.assetSaveChars.addEventListener("click", () => saveAssetToServer("charset"));
    ui.assetSaveTiles.addEventListener("click", () => saveAssetToServer("tiles"));
    ui.assetSaveMap.addEventListener("click", () => saveAssetToServer("map"));
    ["charset", "tiles", "map"].forEach((kind) => {
      ui.assetFileLists[kind].addEventListener("change", () => {
        if (ui.assetFileLists[kind].value) {
          ui.assetSavePaths[kind].value = ui.assetFileLists[kind].value;
        }
      });
    });

    ui.charsFile.addEventListener("change", async () => {
      const file = ui.charsFile.files[0];
      if (!file) return;
      importCharset(await file.arrayBuffer());
      ui.charsFile.value = "";
    });

    ui.tilesFile.addEventListener("change", async () => {
      const file = ui.tilesFile.files[0];
      if (!file) return;
      importTiles(await file.arrayBuffer());
      ui.tilesFile.value = "";
    });

    ui.mapFile.addEventListener("change", async () => {
      const file = ui.mapFile.files[0];
      if (!file) return;
      importMap(await file.arrayBuffer());
      ui.mapFile.value = "";
    });
  }

  function initializeDefaults() {
    for (let i = 0; i < TILE_COUNT; i += 1) {
      const base = i % CHAR_COUNT;
      state.tiles[i].chars = [base, base, base, base];
      state.tiles[i].colors = [1, 1, 1, 1];
      state.tileProps[i] = 0;
    }

    // Small visible marker in char 0 for quick orientation.
    for (let i = 0; i < 8; i += 1) {
      setCharPixel(0, i, i, 1);
      setCharPixel(0, 7 - i, i, 1);
    }
  }

  window.addEventListener("beforeunload", () => {
    if (persistTimer !== null) {
      window.clearTimeout(persistTimer);
      persistTimer = null;
    }
    persistNow();
  });

  bindEvents();
  if (!loadPersistedState()) {
    initializeDefaults();
  } else {
    setStatus("Restored previous editor state.");
  }
  syncUiFromState();
  invalidateTileAtlases();
  renderAll();
  setAssetServerAvailable(false, "Checking asset server...");
  refreshAssetFiles();
})();
