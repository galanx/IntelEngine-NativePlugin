/**
 * Dual-mode transport layer for IntelEngine Dashboard.
 *
 * Detects whether running inside PrismaUI (in-game overlay) or a web browser,
 * and provides a unified API for state reception and action dispatch.
 *
 * PrismaUI mode: Uses window globals injected by RegisterJSListener.
 * Browser mode:  Connects via WebSocket for real-time push/pull.
 */

let ws = null;
let reconnectTimer = null;
let callbacks = { onFullState: null, onSlots: null };

/**
 * Detect PrismaUI by checking for its injected JS listeners.
 * PrismaUI calls RegisterJSListener which creates globals like
 * window.onCloseDashboard before React mounts. A plain browser
 * (even via file://) won't have these.
 */
export function isPrismaUI() {
  return typeof window.onCloseDashboard === 'function';
}

/**
 * Initialize the transport. Call once on app mount.
 * Returns { mode: 'prisma' | 'browser' }.
 */
export function initTransport({ onFullState, onSlots }) {
  callbacks.onFullState = onFullState;
  callbacks.onSlots = onSlots;

  if (isPrismaUI()) {
    // PrismaUI mode: wire up window globals (overwrite the index.jsx buffers)
    window.updateFullState = (json) => onFullState(json);
    window.updateSlots = (json) => onSlots(json);

    // Drain any buffered calls from before React mounted
    const pending = window.__pendingInteropCalls || [];
    for (const call of pending) {
      if (call.name === 'updateFullState') onFullState(call.json);
      else if (call.name === 'updateSlots') onSlots(call.json);
    }
    window.__pendingInteropCalls = [];

    return { mode: 'prisma' };
  }

  // Browser mode: discover WS port from HTTP server, then connect
  connectBrowser();
  return { mode: 'browser' };
}

function connectBrowser() {
  // From file:// we can't fetch — use default port directly.
  // From http:// we can discover the WS port via the config endpoint.
  if (window.location.protocol === 'file:') {
    connectWebSocket(9876);
    return;
  }

  fetch(`${window.location.origin}/api/config`)
    .then(r => r.json())
    .then(config => {
      connectWebSocket(config.wsPort || 9876);
    })
    .catch(() => {
      connectWebSocket(9876);
    });
}

function connectWebSocket(port) {
  if (ws) {
    try { ws.close(); } catch (e) { /* ignore */ }
  }

  const host = window.location.hostname || 'localhost';
  ws = new WebSocket(`ws://${host}:${port}`);

  ws.onmessage = (event) => {
    try {
      const msg = JSON.parse(event.data);
      if (msg.type === 'fullState' && callbacks.onFullState) {
        // Data arrives pre-parsed as JSON object from the C++ envelope
        const data = typeof msg.data === 'string' ? msg.data : JSON.stringify(msg.data);
        callbacks.onFullState(data);
      } else if (msg.type === 'slots' && callbacks.onSlots) {
        const data = typeof msg.data === 'string' ? msg.data : JSON.stringify(msg.data);
        callbacks.onSlots(data);
      }
    } catch (e) {
      console.error('[WS] Failed to parse message:', e);
    }
  };

  ws.onclose = () => {
    // Auto-reconnect after 2 seconds
    if (reconnectTimer) clearTimeout(reconnectTimer);
    reconnectTimer = setTimeout(connectBrowser, 2000);
  };

  ws.onerror = () => {
    // onclose will fire after onerror, triggering reconnect
  };
}

/**
 * Send an action to the game.
 * In PrismaUI mode, calls the injected window global.
 * In browser mode, sends over WebSocket.
 */
export function sendAction(action, payload = {}) {
  if (isPrismaUI()) {
    const fn = window[`onDashboard_${action}`];
    if (fn) fn(JSON.stringify(payload));
  } else if (ws && ws.readyState === WebSocket.OPEN) {
    ws.send(JSON.stringify({ action, ...payload }));
  }
}

/**
 * Clean up transport resources.
 */
export function destroyTransport() {
  if (reconnectTimer) clearTimeout(reconnectTimer);
  if (ws) {
    try { ws.close(); } catch (e) { /* ignore */ }
    ws = null;
  }
  if (isPrismaUI()) {
    window.updateFullState = () => {};
    window.updateSlots = () => {};
  }
}
