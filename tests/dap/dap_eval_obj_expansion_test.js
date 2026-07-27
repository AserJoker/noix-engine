#!/usr/bin/env node
/**
 * Test object expansion via evaluate response
 */
const { spawn } = require('child_process');
const path = require('path');
const { DapClient } = require('./dap_client');

const bridgePath = process.argv[2] || path.join(__dirname, '..', '..', 'dist', 'dap-debug-bridge');
const scriptPath = path.join(__dirname, 'scripts', 'dap_obj_expansion_script.js');

let passed = 0, failed = 0;
function assert(c, m) { if (c) { console.log(`  PASS: ${m}`); passed++; } else { console.log(`  FAIL: ${m}`); failed++; } }

async function runTest() {
    console.log('=== Eval Object Expansion Test ===');
    const proc = spawn(bridgePath, ['--script', scriptPath], { stdio: ['pipe', 'pipe', 'pipe'] });
    const client = new DapClient(proc);
    proc.stderr.on('data', () => {});

    try {
        await client.sendRequest('initialize', { clientID: 'test', adapterID: 'noix', pathFormat: 'path' });
        await client.sendRequest('setBreakpoints', { source: { path: scriptPath }, breakpoints: [{ line: 8 }] });
        await client.sendRequest('launch', { script: scriptPath, stopOnEntry: false });
        await client.waitForEvent('stopped');
        const st = await client.sendRequest('stackTrace', { threadId: 1, startFrame: 0, levels: 5 });
        const fid = st.body.stackFrames[0].id;

        // Evaluate localObj
        console.log('\n--- Eval localObj ---');
        const evalObj = await client.sendRequest('evaluate', { expression: 'localObj', frameId: fid, context: 'repl' });
        assert(evalObj.success, 'eval localObj succeeded');
        assert(evalObj.body.variablesReference > 0, 'eval localObj has variablesReference');

        if (evalObj.body.variablesReference > 0) {
            const objVars = await client.sendRequest('variables', { variablesReference: evalObj.body.variablesReference });
            assert(objVars.success, 'expand eval result succeeded');
            const props = objVars.body.variables.map(v => `${v.name}=${v.value}`).join(', ');
            console.log(`  Props: ${props}`);
            assert(objVars.body.variables.some(v => v.name === 'a' && v.value === '1'), 'eval-expanded a=1');
            assert(objVars.body.variables.some(v => v.name === 'b' && v.value === 'hello'), 'eval-expanded b=hello');
        }

        // Evaluate localArr
        console.log('\n--- Eval localArr ---');
        const evalArr = await client.sendRequest('evaluate', { expression: 'localArr', frameId: fid, context: 'repl' });
        assert(evalArr.success && evalArr.body.variablesReference > 0, 'eval localArr has variablesReference');

        if (evalArr.body.variablesReference > 0) {
            const arrVars = await client.sendRequest('variables', { variablesReference: evalArr.body.variablesReference });
            console.log(`  Array props: ${arrVars.body.variables.map(v => `${v.name}=${v.value}`).join(', ')}`);
            assert(arrVars.body.variables.some(v => v.name === '0' && v.value === '100'), 'localArr[0]=100');
            assert(arrVars.body.variables.some(v => v.name === '1' && v.value === '200'), 'localArr[1]=200');
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
