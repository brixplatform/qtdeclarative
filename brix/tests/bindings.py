#!/usr/bin/env python3
"""QML binding punctuation is supplied by the provider, not the client."""
import pathlib, tempfile, sys
from completions import Server
cases = [
 ("import QtQuick\nimport QtQuick.Layouts\nGridLayout { col@ }", "columns", "columns: $0"),
 ("import QtQuick\nimport QtQuick.Layouts\nGridLayout { col@umns }", "columns", "columns: $0"),
 ("import QtQuick\nimport QtQuick.Layouts\nGridLayout { col@: 3 }", "columns", "columns"),
 ("import QtQuick\nimport QtQuick.Layouts\nGridLayout { col@ /* keep */ : 3 }", "columns", "columns"),
 ("import QtQuick\nItem { anchors.fi@ }", "fill", "fill: $0"),
 ("import QtQuick\nText { font { pixelSi@ } }", "pixelSize", "pixelSize: $0"),
]
with tempfile.TemporaryDirectory(prefix="qmlls-bindings-") as root:
 server=Server(sys.argv[1],root)
 try:
  for source,label,insertion in cases:
   items=server.complete(source)
   item=next(i for i in items if i['label']==label and i['kind']==10)
   assert item['insertTextFormat']==2,item
   assert item['insertText']==insertion,item
   assert item['textEdit']['newText']==insertion,item
   if 'col@umns' in source:
    edit=item['textEdit']['range']; assert edit['end']['character']-edit['start']['character']==7,item
   print('PASS',source,insertion)
  for source,label in [
   ('import QtQuick\nItem { id: root; width: root.wid@ }','width'),
   ('import QtQuick\nItem { id: root; Component.onCompleted: { root.wid@ } }','width'),
  ]:
   item=next(i for i in server.complete(source) if i['label']==label)
   assert ': ' not in item.get('insertText',''),item
   print('PASS reference',source)
 finally:server.close()
