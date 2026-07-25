/**
 * dap_client.js — Shared DAP protocol client for test scripts.
 *
 * Usage:
 *   const { DapClient } = require('./dap_client');
 *   const proc = spawn(bridgePath, ['--script', scriptPath], { stdio: ['pipe', 'pipe', 'pipe'] });
 *   const client = new DapClient(proc);
 */

class DapClient {
    constructor(proc) {
        this.proc = proc;
        this.seq = 1;
        this.pending = new Map();
        this.buffer = Buffer.alloc(0);
        this.eventQueue = [];
        this.eventWaiters = [];

        proc.stdout.on('data', (data) => {
            this.buffer = Buffer.concat([this.buffer, data]);
            this._processBuffer();
        });
    }

    _processBuffer() {
        while (true) {
            const headerEnd = this.buffer.indexOf('\r\n\r\n');
            if (headerEnd === -1) break;

            const headerStr = this.buffer.slice(0, headerEnd).toString('utf8');
            const match = headerStr.match(/Content-Length:\s*(\d+)/i);
            if (!match) break;

            const contentLength = parseInt(match[1], 10);
            const bodyStart = headerEnd + 4;
            const bodyEnd = bodyStart + contentLength;

            if (this.buffer.length < bodyEnd) break;

            const body = this.buffer.slice(bodyStart, bodyEnd).toString('utf8');
            this.buffer = this.buffer.slice(bodyEnd);

            try {
                const msg = JSON.parse(body);
                this._handleMessage(msg);
            } catch (e) {
                console.error('Failed to parse DAP message:', e.message);
            }
        }
    }

    _handleMessage(msg) {
        if (msg.type === 'response' && msg.request_seq) {
            const p = this.pending.get(msg.request_seq);
            if (p) {
                this.pending.delete(msg.request_seq);
                p.resolve(msg);
            }
        } else if (msg.type === 'event') {
            const idx = this.eventWaiters.findIndex(w => w.eventType === msg.event);
            if (idx !== -1) {
                const w = this.eventWaiters.splice(idx, 1)[0];
                clearTimeout(w.timer);
                w.resolve(msg);
            } else {
                this.eventQueue.push(msg);
            }
        }
    }

    sendRequest(command, args = {}) {
        return new Promise((resolve) => {
            const seq = this.seq++;
            const msg = { seq, type: 'request', command, arguments: args };
            this.pending.set(seq, { resolve, command });
            const json = JSON.stringify(msg);
            const header = `Content-Length: ${Buffer.byteLength(json)}\r\n\r\n`;
            this.proc.stdin.write(header + json);
        });
    }

    waitForEvent(eventType, timeoutMs = 10000) {
        const idx = this.eventQueue.findIndex(e => e.event === eventType);
        if (idx !== -1) {
            return Promise.resolve(this.eventQueue.splice(idx, 1)[0]);
        }
        return new Promise((resolve, reject) => {
            const timer = setTimeout(() => {
                const wi = this.eventWaiters.findIndex(w => w.resolve === resolve);
                if (wi !== -1) this.eventWaiters.splice(wi, 1);
                reject(new Error(`Timeout waiting for ${eventType}`));
            }, timeoutMs);
            this.eventWaiters.push({ eventType, resolve, timer });
        });
    }
}

module.exports = { DapClient };
