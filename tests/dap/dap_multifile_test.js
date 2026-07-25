#!/usr/bin/env node
/**
 * Test breakpoints in multi-file + ES Module scenarios
 */
const { spawn } = require('child_process');
const path = require('path');
const { DapClient } = require('./dap_client');

const bridgePath = process.argv[2] || path.join(__dirname, '..', '..', 'dist', 'dap-test-bridge');
const mainScript = path.join(__dirname, 'scripts', 'dap_multifile_main.js');
const modScript = path.join(__dirname, 'scripts', 'dap_multifile_mod.js');

let passed = 0, failed = 0;
function assert(c, m) { if (c) { console.log(`  PASS: ${m}`); passed++; } else { console.log(`  FAIL: ${m}`); failed++; } }

async function runTest() {
    console.log('=== Multi-file + ES Module Breakpoint Test ===');
    const proc = spawn(bridgePath, ['--script', mainScript], { stdio: ['pipe', 'pipe', 'pipe'] });
    const client = new DapClient(proc);
    let stderrOutput = '';
    proc.stderr.on('data', (d) => { stderrOutput += d.toString(); });

    try {
        await client.sendRequest('initialize', { clientID: 'test', adapterID: 'noix', pathFormat: 'path' });
        await client.sendRequest('setBreakpoints', { source: { path: mainScript }, breakpoints: [{ line: 5 }] });
        await client.sendRequest('setBreakpoints', { source: { path: modScript }, breakpoints: [{ line: 3 }] });
        await client.sendRequest('launch', { script: mainScript, stopOnEntry: false });

        const stopped1 = await client.waitForEvent('stopped');
        console.log(`  First stopped event: reason=${stopped1.body.reason}`);

        const st1 = await client.sendRequest('stackTrace', { threadId: 1, startFrame: 0, levels: 5 });
        const topFrame = st1.body.stackFrames[0];
        console.log(`  Stopped at: ${topFrame.source.name}:${topFrame.line}`);

        const isMainFile = topFrame.source.path.endsWith('dap_multifile_main.js');
        const isModFile = topFrame.source.path.endsWith('dap_multifile_mod.js');
        assert(isMainFile || isModFile, 'stopped in one of the script files');

        if (isMainFile) {
            assert(topFrame.line === 5, 'main script breakpoint at line 5');
            await client.sendRequest('continue', { threadId: 1 });
            try {
                const stopped2 = await client.waitForEvent('stopped', 5000);
                const st2 = await client.sendRequest('stackTrace', { threadId: 1, startFrame: 0, levels: 5 });
                const topFrame2 = st2.body.stackFrames[0];
                console.log(`  Second stop: ${topFrame2.source.name}:${topFrame2.line}`);
                assert(topFrame2.source.path.endsWith('dap_multifile_mod.js'), 'module breakpoint was hit after continue');
            } catch (e) {
                assert(false, 'module breakpoint should have been hit');
            }
        } else if (isModFile) {
            console.log('  Module breakpoint hit first (import evaluation)');
        }

        await client.sendRequest('continue', { threadId: 1 });
        try { await client.waitForEvent('terminated', 3000); } catch(e) {}
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
