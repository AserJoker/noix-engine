#!/usr/bin/env node
/**
 * Test variable index debug (var only, and let/const + var mixed)
 */
const { spawn } = require('child_process');
const path = require('path');
const { DapClient } = require('./dap_client');

const bridgePath = process.argv[2] || path.join(__dirname, '..', '..', 'dist', 'dap-debug-bridge');

let passed = 0, failed = 0;
function assert(c, m) { if (c) { console.log(`  PASS: ${m}`); passed++; } else { console.log(`  FAIL: ${m}`); failed++; } }

async function testScript(scriptPath, bpLine, expectedVars) {
    console.log(`\n--- Testing: ${path.basename(scriptPath)} (bp at line ${bpLine}) ---`);
    const proc = spawn(bridgePath, ['--script', scriptPath], { stdio: ['pipe', 'pipe', 'pipe'] });
    const client = new DapClient(proc);
    proc.stderr.on('data', () => {});

    try {
        await client.sendRequest('initialize', { clientID: 'test', adapterID: 'noix', pathFormat: 'path' });
        await client.sendRequest('setBreakpoints', { source: { path: scriptPath }, breakpoints: [{ line: bpLine }] });
        await client.sendRequest('configurationDone');
        await client.sendRequest('launch', { script: scriptPath, stopOnEntry: false });
        await client.waitForEvent('stopped');
        const st = await client.sendRequest('stackTrace', { threadId: 1, startFrame: 0, levels: 5 });
        const fid = st.body.stackFrames[0].id;
        const sc = await client.sendRequest('scopes', { frameId: fid });
        const localScope = sc.body.scopes.find(s => s.name === 'Local') || sc.body.scopes[0];
        const vr = localScope.variablesReference;
        const va = await client.sendRequest('variables', { variablesReference: vr });
        console.log('  Variables:', va.body.variables.map(v => `${v.name}=${v.value}`).join(', '));

        for (const [name, expected] of Object.entries(expectedVars)) {
            const e = await client.sendRequest('evaluate', { expression: name, frameId: fid, context: 'repl' });
            console.log(`  eval ${name} = ${e.body.result} (expected ${expected})`);
            assert(e.success && e.body.result === expected, `${path.basename(scriptPath)}: ${name} = ${expected}`);
        }

        await client.sendRequest('continue', { threadId: 1 });
        try { await client.waitForEvent('terminated', 3000); } catch(e) {}
        await client.sendRequest('disconnect');
    } catch (e) { console.error('Error:', e.message); failed++; }
    proc.kill();
}

async function runTest() {
    console.log('=== Var Index Debug Test ===');
    await testScript(path.join(__dirname, 'scripts', 'dap_varindex_script.js'), 4, { x: '10', y: '20', legacy: '30', z: 'undefined' });
    await testScript(path.join(__dirname, 'scripts', 'dap_letconstvar_script.js'), 6, { x: '10', y: '20', sum: '30', product: '200', legacy: '230', z: 'undefined' });
    console.log(`\n=== Results: ${passed} passed, ${failed} failed ===`);
    process.exit(failed > 0 ? 1 : 0);
}
runTest();
