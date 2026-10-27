Work on the web frontend or web server.

Context for working on web features:

## Frontend
- Location: `resources/web/index.html` (single-file SPA, vanilla JS)
- Resource file: `resources/web.qrc` (must list any new files added)
- FT8/FT4 module: `resources/ft8ts/dist/ft8ts.mjs`
- No build step for frontend - just edit and rebuild the Qt project
- After editing web files, rebuild: `qmake wfweb.pro && make -j$(nproc)`

## Backend
- Server: `src/webserver.cpp` / `include/webserver.h`
- Runs in separate QThread, initialized in `src/servermain.cpp` (search for `webServer`)
- Content types for new file extensions must be added in `src/webserver.cpp` (search for `contentType` in `serveStaticFile`)

## WebSocket JSON Protocol
- Commands from browser: `{"cmd":"setFrequency","value":14074000}`
- State updates to browser: `{"cmd":"rigStatus","frequency":14074000,...}`
- Audio opt-in: `{"cmd":"enableAudio","value":true}`

## cachingQueue API
See "Critical API Patterns" section in CLAUDE.md.

## Testing
- Start wfweb, open browser to https://localhost:8080
- REST API available on HTTP port 8081 (see REST_API.md)

Now address the user's request regarding the web interface.
