const net = require('net');

const DAP_PORT = 4711;
const BRIDGE_EXE = 'D:/projects/noix-engine/dist/dap-debug-bridge.exe';
const SCRIPT_PATH = 'D:/projects/noix-engine/dist/scripts/test_debug.js';
const { spawn } = require('child_process');

let seq = 1;
let buffer = Buffer.alloc(0);

function makeRequest(command, args = {}) {
    const msg = { seq: seq++, type: 'request', command, arguments: args };
    const json = JSON.stringify(msg);
    return `Content-Length: ${Buffer.byteLength(json)}\r\n\r\n${json}`;
}

function parseMessages(data) {
    buffer = Buffer.concat([buffer, data]);
    const messages = [];
    while (true) {
        const headerEnd = buffer.indexOf('\r\n\r\n');
        if (headerEnd === -1) break;
        const header = buffer.slice(0, headerEnd).toString();
        const match = header.match(/Content-Length:\s*(\d+)/);
        if (!match) break;
        const len = parseInt(match[1]);
        const bodyStart = headerEnd + 4;
        if (buffer.length < bodyStart + len) break;
        const body = buffer.slice(bodyStart, bodyStart + len).toString();
        messages.push(JSON.parse(body));
        buffer = buffer.slice(bodyStart + len);
    }
    return messages;
}

function send(sock, command, args = {}) {
    const req = makeRequest(command, args);
    console.log(`>>> SEND: ${command}`);
    sock.write(req);
}

async function main() {
    // Start bridge
    const proc = spawn(BRIDGE_EXE, ['--port', String(DAP_PORT), '--script', SCRIPT_PATH], {
        stdio: ['pipe', 'pipe', 'pipe'],
    });
    proc.stderr.on('data', d => process.stderr.write(d));

    await new Promise(r => setTimeout(r, 1000));

    const sock = net.createConnection({ host: '127.0.0.1', port: DAP_PORT }, () => {
        console.log('Connected');

        // 1. Initialize
        send(sock, 'initialize', {
            clientID: 'test', adapterID: 'noix', pathFormat: 'path',
            linesStartAt1: true, columnsStartAt1: true
        });
    });

    let state = 'init';
    let continueSent = false;

    sock.on('data', (data) => {
        const msgs = parseMessages(data);
        for (const msg of msgs) {
            if (msg.type === 'response') {
                console.log(`<<< RESP: ${msg.command} success=${msg.success}`);
            } else if (msg.type === 'event') {
                console.log(`<<< EVENT: ${msg.event}`);
                if (msg.body) console.log(`    body: ${JSON.stringify(msg.body)}`);

                if (msg.event === 'stopped' && !continueSent) {
                    continueSent = true;
                    // Get stack trace first
                    send(sock, 'stackTrace', { threadId: 1, startFrame: 0, levels: 20 });

                    // Then continue after a short delay
                    setTimeout(() => {
                        console.log('\n>>> Sending continue...');
                        send(sock, 'continue', { threadId: 1 });
                    }, 100);
                }

                if (msg.event === 'terminated') {
                    setTimeout(() => {
                        send(sock, 'disconnect', { restart: false });
                        setTimeout(() => {
                            sock.destroy();
                            proc.kill();
                            console.log('\nDone');
                            process.exit(0);
                        }, 500);
                    }, 500);
                }
            }
        }
    });

    sock.on('error', (err) => {
        console.error('Socket error:', err.message);
    });

    sock.on('close', () => {
        console.log('Socket closed');
    });

    setTimeout(() => {
        console.error('Timeout');
        proc.kill();
        process.exit(1);
    }, 15000);
}

main().catch(console.error);
