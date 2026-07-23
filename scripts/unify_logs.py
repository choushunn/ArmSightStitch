"""Unify SPDLOG format across the project: add [Module] prefix."""
import re, os

mapping = {
    'core/camera/': 'Camera',
    'core/arm/Modbus': 'Arm',
    'core/arm/SMovement': 'SMovement',
    'core/stitch/': 'Stitch',
    'core/detector/': 'Detect',
    'infra/config/Config': 'Config',
    'infra/config/Path': 'Config',
    'infra/report/ReportLayout': 'Report',
    'infra/report/ReportRenderer': 'Report',
    'infra/report/EdgeCrop': 'Stitch',
    'app/main': 'App',
    'ui/AppController': 'App',
    'ui/Workflow': 'Workflow',
    'ui/MainWindow': 'MainWindow',
    'ui/DetectionSettings': 'DetectionDialog',
    'infra/log/': 'Log',
}

repo = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
count = 0

for root, dirs, files in os.walk(repo):
    # Skip build, third_party, .git
    dirs[:] = [d for d in dirs if d not in ('build', 'build-mingw', 'third_party', '.git', 'scripts')]
    for fn in files:
        if not (fn.endswith('.cpp') or fn.endswith('.h')):
            continue
        fpath = os.path.join(root, fn).replace('\\', '/')

        prefix = None
        for pat, mod in mapping.items():
            if pat in fpath:
                prefix = mod
                break
        if not prefix:
            continue

        with open(fpath, 'r', encoding='utf-8', errors='ignore') as f:
            content = f.read()

        if 'SPDLOG_' not in content:
            continue

        def add_prefix(m):
            full = m.group(0)  # e.g., 'SPDLOG_INFO("'
            level = m.group(1)  # e.g., 'INFO'
            return 'SPDLOG_{0}("[{1}] '.format(level, prefix)

        new_content = re.sub(r'SPDLOG_(INFO|ERROR|WARN|DEBUG)\("', add_prefix, content)
        # Fix double prefix
        new_content = re.sub(r'\[{0}\] \[{0}\]'.format(prefix), '[{0}]'.format(prefix), new_content)

        if new_content != content:
            with open(fpath, 'w', encoding='utf-8', newline='\n') as f:
                f.write(new_content)
            count += 1
            print('  [{0}] {1}'.format(prefix, fpath))

print('Done: {0} files updated'.format(count))
