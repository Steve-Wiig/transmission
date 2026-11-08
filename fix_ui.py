with open('qt/PrefsDialog.ui', 'r', encoding='utf-8') as f:
    content = f.read()

# Replace literal & with & in the three new labels
amp = chr(38) + 'amp;'
content = content.replace('Maximum ' + chr(38) + 'open files:', 'Maximum ' + amp + 'open files:')
content = content.replace('Write flush threshold (' + chr(38) + 'MiB):', 'Write flush threshold (' + amp + 'MiB):')
content = content.replace('Verification sleep (' + chr(38) + 'ms per second):', 'Verification sleep (' + amp + 'ms per second):')

with open('qt/PrefsDialog.ui', 'w', encoding='utf-8') as f:
    f.write(content)

print('Done')