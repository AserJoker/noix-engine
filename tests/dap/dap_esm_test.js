#!/usr/bin/env node
/**
 * Test basic ES module import support in the bridge
 */
const { spawn } = require('child_process');
const path = require('path');
const { DapClient } = require('./dap_client');

const bridgePath = process.argv[2] || path.join(__dirname, '..', '..', 'dist', 'dap-debug-bridge');
const mainScript = path.join(__dirname, 'scripts', 'dap_multifile_main.js');

let passed = 0, failed = 0;
function assert(c, m) { if (c) { console.log(`  PASS: ${m}`); passed++; } else { console.log(`  FAIL: ${m}`); failed++; } }

async function runTest() {
    console.log('=== ES Module Import Test ===');
    const proc = spawn(bridgePath, ['--script', mainScript], { stdio: ['pipe', 'pipe', 'pipe'] });
    const client = new DapClient(proc);
    let stderrOutput = '';
    proc.stderr.on('data', (d) => { stderrOutput += d.toString(); });

    try {
        await client.sendRequest('initialize', { clientID: 'test', adapterID: 'noix', pathFormat: 'path' });
        await client.sendRequest('setBreakpoints', { source: { path: mainScript }, breakpoints: [{ line: 5 }] });
        await client.sendRequest('launch', { script: mainScript, stopOnEntry: false });

        const event = await Promise.race([
            client.waitForEvent('stopped', 8000).then(e => ({ type: 'stopped', event: e })),
            client.waitForEvent('terminated', 8000).then(e => ({ type: 'terminated', event: e }))
        ]);

        if (event.type === 'terminated') {
            console.log('  Script terminated immediately (import syntax likely rejected)');
            console.log('  Stderr:', stderrOutput.slice(-300));
            assert(false, 'ES module import should work');
        } else {
            console.log('  Script hit breakpoint — import syntax is supported');
            const st = await client.sendRequest('stackTrace', { threadId: 1, startFrame: 0, levels: 5 });
            console.log(`  Top frame: ${st.body.stackFrames[0].source.name}:${st.body.stackFrames[0].line}`);
            assert(true, 'ES module import works');
            await client.sendRequest('continue', { threadId: 1 });
            try { await client.waitForEvent('terminated', 3000); } catch(e) {}
        }
        await client.sendRequest('disconnect');
    } catch (e) {
        console.error('Error:', e.message);
        if (stderrOutput) console.error('Bridge stderr:', stderrOutput.slice(-500));
        failed++;
    }
    proc.kill();
    console.log(`\n=== Results: ${passed} passed, ${failed} failed ===`);
    process.exit(failed > 0 ? 1 : 0);
}
runTest();
