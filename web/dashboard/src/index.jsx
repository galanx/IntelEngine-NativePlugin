import React from 'react';
import { createRoot } from 'react-dom/client';
import App from './App';
import './styles/dashboard.css';

window.__intelengine_errors = [];
window.onerror = (msg, src, line, col) => {
  window.__intelengine_errors.push(`${msg} at ${src}:${line}:${col}`);
};

// Buffer interop calls from C++ that arrive before React mounts.
const pendingCalls = [];
window.updateSlots = (json) => pendingCalls.push({ name: 'updateSlots', json });
window.updateFullState = (json) => pendingCalls.push({ name: 'updateFullState', json });
window.__pendingInteropCalls = pendingCalls;

const container = document.getElementById('dashboard-root');
const root = createRoot(container);
root.render(<App />);
