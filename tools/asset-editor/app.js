(function () {
  const CHAR_COUNT = 256;
  const CHARSET_BANKS = 2;
  const TILE_COUNT = 256;
  const CHAR_BYTES = 8;
  const MAP_WIDTH = 20;
  const MAP_HEIGHT = 11;
  const MAP_TILE_COUNT = MAP_WIDTH * MAP_HEIGHT;
  const ROOM_OBJECT_COUNT = 256;
  const ROOM_OBJECT_BYTES = ROOM_OBJECT_COUNT * 3;
  const ROOM_TEXT_BYTES = 256;
  const ROOM_V1_FILE_BYTES = 4 + MAP_TILE_COUNT + ROOM_OBJECT_BYTES + ROOM_TEXT_BYTES;
  const ROOM_V2_HEADER_BYTES = 9;
  const ROOM_V2_FILE_BYTES = ROOM_V2_HEADER_BYTES + MAP_TILE_COUNT + ROOM_OBJECT_BYTES + ROOM_TEXT_BYTES;
  const ROOM_HEADER_BYTES = 13;
  const ROOM_FILE_BYTES = ROOM_HEADER_BYTES + MAP_TILE_COUNT + ROOM_OBJECT_BYTES + ROOM_TEXT_BYTES;
  const ROOM_EXIT_NORTH = 0x01;
  const ROOM_EXIT_EAST = 0x02;
  const ROOM_EXIT_WEST = 0x04;
  const ROOM_EXIT_SOUTH = 0x08;
  const OBJECT_TYPE_COUNT = 256;
  const OBJECT_TYPE_BYTES = 64;
  const OBJECT_TYPE_FILE_BYTES = OBJECT_TYPE_COUNT * OBJECT_TYPE_BYTES;
  const PORTRAIT_SPRITE_WIDTH = 24;
  const PORTRAIT_SPRITE_HEIGHT = 21;
  const PORTRAIT_SPRITE_ROW_BYTES = 3;
  const PORTRAIT_SPRITE_BYTES = 64;
  const PORTRAIT_WIDTH = PORTRAIT_SPRITE_WIDTH * 2;
  const PORTRAIT_HEIGHT = PORTRAIT_SPRITE_HEIGHT * 2;
  const PORTRAIT_FILE_BYTES = 4 * PORTRAIT_SPRITE_BYTES;
  const PORTRAIT_SCALE = 8;
  const STORAGE_KEY = "c64-asset-editor-state-v1";
  const STORAGE_VERSION = 6;

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
    showRoomObjects: true,
    drawing: false,
    drawValue: 1,
    map: {
      width: MAP_WIDTH,
      height: MAP_HEIGHT,
      id: 0,
      reserved: 3,
      exitMask: 0,
      north: 0,
      east: 0,
      west: 0,
      south: 0,
      northText: 0,
      eastText: 0,
      westText: 0,
      southText: 0,
      data: new Uint8Array(MAP_TILE_COUNT),
      objects: new Uint8Array(ROOM_OBJECT_BYTES),
      text: new Uint8Array(ROOM_TEXT_BYTES),
      textSource: ""
    },
    objectTypes: Array.from({ length: OBJECT_TYPE_COUNT }, () => ({
      width: 0,
      height: 0,
      hotspotX: 0,
      hotspotY: 0,
      name: "",
      chars: new Uint8Array(16),
      colors: new Uint8Array(16),
      flags: 0,
      light: 0,
      reserved: new Uint8Array(14)
    })),
    selectedObjectType: 1,
    selectedObjectSlot: -1,
    objectTool: "char",
    objectCellColor: 1,
    mapTool: "tile",
    test: {
      width: 16,
      height: 12,
      data: new Uint8Array(16 * 12)
    },
    portrait: {
      id: 0,
      data: new Uint8Array(PORTRAIT_FILE_BYTES)
    },
    portraitUnderlay: {
      image: null,
      visible: false,
      offsetX: 0,
      offsetY: 0,
      scale: 1,
      opacity: 0.5
    }
  };

  const ui = {
    modeButtons: {
      char: document.getElementById("mode-char"),
      tile: document.getElementById("mode-tile"),
      object: document.getElementById("mode-object"),
      room: document.getElementById("mode-room"),
      portrait: document.getElementById("mode-portrait")
    },
    modePanels: {
      char: document.getElementById("char-mode"),
      tile: document.getElementById("tile-mode"),
      object: document.getElementById("object-mode"),
      room: document.getElementById("room-mode"),
      portrait: document.getElementById("portrait-mode")
    },
    sidePanels: {
      char: document.getElementById("side-char"),
      tile: document.getElementById("side-tile"),
      object: document.getElementById("side-object"),
      room: document.getElementById("side-room"),
      portrait: document.getElementById("side-portrait")
    },
    charsetActiveBank: document.getElementById("charset-active-bank"),
    charsetImportBank: document.getElementById("charset-import-bank"),
    selectedBank: document.getElementById("selected-bank"),
    selectedChar: document.getElementById("selected-char"),
    selectedCharTile: document.getElementById("selected-char-tile"),
    selectedCharObject: document.getElementById("selected-char-object"),
    selectedTile: document.getElementById("selected-tile"),
    selectedTileMap: document.getElementById("selected-tile-map"),
    selectedObjectSlot: document.getElementById("selected-object-slot"),
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
    mapId: document.getElementById("map-id"),
    roomExitNorth: document.getElementById("room-exit-north"),
    roomExitEast: document.getElementById("room-exit-east"),
    roomExitWest: document.getElementById("room-exit-west"),
    roomExitSouth: document.getElementById("room-exit-south"),
    roomNeighborNorth: document.getElementById("room-neighbor-north"),
    roomNeighborEast: document.getElementById("room-neighbor-east"),
    roomNeighborWest: document.getElementById("room-neighbor-west"),
    roomNeighborSouth: document.getElementById("room-neighbor-south"),
    roomExitTextNorth: document.getElementById("room-exit-text-north"),
    roomExitTextEast: document.getElementById("room-exit-text-east"),
    roomExitTextWest: document.getElementById("room-exit-text-west"),
    roomExitTextSouth: document.getElementById("room-exit-text-south"),
    mapToolTile: document.getElementById("map-tool-tile"),
    mapToolObject: document.getElementById("map-tool-object"),
    showRoomObjects: document.getElementById("show-room-objects"),
    mapHoverCoordinates: document.getElementById("map-hover-coordinates"),
    roomObjectType: document.getElementById("room-object-type"),
    roomObjectCount: document.getElementById("room-object-count"),
    roomObjectList: document.getElementById("room-object-list"),
    roomSelectedObjectType: document.getElementById("room-selected-object-type"),
    roomObjectX: document.getElementById("room-object-x"),
    roomObjectY: document.getElementById("room-object-y"),
    updateRoomObject: document.getElementById("update-room-object"),
    deleteRoomObject: document.getElementById("delete-room-object"),
    roomTextSource: document.getElementById("room-text-source"),
    roomTextOffsets: document.getElementById("room-text-offsets"),
    objectTypeId: document.getElementById("object-type-id"),
    objectTypeName: document.getElementById("object-type-name"),
    objectTypeWidth: document.getElementById("object-type-width"),
    objectTypeHeight: document.getElementById("object-type-height"),
    objectHotspotX: document.getElementById("object-hotspot-x"),
    objectHotspotY: document.getElementById("object-hotspot-y"),
    objectTypeLight: document.getElementById("object-type-light"),
    objectTypeActor: document.getElementById("object-type-actor"),
    objectTypeList: document.getElementById("object-type-list"),
    objectToolChar: document.getElementById("object-tool-char"),
    objectToolHotspot: document.getElementById("object-tool-hotspot"),
    objectCellColor: document.getElementById("object-cell-color"),
    objectTypeGrid: document.getElementById("object-type-grid"),

    exportChars: document.getElementById("export-chars"),
    importChars: document.getElementById("import-chars"),
    exportTiles: document.getElementById("export-tiles"),
    importTiles: document.getElementById("import-tiles"),
    exportMap: document.getElementById("export-map"),
    importMap: document.getElementById("import-map"),
    exportObjectTypes: document.getElementById("export-object-types"),
    importObjectTypes: document.getElementById("import-object-types"),
    charsFile: document.getElementById("chars-file"),
    tilesFile: document.getElementById("tiles-file"),
    mapFile: document.getElementById("map-file"),
    objectTypesFile: document.getElementById("object-types-file"),

    portraitCanvas: document.getElementById("portrait-canvas"),
    portraitId: document.getElementById("portrait-id"),
    portraitClear: document.getElementById("portrait-clear"),
    selectedPortraitId: document.getElementById("selected-portrait-id"),
    exportPortrait: document.getElementById("export-portrait"),
    importPortrait: document.getElementById("import-portrait"),
    importPortraitPng: document.getElementById("import-portrait-png"),
    portraitFile: document.getElementById("portrait-file"),
    portraitPngFile: document.getElementById("portrait-png-file"),
    portraitUnderlayVisible: document.getElementById("portrait-underlay-visible"),
    portraitUnderlayLoad: document.getElementById("portrait-underlay-load"),
    portraitUnderlayClear: document.getElementById("portrait-underlay-clear"),
    portraitUnderlayFile: document.getElementById("portrait-underlay-file"),
    portraitUnderlayX: document.getElementById("portrait-underlay-x"),
    portraitUnderlayY: document.getElementById("portrait-underlay-y"),
    portraitUnderlayScale: document.getElementById("portrait-underlay-scale"),
    portraitUnderlayOpacity: document.getElementById("portrait-underlay-opacity"),

    assetServerStatus: document.getElementById("asset-server-status"),
    assetFileLists: {
      charset: document.getElementById("asset-file-list-charset"),
      tiles: document.getElementById("asset-file-list-tiles"),
      map: document.getElementById("asset-file-list-map"),
      objecttypes: document.getElementById("asset-file-list-objecttypes"),
      portrait: document.getElementById("asset-file-list-portrait")
    },
    assetRefreshButtons: {
      charset: document.getElementById("asset-refresh-charset"),
      tiles: document.getElementById("asset-refresh-tiles"),
      map: document.getElementById("asset-refresh-map"),
      objecttypes: document.getElementById("asset-refresh-objecttypes"),
      portrait: document.getElementById("asset-refresh-portrait")
    },
    assetOpenButtons: {
      charset: document.getElementById("asset-open-charset"),
      tiles: document.getElementById("asset-open-tiles"),
      map: document.getElementById("asset-open-map"),
      objecttypes: document.getElementById("asset-open-objecttypes"),
      portrait: document.getElementById("asset-open-portrait")
    },
    assetSavePaths: {
      charset: document.getElementById("asset-save-path-charset"),
      tiles: document.getElementById("asset-save-path-tiles"),
      map: document.getElementById("asset-save-path-map"),
      objecttypes: document.getElementById("asset-save-path-objecttypes"),
      portrait: document.getElementById("asset-save-path-portrait")
    },
    assetSaveChars: document.getElementById("asset-save-chars"),
    assetSaveTiles: document.getElementById("asset-save-tiles"),
    assetSaveMap: document.getElementById("asset-save-map"),
    assetSaveObjectTypes: document.getElementById("asset-save-objecttypes"),
    assetSavePortrait: document.getElementById("asset-save-portrait")
  };

  const ctx = {
    char: ui.charCanvas.getContext("2d"),
    charPicker: ui.charPicker.getContext("2d"),
    tile: ui.tileCanvas.getContext("2d"),
    tilePicker: ui.tilePicker.getContext("2d"),
    trial: ui.trialCanvas.getContext("2d"),
    test: ui.testCanvas.getContext("2d"),
    map: ui.mapCanvas.getContext("2d"),
    portrait: ui.portraitCanvas.getContext("2d")
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
        showRoomObjects: state.showRoomObjects,
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
          exitMask: state.map.exitMask,
          north: state.map.north,
          east: state.map.east,
          west: state.map.west,
          south: state.map.south,
          northText: state.map.northText,
          eastText: state.map.eastText,
          westText: state.map.westText,
          southText: state.map.southText,
          data: Array.from(state.map.data),
          objects: Array.from(state.map.objects),
          text: Array.from(state.map.text),
          textSource: state.map.textSource
        },
        objectTypes: state.objectTypes.map((type) => ({
          width: type.width,
          height: type.height,
          hotspotX: type.hotspotX,
          hotspotY: type.hotspotY,
          name: type.name,
          chars: Array.from(type.chars),
          colors: Array.from(type.colors),
          flags: type.flags,
          light: type.light,
          reserved: Array.from(type.reserved)
        })),
        selectedObjectType: state.selectedObjectType,
        selectedObjectSlot: state.selectedObjectSlot,
        objectTool: state.objectTool,
        objectCellColor: state.objectCellColor,
        mapTool: state.mapTool,
        test: {
          width: state.test.width,
          height: state.test.height,
          data: Array.from(state.test.data)
        },
        portrait: {
          id: state.portrait.id,
          data: Array.from(state.portrait.data)
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
      if (!parsed || ![1, 2, 3, 4, 5, STORAGE_VERSION].includes(parsed.version)) return false;

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
          const fixed = new Uint8Array(MAP_TILE_COUNT);
          for (let y = 0; y < Math.min(MAP_HEIGHT, h); y += 1) {
            for (let x = 0; x < Math.min(MAP_WIDTH, w); x += 1) {
              fixed[y * MAP_WIDTH + x] = clampByte(parsed.map.data[y * w + x]);
            }
          }
          state.map.width = MAP_WIDTH;
          state.map.height = MAP_HEIGHT;
          state.map.id = clampByte(parsed.map.id);
          state.map.reserved = 3;
          state.map.exitMask = clampByte(parsed.map.exitMask ?? 0) & 0x0f;
          state.map.north = clampByte(parsed.map.north ?? 0);
          state.map.east = clampByte(parsed.map.east ?? 0);
          state.map.west = clampByte(parsed.map.west ?? 0);
          state.map.south = clampByte(parsed.map.south ?? 0);
          state.map.northText = clampByte(parsed.map.northText ?? 0);
          state.map.eastText = clampByte(parsed.map.eastText ?? 0);
          state.map.westText = clampByte(parsed.map.westText ?? 0);
          state.map.southText = clampByte(parsed.map.southText ?? 0);
          state.map.data = fixed;
          if (Array.isArray(parsed.map.objects) && parsed.map.objects.length === ROOM_OBJECT_BYTES) {
            state.map.objects.set(parsed.map.objects.map(clampByte));
          }
          if (Array.isArray(parsed.map.text) && parsed.map.text.length === ROOM_TEXT_BYTES) {
            state.map.text.set(parsed.map.text.map(clampByte));
          }
          state.map.textSource = typeof parsed.map.textSource === "string" ? parsed.map.textSource : "";
        }
      }

      if (Array.isArray(parsed.objectTypes) && parsed.objectTypes.length === OBJECT_TYPE_COUNT) {
        parsed.objectTypes.forEach((source, index) => {
          const type = state.objectTypes[index];
          type.width = Math.max(0, Math.min(15, source.width | 0));
          type.height = Math.max(0, Math.min(15, source.height | 0));
          type.hotspotX = Math.max(0, Math.min(15, source.hotspotX | 0));
          type.hotspotY = Math.max(0, Math.min(15, source.hotspotY | 0));
          type.name = typeof source.name === "string" ? source.name.slice(0, 14) : "";
          if (Array.isArray(source.chars) && source.chars.length === 16) type.chars.set(source.chars.map(clampByte));
          if (Array.isArray(source.colors) && source.colors.length === 16) type.colors.set(source.colors.map((c) => clampByte(c) & 0x0f));
          type.flags = clampByte(source.flags);
          if (Object.prototype.hasOwnProperty.call(source, "light")) {
            type.light = clampByte(source.light);
          } else if (Array.isArray(source.reserved) && source.reserved.length === 15) {
            type.light = clampByte(source.reserved[0]);
          }
          if (Array.isArray(source.reserved) && source.reserved.length === 14) {
            type.reserved.set(source.reserved.map(clampByte));
          } else if (Array.isArray(source.reserved) && source.reserved.length === 15) {
            type.reserved.set(source.reserved.slice(1).map(clampByte));
          }
        });
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

      if (parsed.portrait && Array.isArray(parsed.portrait.data) &&
          parsed.portrait.data.length === PORTRAIT_FILE_BYTES) {
        state.portrait.id = clampByte(parsed.portrait.id ?? 0);
        state.portrait.data = Uint8Array.from(parsed.portrait.data.map(clampByte));
      }

      if (parsed.mode === "char" || parsed.mode === "tile" || parsed.mode === "object" ||
          parsed.mode === "room" || parsed.mode === "map" || parsed.mode === "portrait") {
        state.mode = parsed.mode === "map" ? "room" : parsed.mode;
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
      state.showRoomObjects = parsed.showRoomObjects !== false;
      state.selectedObjectType = Math.max(1, clampByte(parsed.selectedObjectType ?? 1));
      state.selectedObjectSlot = Number.isInteger(parsed.selectedObjectSlot)
        ? Math.max(-1, Math.min(255, parsed.selectedObjectSlot))
        : -1;
      state.objectTool = parsed.objectTool === "hotspot" ? "hotspot" : "char";
      state.objectCellColor = clampByte(parsed.objectCellColor ?? 1) & 0x0f;
      state.mapTool = parsed.mapTool === "object" ? "object" : "tile";
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

  function clampPortraitId(n) {
    return Math.max(0, Math.min(255, n | 0));
  }

  // Sprite order within the 256-byte block is raster order: top-left,
  // top-right, bottom-left, bottom-right. Each 64-byte sprite block holds 63
  // bytes of pixel data (3 bytes/row * 21 rows) plus one trailing pad byte,
  // matching real C64 sprite memory alignment.
  function portraitPixelOffset(x, y) {
    const quadCol = x < PORTRAIT_SPRITE_WIDTH ? 0 : 1;
    const quadRow = y < PORTRAIT_SPRITE_HEIGHT ? 0 : 1;
    const localX = x - quadCol * PORTRAIT_SPRITE_WIDTH;
    const localY = y - quadRow * PORTRAIT_SPRITE_HEIGHT;
    const spriteIndex = quadRow * 2 + quadCol;
    const byteOffset = spriteIndex * PORTRAIT_SPRITE_BYTES +
      localY * PORTRAIT_SPRITE_ROW_BYTES + (localX >> 3);
    const bitMask = 1 << (7 - (localX & 7));
    return { byteOffset, bitMask };
  }

  function setPortraitPixel(x, y, value) {
    if (x < 0 || x >= PORTRAIT_WIDTH || y < 0 || y >= PORTRAIT_HEIGHT) return;
    const { byteOffset, bitMask } = portraitPixelOffset(x, y);
    if (value) {
      state.portrait.data[byteOffset] |= bitMask;
    } else {
      state.portrait.data[byteOffset] &= ~bitMask;
    }
  }

  function getPortraitPixel(x, y) {
    if (x < 0 || x >= PORTRAIT_WIDTH || y < 0 || y >= PORTRAIT_HEIGHT) return 0;
    const { byteOffset, bitMask } = portraitPixelOffset(x, y);
    return (state.portrait.data[byteOffset] & bitMask) ? 1 : 0;
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
    renderRoomObjects();
    drawMapCellGrid(tx, ty);
  }

  function objectTypeLabel(typeId) {
    const type = state.objectTypes[typeId];
    return type && type.name ? `${typeId.toString(16).padStart(2, "0").toUpperCase()} ${type.name}` :
      typeId.toString(16).padStart(2, "0").toUpperCase();
  }

  function objectTypeIsValid(type) {
    return type && type.width > 0 && type.height > 0 &&
      type.width * type.height <= 16 &&
      type.hotspotX < type.width && type.hotspotY < type.height;
  }

  function objectTypeIsInitialPlaceholder(typeId, type) {
    return typeId === 1 && type.width === 1 && type.height === 1 &&
      type.hotspotX === 0 && type.hotspotY === 0 && type.name === "OBJECT 1" &&
      type.chars[0] === 1 && type.colors[0] === 1 && type.flags === 0 && type.light === 0;
  }

  function drawObjectChar(charIndex, color, dx, dy, scale) {
    ctx.map.fillStyle = C64_COLORS[color & 0x0f];
    for (let py = 0; py < 8; py += 1) {
      const bits = state.charset[charsetOffset(0, charIndex, py)];
      for (let px = 0; px < 8; px += 1) {
        if ((bits >> (7 - px)) & 1) {
          ctx.map.fillRect(dx + px * scale, dy + py * scale, scale, scale);
        }
      }
    }
  }

  function drawMissingObjectType(typeId, halfTileX, halfTileY, cellPixels) {
    const x = halfTileX * cellPixels;
    const y = halfTileY * cellPixels;
    if (x < 0 || y < 0 || x >= ui.mapCanvas.width || y >= ui.mapCanvas.height) return;

    ctx.map.fillStyle = "#06121c";
    ctx.map.fillRect(x + 1, y + 1, cellPixels - 2, cellPixels - 2);
    ctx.map.strokeStyle = "#ffb347";
    ctx.map.lineWidth = 1;
    ctx.map.strokeRect(x + 0.5, y + 0.5, cellPixels - 1, cellPixels - 1);
    ctx.map.fillStyle = "#ffb347";
    ctx.map.font = "9px monospace";
    ctx.map.textAlign = "center";
    ctx.map.textBaseline = "middle";
    ctx.map.fillText(typeId.toString(16).padStart(2, "0").toUpperCase(),
      x + cellPixels / 2, y + cellPixels / 2 + 0.5);
  }

  function renderRoomObjects() {
    if (!state.showRoomObjects) return;
    const scale = 2;
    const cellPixels = 8 * scale;
    for (let slot = 0; slot < ROOM_OBJECT_COUNT; slot += 1) {
      const p = slot * 3;
      const typeId = state.map.objects[p];
      if (typeId === 0) continue;
      const type = state.objectTypes[typeId];
      if (!objectTypeIsValid(type)) {
        drawMissingObjectType(typeId, state.map.objects[p + 1], state.map.objects[p + 2], cellPixels);
        continue;
      }
      const originX = state.map.objects[p + 1] - type.hotspotX;
      const originY = state.map.objects[p + 2] - type.hotspotY;
      for (let index = 0; index < type.width * type.height; index += 1) {
        const charIndex = type.chars[index];
        if (charIndex === 0) continue;
        const x = originX + (index % type.width);
        const y = originY + Math.floor(index / type.width);
        if (x < 0 || y < 0 || x >= MAP_WIDTH * 2 || y >= MAP_HEIGHT * 2) continue;
        drawObjectChar(charIndex, type.colors[index], x * cellPixels, y * cellPixels, scale);
      }
      if (slot === state.selectedObjectSlot) {
        const hx = state.map.objects[p + 1] * cellPixels;
        const hy = state.map.objects[p + 2] * cellPixels;
        ctx.map.strokeStyle = "#ffb347";
        ctx.map.lineWidth = 2;
        ctx.map.strokeRect(hx + 1, hy + 1, cellPixels - 2, cellPixels - 2);
        ctx.map.lineWidth = 1;
      }
    }
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

    renderRoomObjects();

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

  function syncObjectTypeSelect() {
    const current = String(state.selectedObjectType);
    [ui.roomObjectType, ui.roomSelectedObjectType, ui.objectTypeList].forEach((select) => {
      select.innerHTML = "";
    });
    for (let id = 1; id < OBJECT_TYPE_COUNT; id += 1) {
      [ui.roomObjectType, ui.roomSelectedObjectType, ui.objectTypeList].forEach((select) => {
        const option = document.createElement("option");
        option.value = String(id);
        option.textContent = objectTypeLabel(id);
        if (select !== ui.objectTypeList) option.disabled = !objectTypeIsValid(state.objectTypes[id]);
        select.appendChild(option);
      });
    }
    ui.roomObjectType.value = current;
    ui.roomSelectedObjectType.value = current;
    ui.objectTypeList.value = current;
  }

  function renderRoomObjectList() {
    ui.roomObjectList.innerHTML = "";
    let count = 0;
    let nonActors = 0;
    for (let slot = 0; slot < ROOM_OBJECT_COUNT; slot += 1) {
      const p = slot * 3;
      const typeId = state.map.objects[p];
      if (typeId === 0) continue;
      count += 1;
      if ((state.objectTypes[typeId].flags & 1) === 0) nonActors += 1;
      const row = document.createElement("button");
      row.type = "button";
      row.className = "room-object-row";
      row.classList.toggle("active", slot === state.selectedObjectSlot);
      const slotLabel = document.createElement("span");
      const typeLabel = document.createElement("span");
      const positionLabel = document.createElement("span");
      slotLabel.textContent = `#${slot.toString(16).padStart(2, "0").toUpperCase()}`;
      typeLabel.textContent = objectTypeLabel(typeId);
      positionLabel.textContent = `${state.map.objects[p + 1]},${state.map.objects[p + 2]}`;
      row.append(slotLabel, typeLabel, positionLabel);
      row.addEventListener("click", () => {
        state.selectedObjectSlot = slot;
        state.selectedObjectType = typeId;
        renderMapEditorState();
        renderMapCanvas();
        schedulePersist();
      });
      ui.roomObjectList.appendChild(row);
    }
    ui.roomObjectCount.textContent = `${count} / 256 slots, ${nonActors} / 200 non-actors`;
    ui.selectedObjectSlot.textContent = state.selectedObjectSlot < 0
      ? "None"
      : state.selectedObjectSlot.toString(16).padStart(2, "0").toUpperCase();
    ui.deleteRoomObject.disabled = state.selectedObjectSlot < 0;
    ui.updateRoomObject.disabled = state.selectedObjectSlot < 0;
    if (state.selectedObjectSlot >= 0) {
      const p = state.selectedObjectSlot * 3;
      ui.roomSelectedObjectType.value = String(state.map.objects[p]);
      ui.roomObjectX.value = String(state.map.objects[p + 1]);
      ui.roomObjectY.value = String(state.map.objects[p + 2]);
    }
  }

  function encodeRoomTextSource(source, updateState) {
    const bytes = new Uint8Array(ROOM_TEXT_BYTES);
    const offsets = ["00: (empty)"];
    let cursor = 1;
    let truncated = false;
    source.split(/\r?\n/).forEach((line) => {
      if (!line) return;
      if (cursor >= ROOM_TEXT_BYTES - 1) {
        truncated = true;
        return;
      }
      const start = cursor;
      let i = 0;
      for (; i < line.length && cursor < ROOM_TEXT_BYTES - 1; i += 1) {
        bytes[cursor++] = line.charCodeAt(i) & 0xff;
      }
      if (i < line.length) truncated = true;
      bytes[cursor++] = 0;
      offsets.push(`${start.toString(16).padStart(2, "0").toUpperCase()}: ${line}`);
    });
    if (updateState) state.map.text = bytes;
    ui.roomTextOffsets.textContent = offsets.join("\n");
    if (updateState && truncated) setStatus("Room text exceeded 256 bytes and was truncated.", true);
    renderExitTextOptions(bytes);
    return bytes;
  }

  function renderExitTextOptions(bytes) {
    const entries = [[0, "00: generic exit text"]];
    let cursor = 1;
    while (cursor < ROOM_TEXT_BYTES) {
      while (cursor < ROOM_TEXT_BYTES && bytes[cursor] === 0) cursor += 1;
      if (cursor >= ROOM_TEXT_BYTES) break;
      const start = cursor;
      let text = "";
      while (cursor < ROOM_TEXT_BYTES && bytes[cursor] !== 0) {
        text += String.fromCharCode(bytes[cursor++]);
      }
      entries.push([start, `${start.toString(16).padStart(2, "0").toUpperCase()}: ${text}`]);
    }
    [
      [ui.roomExitTextNorth, "northText"],
      [ui.roomExitTextEast, "eastText"],
      [ui.roomExitTextWest, "westText"],
      [ui.roomExitTextSouth, "southText"]
    ].forEach(([select, field]) => {
      const selected = state.map[field];
      select.innerHTML = "";
      entries.forEach(([value, label]) => {
        const option = document.createElement("option");
        option.value = String(value);
        option.textContent = label;
        select.appendChild(option);
      });
      if (!entries.some(([value]) => value === selected)) {
        const option = document.createElement("option");
        option.value = String(selected);
        option.textContent = `${selected.toString(16).padStart(2, "0").toUpperCase()}: invalid offset`;
        select.appendChild(option);
      }
      select.value = String(selected);
    });
  }

  function decodeRoomText(bytes) {
    const lines = [];
    let cursor = 1;
    while (cursor < ROOM_TEXT_BYTES) {
      while (cursor < ROOM_TEXT_BYTES && bytes[cursor] === 0) cursor += 1;
      if (cursor >= ROOM_TEXT_BYTES) break;
      let line = "";
      while (cursor < ROOM_TEXT_BYTES && bytes[cursor] !== 0) {
        line += String.fromCharCode(bytes[cursor++]);
      }
      lines.push(line);
    }
    return lines.join("\n");
  }

  function renderObjectTypeGrid() {
    const type = state.objectTypes[state.selectedObjectType];
    const width = type.width || 1;
    const height = type.height || 1;
    ui.objectTypeGrid.innerHTML = "";
    ui.objectTypeGrid.style.gridTemplateColumns = `repeat(${width}, 64px)`;
    for (let index = 0; index < width * height; index += 1) {
      const cell = document.createElement("button");
      const canvas = document.createElement("canvas");
      const label = document.createElement("span");
      const x = index % width;
      const y = Math.floor(index / width);
      cell.type = "button";
      cell.className = "object-grid-cell";
      cell.classList.toggle("hotspot", x === type.hotspotX && y === type.hotspotY);
      cell.title = `Cell ${x},${y}: char ${type.chars[index]}, color ${type.colors[index] & 0x0f}`;
      canvas.width = 48;
      canvas.height = 48;
      if (type.chars[index] === 0) {
        const cellContext = canvas.getContext("2d");
        cellContext.fillStyle = "#050d14";
        cellContext.fillRect(0, 0, 48, 48);
      } else {
        drawChar(canvas.getContext("2d"), 0, type.chars[index], 0, 0, 6,
          C64_COLORS[type.colors[index] & 0x0f], "#050d14");
      }
      label.textContent = type.chars[index] === 0 ? "empty" : String(type.chars[index]);
      cell.append(canvas, label);
      cell.addEventListener("click", () => {
        type.width = width;
        type.height = height;
        if (state.objectTool === "hotspot") {
          type.hotspotX = x;
          type.hotspotY = y;
        } else {
          type.chars[index] = state.selectedChar;
          type.colors[index] = state.objectCellColor;
        }
        renderObjectTypeEditor();
        renderMapCanvas();
        schedulePersist();
      });
      cell.addEventListener("contextmenu", (event) => {
        event.preventDefault();
        type.chars[index] = 0;
        renderObjectTypeEditor();
        renderMapCanvas();
        schedulePersist();
      });
      ui.objectTypeGrid.appendChild(cell);
    }
  }

  function renderObjectTypeEditor() {
    const type = state.objectTypes[state.selectedObjectType];
    ui.objectTypeId.value = String(state.selectedObjectType);
    ui.objectTypeName.value = type.name;
    ui.objectTypeWidth.value = String(type.width || 1);
    ui.objectTypeHeight.value = String(type.height || 1);
    ui.objectHotspotX.value = String(type.hotspotX);
    ui.objectHotspotY.value = String(type.hotspotY);
    ui.objectTypeLight.value = String(type.light);
    ui.objectTypeActor.checked = (type.flags & 1) !== 0;
    ui.objectTypeList.value = String(state.selectedObjectType);
    ui.objectToolChar.classList.toggle("active", state.objectTool === "char");
    ui.objectToolHotspot.classList.toggle("active", state.objectTool === "hotspot");
    ui.objectCellColor.value = String(state.objectCellColor);
    renderObjectTypeGrid();
  }

  function renderMapEditorState() {
    ui.mapToolTile.classList.toggle("active", state.mapTool === "tile");
    ui.mapToolObject.classList.toggle("active", state.mapTool === "object");
    syncObjectTypeSelect();
    ui.roomObjectType.value = String(state.selectedObjectType);
    renderRoomObjectList();
    ui.roomTextSource.value = state.map.textSource;
    encodeRoomTextSource(state.map.textSource, false);
    [
      [ui.roomExitNorth, ui.roomNeighborNorth, ui.roomExitTextNorth, ROOM_EXIT_NORTH, state.map.north],
      [ui.roomExitEast, ui.roomNeighborEast, ui.roomExitTextEast, ROOM_EXIT_EAST, state.map.east],
      [ui.roomExitWest, ui.roomNeighborWest, ui.roomExitTextWest, ROOM_EXIT_WEST, state.map.west],
      [ui.roomExitSouth, ui.roomNeighborSouth, ui.roomExitTextSouth, ROOM_EXIT_SOUTH, state.map.south]
    ].forEach(([checkbox, input, textInput, bit, value]) => {
      checkbox.checked = (state.map.exitMask & bit) !== 0;
      input.disabled = !checkbox.checked;
      textInput.disabled = !checkbox.checked;
      input.value = String(value);
    });
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

  function renderPortraitCanvas() {
    const scale = PORTRAIT_SCALE;
    ctx.portrait.fillStyle = "#000000";
    ctx.portrait.fillRect(0, 0, ui.portraitCanvas.width, ui.portraitCanvas.height);

    const underlay = state.portraitUnderlay;
    if (underlay.visible && underlay.image) {
      ctx.portrait.save();
      ctx.portrait.globalAlpha = underlay.opacity;
      const dw = underlay.image.width * underlay.scale * scale;
      const dh = underlay.image.height * underlay.scale * scale;
      const dx = underlay.offsetX * scale;
      const dy = underlay.offsetY * scale;
      ctx.portrait.drawImage(underlay.image, dx, dy, dw, dh);
      ctx.portrait.restore();
    }

    // Only the set (white) pixels are drawn; unset pixels stay transparent
    // over the black fill / reference image so tracing over it works.
    ctx.portrait.fillStyle = "#ffffff";
    for (let y = 0; y < PORTRAIT_HEIGHT; y += 1) {
      for (let x = 0; x < PORTRAIT_WIDTH; x += 1) {
        if (getPortraitPixel(x, y)) {
          ctx.portrait.fillRect(x * scale, y * scale, scale, scale);
        }
      }
    }

    // Dashed cross marks the boundary between the four underlying sprites.
    ctx.portrait.strokeStyle = "rgba(255, 179, 71, 0.8)";
    ctx.portrait.setLineDash([4, 4]);
    const midX = PORTRAIT_SPRITE_WIDTH * scale + 0.5;
    const midY = PORTRAIT_SPRITE_HEIGHT * scale + 0.5;
    ctx.portrait.beginPath();
    ctx.portrait.moveTo(midX, 0);
    ctx.portrait.lineTo(midX, ui.portraitCanvas.height);
    ctx.portrait.moveTo(0, midY);
    ctx.portrait.lineTo(ui.portraitCanvas.width, midY);
    ctx.portrait.stroke();
    ctx.portrait.setLineDash([]);
  }

  function applyPortraitDraw(event) {
    const { x, y } = canvasPos(ui.portraitCanvas, event);
    const px = Math.floor(x / PORTRAIT_SCALE);
    const py = Math.floor(y / PORTRAIT_SCALE);
    setPortraitPixel(px, py, state.drawValue);
    renderPortraitCanvas();
    schedulePersist();
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
    ["char", "tile", "object", "room", "portrait"].forEach((m) => {
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
    ui.selectedCharObject.textContent = String(state.selectedChar);
    ui.selectedTile.textContent = String(state.selectedTile);
    ui.selectedTileMap.textContent = String(state.selectedTile);
    ui.selectedPortraitId.textContent = String(state.portrait.id);
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
    if (state.mode === "object") {
      syncObjectTypeSelect();
      renderObjectTypeEditor();
    }
    if (state.mode === "room") {
      renderMapCanvas();
      renderMapEditorState();
    }
    if (state.mode === "portrait") {
      renderPortraitCanvas();
    }
    ui.charPaste.disabled = !state.charClipboard;
  }

  function syncUiFromState() {
    ui.charPreviewColor.value = String(state.charPreviewColor);
    ui.trialText.value = state.trialText;
    ui.trialMapping.value = state.trialMapping;
    ui.trialColor.value = String(state.trialColor);
    ui.mapId.value = String(state.map.id);
    ui.testWidth.value = String(state.test.width);
    ui.testHeight.value = String(state.test.height);
    ui.portraitId.value = String(state.portrait.id);
    ui.portraitUnderlayVisible.checked = state.portraitUnderlay.visible;
    ui.portraitUnderlayX.value = String(state.portraitUnderlay.offsetX);
    ui.portraitUnderlayY.value = String(state.portraitUnderlay.offsetY);
    ui.portraitUnderlayScale.value = String(state.portraitUnderlay.scale);
    ui.portraitUnderlayOpacity.value = String(Math.round(state.portraitUnderlay.opacity * 100));
    ui.showGrid.checked = state.showGrid;
    ui.showRoomObjects.checked = state.showRoomObjects;
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

  function updateMapHoverCoordinates(event) {
    const { x, y } = canvasPos(ui.mapCanvas, event);
    const tileX = Math.floor(x / 32);
    const tileY = Math.floor(y / 32);
    if (tileX < 0 || tileX >= MAP_WIDTH || tileY < 0 || tileY >= MAP_HEIGHT) {
      ui.mapHoverCoordinates.textContent = "Tile (-, -)";
      return;
    }
    ui.mapHoverCoordinates.textContent = `Tile (${tileX}, ${tileY})`;
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
    if (state.mode === "room") {
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
    if (state.mode === "room") {
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

  function roomObjectAt(halfX, halfY) {
    for (let slot = ROOM_OBJECT_COUNT - 1; slot >= 0; slot -= 1) {
      const p = slot * 3;
      const typeId = state.map.objects[p];
      if (typeId === 0) continue;
      const type = state.objectTypes[typeId];
      if (!objectTypeIsValid(type)) continue;
      const localX = halfX - (state.map.objects[p + 1] - type.hotspotX);
      const localY = halfY - (state.map.objects[p + 2] - type.hotspotY);
      if (localX < 0 || localY < 0 || localX >= type.width || localY >= type.height) continue;
      const index = localY * type.width + localX;
      if (type.chars[index] !== 0) return slot;
    }
    return -1;
  }

  function addRoomObject(halfX, halfY) {
    const typeId = state.selectedObjectType;
    const type = state.objectTypes[typeId];
    if (!objectTypeIsValid(type)) {
      setStatus(`Define object type ${typeId} before placing it.`, true);
      return;
    }
    let nonActors = 0;
    let empty = -1;
    for (let slot = 0; slot < ROOM_OBJECT_COUNT; slot += 1) {
      const existingType = state.map.objects[slot * 3];
      if (existingType === 0 && empty < 0) empty = slot;
      if (existingType !== 0 && (state.objectTypes[existingType].flags & 1) === 0) nonActors += 1;
    }
    if ((type.flags & 1) === 0 && nonActors >= 200) {
      setStatus("Room already contains 200 non-actor objects.", true);
      return;
    }
    if (empty < 0) {
      setStatus("Room object list is full.", true);
      return;
    }
    const p = empty * 3;
    state.map.objects[p] = typeId;
    state.map.objects[p + 1] = clampByte(halfX);
    state.map.objects[p + 2] = clampByte(halfY);
    state.selectedObjectSlot = empty;
    setStatus(`Placed ${objectTypeLabel(typeId)} in slot ${empty}.`);
  }

  function deleteRoomObject(slot) {
    if (slot < 0 || slot >= ROOM_OBJECT_COUNT) return;
    state.map.objects.fill(0, slot * 3, slot * 3 + 3);
    if (state.selectedObjectSlot === slot) state.selectedObjectSlot = -1;
  }

  function applyObjectTool(event) {
    const { x, y } = canvasPos(ui.mapCanvas, event);
    const halfX = Math.floor(x / 16);
    const halfY = Math.floor(y / 16);
    if (halfX < 0 || halfY < 0 || halfX >= MAP_WIDTH * 2 || halfY >= MAP_HEIGHT * 2) return;
    const found = roomObjectAt(halfX, halfY);
    if (event.button === 2) {
      if (found >= 0) deleteRoomObject(found);
    } else if (event.shiftKey && state.selectedObjectSlot >= 0) {
      const p = state.selectedObjectSlot * 3;
      state.map.objects[p + 1] = halfX;
      state.map.objects[p + 2] = halfY;
    } else if (found >= 0) {
      state.selectedObjectSlot = found;
      state.selectedObjectType = state.map.objects[found * 3];
    } else {
      addRoomObject(halfX, halfY);
    }
    renderMapCanvas();
    renderMapEditorState();
    schedulePersist();
  }

  function updateSelectedRoomObject() {
    const slot = state.selectedObjectSlot;
    if (slot < 0 || slot >= ROOM_OBJECT_COUNT) return;
    const p = slot * 3;
    const oldTypeId = state.map.objects[p];
    const typeId = Math.max(1, clampByte(Number(ui.roomSelectedObjectType.value)));
    if (!objectTypeIsValid(state.objectTypes[typeId])) {
      setStatus(`Define object type ${typeId} before assigning it.`, true);
      renderMapEditorState();
      return;
    }
    if ((state.objectTypes[oldTypeId].flags & 1) !== 0 &&
        (state.objectTypes[typeId].flags & 1) === 0) {
      let nonActors = 0;
      for (let i = 0; i < ROOM_OBJECT_COUNT; i += 1) {
        const existing = state.map.objects[i * 3];
        if (existing !== 0 && (state.objectTypes[existing].flags & 1) === 0) nonActors += 1;
      }
      if (nonActors >= 200) {
        setStatus("Room already contains 200 non-actor objects.", true);
        renderMapEditorState();
        return;
      }
    }
    state.map.objects[p] = typeId;
    state.map.objects[p + 1] = Math.max(0, Math.min(MAP_WIDTH * 2 - 1, Number(ui.roomObjectX.value) | 0));
    state.map.objects[p + 2] = Math.max(0, Math.min(MAP_HEIGHT * 2 - 1, Number(ui.roomObjectY.value) | 0));
    state.selectedObjectType = typeId;
    renderMapCanvas();
    renderMapEditorState();
    schedulePersist();
  }

  function updateSelectedObjectType() {
    const type = state.objectTypes[state.selectedObjectType];
    const width = Math.max(1, Math.min(15, Number(ui.objectTypeWidth.value) | 0));
    const height = Math.max(1, Math.min(15, Number(ui.objectTypeHeight.value) | 0));
    if (width * height > 16) {
      setStatus("Object graphics may use at most 16 character cells.", true);
      renderObjectTypeEditor();
      return;
    }
    type.width = width;
    type.height = height;
    type.hotspotX = Math.max(0, Math.min(width - 1, Number(ui.objectHotspotX.value) | 0));
    type.hotspotY = Math.max(0, Math.min(height - 1, Number(ui.objectHotspotY.value) | 0));
    type.name = ui.objectTypeName.value.slice(0, 14);
    type.light = clampByte(Number(ui.objectTypeLight.value));
    type.flags = ui.objectTypeActor.checked ? (type.flags | 1) : (type.flags & 0xfe);
    renderObjectTypeEditor();
    syncObjectTypeSelect();
    renderRoomObjectList();
    renderMapCanvas();
    schedulePersist();
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
    const out = new Uint8Array(ROOM_FILE_BYTES);
    out[0] = MAP_WIDTH;
    out[1] = MAP_HEIGHT;
    out[2] = state.map.id & 0xff;
    out[3] = 3;
    out[4] = state.map.exitMask & 0x0f;
    out[5] = state.map.north;
    out[6] = state.map.east;
    out[7] = state.map.west;
    out[8] = state.map.south;
    out[9] = state.map.northText;
    out[10] = state.map.eastText;
    out[11] = state.map.westText;
    out[12] = state.map.southText;
    out.set(state.map.data, ROOM_HEADER_BYTES);
    out.set(state.map.objects, ROOM_HEADER_BYTES + MAP_TILE_COUNT);
    out.set(state.map.text, ROOM_HEADER_BYTES + MAP_TILE_COUNT + ROOM_OBJECT_BYTES);
    return out;
  }

  function buildObjectTypesBytes() {
    const out = new Uint8Array(OBJECT_TYPE_FILE_BYTES);
    state.objectTypes.forEach((type, typeId) => {
      const base = typeId * OBJECT_TYPE_BYTES;
      out[base] = ((type.width & 0x0f) << 4) | (type.height & 0x0f);
      out[base + 1] = ((type.hotspotX & 0x0f) << 4) | (type.hotspotY & 0x0f);
      for (let i = 0; i < Math.min(14, type.name.length); i += 1) {
        out[base + 2 + i] = type.name.charCodeAt(i) & 0xff;
      }
      out.set(type.chars, base + 16);
      out.set(type.colors, base + 32);
      out[base + 48] = type.flags;
      out[base + 49] = type.light;
      out.set(type.reserved, base + 50);
    });
    return out;
  }

  function buildPortraitBytes() {
    return state.portrait.data.slice();
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
    const name = state.map.id.toString(16).padStart(2, "0").toUpperCase();
    downloadBinary(name, buildMapBytes());
    setStatus(`Exported room ${name}`);
  }

  function importMap(buffer) {
    const data = new Uint8Array(buffer);
    if (data.length < 4 + MAP_TILE_COUNT) {
      setStatus("Room file too small.", true);
      return;
    }
    const w = data[0];
    const h = data[1];
    const id = data[2];
    if (w !== MAP_WIDTH || h !== MAP_HEIGHT) {
      setStatus(`Rooms must be ${MAP_WIDTH}x${MAP_HEIGHT} tiles.`, true);
      return;
    }

    if (data.length !== 4 + MAP_TILE_COUNT && data.length !== ROOM_V1_FILE_BYTES &&
        data.length !== ROOM_V2_FILE_BYTES && data.length !== ROOM_FILE_BYTES) {
      setStatus(`Room must be legacy 224, v1 ${ROOM_V1_FILE_BYTES}, v2 ${ROOM_V2_FILE_BYTES}, or v3 ${ROOM_FILE_BYTES} bytes.`, true);
      return;
    }
    if ((data.length === ROOM_V1_FILE_BYTES && data[3] !== 1) ||
        (data.length === ROOM_V2_FILE_BYTES && data[3] !== 2) ||
        (data.length === ROOM_FILE_BYTES && data[3] !== 3)) {
      setStatus(`Unsupported room format ${data[3]}.`, true);
      return;
    }

    state.map.width = MAP_WIDTH;
    state.map.height = MAP_HEIGHT;
    state.map.id = id;
    state.map.reserved = 3;
    state.map.exitMask = 0;
    state.map.north = 0;
    state.map.east = 0;
    state.map.west = 0;
    state.map.south = 0;
    state.map.northText = 0;
    state.map.eastText = 0;
    state.map.westText = 0;
    state.map.southText = 0;
    const headerBytes = data.length === ROOM_FILE_BYTES ? ROOM_HEADER_BYTES :
      data.length === ROOM_V2_FILE_BYTES ? ROOM_V2_HEADER_BYTES : 4;
    if (data.length === ROOM_V2_FILE_BYTES || data.length === ROOM_FILE_BYTES) {
      state.map.exitMask = data[4] & 0x0f;
      state.map.north = data[5];
      state.map.east = data[6];
      state.map.west = data[7];
      state.map.south = data[8];
    }
    if (data.length === ROOM_FILE_BYTES) {
      state.map.northText = data[9];
      state.map.eastText = data[10];
      state.map.westText = data[11];
      state.map.southText = data[12];
    }
    state.map.data = data.slice(headerBytes, headerBytes + MAP_TILE_COUNT);
    state.map.objects.fill(0);
    state.map.text.fill(0);
    if (data.length === ROOM_V1_FILE_BYTES || data.length === ROOM_V2_FILE_BYTES ||
        data.length === ROOM_FILE_BYTES) {
      state.map.objects.set(data.subarray(headerBytes + MAP_TILE_COUNT,
        headerBytes + MAP_TILE_COUNT + ROOM_OBJECT_BYTES));
      state.map.text.set(data.subarray(headerBytes + MAP_TILE_COUNT + ROOM_OBJECT_BYTES));
    }
    state.map.textSource = decodeRoomText(state.map.text);
    state.selectedObjectSlot = -1;
    ui.mapId.value = String(id);
    ui.assetSavePaths.map.value = id.toString(16).padStart(2, "0").toUpperCase();

    setStatus(`Imported room ${id.toString(16).padStart(2, "0").toUpperCase()}.`);
    renderAll();
    schedulePersist();
  }

  function exportObjectTypes() {
    downloadBinary("objects.cobj", buildObjectTypesBytes());
    setStatus("Exported objects.cobj");
  }

  function importObjectTypes(buffer) {
    const data = new Uint8Array(buffer);
    const decoder = new TextDecoder("latin1");
    if (data.length !== OBJECT_TYPE_FILE_BYTES) {
      setStatus(`Object type list must be ${OBJECT_TYPE_FILE_BYTES} bytes.`, true);
      return;
    }
    for (let typeId = 0; typeId < OBJECT_TYPE_COUNT; typeId += 1) {
      importObjectTypeRecord(data, typeId, decoder);
    }
    setStatus("Imported 256 object types.");
    renderAll();
    schedulePersist();
  }

  function importObjectTypeRecord(data, typeId, decoder) {
    const base = typeId * OBJECT_TYPE_BYTES;
    const type = state.objectTypes[typeId];
    type.width = data[base] >> 4;
    type.height = data[base] & 0x0f;
    type.hotspotX = data[base + 1] >> 4;
    type.hotspotY = data[base + 1] & 0x0f;
    const nameBytes = data.subarray(base + 2, base + 16);
    const zero = nameBytes.indexOf(0);
    type.name = decoder.decode(zero >= 0 ? nameBytes.subarray(0, zero) : nameBytes).trimEnd();
    type.chars.set(data.subarray(base + 16, base + 32));
    type.colors.set(data.subarray(base + 32, base + 48));
    for (let i = 0; i < 16; i += 1) type.colors[i] &= 0x0f;
    type.flags = data[base + 48];
    type.light = data[base + 49];
    type.reserved.set(data.subarray(base + 50, base + 64));
  }

  function exportPortrait() {
    const name = state.portrait.id.toString(16).padStart(2, "0").toUpperCase();
    downloadBinary(name, buildPortraitBytes());
    setStatus(`Exported portrait ${name}`);
  }

  function importPortrait(buffer) {
    const data = new Uint8Array(buffer);
    if (data.length !== PORTRAIT_FILE_BYTES) {
      setStatus(`Portrait file must be ${PORTRAIT_FILE_BYTES} bytes.`, true);
      return;
    }
    state.portrait.data.set(data);
    setStatus(`Imported portrait ${state.portrait.id.toString(16).padStart(2, "0").toUpperCase()}.`);
    renderAll();
    schedulePersist();
  }

  async function importPortraitPng(file) {
    try {
      const bitmap = await createImageBitmap(file);
      const canvas = document.createElement("canvas");
      canvas.width = PORTRAIT_WIDTH;
      canvas.height = PORTRAIT_HEIGHT;
      const offCtx = canvas.getContext("2d");
      offCtx.fillStyle = "#000000";
      offCtx.fillRect(0, 0, PORTRAIT_WIDTH, PORTRAIT_HEIGHT);
      offCtx.drawImage(bitmap, 0, 0, PORTRAIT_WIDTH, PORTRAIT_HEIGHT);
      const image = offCtx.getImageData(0, 0, PORTRAIT_WIDTH, PORTRAIT_HEIGHT).data;
      state.portrait.data.fill(0);
      for (let y = 0; y < PORTRAIT_HEIGHT; y += 1) {
        for (let x = 0; x < PORTRAIT_WIDTH; x += 1) {
          const i = (y * PORTRAIT_WIDTH + x) * 4;
          const luma = 0.299 * image[i] + 0.587 * image[i + 1] + 0.114 * image[i + 2];
          const alpha = image[i + 3];
          setPortraitPixel(x, y, alpha >= 128 && luma >= 128 ? 1 : 0);
        }
      }
      setStatus(`Imported PNG into portrait ${state.portrait.id.toString(16).padStart(2, "0").toUpperCase()} (${PORTRAIT_WIDTH}x${PORTRAIT_HEIGHT}, thresholded).`);
      renderAll();
      schedulePersist();
    } catch (err) {
      setStatus(`Failed to import PNG: ${String(err)}`, true);
    }
  }

  async function loadPortraitUnderlayImage(file) {
    try {
      const bitmap = await createImageBitmap(file);
      if (state.portraitUnderlay.image) {
        state.portraitUnderlay.image.close();
      }
      state.portraitUnderlay.image = bitmap;
      state.portraitUnderlay.visible = true;
      ui.portraitUnderlayVisible.checked = true;
      renderPortraitCanvas();
      setStatus(`Loaded reference image (${bitmap.width}x${bitmap.height}).`);
    } catch (err) {
      setStatus(`Failed to load reference image: ${String(err)}`, true);
    }
  }

  function clearPortraitUnderlay() {
    if (state.portraitUnderlay.image) {
      state.portraitUnderlay.image.close();
    }
    state.portraitUnderlay.image = null;
    state.portraitUnderlay.visible = false;
    ui.portraitUnderlayVisible.checked = false;
    renderPortraitCanvas();
    setStatus("Cleared reference image.");
  }

  function missingRoomObjectTypeIds() {
    const missing = new Set();
    for (let slot = 0; slot < ROOM_OBJECT_COUNT; slot += 1) {
      const typeId = state.map.objects[slot * 3];
      const type = state.objectTypes[typeId];
      if (typeId !== 0 && (!objectTypeIsValid(type) || objectTypeIsInitialPlaceholder(typeId, type))) {
        missing.add(typeId);
      }
    }
    return Array.from(missing);
  }

  function charsetIsInitialDefault() {
    const marker = [0x81, 0x42, 0x24, 0x18, 0x18, 0x24, 0x42, 0x81];
    for (let i = 0; i < state.charset.length; i += 1) {
      const expected = i < marker.length ? marker[i] : 0;
      if (state.charset[i] !== expected) return false;
    }
    return true;
  }

  function tilesAreInitialDefault() {
    for (let tileId = 0; tileId < TILE_COUNT; tileId += 1) {
      const tile = state.tiles[tileId];
      for (let cell = 0; cell < 4; cell += 1) {
        if (tile.chars[cell] !== tileId || tile.colors[cell] !== 1) return false;
      }
      if (state.tileProps[tileId] !== 0) return false;
    }
    return true;
  }

  function companionAsset(kind, preferredPath) {
    const candidates = assetFiles.filter((file) =>
      Array.isArray(file.kinds) && file.kinds.includes(kind));
    return candidates.find((file) => file.path === preferredPath) ||
      (candidates.length === 1 ? candidates[0] : null);
  }

  async function fetchAssetData(file) {
    const response = await fetch(assetApiUrl(file.path));
    if (!response.ok) throw new Error(`HTTP ${response.status} loading ${file.path}`);
    return response.arrayBuffer();
  }

  async function loadDefaultRoomDisplayAssets() {
    const loaded = [];
    const charsetFile = charsetIsInitialDefault() ? companionAsset("charset", "charset.cchr") : null;
    if (charsetFile) {
      importCharset(await fetchAssetData(charsetFile));
      loaded.push("charset");
    }

    const tileFile = tilesAreInitialDefault() ? companionAsset("tiles", "tiles.ctil") : null;
    if (tileFile) {
      importTiles(await fetchAssetData(tileFile));
      loaded.push("tiles");
    }
    return loaded;
  }

  async function loadMissingRoomObjectTypes() {
    const missing = missingRoomObjectTypeIds();
    if (missing.length === 0) return 0;

    const source = companionAsset("objecttypes", "objects.cobj");
    if (!source) return 0;

    const data = new Uint8Array(await fetchAssetData(source));
    if (data.length !== OBJECT_TYPE_FILE_BYTES) {
      throw new Error(`${source.path} is not an object type list`);
    }

    const decoder = new TextDecoder("latin1");
    let loaded = 0;
    missing.forEach((typeId) => {
      const packedSize = data[typeId * OBJECT_TYPE_BYTES];
      if ((packedSize >> 4) === 0 || (packedSize & 0x0f) === 0) return;
      importObjectTypeRecord(data, typeId, decoder);
      loaded += 1;
    });
    if (loaded > 0) {
      renderAll();
      schedulePersist();
    }
    return loaded;
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
      ui.assetSaveMap,
      ui.assetSaveObjectTypes,
      ui.assetSavePortrait
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
    if (bytes.length >= 4 && bytes[0] === MAP_WIDTH && bytes[1] === MAP_HEIGHT &&
        (bytes.length === 4 + MAP_TILE_COUNT || bytes.length === ROOM_V1_FILE_BYTES ||
         bytes.length === ROOM_V2_FILE_BYTES || bytes.length === ROOM_FILE_BYTES)) {
      kinds.add("map");
    }
    if (bytes.length === OBJECT_TYPE_FILE_BYTES) kinds.add("objecttypes");
    if (bytes.length === PORTRAIT_FILE_BYTES && /(^|\/)portraits\//.test(lower)) kinds.add("portrait");
    if (kinds.size > 0) return Array.from(kinds);
    if (lower.endsWith(".cchr") || lower.endsWith(".rom") || lower.endsWith(".chr")) kinds.add("charset");
    if (lower.endsWith(".ctil") || lower.endsWith(".til") || lower.endsWith(".tiles")) kinds.add("tiles");
    if (lower.endsWith(".map") || lower.endsWith(".cmap")) kinds.add("map");
    if (lower.endsWith(".cobj") || lower.endsWith(".objects")) kinds.add("objecttypes");
    if (/(^|\/)portraits\/[0-9a-f]{2}$/.test(lower)) kinds.add("portrait");
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
    if (kind === "objecttypes") {
      importObjectTypes(buffer);
      return true;
    }
    if (kind === "portrait") {
      importPortrait(buffer);
      return true;
    }
    setStatus(`Unknown asset type for ${path}.`, true);
    return false;
  }

  function assetKindLabel(kind) {
    if (kind === "charset") return "charset";
    if (kind === "tiles") return "tile";
    if (kind === "objecttypes") return "object type";
    if (kind === "portrait") return "portrait";
    return "room";
  }

  function assetExtensionLooksCompatible(kind, path) {
    const lower = path.toLowerCase();
    if (lower.endsWith(".bin")) return kind === "charset" || kind === "map" || kind === "objecttypes";
    if (kind === "charset") return /\.(cchr|rom|chr)$/.test(lower);
    if (kind === "tiles") return /\.(ctil|til|tiles)$/.test(lower);
    if (kind === "map") return /\.(map|cmap)$/.test(lower) || /(^|\/)[0-9a-f]{2}$/.test(lower);
    if (kind === "objecttypes") return /\.(cobj|objects)$/.test(lower);
    if (kind === "portrait") return /(^|\/)portraits\/[0-9a-f]{2}$/.test(lower);
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
    ["charset", "tiles", "map", "objecttypes", "portrait"].forEach((kind) => {
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
        const companions = kind === "map" ? await loadDefaultRoomDisplayAssets() : [];
        const loadedTypes = kind === "map" ? await loadMissingRoomObjectTypes() : 0;
        if (loadedTypes > 0) companions.push(`${loadedTypes} object type${loadedTypes === 1 ? "" : "s"}`);
        setStatus(companions.length > 0
          ? `Opened assets/${path}; loaded ${companions.join(", ")}.`
          : `Opened assets/${path}.`);
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
        : kind === "objecttypes"
          ? buildObjectTypesBytes()
          : kind === "portrait"
            ? buildPortraitBytes()
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
        if (mode === "object" || mode === "room") {
          state.activeCharsetBank = 0;
          invalidateTileAtlases();
        }
        renderAll();
        schedulePersist();
      });
    });

    ui.showGrid.addEventListener("change", () => {
      state.showGrid = ui.showGrid.checked;
      if (state.mode === "tile") {
        renderTestCanvas();
      }
      if (state.mode === "room") {
        renderMapCanvas();
      }
      schedulePersist();
    });

    ui.showRoomObjects.addEventListener("change", () => {
      state.showRoomObjects = ui.showRoomObjects.checked;
      renderMapCanvas();
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

      const objectOpt = document.createElement("option");
      objectOpt.value = String(c);
      objectOpt.textContent = `${c}`;
      objectOpt.style.backgroundColor = C64_COLORS[c];
      ui.objectCellColor.appendChild(objectOpt);
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

    [ui.charCanvas, ui.tileCanvas, ui.mapCanvas, ui.testCanvas, ui.portraitCanvas].forEach((canvas) => {
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
      if (state.mapTool === "object") {
        state.drawing = false;
        applyObjectTool(event);
        return;
      }
      state.drawing = true;
      state.drawValue = event.button === 2 ? 0 : 1;
      applyTilePaintOnCanvas("map", event);
    });
    ui.mapCanvas.addEventListener("mousemove", (event) => {
      updateMapHoverCoordinates(event);
      if (state.drawing && state.mapTool === "tile") applyTilePaintOnCanvas("map", event);
    });
    ui.mapCanvas.addEventListener("mouseleave", () => {
      ui.mapHoverCoordinates.textContent = "Tile (-, -)";
    });

    ui.testCanvas.addEventListener("mousedown", (event) => {
      state.drawing = true;
      state.drawValue = event.button === 2 ? 0 : 1;
      applyTilePaintOnCanvas("test", event);
    });
    ui.testCanvas.addEventListener("mousemove", (event) => {
      if (state.drawing) applyTilePaintOnCanvas("test", event);
    });

    ui.portraitCanvas.addEventListener("mousedown", (event) => {
      state.drawing = true;
      state.drawValue = event.button === 2 ? 0 : 1;
      applyPortraitDraw(event);
    });
    ui.portraitCanvas.addEventListener("mousemove", (event) => {
      if (state.drawing) applyPortraitDraw(event);
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
      if (state.mode === "object" || state.mode === "room") {
        setStatus("Objects and room tiles always use charset bank 0.");
        return;
      }
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

    ui.mapId.addEventListener("change", () => {
      state.map.id = clampByte(Number(ui.mapId.value));
      ui.mapId.value = String(state.map.id);
      ui.assetSavePaths.map.value = state.map.id.toString(16).padStart(2, "0").toUpperCase();
      schedulePersist();
    });

    ui.portraitId.addEventListener("change", () => {
      state.portrait.id = clampPortraitId(Number(ui.portraitId.value));
      ui.portraitId.value = String(state.portrait.id);
      ui.assetSavePaths.portrait.value =
        `portraits/${state.portrait.id.toString(16).padStart(2, "0").toUpperCase()}`;
      renderSelection();
      schedulePersist();
    });
    ui.portraitClear.addEventListener("click", () => {
      state.portrait.data.fill(0);
      renderPortraitCanvas();
      schedulePersist();
      setStatus(`Cleared portrait ${state.portrait.id.toString(16).padStart(2, "0").toUpperCase()}.`);
    });

    ui.portraitUnderlayVisible.addEventListener("change", () => {
      state.portraitUnderlay.visible = ui.portraitUnderlayVisible.checked;
      renderPortraitCanvas();
    });
    ui.portraitUnderlayLoad.addEventListener("click", () => ui.portraitUnderlayFile.click());
    ui.portraitUnderlayClear.addEventListener("click", clearPortraitUnderlay);
    ui.portraitUnderlayFile.addEventListener("change", async () => {
      const file = ui.portraitUnderlayFile.files[0];
      if (!file) return;
      await loadPortraitUnderlayImage(file);
      ui.portraitUnderlayFile.value = "";
    });
    ui.portraitUnderlayX.addEventListener("input", () => {
      state.portraitUnderlay.offsetX = Number(ui.portraitUnderlayX.value) || 0;
      renderPortraitCanvas();
    });
    ui.portraitUnderlayY.addEventListener("input", () => {
      state.portraitUnderlay.offsetY = Number(ui.portraitUnderlayY.value) || 0;
      renderPortraitCanvas();
    });
    ui.portraitUnderlayScale.addEventListener("input", () => {
      const value = Number(ui.portraitUnderlayScale.value);
      state.portraitUnderlay.scale = value > 0 ? value : 1;
      renderPortraitCanvas();
    });
    ui.portraitUnderlayOpacity.addEventListener("input", () => {
      state.portraitUnderlay.opacity = Math.max(0, Math.min(100, Number(ui.portraitUnderlayOpacity.value))) / 100;
      renderPortraitCanvas();
    });

    [
      [ui.roomExitNorth, ui.roomNeighborNorth, ui.roomExitTextNorth, ROOM_EXIT_NORTH, "north", "northText"],
      [ui.roomExitEast, ui.roomNeighborEast, ui.roomExitTextEast, ROOM_EXIT_EAST, "east", "eastText"],
      [ui.roomExitWest, ui.roomNeighborWest, ui.roomExitTextWest, ROOM_EXIT_WEST, "west", "westText"],
      [ui.roomExitSouth, ui.roomNeighborSouth, ui.roomExitTextSouth, ROOM_EXIT_SOUTH, "south", "southText"]
    ].forEach(([checkbox, input, textInput, bit, field, textField]) => {
      checkbox.addEventListener("change", () => {
        if (checkbox.checked) state.map.exitMask |= bit;
        else state.map.exitMask &= ~bit;
        input.disabled = !checkbox.checked;
        textInput.disabled = !checkbox.checked;
        schedulePersist();
      });
      input.addEventListener("change", () => {
        state.map[field] = clampByte(Number(input.value));
        input.value = String(state.map[field]);
        schedulePersist();
      });
      textInput.addEventListener("change", () => {
        state.map[textField] = clampByte(Number(textInput.value));
        schedulePersist();
      });
    });

    ui.mapToolTile.addEventListener("click", () => {
      state.mapTool = "tile";
      renderMapEditorState();
      schedulePersist();
    });
    ui.mapToolObject.addEventListener("click", () => {
      state.mapTool = "object";
      renderMapEditorState();
      schedulePersist();
    });
    ui.roomObjectType.addEventListener("change", () => {
      state.selectedObjectType = Math.max(1, clampByte(Number(ui.roomObjectType.value)));
      schedulePersist();
    });
    ui.updateRoomObject.addEventListener("click", updateSelectedRoomObject);
    ui.deleteRoomObject.addEventListener("click", () => {
      deleteRoomObject(state.selectedObjectSlot);
      renderMapCanvas();
      renderMapEditorState();
      schedulePersist();
    });
    ui.roomTextSource.addEventListener("input", () => {
      state.map.textSource = ui.roomTextSource.value;
      encodeRoomTextSource(state.map.textSource, true);
      schedulePersist();
    });
    ui.objectTypeId.addEventListener("change", () => {
      state.selectedObjectType = Math.max(1, clampByte(Number(ui.objectTypeId.value)));
      renderObjectTypeEditor();
      syncObjectTypeSelect();
      ui.roomObjectType.value = String(state.selectedObjectType);
      schedulePersist();
    });
    ui.objectTypeList.addEventListener("change", () => {
      state.selectedObjectType = Math.max(1, clampByte(Number(ui.objectTypeList.value)));
      renderObjectTypeEditor();
      schedulePersist();
    });
    ui.objectToolChar.addEventListener("click", () => {
      state.objectTool = "char";
      renderObjectTypeEditor();
      schedulePersist();
    });
    ui.objectToolHotspot.addEventListener("click", () => {
      state.objectTool = "hotspot";
      renderObjectTypeEditor();
      schedulePersist();
    });
    ui.objectCellColor.addEventListener("change", () => {
      state.objectCellColor = clampByte(Number(ui.objectCellColor.value)) & 0x0f;
      schedulePersist();
    });
    [ui.objectTypeName, ui.objectTypeWidth, ui.objectTypeHeight,
      ui.objectHotspotX, ui.objectHotspotY, ui.objectTypeLight,
      ui.objectTypeActor].forEach((control) => {
      control.addEventListener("change", updateSelectedObjectType);
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
    ui.exportObjectTypes.addEventListener("click", exportObjectTypes);
    ui.importObjectTypes.addEventListener("click", () => ui.objectTypesFile.click());
    ui.exportPortrait.addEventListener("click", exportPortrait);
    ui.importPortrait.addEventListener("click", () => ui.portraitFile.click());
    ui.importPortraitPng.addEventListener("click", () => ui.portraitPngFile.click());
    Object.values(ui.assetRefreshButtons).forEach((btn) => {
      btn.addEventListener("click", refreshAssetFiles);
    });
    ui.assetOpenButtons.charset.addEventListener("click", () => openAssetFromServer("charset"));
    ui.assetOpenButtons.tiles.addEventListener("click", () => openAssetFromServer("tiles"));
    ui.assetOpenButtons.map.addEventListener("click", () => openAssetFromServer("map"));
    ui.assetOpenButtons.objecttypes.addEventListener("click", () => openAssetFromServer("objecttypes"));
    ui.assetOpenButtons.portrait.addEventListener("click", () => openAssetFromServer("portrait"));
    ui.assetSaveChars.addEventListener("click", () => saveAssetToServer("charset"));
    ui.assetSaveTiles.addEventListener("click", () => saveAssetToServer("tiles"));
    ui.assetSaveMap.addEventListener("click", () => saveAssetToServer("map"));
    ui.assetSaveObjectTypes.addEventListener("click", () => saveAssetToServer("objecttypes"));
    ui.assetSavePortrait.addEventListener("click", () => saveAssetToServer("portrait"));
    ["charset", "tiles", "map", "objecttypes", "portrait"].forEach((kind) => {
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

    ui.objectTypesFile.addEventListener("change", async () => {
      const file = ui.objectTypesFile.files[0];
      if (!file) return;
      importObjectTypes(await file.arrayBuffer());
      ui.objectTypesFile.value = "";
    });

    ui.portraitFile.addEventListener("change", async () => {
      const file = ui.portraitFile.files[0];
      if (!file) return;
      importPortrait(await file.arrayBuffer());
      ui.portraitFile.value = "";
    });

    ui.portraitPngFile.addEventListener("change", async () => {
      const file = ui.portraitPngFile.files[0];
      if (!file) return;
      await importPortraitPng(file);
      ui.portraitPngFile.value = "";
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

    state.objectTypes[1].width = 1;
    state.objectTypes[1].height = 1;
    state.objectTypes[1].name = "OBJECT 1";
    state.objectTypes[1].chars[0] = 1;
    state.objectTypes[1].colors[0] = 1;
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
