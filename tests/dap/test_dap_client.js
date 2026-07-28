const net = require('net');

const HOST = '127.0.0.1';
const PORT = 4711;

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
    console.log(`>>> SEND: ${command} (seq=${seq - 1})`);
    if (args && Object.keys(args).length > 0) {
        console.log(`    args: ${JSON.stringify(args)}`);
    }
    sock.write(req);
}

const sock = net.createConnection({ host: HOST, port: PORT }, () => {
    console.log('Connected to DAP server');

    // 1. initialize
    send(sock, 'initialize', {
        clientID: 'test-client',
        adapterID: 'noix',
        pathFormat: 'path',
        linesStartAt1: true,
        columnsStartAt1: true
    });
});

let initialized = false;
let attachDone = false;
let configDone = false;

sock.on('data', (data) => {
    const msgs = parseMessages(data);
    for (const msg of msgs) {
        if (msg.type === 'response') {
            console.log(`<<< RECV response: command=${msg.command} success=${msg.success} request_seq=${msg.request_seq}`);
            if (msg.body) console.log(`    body: ${JSON.stringify(msg.body).slice(0, 200)}`);

            if (msg.command === 'initialize' && msg.success) {
                initialized = true;
                // 2. attach
                send(sock, 'attach', { port: 4711 });
            }
            if (msg.command === 'attach' && msg.success) {
                attachDone = true;
            }
        } else if (msg.type === 'event') {
            console.log(`<<< RECV event: ${msg.event}`);
            if (msg.body) console.log(`    body: ${JSON.stringify(msg.body).slice(0, 200)}`);

            if (msg.event === 'stopped') {
                console.log('\n*** STOPPED event received! ***');
                console.log(`    reason: ${msg.body.reason}`);
                console.log(`    line: ${msg.body.line}, col: ${msg.body.column}`);

                // 3. threads
                send(sock, 'threads');

                // 4. stackTrace
                send(sock, 'stackTrace', { threadId: 1, startFrame: 0, levels: 20 });

                // 5. scopes
                send(sock, 'scopes', { frameId: 0 });

                // 6. variables — use varRef from scopes response (assume 1 for frame 0 scope 0)
                // We'll send this after we get scopes response

                // 7. continue
                console.log('\n>>> Sending continue...');
                send(sock, 'continue', { threadId: 1 });
            }

            if (msg.event === 'terminated') {
                console.log('\n*** TERMINATED event ***');
                send(sock, 'disconnect', { restart: false });
            }
        }
    }
});

sock.on('close', () => {
    console.log('Connection closed');
    process.exit(0);
});

sock.on('error', (err) => {
    console.error('Socket error:', err);
    process.exit(1);
});

// Timeout safety
setTimeout(() => {
    console.error('\nTimeout — no response for 10 seconds, exiting');
    process.exit(1);
}, 10000);
