(function () {
  const CHAR_COUNT = 256;
  const CHARSET_BANKS = 2;
  const TILE_COUNT = 256;
  const CHAR_BYTES = 8;

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
    charsetActiveBank: document.getElementById("charset-active-bank"),
    charsetImportBank: document.getElementById("charset-import-bank"),
    selectedBank: document.getElementById("selected-bank"),
    selectedChar: document.getElementById("selected-char"),
    selectedTile: document.getElementById("selected-tile"),
    status: document.getElementById("status"),

    charCanvas: document.getElementById("char-canvas"),
    charPicker: document.getElementById("char-picker"),
    charPreviewColor: document.getElementById("char-preview-color"),
    charClear: document.getElementById("char-clear"),

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
    mapFile: document.getElementById("map-file")
  };

  const ctx = {
    char: ui.charCanvas.getContext("2d"),
    charPicker: ui.charPicker.getContext("2d"),
    tile: ui.tileCanvas.getContext("2d"),
    tilePicker: ui.tilePicker.getContext("2d"),
    test: ui.testCanvas.getContext("2d"),
    map: ui.mapCanvas.getContext("2d")
  };

  function setStatus(msg, isError) {
    ui.status.textContent = msg;
    ui.status.style.color = isError ? "#ffd0c9" : "#f8f5ef";
    ui.status.style.borderColor = isError ? "#ff6f61" : "#3f637e";
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
      drawTile(ctx.tilePicker, i, x, y, 2);
      ctx.tilePicker.strokeStyle = i === state.selectedTile ? "#ffb347" : "#2b4a61";
      ctx.tilePicker.strokeRect(x + 0.5, y + 0.5, cell - 1, cell - 1);
    }
  }

  function renderMapCanvas() {
    const scale = 2;
    ui.mapCanvas.width = state.map.width * 16 * scale;
    ui.mapCanvas.height = state.map.height * 16 * scale;

    for (let y = 0; y < state.map.height; y += 1) {
      for (let x = 0; x < state.map.width; x += 1) {
        const tile = state.map.data[y * state.map.width + x];
        drawTile(ctx.map, tile, x * 16 * scale, y * 16 * scale, scale);
      }
    }

    ctx.map.strokeStyle = "rgba(255,255,255,0.12)";
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
        drawTile(ctx.test, tile, x * 16 * scale, y * 16 * scale, scale);
      }
    }

    ctx.test.strokeStyle = "rgba(255,255,255,0.1)";
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
        renderAll();
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
        renderAll();
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
      ui.modeButtons[m].classList.toggle("active", m === state.mode);
    });
  }

  function renderSelection() {
    ui.selectedBank.textContent = String(state.activeCharsetBank);
    ui.charsetActiveBank.value = String(state.activeCharsetBank);
    ui.selectedChar.textContent = String(state.selectedChar);
    ui.selectedTile.textContent = String(state.selectedTile);
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
    renderTestCanvas();
    renderMapCanvas();
  }

  function canvasPos(canvas, event) {
    const rect = canvas.getBoundingClientRect();
    const x = Math.floor((event.clientX - rect.left) * canvas.width / rect.width);
    const y = Math.floor((event.clientY - rect.top) * canvas.height / rect.height);
    return { x, y };
  }

  function applyCharDraw(event) {
    const { x, y } = canvasPos(ui.charCanvas, event);
    const px = Math.floor(x / 32);
    const py = Math.floor(y / 32);
    setCharPixel(state.selectedChar, px, py, state.drawValue);
    renderCharCanvas();
    renderCharPicker();
    renderTileCanvas();
    renderTilePicker();
    renderTestCanvas();
    renderMapCanvas();
  }

  function applyTileDraw(event) {
    const { x, y } = canvasPos(ui.tileCanvas, event);
    const px = Math.floor(x / 16);
    const py = Math.floor(y / 16);
    if (px < 0 || px > 15 || py < 0 || py > 15) return;

    const qx = px >= 8 ? 1 : 0;
    const qy = py >= 8 ? 1 : 0;
    const q = qx + qy * 2;
    const lx = px % 8;
    const ly = py % 8;

    const tile = state.tiles[state.selectedTile];
    const targetChar = tile.chars[q];
    setCharPixel(targetChar, lx, ly, state.drawValue);
    renderCharCanvas();
    renderCharPicker();
    renderTileCanvas();
    renderTilePicker();
    renderTestCanvas();
    renderMapCanvas();
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
      renderMapCanvas();
    } else {
      renderTestCanvas();
    }
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

  function downloadBinary(name, bytes) {
    const blob = new Blob([bytes], { type: "application/octet-stream" });
    const a = document.createElement("a");
    a.href = URL.createObjectURL(blob);
    a.download = name;
    a.click();
    URL.revokeObjectURL(a.href);
  }

  function exportCharset() {
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
    downloadBinary("charset.cchr", out);
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
        renderAll();
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
        renderAll();
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
      renderAll();
      return;
    }

    if (data.length === 512 * CHAR_BYTES) {
      state.charset.set(data.subarray(0, CHAR_COUNT * CHAR_BYTES), 0);
      state.charset.set(
        data.subarray(CHAR_COUNT * CHAR_BYTES, 2 * CHAR_COUNT * CHAR_BYTES),
        CHAR_COUNT * CHAR_BYTES
      );
      setStatus("Imported raw dual-bank ROM charset (banks 0 and 1).");
      renderAll();
      return;
    }

    setStatus("Unsupported charset file. Use CCHR v1/v2, 2048-byte raw ROM, or 4096-byte dual-bank ROM.", true);
  }

  function exportTiles() {
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

    downloadBinary("tiles.ctil", out);
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
    renderAll();
  }

  function exportMap() {
    const size = state.map.width * state.map.height;
    const out = new Uint8Array(4 + size);
    out[0] = state.map.width & 0xff;
    out[1] = state.map.height & 0xff;
    out[2] = state.map.id & 0xff;
    out[3] = state.map.reserved & 0xff;
    out.set(state.map.data, 4);
    downloadBinary("map.bin", out);
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
  }

  function bindEvents() {
    Object.entries(ui.modeButtons).forEach(([mode, btn]) => {
      btn.addEventListener("click", () => {
        state.mode = mode;
        renderMode();
      });
    });

    for (let c = 0; c < 16; c += 1) {
      const opt = document.createElement("option");
      opt.value = String(c);
      opt.textContent = `${c}`;
      ui.charPreviewColor.appendChild(opt);
    }
    ui.charPreviewColor.value = String(state.charPreviewColor);
    ui.charPreviewColor.addEventListener("change", () => {
      state.charPreviewColor = clampByte(Number(ui.charPreviewColor.value)) & 0x0f;
      renderCharCanvas();
    });

    ui.charsetActiveBank.value = String(state.activeCharsetBank);
    ui.charsetActiveBank.addEventListener("change", () => {
      state.activeCharsetBank = clampCharsetBank(Number(ui.charsetActiveBank.value));
      renderAll();
    });

    ui.charClear.addEventListener("click", () => {
      const base = charsetOffset(state.activeCharsetBank, state.selectedChar, 0);
      state.charset.fill(0, base, base + CHAR_BYTES);
      renderAll();
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
      state.drawing = true;
      state.drawValue = event.button === 2 ? 0 : 1;
      applyTileDraw(event);
    });
    ui.tileCanvas.addEventListener("mousemove", (event) => {
      if (state.drawing) applyTileDraw(event);
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
      if (event.key !== "Tab") return;
      const target = event.target;
      if (
        target instanceof HTMLInputElement ||
        target instanceof HTMLSelectElement ||
        target instanceof HTMLTextAreaElement ||
        target instanceof HTMLButtonElement
      ) {
        return;
      }
      event.preventDefault();
      state.activeCharsetBank = state.activeCharsetBank === 0 ? 1 : 0;
      renderAll();
      setStatus(`Active charset bank: ${state.activeCharsetBank}.`);
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
      });
    });

    ui.resizeMap.addEventListener("click", () => {
      resizeMap(Number(ui.mapWidth.value), Number(ui.mapHeight.value));
      state.map.id = clampByte(Number(ui.mapId.value));
      state.map.reserved = clampByte(Number(ui.mapReserved.value));
      ui.mapId.value = String(state.map.id);
      ui.mapReserved.value = String(state.map.reserved);
      setStatus(`Map resized to ${state.map.width}x${state.map.height}.`);
    });

    ui.mapId.addEventListener("change", () => {
      state.map.id = clampByte(Number(ui.mapId.value));
      ui.mapId.value = String(state.map.id);
    });

    ui.mapReserved.addEventListener("change", () => {
      state.map.reserved = clampByte(Number(ui.mapReserved.value));
      ui.mapReserved.value = String(state.map.reserved);
    });

    ui.resizeTest.addEventListener("click", () => {
      resizeTest(Number(ui.testWidth.value), Number(ui.testHeight.value));
      setStatus(`Test canvas resized to ${state.test.width}x${state.test.height}.`);
    });

    ui.exportChars.addEventListener("click", exportCharset);
    ui.importChars.addEventListener("click", () => ui.charsFile.click());
    ui.exportTiles.addEventListener("click", exportTiles);
    ui.importTiles.addEventListener("click", () => ui.tilesFile.click());
    ui.exportMap.addEventListener("click", exportMap);
    ui.importMap.addEventListener("click", () => ui.mapFile.click());

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

  bindEvents();
  initializeDefaults();
  renderAll();
})();
