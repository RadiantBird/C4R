'use strict';
const vscode = require('vscode');
const { spawn } = require('child_process');
const path = require('path');
const fs = require('fs');

function activate(context) {
    const diagnostics = vscode.languages.createDiagnosticCollection('c4r');
    const output = vscode.window.createOutputChannel('C4R');
    const pending = new Map();
    context.subscriptions.push(diagnostics, output);
    output.appendLine('C4R extension activated.');

    function compilerPath(document) {
        const folder = vscode.workspace.getWorkspaceFolder(document.uri);
        const configured = vscode.workspace.getConfiguration('c4r', document.uri).get('compilerPath');
        if (configured) return folder && !path.isAbsolute(configured) ? path.join(folder.uri.fsPath, configured) : configured;
        const roots = [];
        if (folder) roots.push(folder.uri.fsPath);
        if (document.uri.scheme === 'file') roots.push(path.dirname(document.uri.fsPath));
        roots.push(context.extensionPath);
        for (const root of roots) {
            let dir = root;
            while (true) {
                const candidate = path.join(dir, 'compiler', 'build', 'Debug', 'C4R.exe');
                if (fs.existsSync(candidate)) return candidate;
                const parent = path.dirname(dir);
                if (parent === dir) break;
                dir = parent;
            }
        }
        return 'C4R';
    }
    function querySymbols(document, position, token) {
        if (token.isCancellationRequested) return Promise.resolve([]);
        const text = document.getText();
        const byteOffset = Buffer.byteLength(text.slice(0, document.offsetAt(position)), 'utf8');
        return new Promise(resolve => {
            const child = spawn(compilerPath(document), ['--symbols', String(byteOffset), '-'], { windowsHide: true });
            let stdout = '';
            let stderr = '';
            let done = false;
            const finish = value => { if (!done) { done = true; subscription.dispose(); resolve(value); } };
            const subscription = token.onCancellationRequested(() => { child.kill(); finish([]); });
            child.stdout.setEncoding('utf8');
            child.stdout.on('data', chunk => { stdout += chunk; });
            child.stderr.setEncoding('utf8');
            child.stderr.on('data', chunk => { stderr += chunk; });
            child.on('error', error => { output.appendLine(`Completion: unable to start compiler: ${error.message}`); finish([]); });
            child.stdin.on('error', () => {});
            child.stdin.end(text, 'utf8');
            child.on('close', code => {
                if (token.isCancellationRequested) return finish([]);
                if (code !== 0) {
                    output.appendLine(`Completion: compiler exited with code ${code}: ${stderr.trim()}. Check that C4R.exe is up to date.`);
                    return finish([]);
                }
                const symbols = stdout.split(/\r?\n/).map(line => line.split('\t'))
                    .filter(parts => parts.length === 4 && parts[0] === 'C4R_SYMBOL')
                    .map(parts => ({ kind: parts[1], name: parts[2], detail: parts[3] }));
                finish(symbols);
            });
        });
    }
    context.subscriptions.push(vscode.languages.registerCompletionItemProvider('c4r', {
        async provideCompletionItems(document, position, token) {
            const symbols = await querySymbols(document, position, token);
            const kinds = { function: vscode.CompletionItemKind.Function, variable: vscode.CompletionItemKind.Variable,
                type: vscode.CompletionItemKind.Class, keyword: vscode.CompletionItemKind.Keyword };
            return symbols.map(symbol => {
                const item = new vscode.CompletionItem(symbol.name, kinds[symbol.kind] || vscode.CompletionItemKind.Text);
                item.detail = symbol.detail;
                return item;
            });
        }
    }));
    context.subscriptions.push(vscode.languages.registerHoverProvider('c4r', {
        async provideHover(document, position, token) {
            const range = document.getWordRangeAtPosition(position);
            if (!range) return;
            const name = document.getText(range);
            const symbols = await querySymbols(document, range.end, token);
            const symbol = symbols.find(item => item.name === name && (item.kind === 'variable' || item.kind === 'function'));
            if (!symbol) return;
            const markdown = new vscode.MarkdownString();
            markdown.appendCodeblock(symbol.kind === 'variable' ? `${symbol.detail} ${symbol.name}` : symbol.detail, 'c4r');
            return new vscode.Hover(markdown, range);
        }
    }));

    function cancel(key) {
        const old = pending.get(key);
        if (old) { clearTimeout(old.timer); if (old.child) old.child.kill(); pending.delete(key); }
    }
    function schedule(document) {
        if (document.languageId !== 'c4r') return;
        const key = document.uri.toString();
        cancel(key);
        const state = {};
        pending.set(key, state);
        const config = vscode.workspace.getConfiguration('c4r', document.uri);
        state.timer = setTimeout(() => {
            const text = document.getText();
            const version = document.version;
            const compiler = compilerPath(document);
            const child = state.child = spawn(compiler, ['--check', '-'], { windowsHide: true });
            let stderr = '';
            let failed = false;
            child.stderr.setEncoding('utf8');
            child.stderr.on('data', chunk => { stderr += chunk; });
            child.stdout.resume();
            child.on('error', error => {
                failed = true;
                if (pending.get(key) !== state) return;
                diagnostics.delete(document.uri);
                output.appendLine(`Unable to start C4R compiler (${compiler}): ${error.message}`);
                vscode.window.setStatusBarMessage('C4R: compiler unavailable; see C4R output', 8000);
            });
            child.stdin.on('error', () => {});
            child.stdin.end(text, 'utf8');
            child.on('close', code => {
                if (pending.get(key) !== state || document.isClosed || document.version !== version) return;
                pending.delete(key);
                if (failed) return;
                if (code === 0) { diagnostics.delete(document.uri); return; }
                const match = /^C4R_DIAGNOSTIC\t(\d+)\t(.*)$/m.exec(stderr);
                if (!match) {
                    diagnostics.delete(document.uri);
                    output.appendLine(`C4R check failed (${code}): ${stderr}`);
                    vscode.window.setStatusBarMessage('C4R: check failed; see C4R output', 8000);
                    return;
                }
                const bytes = Buffer.from(text, 'utf8');
                const byteOffset = Math.min(Number(match[1]), bytes.length);
                const offset = bytes.subarray(0, byteOffset).toString('utf8').length;
                const start = document.positionAt(offset);
                const word = document.getWordRangeAtPosition(start);
                const end = document.positionAt(Math.min(offset + 1, text.length));
                const range = word || new vscode.Range(start, end);
                const diagnostic = new vscode.Diagnostic(range, match[2], vscode.DiagnosticSeverity.Error);
                diagnostic.source = 'C4R';
                diagnostics.set(document.uri, [diagnostic]);
            });
        }, config.get('checkDelay', 500));
    }
    context.subscriptions.push(vscode.workspace.onDidOpenTextDocument(schedule));
    context.subscriptions.push(vscode.workspace.onDidChangeTextDocument(event => schedule(event.document)));
    context.subscriptions.push(vscode.workspace.onDidCloseTextDocument(document => { cancel(document.uri.toString()); diagnostics.delete(document.uri); }));
    context.subscriptions.push(vscode.workspace.onDidChangeConfiguration(event => { if (event.affectsConfiguration('c4r')) vscode.workspace.textDocuments.forEach(schedule); }));
    context.subscriptions.push({ dispose() { for (const key of pending.keys()) cancel(key); } });
    vscode.workspace.textDocuments.forEach(schedule);
}
exports.activate = activate;
