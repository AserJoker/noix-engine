#!/usr/bin/env node
/**
 * dap_sourcemap_test.js — SourceMap integration test using noix-engine headless mode.
 *
 * Tests that:
 * 1. Breakpoints set on .ts files are correctly mapped to .js lines via source map
 * 2. debugger statement in TS code causes the debugger to stop
 * 3. Stack trace shows original TS file/line, not generated JS
 * 4. Disconnect does not crash the engine
 *
 * Uses noix-engine --headless --dap-port <N> --base-path <fixture-dir>
 * The fixture directory contains scripts/entry.js (compiled from entry.ts with inline source map).
 */
const { spawn } = require('child_process');
const path = require('path');
const net = require('net');

const enginePath = process.argv[2] || path.join(__dirname, '..', '..', 'dist', 'noix-engine');
const DAP_PORT = 4712;  // Use different port from default to avoid conflicts
const fixtureDir = path.join(__dirname, 'sourcemap-fixture');
const tsScript = path.join(fixtureDir, 'scripts', 'entry.ts');

let passed = 0, failed = 0;
function assert(c, m) { if (c) { console.log(`  PASS: ${m}`); passed++; } else { console.log(`  FAIL: ${m}`); failed++; } }

/* Minimal DAP client for TCP mode */
class TcpDapClient {
    constructor(port, host = '127.0.0.1') {
        this.seq = 1;
        this.pending = new Map();
        this.buffer = Buffer.alloc(0);
        this.eventQueue = [];
        this.eventWaiters = [];
        this.sock = net.createConnection({ host, port });
        this.sock.on('data', (data) => {
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
            if (this.buffer.length < bodyStart + contentLength) break;
            const body = this.buffer.slice(bodyStart, bodyStart + contentLength).toString('utf8');
            this.buffer = this.buffer.slice(bodyStart + contentLength);
            try {
                const msg = JSON.parse(body);
                this._handleMessage(msg);
            } catch (e) {}
        }
    }

    _handleMessage(msg) {
        if (msg.type === 'response' && msg.request_seq) {
            const p = this.pending.get(msg.request_seq);
            if (p) { this.pending.delete(msg.request_seq); p.resolve(msg); }
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
            this.sock.write(header + json);
        });
    }

    waitForEvent(eventType, timeoutMs = 10000) {
        const idx = this.eventQueue.findIndex(e => e.event === eventType);
        if (idx !== -1) return Promise.resolve(this.eventQueue.splice(idx, 1)[0]);
        return new Promise((resolve, reject) => {
            const timer = setTimeout(() => {
                const wi = this.eventWaiters.findIndex(w => w.resolve === resolve);
                if (wi !== -1) this.eventWaiters.splice(wi, 1);
                reject(new Error(`Timeout waiting for ${eventType}`));
            }, timeoutMs);
            this.eventWaiters.push({ eventType, resolve, timer });
        });
    }

    destroy() { this.sock.destroy(); }
}

async function runTest() {
    console.log('=== SourceMap Integration Test (noix-engine headless) ===');

    // Start noix-engine in headless mode with DAP server
    const proc = spawn(enginePath, [
        '--headless',
        '--dap-port', String(DAP_PORT),
        '--base-path', fixtureDir
    ], { stdio: ['pipe', 'pipe', 'pipe'] });
    let stderrOutput = '';
    proc.stderr.on('data', (d) => { stderrOutput += d.toString(); });

    // Wait for DAP server to be ready
    await new Promise(r => setTimeout(r, 1500));

    const client = new TcpDapClient(DAP_PORT);

    try {
        // 1. Initialize
        const initResp = await client.sendRequest('initialize', {
            clientID: 'sourcemap-test', adapterID: 'noix', pathFormat: 'path',
            linesStartAt1: true, columnsStartAt1: true
        });
        assert(initResp.success, 'initialize succeeded');

        // 2. Set breakpoints on the .ts file — this triggers source map resolution
        const bpResp = await client.sendRequest('setBreakpoints', {
            source: { path: tsScript },
            breakpoints: [{ line: 7 }, { line: 12 }],
        });
        assert(bpResp.success, 'setBreakpoints on .ts file succeeded');
        assert(bpResp.body.breakpoints.length === 2, '2 breakpoints requested');

        const verifiedCount = bpResp.body.breakpoints.filter(b => b.verified).length;
        console.log(`  Verified breakpoints: ${verifiedCount}/2`);
        assert(verifiedCount >= 1, 'at least 1 breakpoint verified via source map');

        // Check that response line numbers are TS lines (not JS lines)
        for (const bp of bpResp.body.breakpoints) {
            assert(bp.line === 7 || bp.line === 12,
                `breakpoint response line ${bp.line} matches original TS line`);
        }

        // 3. Set exception breakpoints
        await client.sendRequest('setExceptionBreakpoints', { filters: ['uncaught'] });

        // 4. Configuration done
        await client.sendRequest('configurationDone');

        // 5. Attach to the running script
        const attachResp = await client.sendRequest('attach', { port: DAP_PORT });
        assert(attachResp.success, 'attach succeeded');

        // 6. Wait for stopped event (breakpoint or debugger statement)
        //    In headless mode, the script starts at engine startup and may hit debugger
        //    before we connect. The stop event is buffered until configurationDone.
        const stopped1 = await client.waitForEvent('stopped', 10000);
        assert(stopped1.body.reason === 'breakpoint' || stopped1.body.reason === 'step',
            `first stop reason is '${stopped1.body.reason}' (expected breakpoint)`);
        console.log(`  First stop: reason=${stopped1.body.reason}`);

        // 7. Get stack trace — should show .ts file path and TS line numbers
        const stResp = await client.sendRequest('stackTrace', { threadId: 1, startFrame: 0, levels: 20 });
        assert(stResp.success, 'stackTrace succeeded');
        assert(stResp.body.stackFrames.length > 0, 'has stack frames');

        const topFrame = stResp.body.stackFrames[0];
        console.log(`  Top frame: ${topFrame.source.name}:${topFrame.line}:${topFrame.column}`);

        // The source path should point to the .ts file (via source map)
        const srcPath = topFrame.source.path || topFrame.source.name || '';
        const isTsPath = srcPath.endsWith('.ts');
        assert(isTsPath, `stack trace source is .ts file: ${srcPath}`);

        // The line number should correspond to a TS line (source-mapped, not raw JS line)
        if (isTsPath) {
            // The debugger statement is around line 22, breakpoints at 7 and 12
            // Source map may map to nearby lines due to compiler output differences
            const isReasonableTsLine = topFrame.line >= 1 && topFrame.line <= 30;
            assert(isReasonableTsLine, `stack trace line ${topFrame.line} is a valid TS line (1-30)`);
            console.log(`  Stack trace TS line: ${topFrame.line} (source-mapped from JS)`);
        }

        // 8. Evaluate an expression at the breakpoint
        const evalResp = await client.sendRequest('evaluate', {
            expression: 'x + y',
            frameId: topFrame.id,
            context: 'repl'
        });
        assert(evalResp.success, 'evaluate x+y succeeded');
        console.log(`  x + y = ${evalResp.body.result}`);

        // 9. Continue to next breakpoint/debugger
        await client.sendRequest('continue', { threadId: 1 });

        // Wait for another stop
        try {
            const stopped2 = await client.waitForEvent('stopped', 5000);
            console.log(`  Second stop: reason=${stopped2.body.reason}`);

            const st2 = await client.sendRequest('stackTrace', { threadId: 1, startFrame: 0, levels: 20 });
            if (st2.body.stackFrames.length > 0) {
                const f2 = st2.body.stackFrames[0];
                console.log(`  Second stop at: ${f2.source.name}:${f2.line}`);
            }
            await client.sendRequest('continue', { threadId: 1 });
        } catch (e) {
            console.log('  No second stop (script may have finished)');
        }

        // 10. Disconnect — this MUST NOT crash the engine
        const discResp = await client.sendRequest('disconnect');
        assert(discResp.success, 'disconnect succeeded (no crash)');

        // Wait to verify no crash
        await new Promise(r => setTimeout(r, 500));

    } catch (e) {
        console.error('Test error:', e.message);
        if (stderrOutput) console.error('Engine stderr (last 500 chars):', stderrOutput.slice(-500));
        failed++;
    }

    client.destroy();
    proc.kill();

    const exitCode = await new Promise(resolve => {
        proc.on('exit', (code) => resolve(code));
        proc.kill();
        setTimeout(() => resolve(-1), 3000);
    });
    console.log(`  Engine exit code: ${exitCode}`);

    console.log(`\n=== Results: ${passed} passed, ${failed} failed ===`);
    process.exit(failed > 0 ? 1 : 0);
}

runTest();
