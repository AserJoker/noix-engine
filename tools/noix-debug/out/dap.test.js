"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
const assert = require("assert");
const debugadapter_testsupport_1 = require("@vscode/debugadapter-testsupport");
const path = require("path");
const child_process_1 = require("child_process");
const net = require("net");
const BRIDGE_EXE = path.resolve(__dirname, '../../../dist/dap-debug-bridge.exe');
const SCRIPT_PATH = path.resolve(__dirname, '../../../dist/scripts/test_debug.js');
const ADVANCED_SCRIPT = path.resolve(__dirname, '../../../dist/scripts/test_debug_advanced.js');
const INIT_ARGS = {
    clientID: 'test',
    adapterID: 'noix',
    pathFormat: 'path',
    linesStartAt1: true,
    columnsStartAt1: true,
};
function findFreePort() {
    return new Promise((resolve, reject) => {
        const server = net.createServer();
        server.listen(0, '127.0.0.1', () => {
            const port = server.address().port;
            server.close(() => resolve(port));
        });
        server.on('error', reject);
    });
}
class SafeDebugClient extends debugadapter_testsupport_1.DebugClient {
    start(port) {
        return new Promise((resolve, reject) => {
            const socket = net.createConnection(port, '127.0.0.1', () => {
                socket.on('error', (err) => {
                    if (err.code !== 'ECONNRESET' && err.code !== 'EPIPE')
                        throw err;
                });
                this.connect(socket, socket);
                resolve();
            });
            socket.on('error', (err) => {
                if (err.code !== 'ECONNRESET' && err.code !== 'EPIPE')
                    reject(err);
            });
        });
    }
}
async function withSession(scriptPath, testFn) {
    const port = await findFreePort();
    const proc = await new Promise((resolve, reject) => {
        const p = (0, child_process_1.spawn)(BRIDGE_EXE, ['--port', String(port), '--script', scriptPath], {
            stdio: ['pipe', 'pipe', 'pipe'],
        });
        let started = false;
        p.stderr.on('data', (data) => {
            const msg = data.toString();
            if (msg.includes('[DAP]'))
                process.stderr.write('[BRIDGE] ' + msg);
            if (!started && msg.includes('listening on port')) {
                started = true;
                resolve(p);
            }
        });
        p.on('error', (err) => { if (!started)
            reject(err); });
        setTimeout(() => { if (!started) {
            p.kill();
            reject(new Error('Bridge start timeout'));
        } }, 5000);
    });
    const dc = new SafeDebugClient('node', 'dummy', 'noix');
    try {
        await dc.start(port);
        await testFn(dc, port);
    }
    finally {
        try {
            if (!proc.killed)
                proc.kill();
        }
        catch { /* ignore */ }
        await new Promise(r => { proc.on('exit', () => setTimeout(r, 200)); setTimeout(r, 2000); });
    }
}
/** Initialize + attach, return the stopped event body from debugger statement */
async function initAndAttach(dc, port) {
    const stoppedPromise = dc.waitForEvent('stopped');
    await dc.initializeRequest(INIT_ARGS);
    await dc.attachRequest({ port });
    await dc.configurationDoneRequest();
    const stopped = await stoppedPromise;
    return stopped.body;
}
/* ============================================================
 *  DAP Debug Bridge — Comprehensive Test Suite
 * ============================================================ */
describe('DAP Debug Bridge', function () {
    this.timeout(30000);
    /* ============================================================
     *  1. BASIC FLOW
     * ============================================================ */
    it('should stop on debugger statement and continue', async () => {
        await withSession(SCRIPT_PATH, async (dc, port) => {
            const body = await initAndAttach(dc, port);
            assert.strictEqual(body.reason, 'entry');
            await dc.continueRequest({ threadId: 1 });
            await dc.waitForEvent('terminated');
        });
    });
    it('should report stackTrace with correct line number', async () => {
        await withSession(SCRIPT_PATH, async (dc, port) => {
            await initAndAttach(dc, port);
            const resp = await dc.stackTraceRequest({ threadId: 1, startFrame: 0, levels: 20 });
            const top = resp.body.stackFrames[0];
            assert.strictEqual(top.line, 1);
            assert.ok(top.name, 'Frame should have a name');
            assert.ok(top.source, 'Frame should have a source');
            await dc.continueRequest({ threadId: 1 });
            await dc.waitForEvent('terminated');
        });
    });
    it('should evaluate expression while paused', async () => {
        await withSession(SCRIPT_PATH, async (dc, port) => {
            await initAndAttach(dc, port);
            const resp = await dc.evaluateRequest({ expression: '1 + 2', context: 'repl', frameId: 0 });
            assert.strictEqual(resp.body.result, '3');
            await dc.continueRequest({ threadId: 1 });
            await dc.waitForEvent('terminated');
        });
    });
    /* ============================================================
     *  2. STEPPING
     * ============================================================ */
    it('should step over (next) from debugger statement', async () => {
        await withSession(SCRIPT_PATH, async (dc, port) => {
            await initAndAttach(dc, port);
            const stoppedPromise = dc.waitForEvent('stopped');
            await dc.nextRequest({ threadId: 1 });
            const stepEvent = await stoppedPromise;
            assert.strictEqual(stepEvent.body.reason, 'step');
            const resp = await dc.stackTraceRequest({ threadId: 1 });
            assert.ok(resp.body.stackFrames[0].line > 1, 'Should have moved past line 1');
            await dc.continueRequest({ threadId: 1 });
            await dc.waitForEvent('terminated');
        });
    });
    it('should step into function call', async () => {
        await withSession(SCRIPT_PATH, async (dc, port) => {
            await initAndAttach(dc, port);
            let enteredFunction = false;
            for (let i = 0; i < 50; i++) {
                const stoppedPromise = dc.waitForEvent('stopped');
                await dc.stepInRequest({ threadId: 1 });
                await stoppedPromise;
                const stack = await dc.stackTraceRequest({ threadId: 1 });
                if (stack.body.stackFrames.length > 1) {
                    enteredFunction = true;
                    break;
                }
            }
            assert.ok(enteredFunction, 'Should have stepped into a function within 50 steps');
            await dc.continueRequest({ threadId: 1 });
            await dc.waitForEvent('terminated');
        });
    });
    it('should step out of function', async () => {
        await withSession(SCRIPT_PATH, async (dc, port) => {
            await initAndAttach(dc, port);
            let enteredFunction = false;
            for (let i = 0; i < 30; i++) {
                const stoppedPromise = dc.waitForEvent('stopped');
                await dc.stepInRequest({ threadId: 1 });
                await stoppedPromise;
                const stack = await dc.stackTraceRequest({ threadId: 1 });
                if (stack.body.stackFrames.length > 1) {
                    enteredFunction = true;
                    const depthBefore = stack.body.stackFrames.length;
                    const outPromise = dc.waitForEvent('stopped');
                    await dc.stepOutRequest({ threadId: 1 });
                    const outEvent = await outPromise;
                    assert.strictEqual(outEvent.body.reason, 'step');
                    const stackAfter = await dc.stackTraceRequest({ threadId: 1 });
                    assert.ok(stackAfter.body.stackFrames.length < depthBefore, 'Stack should be shallower after step out');
                    break;
                }
            }
            assert.ok(enteredFunction, 'Should have entered a function to test step out');
            await dc.continueRequest({ threadId: 1 });
            await dc.waitForEvent('terminated');
        });
    });
    it('should step over at top level (stay at depth 1)', async () => {
        await withSession(ADVANCED_SCRIPT, async (dc, port) => {
            await initAndAttach(dc, port);
            const stoppedPromise = dc.waitForEvent('stopped');
            await dc.nextRequest({ threadId: 1 });
            const stepEvent = await stoppedPromise;
            assert.strictEqual(stepEvent.body.reason, 'step');
            const stack = await dc.stackTraceRequest({ threadId: 1 });
            assert.strictEqual(stack.body.stackFrames.length, 1, 'Should stay at top level');
            await dc.continueRequest({ threadId: 1 });
            await dc.waitForEvent('terminated');
        });
    });
    /* ============================================================
     *  3. BREAKPOINTS
     * ============================================================ */
    it('should set and hit a line breakpoint (set before attach)', async () => {
        await withSession(ADVANCED_SCRIPT, async (dc, port) => {
            const stoppedPromise = dc.waitForEvent('stopped');
            await dc.initializeRequest(INIT_ARGS);
            const bpResp = await dc.setBreakpointsRequest({
                source: { path: ADVANCED_SCRIPT },
                breakpoints: [{ line: 7 }],
            });
            assert.strictEqual(bpResp.body.breakpoints.length, 1);
            assert.strictEqual(bpResp.body.breakpoints[0].verified, true);
            await dc.attachRequest({ port });
            await dc.configurationDoneRequest();
            await stoppedPromise;
            const stopped2Promise = dc.waitForEvent('stopped');
            await dc.continueRequest({ threadId: 1 });
            const stopped2 = await stopped2Promise;
            assert.strictEqual(stopped2.body.reason, 'breakpoint');
            await dc.continueRequest({ threadId: 1 });
            await dc.waitForEvent('terminated');
        });
    });
    it('should set and hit a line breakpoint (set while paused)', async () => {
        await withSession(ADVANCED_SCRIPT, async (dc, port) => {
            await initAndAttach(dc, port);
            const bpResp = await dc.setBreakpointsRequest({
                source: { path: ADVANCED_SCRIPT },
                breakpoints: [{ line: 7 }],
            });
            assert.strictEqual(bpResp.body.breakpoints[0].verified, true);
            const stopped2Promise = dc.waitForEvent('stopped');
            await dc.continueRequest({ threadId: 1 });
            const stopped2 = await stopped2Promise;
            assert.strictEqual(stopped2.body.reason, 'breakpoint');
            await dc.continueRequest({ threadId: 1 });
            await dc.waitForEvent('terminated');
        });
    });
    it('should set multiple breakpoints', async () => {
        await withSession(ADVANCED_SCRIPT, async (dc, port) => {
            const stoppedPromise = dc.waitForEvent('stopped');
            await dc.initializeRequest(INIT_ARGS);
            const bpResp = await dc.setBreakpointsRequest({
                source: { path: ADVANCED_SCRIPT },
                breakpoints: [{ line: 7 }, { line: 12 }],
            });
            assert.strictEqual(bpResp.body.breakpoints.length, 2);
            for (const bp of bpResp.body.breakpoints) {
                assert.strictEqual(bp.verified, true);
            }
            await dc.attachRequest({ port });
            await dc.configurationDoneRequest();
            await stoppedPromise;
            // Continue through first breakpoint
            const stopped2Promise = dc.waitForEvent('stopped');
            await dc.continueRequest({ threadId: 1 });
            const stopped2 = await stopped2Promise;
            assert.strictEqual(stopped2.body.reason, 'breakpoint');
            // Continue through second breakpoint
            const stopped3Promise = dc.waitForEvent('stopped');
            await dc.continueRequest({ threadId: 1 });
            const stopped3 = await stopped3Promise;
            assert.strictEqual(stopped3.body.reason, 'breakpoint');
            await dc.continueRequest({ threadId: 1 });
            await dc.waitForEvent('terminated');
        });
    });
    it('should clear breakpoints when replacing for same file', async () => {
        await withSession(ADVANCED_SCRIPT, async (dc, port) => {
            const stoppedPromise = dc.waitForEvent('stopped');
            await dc.initializeRequest(INIT_ARGS);
            await dc.setBreakpointsRequest({
                source: { path: ADVANCED_SCRIPT },
                breakpoints: [{ line: 7 }],
            });
            const bpResp = await dc.setBreakpointsRequest({
                source: { path: ADVANCED_SCRIPT },
                breakpoints: [{ line: 12 }],
            });
            assert.strictEqual(bpResp.body.breakpoints.length, 1);
            assert.strictEqual(bpResp.body.breakpoints[0].line, 12);
            await dc.attachRequest({ port });
            await dc.configurationDoneRequest();
            await stoppedPromise;
            const stopped2Promise = dc.waitForEvent('stopped');
            await dc.continueRequest({ threadId: 1 });
            const stopped2 = await stopped2Promise;
            assert.strictEqual(stopped2.body.reason, 'breakpoint');
            await dc.continueRequest({ threadId: 1 });
            await dc.waitForEvent('terminated');
        });
    });
    it('should support conditional breakpoints', async () => {
        await withSession(ADVANCED_SCRIPT, async (dc, port) => {
            const stoppedPromise = dc.waitForEvent('stopped');
            await dc.initializeRequest(INIT_ARGS);
            const bpResp = await dc.setBreakpointsRequest({
                source: { path: ADVANCED_SCRIPT },
                breakpoints: [{ line: 44, condition: 'i === 2' }],
            });
            assert.strictEqual(bpResp.body.breakpoints[0].verified, true);
            await dc.attachRequest({ port });
            await dc.configurationDoneRequest();
            await stoppedPromise;
            const stopped2Promise = dc.waitForEvent('stopped');
            await dc.continueRequest({ threadId: 1 });
            const stopped2 = await stopped2Promise;
            assert.strictEqual(stopped2.body.reason, 'breakpoint');
            const evalResp = await dc.evaluateRequest({ expression: 'i', context: 'repl', frameId: 0 });
            assert.strictEqual(evalResp.body.result, '2', 'Conditional breakpoint should only fire when i === 2');
            await dc.continueRequest({ threadId: 1 });
            await dc.waitForEvent('terminated');
        });
    });
    it('should include hitBreakpointIds in breakpoint stopped event', async () => {
        await withSession(ADVANCED_SCRIPT, async (dc, port) => {
            await initAndAttach(dc, port);
            const bpResp = await dc.setBreakpointsRequest({
                source: { path: ADVANCED_SCRIPT },
                breakpoints: [{ line: 7 }],
            });
            const bpId = bpResp.body.breakpoints[0].id;
            assert.ok(bpId, 'Should have a breakpoint id');
            const stopped2Promise = dc.waitForEvent('stopped');
            await dc.continueRequest({ threadId: 1 });
            const stopped2 = await stopped2Promise;
            assert.strictEqual(stopped2.body.reason, 'breakpoint');
            assert.ok(stopped2.body.hitBreakpointIds, 'Should have hitBreakpointIds');
            await dc.continueRequest({ threadId: 1 });
            await dc.waitForEvent('terminated');
        });
    });
    /* ============================================================
     *  4. SCOPES & VARIABLES
     * ============================================================ */
    it('should return scopes for a paused frame', async () => {
        await withSession(ADVANCED_SCRIPT, async (dc, port) => {
            await initAndAttach(dc, port);
            const resp = await dc.scopesRequest({ frameId: 0 });
            assert.ok(resp.body.scopes.length >= 1, 'Should have at least one scope');
            const scope = resp.body.scopes[0];
            assert.ok(scope.name, 'Scope should have a name');
            assert.ok(scope.variablesReference > 0, 'Scope should have a variablesReference');
            await dc.continueRequest({ threadId: 1 });
            await dc.waitForEvent('terminated');
        });
    });
    it('should return local variables with correct values', async () => {
        await withSession(ADVANCED_SCRIPT, async (dc, port) => {
            await initAndAttach(dc, port);
            // Set breakpoint on line 25 where x, y, z, obj, arr are all assigned
            await dc.setBreakpointsRequest({
                source: { path: ADVANCED_SCRIPT },
                breakpoints: [{ line: 25 }],
            });
            const stopped2Promise = dc.waitForEvent('stopped');
            await dc.continueRequest({ threadId: 1 });
            await stopped2Promise;
            const scopesResp = await dc.scopesRequest({ frameId: 0 });
            // Collect all variables from all scopes
            const allVars = [];
            for (const scope of scopesResp.body.scopes) {
                const varsResp = await dc.variablesRequest({ variablesReference: scope.variablesReference });
                allVars.push(...varsResp.body.variables);
            }
            const xVar = allVars.find((v) => v.name === 'x');
            const yVar = allVars.find((v) => v.name === 'y');
            const zVar = allVars.find((v) => v.name === 'z');
            assert.ok(xVar, 'x should be in variables');
            assert.ok(yVar, 'y should be in variables');
            assert.ok(zVar, 'z should be in variables');
            assert.strictEqual(xVar.value, '10');
            assert.strictEqual(yVar.value, '20');
            assert.strictEqual(zVar.value, '30');
            await dc.continueRequest({ threadId: 1 });
            await dc.waitForEvent('terminated');
        });
    });
    it('should expand object variables', async () => {
        await withSession(ADVANCED_SCRIPT, async (dc, port) => {
            await initAndAttach(dc, port);
            await dc.setBreakpointsRequest({
                source: { path: ADVANCED_SCRIPT },
                breakpoints: [{ line: 25 }],
            });
            const stopped2Promise = dc.waitForEvent('stopped');
            await dc.continueRequest({ threadId: 1 });
            await stopped2Promise;
            const scopesResp = await dc.scopesRequest({ frameId: 0 });
            const allVars = [];
            for (const scope of scopesResp.body.scopes) {
                const varsResp = await dc.variablesRequest({ variablesReference: scope.variablesReference });
                allVars.push(...varsResp.body.variables);
            }
            const objVar = allVars.find((v) => v.name === 'obj');
            assert.ok(objVar, 'obj should be in variables');
            assert.ok(objVar.variablesReference > 0, 'Object should have variablesReference');
            const objVarsResp = await dc.variablesRequest({ variablesReference: objVar.variablesReference });
            const nameVar = objVarsResp.body.variables.find((v) => v.name === 'name');
            assert.ok(nameVar, 'obj.name should be present');
            assert.ok(nameVar.value === '"noix"' || nameVar.value === 'noix', `Expected "noix" or noix, got ${nameVar.value}`);
            await dc.continueRequest({ threadId: 1 });
            await dc.waitForEvent('terminated');
        });
    });
    it('should expand array variables', async () => {
        await withSession(ADVANCED_SCRIPT, async (dc, port) => {
            await initAndAttach(dc, port);
            await dc.setBreakpointsRequest({
                source: { path: ADVANCED_SCRIPT },
                breakpoints: [{ line: 25 }],
            });
            const stopped2Promise = dc.waitForEvent('stopped');
            await dc.continueRequest({ threadId: 1 });
            await stopped2Promise;
            const scopesResp = await dc.scopesRequest({ frameId: 0 });
            const allVars = [];
            for (const scope of scopesResp.body.scopes) {
                const varsResp = await dc.variablesRequest({ variablesReference: scope.variablesReference });
                allVars.push(...varsResp.body.variables);
            }
            const arrVar = allVars.find((v) => v.name === 'arr');
            assert.ok(arrVar, 'arr should be in variables');
            assert.ok(arrVar.variablesReference > 0);
            const arrVarsResp = await dc.variablesRequest({ variablesReference: arrVar.variablesReference });
            assert.ok(arrVarsResp.body.variables.length >= 3, `Array should have >= 3 elements, got ${arrVarsResp.body.variables.length}`);
            await dc.continueRequest({ threadId: 1 });
            await dc.waitForEvent('terminated');
        });
    });
    /* ============================================================
     *  5. EVALUATE — success, errors, objects
     * ============================================================ */
    it('should evaluate simple expression', async () => {
        await withSession(ADVANCED_SCRIPT, async (dc, port) => {
            await initAndAttach(dc, port);
            const resp = await dc.evaluateRequest({ expression: '1 + 2', context: 'repl', frameId: 0 });
            assert.strictEqual(resp.body.result, '3');
            await dc.continueRequest({ threadId: 1 });
            await dc.waitForEvent('terminated');
        });
    });
    it('should return error for undefined variable evaluate', async () => {
        await withSession(ADVANCED_SCRIPT, async (dc, port) => {
            await initAndAttach(dc, port);
            try {
                await dc.evaluateRequest({ expression: 'nonexistentVariable', context: 'repl', frameId: 0 });
                assert.fail('Should have thrown or returned error');
            }
            catch (e) {
                assert.ok(e.message || e, 'Should indicate an error');
            }
            await dc.continueRequest({ threadId: 1 });
            await dc.waitForEvent('terminated');
        });
    });
    it('should evaluate object with variablesReference', async () => {
        await withSession(ADVANCED_SCRIPT, async (dc, port) => {
            await initAndAttach(dc, port);
            await dc.setBreakpointsRequest({
                source: { path: ADVANCED_SCRIPT },
                breakpoints: [{ line: 25 }],
            });
            const stopped2Promise = dc.waitForEvent('stopped');
            await dc.continueRequest({ threadId: 1 });
            await stopped2Promise;
            const evalResp = await dc.evaluateRequest({ expression: 'obj', context: 'repl', frameId: 0 });
            assert.strictEqual(evalResp.success, true);
            assert.ok(evalResp.body.variablesReference > 0, 'Object eval should have variablesReference');
            const objVars = await dc.variablesRequest({ variablesReference: evalResp.body.variablesReference });
            const nameVar = objVars.body.variables.find((v) => v.name === 'name');
            assert.ok(nameVar, 'Evaluated obj should have name property');
            assert.ok(nameVar.value === '"noix"' || nameVar.value === 'noix', `Expected "noix" or noix, got ${nameVar.value}`);
            await dc.continueRequest({ threadId: 1 });
            await dc.waitForEvent('terminated');
        });
    });
    /* ============================================================
     *  6. EXCEPTION BREAKPOINTS
     * ============================================================ */
    it('should stop on exception when "all" filter is set', async () => {
        await withSession(ADVANCED_SCRIPT, async (dc, port) => {
            await dc.initializeRequest(INIT_ARGS);
            await dc.setExceptionBreakpointsRequest({ filters: ['all'] });
            await initAndAttach(dc, port);
            // Continue — may stop on multiple exceptions before terminating
            const stopped2Promise = dc.waitForEvent('stopped');
            await dc.continueRequest({ threadId: 1 });
            const stopped2 = await stopped2Promise;
            assert.strictEqual(stopped2.body.reason, 'exception', 'Should stop on exception');
            // Keep continuing until terminated (may hit more exceptions)
            let done = false;
            for (let i = 0; i < 10 && !done; i++) {
                const nextStopP = dc.waitForEvent('stopped');
                const nextTermP = dc.waitForEvent('terminated');
                await dc.continueRequest({ threadId: 1 });
                const result = await Promise.race([nextStopP, nextTermP]);
                if (result.event === 'terminated')
                    done = true;
            }
        });
    });
    it('should NOT stop on caught exceptions when no filter is set', async () => {
        await withSession(ADVANCED_SCRIPT, async (dc, port) => {
            await dc.initializeRequest(INIT_ARGS);
            await initAndAttach(dc, port);
            await dc.continueRequest({ threadId: 1 });
            await dc.waitForEvent('terminated');
        });
    });
    /* ============================================================
     *  7. THREADS & LOADED SOURCES
     * ============================================================ */
    it('should return thread list', async () => {
        await withSession(SCRIPT_PATH, async (dc, port) => {
            await initAndAttach(dc, port);
            const resp = await dc.threadsRequest();
            assert.ok(resp.body.threads.length >= 1, 'Should have at least one thread');
            const mainThread = resp.body.threads.find((t) => t.id === 1);
            assert.ok(mainThread, 'Should have Main Thread with id 1');
            await dc.continueRequest({ threadId: 1 });
            await dc.waitForEvent('terminated');
        });
    });
    it('should return loaded sources', async () => {
        await withSession(SCRIPT_PATH, async (dc, port) => {
            await initAndAttach(dc, port);
            const resp = await dc.send('loadedSources', {});
            assert.ok(resp.body.sources.length >= 1, 'Should have at least one source');
            const mainSource = resp.body.sources.find((s) => s.path && s.path.includes('test_debug'));
            assert.ok(mainSource, 'Should list test_debug as a source');
            await dc.continueRequest({ threadId: 1 });
            await dc.waitForEvent('terminated');
        });
    });
    /* ============================================================
     *  8. DISCONNECT & CONFIGURATION DONE
     * ============================================================ */
    it('should handle configurationDone request', async () => {
        await withSession(SCRIPT_PATH, async (dc, port) => {
            await dc.initializeRequest(INIT_ARGS);
            await dc.attachRequest({ port });
            const resp = await dc.configurationDoneRequest();
            assert.strictEqual(resp.success, true);
            await dc.continueRequest({ threadId: 1 });
            await dc.waitForEvent('terminated');
        });
    });
    it('should handle disconnect while paused', async () => {
        await withSession(SCRIPT_PATH, async (dc, port) => {
            await initAndAttach(dc, port);
            const resp = await dc.disconnectRequest({});
            assert.strictEqual(resp.success, true, 'Disconnect while paused should succeed');
        });
    });
    /* ============================================================
     *  9. STACK TRACE EDGE CASES
     * ============================================================ */
    it('should report correct stack depth inside nested function', async () => {
        await withSession(ADVANCED_SCRIPT, async (dc, port) => {
            await initAndAttach(dc, port);
            await dc.setBreakpointsRequest({
                source: { path: ADVANCED_SCRIPT },
                breakpoints: [{ line: 58 }],
            });
            const stopped2Promise = dc.waitForEvent('stopped');
            await dc.continueRequest({ threadId: 1 });
            await stopped2Promise;
            const stack = await dc.stackTraceRequest({ threadId: 1 });
            assert.ok(stack.body.stackFrames.length >= 2, `Expected >= 2 frames in nested call, got ${stack.body.stackFrames.length}`);
            await dc.continueRequest({ threadId: 1 });
            await dc.waitForEvent('terminated');
        });
    });
    it('should support stackTrace pagination', async () => {
        await withSession(ADVANCED_SCRIPT, async (dc, port) => {
            await initAndAttach(dc, port);
            await dc.setBreakpointsRequest({
                source: { path: ADVANCED_SCRIPT },
                breakpoints: [{ line: 58 }],
            });
            const stopped2Promise = dc.waitForEvent('stopped');
            await dc.continueRequest({ threadId: 1 });
            await stopped2Promise;
            const fullStack = await dc.stackTraceRequest({ threadId: 1 });
            const total = fullStack.body.totalFrames;
            const partial = await dc.stackTraceRequest({ threadId: 1, startFrame: 0, levels: 1 });
            assert.strictEqual(partial.body.stackFrames.length, 1);
            assert.strictEqual(partial.body.totalFrames, total);
            await dc.continueRequest({ threadId: 1 });
            // Use longer timeout — stackTrace requests may have consumed time
            await dc.waitForEvent('terminated', 10000);
        });
    });
    /* ============================================================
     *  10. PAUSE REQUEST
     * ============================================================ */
    it('should respond to pause request while already paused', async () => {
        await withSession(ADVANCED_SCRIPT, async (dc, port) => {
            await initAndAttach(dc, port);
            const resp = await dc.pauseRequest({ threadId: 1 });
            assert.strictEqual(resp.success, true, 'Pause request should succeed');
            await dc.continueRequest({ threadId: 1 });
            await dc.waitForEvent('terminated');
        });
    });
});
//# sourceMappingURL=dap.test.js.map