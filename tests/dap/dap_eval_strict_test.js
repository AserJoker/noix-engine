#!/usr/bin/env node
/**
 * Test frame-scoped evaluation in strict mode
 */
const { spawn } = require('child_process');
const path = require('path');
const fs = require('fs');
const { DapClient } = require('./dap_client');

const bridgePath = process.argv[2] || path.join(__dirname, '..', '..', 'dist', 'dap-debug-bridge');

let passed = 0, failed = 0;
function assert(c, m) { if (c) { console.log(`  PASS: ${m}`); passed++; } else { console.log(`  FAIL: ${m}`); failed++; } }

// Create temporary test script
const testScript = path.join(__dirname, 'scripts', '_eval_strict_tmp.js');
fs.writeFileSync(testScript, `"use strict";
var g = 42;

function strictFn(x, y) {
    var sum = x + y;
    debugger;         // line 6: stop here
    return sum;
}

strictFn(10, 20);
`);

async function runTest() {
    console.log('=== Eval Strict Mode Test ===');
    const proc = spawn(bridgePath, ['--script', testScript], { stdio: ['pipe', 'pipe', 'pipe'] });
    const client = new DapClient(proc);
    proc.stderr.on('data', () => {});

    try {
        await client.sendRequest('initialize', { clientID: 'test', adapterID: 'noix', pathFormat: 'path' });
        const bpResp = await client.sendRequest('setBreakpoints', { source: { path: testScript }, breakpoints: [{ line: 6 }] });
        assert(bpResp.success, 'setBreakpoints succeeded');
        await client.sendRequest('configurationDone');
        await client.sendRequest('launch', { script: testScript, stopOnEntry: false });
        const stopped = await client.waitForEvent('stopped');
        assert(stopped.body.reason === 'breakpoint', 'stopped at breakpoint');

        const stResp = await client.sendRequest('stackTrace', { threadId: 1, startFrame: 0, levels: 5 });
        const topFrame = stResp.body.stackFrames[0];
        console.log(`  Top frame: ${topFrame.name} at line ${topFrame.line}`);

        const eval1 = await client.sendRequest('evaluate', { expression: 'x + y', frameId: topFrame.id, context: 'repl' });
        assert(eval1.success, 'evaluate x + y succeeded');
        assert(eval1.body.result === '30', 'x + y = 30');

        const eval2 = await client.sendRequest('evaluate', { expression: 'sum', frameId: topFrame.id, context: 'repl' });
        assert(eval2.success, 'evaluate sum succeeded');
        assert(eval2.body.result === '30', 'sum = 30');

        const eval3 = await client.sendRequest('evaluate', { expression: 'g', frameId: topFrame.id, context: 'repl' });
        console.log(`  g = ${eval3.body.result} (success=${eval3.success})`);
        assert(eval3.success, 'evaluate g succeeded');

        const eval4 = await client.sendRequest('evaluate', { expression: 'x * 2 + y', frameId: topFrame.id, context: 'repl' });
        assert(eval4.success, 'evaluate x * 2 + y succeeded');
        assert(eval4.body.result === '40', 'x * 2 + y = 40');

        await client.sendRequest('continue', { threadId: 1 });
        try { await client.waitForEvent('terminated', 3000); } catch(e) {}
        await client.sendRequest('disconnect');
    } catch (e) { console.error('Test error:', e.message); failed++; }

    proc.kill();
    try { fs.unlinkSync(testScript); } catch(e) {}
    console.log(`\n=== Results: ${passed} passed, ${failed} failed ===`);
    process.exit(failed > 0 ? 1 : 0);
}
runTest();
