"""CLI integration checks; Python standard library only, no assert dependence."""
import csv
import math
import pathlib
import subprocess
import sys
import tempfile

executable = str(pathlib.Path(sys.argv[1]).resolve())
checks = 0


def check(condition, message):
    global checks
    checks += 1
    if not condition:
        raise RuntimeError(message)


def run(arguments):
    return subprocess.run([executable, *arguments], capture_output=True, text=True, timeout=30)


invalid_arguments = [
    ['--unknown'], ['--runs'], ['--runs', '--seed', '42'],
    ['--runs', '2', '--runs', '3'], ['--help', '--help'],
    ['--runs', '-1'], ['--runs', '0'], ['--runs', '1'], ['--runs', '10000001'],
    ['--runs', '2.5'], ['--runs', '2junk'], ['--runs', ''], ['--runs', ' 2'],
    ['--seed', '18446744073709551616'], ['--seed', '-1'],
    ['--qmax', '0'], ['--qmax', '101'], ['--bid', '0'], ['--ask', '21'],
    ['--model', 'invalid'], ['--model', 'constant', '--model', 'linear'],
    ['--event-limit', '0'], ['--event-limit', '100000001'],
    ['--max-iterations', '0'], ['--max-iterations', '1000001'],
    ['--prob-tol', 'nan'], ['--time-tol', 'inf'], ['--time-tol', '-1'],
    ['--prob-tol', '0'], ['--prob-tol', '1e-10junk'], ['--time-tol', ' 1e-10'],
    ['--csv', ''], ['--help', '--bogus'],
]
for arguments in invalid_arguments:
    result = run(arguments)
    check(result.returncode != 0 and 'Error:' in result.stderr, f'not rejected clearly: {arguments}')

result = run(['--help'])
check(result.returncode == 0 and '--event-limit' in result.stdout, 'help')
result = run(['--max-iterations', '1', '--runs', '2'])
check(result.returncode != 0 and 'did not converge' in result.stderr and 'converged=no' in result.stdout,
      'nonconvergence must fail')
result = run(['--event-limit', '1', '--runs', '2'])
check(result.returncode != 0 and 'incomplete experiment' in result.stderr and 'completed=0' in result.stdout
      and 'Wilson' not in result.stdout, 'incomplete experiment must suppress estimates')
with tempfile.TemporaryDirectory() as directory:
    path = pathlib.Path(directory) / 'grid.csv'
    result = run(['--qmax', '2', '--bid', '1', '--ask', '2', '--runs', '10', '--csv', str(path)])
    check(result.returncode == 0, result.stderr)
    with path.open(newline='') as stream:
        reader = csv.DictReader(stream)
        check(reader.fieldnames == ['bid_qty', 'ask_qty', 'p_ask_first', 'expected_seconds'], 'CSV header')
        rows = list(reader)
    check(len(rows) == 4, 'CSV row count')
    check({(int(r['bid_qty']), int(r['ask_qty'])) for r in rows} == {(1, 1), (1, 2), (2, 1), (2, 2)}, 'CSV states')
    for row in rows:
        p, w = float(row['p_ask_first']), float(row['expected_seconds'])
        check(math.isfinite(p) and 0 <= p <= 1 and math.isfinite(w) and w >= 0, 'CSV numeric values')
    result = run(['--runs', '2', '--csv', str(pathlib.Path(directory) / 'missing' / 'grid.csv')])
    check(result.returncode != 0 and 'Error:' in result.stderr, 'CSV open failure')
if pathlib.Path('/dev/full').exists():
    result = run(['--runs', '2', '--csv', '/dev/full'])
    check(result.returncode != 0, 'CSV write/close failure')
    with open('/dev/full', 'w') as stream:
        result = subprocess.run([executable, '--help'], stdout=stream, stderr=subprocess.PIPE, text=True)
    check(result.returncode != 0, 'stdout failure')
print(f'PASS: CLI: {checks} checks')
