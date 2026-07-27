import * as vscode from 'vscode';

export function activate(context: vscode.ExtensionContext) {
    const factory = new NoixDebugAdapterServerDescriptorFactory();
    context.subscriptions.push(
        vscode.debug.registerDebugAdapterDescriptorFactory('noix', factory)
    );
}

export function deactivate() {}

class NoixDebugAdapterServerDescriptorFactory implements vscode.DebugAdapterDescriptorFactory {
    createDebugAdapterDescriptor(
        session: vscode.DebugSession,
        _executable: vscode.DebugAdapterExecutable | undefined
    ): vscode.ProviderResult<vscode.DebugAdapterDescriptor> {
        const config = session.configuration;
        const host = (config.host as string) || '127.0.0.1';
        const port = config.port as number;

        if (!port) {
            throw new Error('Noix debug: "port" is required for attach configuration');
        }

        return new vscode.DebugAdapterServer(port, host);
    }
}
