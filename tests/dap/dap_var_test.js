#!/usr/bin/env node
/**
 * Test basic variable inspection
 */
const { spawn } = require('child_process');
const path = require('path');
const { DapClient } = require('./dap_client');

const bridgePath = process.argv[2] || path.join(__dirname, '..', '..', 'dist', 'dap-debug-bridge');
const scriptPath = path.join(__dirname, 'scripts', 'dap_var_test_script.js');

let passed = 0, failed = 0;
function assert(c, m) { if (c) { console.log(`  PASS: ${m}`); passed++; } else { console.log(`  FAIL: ${m}`); failed++; } }

async function runTest() {
    console.log('=== Var Debug Test ===');
    const proc = spawn(bridgePath, ['--script', scriptPath], { stdio: ['pipe', 'pipe', 'pipe'] });
    const client = new DapClient(proc);
    proc.stderr.on('data', () => {});

    try {
        await client.sendRequest('initialize', { clientID: 'test', adapterID: 'noix', pathFormat: 'path' });
        await client.sendRequest('setBreakpoints', { source: { path: scriptPath }, breakpoints: [{ line: 5 }] });
        await client.sendRequest('configurationDone');
        await client.sendRequest('launch', { script: scriptPath, stopOnEntry: false });
        await client.waitForEvent('stopped');
        const st = await client.sendRequest('stackTrace', { threadId: 1, startFrame: 0, levels: 5 });
        const fid = st.body.stackFrames[0].id;
        console.log(`  Stopped at line ${st.body.stackFrames[0].line}`);

        const sc = await client.sendRequest('scopes', { frameId: fid });
        const localScope = sc.body.scopes.find(s => s.name === 'Local') || sc.body.scopes[0];
        const vr = localScope.variablesReference;
        const va = await client.sendRequest('variables', { variablesReference: vr });
        console.log('  Variables:', va.body.variables.map(v => `${v.name}=${v.value}(${v.type})`).join(', '));

        for (const [expr, expected] of [['a', '10'], ['x', '11'], ['y', '22']]) {
            const e = await client.sendRequest('evaluate', { expression: expr, frameId: fid, context: 'repl' });
            console.log(`  ${expr} = ${e.body.result} (expected ${expected})`);
            assert(e.success && e.body.result === expected, `${expr} = ${expected}`);
        }

        await client.sendRequest('continue', { threadId: 1 });
        try { await client.waitForEvent('terminated', 3000); } catch(e) {}
        await client.sendRequest('disconnect');
    } catch (e) { console.error('Error:', e.message); failed++; }
    proc.kill();
    console.log(`\n=== Results: ${passed} passed, ${failed} failed ===`);
    process.exit(failed > 0 ? 1 : 0);
}
runTest();
