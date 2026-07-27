#!/usr/bin/env node
/**
 * Test adding/removing breakpoints while paused
 */
const { spawn } = require('child_process');
const path = require('path');
const { DapClient } = require('./dap_client');

const bridgePath = process.argv[2] || path.join(__dirname, '..', '..', 'dist', 'dap-debug-bridge');
const scriptPath = path.join(__dirname, 'scripts', 'dap_test.js');

let passed = 0, failed = 0;
function assert(c, m) { if (c) { console.log(`  PASS: ${m}`); passed++; } else { console.log(`  FAIL: ${m}`); failed++; } }

async function runTest() {
    console.log('=== Breakpoint-While-Paused Test ===');
    const proc = spawn(bridgePath, ['--script', scriptPath], { stdio: ['pipe', 'pipe', 'pipe'] });
    const client = new DapClient(proc);
    proc.stderr.on('data', (d) => console.error('[bridge stderr]', d.toString().trim()));

    try {
        await client.sendRequest('initialize', { clientID: 'dap-test', adapterID: 'noix', pathFormat: 'path' });

        const bp1 = await client.sendRequest('setBreakpoints', { source: { path: scriptPath }, breakpoints: [{ line: 8 }] });
        assert(bp1.success, 'setBreakpoints (line 8) succeeded');

        await client.sendRequest('configurationDone');

        await client.sendRequest('launch', { script: scriptPath, stopOnEntry: false });
        const stop1 = await client.waitForEvent('stopped');
        assert(stop1.body.reason === 'breakpoint', 'first stop is breakpoint');

        const st1 = await client.sendRequest('stackTrace', { threadId: 1, startFrame: 0, levels: 1 });
        assert(st1.body.stackFrames[0].line === 8, 'stopped at line 8');

        // While paused, replace breakpoints with line 29 only
        const bp2 = await client.sendRequest('setBreakpoints', { source: { path: scriptPath }, breakpoints: [{ line: 29 }] });
        assert(bp2.success, 'setBreakpoints (line 29) succeeded');

        await client.sendRequest('continue', { threadId: 1 });
        const stop2 = await client.waitForEvent('stopped');
        assert(stop2.body.reason === 'breakpoint', 'second stop is breakpoint');

        const st2 = await client.sendRequest('stackTrace', { threadId: 1, startFrame: 0, levels: 1 });
        assert(st2.body.stackFrames[0].line === 29, 'stopped at line 29');

        // While paused at line 29, add back breakpoints at 8, 13, 29
        const bp3 = await client.sendRequest('setBreakpoints', {
            source: { path: scriptPath },
            breakpoints: [{ line: 8 }, { line: 13 }, { line: 29 }],
        });
        assert(bp3.success, 'setBreakpoints (8, 13, 29) succeeded');

        // Continue — should get terminated, NOT another stopped:breakpoint
        await client.sendRequest('continue', { threadId: 1 });

        // Drain noise events
        while (true) { const idx = client.eventQueue.findIndex(e => e.event === 'loadedSource' || e.event === 'output'); if (idx === -1) break; client.eventQueue.splice(idx, 1); }

        const nextEvent = await Promise.race([
            client.waitForEvent('terminated', 5000).then(e => ({ type: 'terminated', event: e })),
            client.waitForEvent('stopped', 5000).then(e => ({ type: 'stopped', event: e })),
        ]);

        if (nextEvent.type === 'terminated') {
            assert(true, 'got terminated after adding breakpoints while paused');
        } else {
            assert(false, `got stopped instead of terminated (reason=${nextEvent.event.body.reason})`);
            await client.sendRequest('continue', { threadId: 1 });
            try { await client.waitForEvent('terminated', 3000); } catch(e) {}
        }

        const discResp = await client.sendRequest('disconnect');
        assert(discResp.success, 'disconnect succeeded');
    } catch (e) { console.error('Test error:', e.message); failed++; }

    proc.kill();
    console.log(`\n=== Results: ${passed} passed, ${failed} failed ===`);
    process.exit(failed > 0 ? 1 : 0);
}
runTest();
