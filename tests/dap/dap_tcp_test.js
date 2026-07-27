#!/usr/bin/env node
/**
 * Test DAP over TCP socket (--port mode).
 *
 * Starts dap-debug-bridge with --port, then connects
 * via Node.js net.Socket to exercise the full DAP protocol.
 */
const { spawn } = require('child_process');
const net = require('net');
const path = require('path');

const bridgePath = process.argv[2] || path.join(__dirname, '..', '..', 'dist', 'dap-debug-bridge');
const TEST_PORT = 4711;

let passed = 0, failed = 0;
function assert(c, m) { if (c) { console.log(`  PASS: ${m}`); passed++; } else { console.log(`  FAIL: ${m}`); failed++; } }

/**
 * DAP client over a TCP socket (instead of child process stdio).
 */
class DapTcpClient {
    constructor(socket) {
        this.socket = socket;
        this.seq = 1;
        this.pending = new Map();
        this.buffer = Buffer.alloc(0);
        this.eventQueue = [];
        this.eventWaiters = [];

        socket.on('data', (data) => {
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
            this.socket.write(header + json);
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

    destroy() {
        this.socket.destroy();
    }
}

async function runTest() {
    console.log('=== DAP TCP Transport Test ===');

    const scriptPath = path.join(__dirname, 'scripts', 'dap_test.js');
    const proc = spawn(bridgePath, ['--port', String(TEST_PORT), '--script', scriptPath], {
        stdio: ['pipe', 'pipe', 'pipe']
    });

    /* Collect stderr for debugging */
    proc.stderr.on('data', (d) => process.stderr.write(d));

    /* Wait a moment for the server to start listening */
    await new Promise(r => setTimeout(r, 500));

    /* Connect to the DAP bridge via TCP */
    const socket = await new Promise((resolve, reject) => {
        const s = net.connect(TEST_PORT, '127.0.0.1', () => resolve(s));
        s.on('error', reject);
    });

    const client = new DapTcpClient(socket);

    try {
        /* initialize */
        const init = await client.sendRequest('initialize', {
            clientID: 'test-tcp', adapterID: 'noix', pathFormat: 'path'
        });
        assert(init.success, 'initialize succeeded');

        /* setBreakpoints */
        const bp = await client.sendRequest('setBreakpoints', {
            source: { path: scriptPath },
            breakpoints: [{ line: 8 }, { line: 13 }]
        });
        assert(bp.success, 'setBreakpoints succeeded');
        assert(bp.body.breakpoints.length === 2, '2 breakpoints set');
        assert(bp.body.breakpoints[0].verified, 'bp1 verified');
        assert(bp.body.breakpoints[1].verified, 'bp2 verified');

        /* setExceptionBreakpoints */
        const exbp = await client.sendRequest('setExceptionBreakpoints', {
            filters: ['uncaught']
        });
        assert(exbp.success, 'setExceptionBreakpoints succeeded');

        /* launch */
        const launch = await client.sendRequest('launch', {
            script: scriptPath, stopOnEntry: false
        });
        assert(launch.success, 'launch succeeded');

        /* Wait for first stopped event */
        const stopped1 = await client.waitForEvent('stopped');
        assert(stopped1.body.reason === 'breakpoint', 'first stopped reason is breakpoint');

        /* stackTrace */
        const st = await client.sendRequest('stackTrace', {
            threadId: 1, startFrame: 0, levels: 5
        });
        assert(st.success, 'stackTrace succeeded');
        assert(st.body.stackFrames.length > 0, 'has stack frames');
        const topFrame = st.body.stackFrames[0];
        console.log(`  Top frame: ${topFrame.name} at ${topFrame.line}`);

        /* scopes */
        const sc = await client.sendRequest('scopes', { frameId: topFrame.id });
        assert(sc.success, 'scopes succeeded');
        assert(sc.body.scopes.length > 0, 'has scopes');

        /* variables */
        const vr = sc.body.scopes[0].variablesReference;
        const va = await client.sendRequest('variables', { variablesReference: vr });
        assert(va.success, 'variables succeeded');
        assert(va.body.variables.length > 0, 'has variables');
        console.log('  Variables:', va.body.variables.map(v => v.name).join(', '));

        /* evaluate — test an expression that works in the current frame */
        const ev = await client.sendRequest('evaluate', {
            expression: '1 + 2', frameId: topFrame.id, context: 'repl'
        });
        assert(ev.success, 'evaluate 1+2 succeeded');
        assert(ev.body.result === '3', '1 + 2 = 3');
        console.log(`  1 + 2 = ${ev.body.result}`);

        /* continue */
        const cont = await client.sendRequest('continue', { threadId: 1 });
        assert(cont.success, 'continue succeeded');

        /* Wait for second stopped event */
        const stopped2 = await client.waitForEvent('stopped');
        console.log(`  Second stop: reason=${stopped2.body.reason}`);

        /* next (step over) */
        const nxt = await client.sendRequest('next', { threadId: 1 });
        assert(nxt.success, 'next succeeded');

        /* threads */
        const th = await client.sendRequest('threads');
        assert(th.success, 'threads succeeded');
        assert(th.body.threads.length === 1, 'single thread');
        assert(th.body.threads[0].name === 'Main Thread', 'thread name is Main Thread');

        /* continue to finish */
        await client.sendRequest('continue', { threadId: 1 });
        try { await client.waitForEvent('terminated', 5000); } catch (e) {}

        /* disconnect */
        const disc = await client.sendRequest('disconnect');
        assert(disc.success, 'disconnect succeeded');

    } catch (e) {
        console.error('Error:', e.message);
        failed++;
    }

    client.destroy();
    proc.kill();

    console.log(`\n=== Results: ${passed} passed, ${failed} failed ===`);
    process.exit(failed > 0 ? 1 : 0);
}

runTest();
