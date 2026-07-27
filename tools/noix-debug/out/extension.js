"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.activate = activate;
exports.deactivate = deactivate;
const vscode = require("vscode");
function activate(context) {
    const factory = new NoixDebugAdapterServerDescriptorFactory();
    context.subscriptions.push(vscode.debug.registerDebugAdapterDescriptorFactory('noix', factory));
}
function deactivate() { }
class NoixDebugAdapterServerDescriptorFactory {
    createDebugAdapterDescriptor(session, _executable) {
        const config = session.configuration;
        const host = config.host || '127.0.0.1';
        const port = config.port;
        if (!port) {
            throw new Error('Noix debug: "port" is required for attach configuration');
        }
        return new vscode.DebugAdapterServer(port, host);
    }
}
//# sourceMappingURL=extension.js.map