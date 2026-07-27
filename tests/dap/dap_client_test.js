#!/usr/bin/env node
/**
 * dap_client_test.js — Full DAP protocol test for dap-debug-bridge
 */
const { spawn } = require('child_process');
const path = require('path');
const { DapClient } = require('./dap_client');

const bridgePath = process.argv[2] || path.join(__dirname, '..', '..', 'dist', 'dap-debug-bridge');
const scriptPath = path.join(__dirname, 'scripts', 'dap_test.js');

let passed = 0, failed = 0;
function assert(c, m) { if (c) { console.log(`  PASS: ${m}`); passed++; } else { console.log(`  FAIL: ${m}`); failed++; } }

async function runTest() {
    console.log('=== DAP Client Test ===');
    const proc = spawn(bridgePath, ['--script', scriptPath], { stdio: ['pipe', 'pipe', 'pipe'] });
    const client = new DapClient(proc);
    proc.stderr.on('data', () => {});

    try {
        await client.sendRequest('initialize', { clientID: 'dap-test', adapterID: 'noix', pathFormat: 'path' });
        assert(true, 'initialize succeeded');

        const bpResp = await client.sendRequest('setBreakpoints', {
            source: { path: scriptPath },
            breakpoints: [{ line: 8 }, { line: 29 }],
        });
        assert(bpResp.success, 'setBreakpoints succeeded');
        assert(bpResp.body.breakpoints.length === 2, '2 breakpoints set');
        assert(bpResp.body.breakpoints[0].verified, 'bp1 verified');
        assert(bpResp.body.breakpoints[1].verified, 'bp2 verified');

        const excResp = await client.sendRequest('setExceptionBreakpoints', { filters: ['uncaught'] });
        assert(excResp.success, 'setExceptionBreakpoints succeeded');

        const launchResp = await client.sendRequest('launch', { script: scriptPath, stopOnEntry: false });
        assert(launchResp.success, 'launch succeeded');

        const stoppedEvent = await client.waitForEvent('stopped');
        assert(stoppedEvent.body.reason === 'breakpoint', 'stopped reason is breakpoint');

        const stackResp = await client.sendRequest('stackTrace', { threadId: 1, startFrame: 0, levels: 20 });
        assert(stackResp.success, 'stackTrace succeeded');
        assert(stackResp.body.stackFrames.length > 0, 'has stack frames');
        const topFrame = stackResp.body.stackFrames[0];
        console.log(`  Top frame: ${topFrame.name} at ${topFrame.line}:${topFrame.column}`);

        const scopesResp = await client.sendRequest('scopes', { frameId: topFrame.id });
        assert(scopesResp.success, 'scopes succeeded');
        assert(scopesResp.body.scopes.length > 0, 'has scopes');

        const varRef = scopesResp.body.scopes[0].variablesReference;
        const varsResp = await client.sendRequest('variables', { variablesReference: varRef });
        assert(varsResp.success, 'variables succeeded');
        assert(varsResp.body.variables.length > 0, 'has variables');
        console.log(`  Variables: ${varsResp.body.variables.map(v => v.name).join(', ')}`);

        // Evaluate frame locals (a, b are in scope)
        const evalLocal = await client.sendRequest('evaluate', { expression: 'a + b', frameId: topFrame.id, context: 'repl' });
        assert(evalLocal.success, 'evaluate a+b succeeded');
        console.log(`  a + b = ${evalLocal.body.result}`);

        // Evaluate module-scope vars (x, y NOT in frame scope in module mode)
        const evalModule = await client.sendRequest('evaluate', { expression: 'x + y', frameId: topFrame.id, context: 'repl' });
        assert(!evalModule.success, 'evaluate x+y fails (module scope, not frame local)');
        console.log(`  x + y = ${evalModule.body.result} (expected: not defined)`);

        const contResp = await client.sendRequest('continue', { threadId: 1 });
        assert(contResp.success, 'continue succeeded');

        const stopped2 = await client.waitForEvent('stopped');
        console.log(`  Second stop: reason=${stopped2.body.reason}`);

        const nextResp = await client.sendRequest('next', { threadId: 1 });
        assert(nextResp.success, 'next succeeded');
        try { await client.waitForEvent('stopped', 3000); } catch (e) {}

        const threadsResp = await client.sendRequest('threads');
        assert(threadsResp.success, 'threads succeeded');
        assert(threadsResp.body.threads.length === 1, 'single thread');

        await client.sendRequest('continue', { threadId: 1 });
        try { await client.waitForEvent('terminated', 5000); } catch (e) {}

        const discResp = await client.sendRequest('disconnect');
        assert(discResp.success, 'disconnect succeeded');

    } catch (e) { console.error('Test error:', e.message); failed++; }

    proc.kill();
    console.log(`\n=== Results: ${passed} passed, ${failed} failed ===`);
    process.exit(failed > 0 ? 1 : 0);
}
runTest();
