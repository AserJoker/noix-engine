#!/usr/bin/env node
/**
 * Test object variable expansion via scopes/variables
 */
const { spawn } = require('child_process');
const path = require('path');
const { DapClient } = require('./dap_client');

const bridgePath = process.argv[2] || path.join(__dirname, '..', '..', 'dist', 'dap-debug-bridge');
const scriptPath = path.join(__dirname, 'scripts', 'dap_obj_expansion_script.js');

let passed = 0, failed = 0;
function assert(c, m) { if (c) { console.log(`  PASS: ${m}`); passed++; } else { console.log(`  FAIL: ${m}`); failed++; } }

async function runTest() {
    console.log('=== Object Expansion Test ===');
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
        console.log(`  Stopped at line ${st.body.stackFrames[0].line}`);

        const sc = await client.sendRequest('scopes', { frameId: fid });
        const scopeVarRef = sc.body.scopes[0].variablesReference;
        const va = await client.sendRequest('variables', { variablesReference: scopeVarRef });
        const vars = va.body.variables;
        console.log('  Frame vars:', vars.map(v => `${v.name}=${v.value}(ref=${v.variablesReference})`).join(', '));

        const localObj = vars.find(v => v.name === 'localObj');
        const localArr = vars.find(v => v.name === 'localArr');
        assert(localObj && localObj.type === 'object' && localObj.variablesReference > 0, 'localObj is an object with variablesReference');
        assert(localArr && localArr.type === 'object' && localArr.variablesReference > 0, 'localArr is an array object with variablesReference');

        if (localObj && localObj.variablesReference > 0) {
            console.log('\n--- Expanding localObj ---');
            const objVars = await client.sendRequest('variables', { variablesReference: localObj.variablesReference });
            assert(objVars.success, 'localObj expansion succeeded');
            const objPropNames = objVars.body.variables.map(v => v.name);
            console.log('  localObj props:', objPropNames.join(', '));
            assert(objPropNames.includes('a'), 'localObj has property "a"');
            assert(objPropNames.includes('b'), 'localObj has property "b"');
            const aProp = objVars.body.variables.find(v => v.name === 'a');
            const bProp = objVars.body.variables.find(v => v.name === 'b');
            assert(aProp && aProp.value === '1', 'localObj.a = 1');
            assert(bProp && bProp.value === 'hello', 'localObj.b = "hello"');
        }

        if (localArr && localArr.variablesReference > 0) {
            console.log('\n--- Expanding localArr ---');
            const arrVars = await client.sendRequest('variables', { variablesReference: localArr.variablesReference });
            assert(arrVars.success, 'localArr expansion succeeded');
            const arrProps = arrVars.body.variables.map(v => v.name);
            console.log('  localArr props:', arrProps.join(', '));
            assert(arrProps.includes('0'), 'localArr has index 0');
            assert(arrProps.includes('1'), 'localArr has index 1');
            const el0 = arrVars.body.variables.find(v => v.name === '0');
            const el1 = arrVars.body.variables.find(v => v.name === '1');
            assert(el0 && el0.value === '100', 'localArr[0] = 100');
            assert(el1 && el1.value === '200', 'localArr[1] = 200');
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
