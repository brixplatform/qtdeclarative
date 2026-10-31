#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Exercise qmlls over standard LSP without an editor or loaded QML components."""
import argparse
import json
import pathlib
import queue
import sys
import subprocess
import tempfile
import threading
import time

class Server:
    def __init__(self, executable, root):
        self.root = pathlib.Path(root)
        self.errors = tempfile.TemporaryFile()
        self.log = self.root / 'server.log'
        self.process = subprocess.Popen([executable, '--no-cmake-calls', '-l', str(self.log)], stdin=subprocess.PIPE,
                                        stdout=subprocess.PIPE, stderr=self.errors)
        self.messages = queue.Queue()
        self.sequence = 0
        self.timings = []
        threading.Thread(target=self.read, daemon=True).start()
        initialized = self.request('initialize', {'processId': None, 'rootUri': self.root.as_uri(),
            'capabilities': {'textDocument': {'completion': {'completionItem': {'snippetSupport': True}}}}})
        self.capabilities = initialized['capabilities']
        self.send('initialized', {})

    def read(self):
        while True:
            header = self.process.stdout.readline()
            if not header:
                self.messages.put({'error': 'server exited'})
                return
            if header.lower().startswith(b'content-length:'):
                length = int(header.split(b':')[1])
                while True:
                    line = self.process.stdout.readline()
                    if not line:
                        return
                    if line == b'\r\n':
                        break
                self.messages.put(json.loads(self.process.stdout.read(length)))

    def send(self, method, params, request_id=None):
        message = dict(jsonrpc='2.0', method=method, params=params)
        if request_id is not None:
            message['id'] = request_id
        self.write(message)

    def write(self, message):
        body = json.dumps(message).encode()
        self.process.stdin.write(b'Content-Length: ' + str(len(body)).encode() + b'\r\n\r\n' + body)
        self.process.stdin.flush()

    def request(self, method, params):
        self.sequence += 1
        started = time.perf_counter()
        self.send(method, params, self.sequence)
        deadline = time.monotonic() + 20
        while time.monotonic() < deadline:
            response = self.messages.get(timeout=max(.1, deadline - time.monotonic()))
            if 'error' in response:
                raise RuntimeError(response)
            if response.get('id') == self.sequence and 'method' not in response:
                self.timings.append({'method': method, 'ms': round((time.perf_counter() - started) * 1000, 3)})
                return response.get('result')
            if 'method' in response and 'id' in response:
                self.write(dict(jsonrpc='2.0', id=response['id'], result=None))
        raise TimeoutError(method)

    def open(self, source):
        offset = source.index('@')
        source = source[:offset] + source[offset + 1:]
        path = self.root / f'Probe{self.sequence}.qml'
        path.write_text(source)
        uri = path.as_uri()
        self.send('textDocument/didOpen', dict(textDocument=dict(uri=uri, languageId='qml', version=1, text=source)))
        return dict(textDocument=dict(uri=uri), position=dict(line=source[:offset].count('\n'),
            character=len(source[:offset].rsplit('\n', 1)[-1].encode('utf-16-le')) // 2))

    def complete(self, source):
        result = self.request('textDocument/completion', self.open(source))
        return result.get('items', []) if isinstance(result, dict) else result or []

    def close(self):
        self.process.terminate()
        self.process.wait(timeout=5)
        self.errors.seek(0)
        errors = self.errors.read().decode(errors='replace')
        self.errors.close()
        return errors + (self.log.read_text() if self.log.exists() else '')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('server')
    parser.add_argument('--binding', default='Layout.alignment: @')
    parser.add_argument('--test', action='store_true')
    parser.add_argument('--report', type=pathlib.Path)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='qmlls-values-') as root:
        (pathlib.Path(root) / 'Fancy.qml').write_text(
            'import QtQuick\nText { enum Mode { Fast, Slow }; property Mode mode; property string headline: "Hello"; property font titleFont }\n')
        server = Server(args.server, root)
        try:
            source = ('import QtQuick\nimport QtQuick.Layouts\nText {\n id: root\n'
                      ' property font bodyFont\n property string heading\n property int preferredSize\n'
                      ' property vector3d light\n property Item peer\n ' + args.binding + '\n}\n')
            if not args.test:
                print(json.dumps(server.complete(source), indent=2))
            else:
                assert {'.', ':', '|', ' '} <= set(server.capabilities['completionProvider']['triggerCharacters'])
                cases = [
                    ('Layout.alignment: @', 'Qt.AlignCenter'),
                    ('Layout.alignment: Qt.@', 'AlignCenter'),
                    ('Layout.alignment: Qt.AlignLeft | @', 'Qt.AlignTop'),
                    ('Layout.alignment:\n    @', 'Qt.AlignLeft'),
                    ('horizontalAlignment: @', 'Text.AlignHCenter'),
                    ('wrapMode: @', 'Text.WordWrap'),
                    ('font.capitalization: @', 'Font.AllUppercase'),
                    ('font.weight: Font.@', 'Bold'),
                    ('font { capitalization: @ }', 'Font.AllUppercase'),
                    ('font { pixelSize: @ }', 'preferredSize'),
                    ('Layout.fillWidth: @', 'true'),
                    ('font: @', 'bodyFont'), ('text: @', 'heading'),
                    ('width: @', 'preferredSize'), ('visible: @', 'true'),
                    ('property vector3d direction: @', 'light'),
                    ('property font displayFont: @', 'bodyFont'),
                    ('property Item target: @', 'peer'),
                    ('Layout.alignment: Qt.A@', 'AlignCenter'),
                    ('anchors.verticalCenter: par@', 'parent')
                ]
                failures = []
                for binding, expected in cases:
                    items = server.complete(source.replace(args.binding, binding))
                    matches = [i for i in items if i['label'] == expected]
                    if not matches:
                        failures.append(binding)
                        print('FAIL', binding, 'missing', expected, 'of', len(items))
                    else:
                        if expected not in ('Bold', 'parent'):
                            assert matches[0].get('sortText', '').startswith('0'), (binding, matches[0])
                        if '|' in binding:
                            assert not any(i['label'] == 'Qt.AlignLeft' for i in items)
                        if expected.startswith(('Qt.Align', 'Align')) and 'alignment:' in binding:
                            assert not any(i['label'].endswith('_Mask') and i.get('sortText', '').startswith('0') for i in items)
                        print('PASS', binding, matches[0].get('sortText'), matches[0].get('insertText'))
                # Import aliases must come from the resolver, not hard-coded Qt names.
                aliases = source.replace('import QtQuick\n', 'import QtQuick as Quick\n').replace(
                    'import QtQuick.Layouts', 'import QtQuick.Layouts as Layouts').replace(
                    'Text {', 'Quick.Text {').replace(args.binding, 'Layouts.Layout.alignment: @')
                checks = [(aliases, 'Qt.AlignCenter')]
                checks.append((aliases.replace('Layouts.Layout.alignment: @', 'horizontalAlignment: @'),
                               'Quick.Text.AlignHCenter'))
                project = source.replace('Text {', 'Fancy {').replace(args.binding, 'text: @')
                checks.append((project, 'headline'))
                checks.append((project.replace('text: @', 'font: @'), 'titleFont'))
                checks.append((project.replace('text: @', 'mode: @'), 'Fancy.Fast'))
                checks.append((project.replace('text: @', 'mode: Fancy.Mode.@'), 'Fast'))
                checks.append(('import QtQuick\nimport QtQuick.Layouts\nText { Layout.alignment: @ }', 'Qt.AlignCenter'))
                checks.append(('import QtQuick\nimport QtQuick.Layouts\n/*😀*/ Text { Layout.alignment: @; text: "Hi" }', 'Qt.AlignCenter'))
                for document, expected in checks:
                    items = server.complete(document)
                    matches = [i for i in items if i['label'] == expected]
                    if not matches or not matches[0].get('sortText', '').startswith('0'):
                        failures.append(expected)
                        print('FAIL additional', expected)
                    else:
                        print('PASS additional', expected)
                negative = ['text: "Layout.alignment: @"', 'width: Math.max(@)',
                                'Layout.alignment: otherObject.@', 'unknownProperty: @',
                                'text: "unterminated: @', 'width: // comment: @']
                for binding in negative:
                    items = server.complete(source.replace(args.binding, binding))
                    if any(i.get('sortText', '').startswith('0') for i in items):
                        failures.append(binding)
                        print('FAIL no expected-type guessing in', binding)
                    else:
                        print('PASS no expected-type guessing in', binding)
                dynamic = [
                    ('import QtQuick\nText { id: key; required property var modelData; text: key.modelData.label; property var action: key.modelData.action; property var result: key.modelData.@ }', {'label', 'action'}),
                    ('import QtQuick\nItem { required property var modelData; property string label: modelData.label; property string other: modelData.action; property var result: modelData.@ }', {'label', 'action'}),
                    ('import QtQuick\nItem { id: root; property var record: ({ label: "Go", count: 3, enabled: true, "not-a-member": 1 }); property var result: root.record.@ }', {'label', 'count', 'enabled'}),
                    ('import QtQuick\nItem { property var record: ({ child: { label: "Go" } }); property var result: record.child.@ }', {'label'}),
                    ('import QtQuick\nRepeater { model: [{label: "Go", count: 3}]; delegate: Text { required property var modelData; text: modelData.@ } }', {'label', 'count'}),
                    ('import QtQuick\nItem { required property var first; required property var second; property var known: first.action; property var unrelated: second.wrong; property var result: first.@ }', {'action'}),
                ]
                for document, expected in dynamic:
                    items = server.complete(document)
                    actual = {i['label'] for i in items}
                    if not expected <= actual or 'wrong' in actual or 'toLocaleString' in actual or 'not-a-member' in actual:
                        failures.append(document)
                        print('FAIL dynamic', expected, actual)
                    else:
                        print('PASS dynamic', expected)
                    if 'enabled' in actual:
                        details = {i['label']: i.get('detail', '') for i in items}
                        assert details['enabled'] == 'has type bool', details
                        assert details['count'] == 'has type double', details
                        assert details['label'] == 'has type QString', details
                # Real compiler signatures must survive plain LSP, including the
                # QML embedded-JS scope. No client-side parameter-name guessing.
                methods = server.complete('import QtQuick\nItem { function sum(x: int, y: int): int { return x + y; } Component.onCompleted: { su@ } }')
                method = next(i for i in methods if i['label'] == 'sum')
                assert method['detail'] == 'sum(x: int, y: int): int', method
                assert method['labelDetails']['detail'] == '(x: int, y: int): int', method
                print('PASS compiler method signature', method['detail'])
                # Only QML object declarations receive braces. Annotations and
                # embedded JS references retain their standard Class metadata.
                objects = [
                    ('import QtQuick\nTe@ {}', 'Text', 'Text'),
                    ('import QtQuick\nItem {\n    Te@\n}', 'Text', 'Text {$0}'),
                    ('import QtQuick as Quick\nQuick.Item { Quick.Te@ }', 'Text', 'Text {$0}'),
                    ('import QtQuick\nItem { property Item header: Te@ }', 'Text', 'Text {$0}'),
                    ('import QtQuick\nRepeater { delegate: Te@ }', 'Text', 'Text {$0}'),
                    ('import QtQuick\nItem { Te@ { text: "Keep" } }', 'Text', 'Text'),
                    ('import QtQuick\nItem { Te@xt { text: "Keep" } }', 'Text', 'Text'),
                    ('import QtQuick\nItem { Te@ /* keep */ { text: "Keep" } }', 'Text', 'Text'),
                ]
                for document, label, insertion in objects:
                    items = server.complete(document)
                    completion = next(i for i in items if i['label'] == label and i['kind'] == 4)
                    assert completion['insertTextFormat'] == 2, completion
                    assert completion['insertText'] == insertion, completion
                    edit = completion['textEdit']
                    assert edit['newText'] == insertion, completion
                    if 'Te@xt' in document:
                        assert edit['range']['end']['character'] - edit['range']['start']['character'] == 4, completion
                    print('PASS object declaration', document, insertion)
                for document, label in [
                    ('import QtQuick\nItem { property Te@ value }', 'Text'),
                    ('import QtQuick\nItem { Component.onCompleted: { Com@ } }', 'Component'),
                    ('import QtQuick\nText { horizontalAlignment: Te@.AlignLeft }', 'Text'),
                ]:
                    reference = next(i for i in server.complete(document) if i['label'] == label)
                    assert reference['kind'] == 7 and '{' not in reference.get('insertText', ''), reference
                    print('PASS type reference', document)
                position = server.open('import QtQml\nTi@mer { interval: 500 }')
                hover = server.request('textDocument/hover', position)
                assert hover['contents']['kind'] == 'markdown', hover
                text = hover['contents']['value']
                assert '# Timer' in text and 'property int interval' in text and 'signal triggered()' in text, text
                locations = server.request('textDocument/definition', position)
                assert locations, locations
                from urllib.parse import urlparse, unquote
                definition = pathlib.Path(unquote(urlparse(locations[0]['uri']).path))
                assert definition.is_file() and definition.name == 'qqmltimer_p.h', locations
                assert definition.as_uri() in text, text
                print('PASS native Timer reference and real declaration', definition)
                if failures:
                    raise AssertionError(f'{len(failures)} failed: {failures}')
                print(f'{len(cases) + len(checks) + len(negative) + len(dynamic) + len(objects) + 5} LSP completion cases passed')
        finally:
            log = server.close()
            if args.report:
                args.report.write_text(json.dumps({'server': str(pathlib.Path(args.server).resolve()),
                    'passed': sys.exc_info()[0] is None, 'requests': server.timings}, indent=2) + '\n')
            if sys.exc_info()[0] is not None:
                print(log, file=sys.stderr)

if __name__ == '__main__':
    main()
