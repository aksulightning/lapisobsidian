// SPDX-License-Identifier: GPL-3.0-only
import type {Settings} from './settings';
export interface Transport {
  connect(host: string, port: number): Promise<void>;
  send(data: Uint8Array): void;
  close(): void;
  onData: (data: Uint8Array) => void;
  onClose: () => void;
  onError: (error: Error) => void;
}
export type Bootstrap = {origin: string; token: string};
declare global { interface Window { __LAPIS_BOOTSTRAP__?: Bootstrap } }
const messages: Record<string, string> = {
  PROTOCOL_NOT_READY: 'Remote gameplay is not available in Milestone 1.',
  AUTH_FAILED: 'Local authentication failed. Restart the application.',
  CLIENT_BUSY: 'Another window is using this local bridge.',
  INVALID_TARGET: 'Check the server address and port.',
  RATE_LIMIT: 'The local connection sent too many commands.',
};
export class BridgeTransport implements Transport {
  onPreferences = (_settings: unknown): void => {};
  onData = (_data: Uint8Array): void => {};
  onClose = (): void => {};
  onError = (_error: Error): void => {};
  private socket?: WebSocket;
  private heartbeat?: number;
  private deadline?: number;
  private preferencesTimer?: number;
  private queuedPreferences?: Settings;
  private ready = false;
  private connected = false;
  private pending?: {resolve: () => void; reject: (e: Error) => void};
  constructor(private status: (status: string, state: 'ready' | 'error' | 'starting') => void) {}
  start(bootstrap: Bootstrap): void {
    const origin = new URL(bootstrap.origin);
    if (origin.protocol !== 'http:' || origin.hostname !== '127.0.0.1' || origin.origin !== location.origin || !/^[a-f0-9]{64}$/.test(bootstrap.token)) throw new Error('Invalid local bridge bootstrap.');
    const socket = new WebSocket(`ws://${origin.host}/bridge`);
    this.socket = socket;
    socket.binaryType = 'arraybuffer';
    this.deadline = window.setTimeout(() => this.fail('Local bridge did not respond. Restart the application.'), 5000);
    socket.onopen = () => { socket.send(JSON.stringify({type: 'AUTH', token: bootstrap.token})); bootstrap.token = ''; };
    socket.onmessage = event => {
      if (event.data instanceof ArrayBuffer) {
        if (!this.connected || event.data.byteLength > 8192) return this.fail('Unexpected bridge data.');
        this.onData(new Uint8Array(event.data)); return;
      }
      if (typeof event.data !== 'string' || event.data.length > 1024) return this.fail('Invalid local bridge response.');
      try {
        const message = JSON.parse(event.data) as {type?: string; code?: string; version?: number; value?: unknown};
        switch (message.type) {
          case 'AUTH_OK':
            if (this.ready || message.version !== 1) return this.fail('Incompatible local bridge.');
            this.ready = true; clearTimeout(this.deadline); this.status('Bridge ready', 'ready'); this.flushPreferences();
            this.heartbeat = window.setInterval(() => { if (socket.readyState === WebSocket.OPEN) socket.send('{"type":"PING"}'); }, 10000); break;
          case 'PONG': case 'SETTINGS_OK': break;
          case 'PREFERENCES': if (!this.ready) return this.fail('Invalid settings response.'); if (!this.queuedPreferences) this.onPreferences(message.value); break;
          case 'CONNECTED':
            if (!this.pending) return this.fail('Unexpected connection response.');
            this.connected = true; this.pending.resolve(); this.pending = undefined; clearTimeout(this.deadline); break;
          case 'DISCONNECTED': this.connected = false; this.pending?.reject(new Error('Disconnected')); this.pending = undefined; this.onClose(); break;
          case 'ERROR': {
            const error = new Error(messages[message.code ?? ''] ?? 'The local bridge rejected this request.');
            this.pending?.reject(error); this.pending = undefined; clearTimeout(this.deadline); this.onError(error); break;
          }
          default: this.fail('Unknown local bridge response.');
        }
      } catch { this.fail('Malformed local bridge response.'); }
    };
    socket.onerror = () => this.fail('Cannot reach the local bridge. Restart the application.');
    socket.onclose = () => { this.ready = false; this.connected = false; this.clearTimers(); this.pending?.reject(new Error('Local bridge closed.')); this.pending = undefined; this.status('Disconnected', 'error'); this.onClose(); };
  }
  savePreferences(settings: Settings): void {
    this.queuedPreferences={...settings}; clearTimeout(this.preferencesTimer);
    this.preferencesTimer=window.setTimeout(()=>this.flushPreferences(),250);
  }
  private flushPreferences(): void {
    if (this.queuedPreferences && this.ready && this.socket?.readyState === WebSocket.OPEN) {
      this.socket.send(JSON.stringify({type:'SETTINGS', value:this.queuedPreferences})); this.queuedPreferences=undefined;
    }
  }
  connect(host: string, port: number): Promise<void> {
    if (!this.ready || !this.socket || this.pending || this.connected) return Promise.reject(new Error('Bridge is not ready for a connection.'));
    if (!host || host.length > 253 || !Number.isInteger(port) || port < 1 || port > 65535) return Promise.reject(new Error('Invalid server address or port.'));
    return new Promise((resolve, reject) => {
      this.pending = {resolve, reject};
      this.socket!.send(JSON.stringify({type: 'CONNECT', host, port}));
      this.deadline = window.setTimeout(() => this.fail('Connection timed out.'), 10000);
    });
  }
  send(data: Uint8Array): void {
    if (!this.connected || !this.socket || this.socket.readyState !== WebSocket.OPEN || data.length > 8192 || this.socket.bufferedAmount > 65536) throw new Error('Transport cannot send data.');
    this.socket.send(data.slice().buffer);
  }
  close(): void { this.flushPreferences(); clearTimeout(this.preferencesTimer); this.clearTimers(); this.socket?.close(1000); }
  private clearTimers(): void { clearTimeout(this.deadline); clearInterval(this.heartbeat); }
  private fail(message: string): void { this.onError(new Error(message)); this.status('Bridge error', 'error'); this.close(); }
}
