#!/usr/bin/env node
/**
 * Test TDZ (temporal dead zone) variable access
 */
const { spawn } = require('child_process');
const path = require('path');
const { DapClient } = require('./dap_client');

const bridgePath = process.argv[2] || path.join(__dirname, '..', '..', 'dist', 'dap-test-bridge');
const scriptPath = path.join(__dirname, 'scripts', 'dap_tdz_script.js');

let passed = 0, failed = 0;
function assert(c, m) { if (c) { console.log(`  PASS: ${m}`); passed++; } else { console.log(`  FAIL: ${m}`); failed++; } }

async function runTest() {
    console.log('=== TDZ Test ===');
    const proc = spawn(bridgePath, ['--script', scriptPath], { stdio: ['pipe', 'pipe', 'pipe'] });
    const client = new DapClient(proc);
    proc.stderr.on('data', () => {});

    try {
        await client.sendRequest('initialize', { clientID: 'test', adapterID: 'noix', pathFormat: 'path' });
        await client.sendRequest('setBreakpoints', { source: { path: scriptPath }, breakpoints: [{ line: 4 }] });
        await client.sendRequest('launch', { script: scriptPath, stopOnEntry: false });
        await client.waitForEvent('stopped');
        const st = await client.sendRequest('stackTrace', { threadId: 1, startFrame: 0, levels: 5 });
        const fid = st.body.stackFrames[0].id;
        console.log(`  Stopped at line ${st.body.stackFrames[0].line}`);

        const sc = await client.sendRequest('scopes', { frameId: fid });
        const vr = sc.body.scopes[0].variablesReference;
        const va = await client.sendRequest('variables', { variablesReference: vr });
        console.log('  Variables:', va.body.variables.map(v => `${v.name}=${v.value}`).join(', '));

        const e = await client.sendRequest('evaluate', { expression: 'sum', frameId: fid, context: 'repl' });
        console.log(`  eval sum = ${e.body.result} (success=${e.success})`);
        assert(e.success && e.body.result === '11', 'let sum = 11 after initialization');

        await client.sendRequest('continue', { threadId: 1 });
        try { await client.waitForEvent('terminated', 3000); } catch(e) {}
        await client.sendRequest('disconnect');
    } catch (e) { console.error('Error:', e.message); failed++; }
    proc.kill();
    console.log(`\n=== Results: ${passed} passed, ${failed} failed ===`);
    process.exit(failed > 0 ? 1 : 0);
}
runTest();
