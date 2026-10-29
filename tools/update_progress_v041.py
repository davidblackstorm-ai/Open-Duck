"""Update the local panel and append concise operational evidence."""
import argparse
from datetime import datetime
import json
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('task')
parser.add_argument('--state', choices=['pending', 'working', 'done', 'blocked'])
parser.add_argument('--completed', type=int)
parser.add_argument('--detail')
parser.add_argument('--event')
parser.add_argument('--result')
parser.add_argument('--active', action='store_true')
args = parser.parse_args()
path = Path(__file__).resolve().parents[1] / 'dashboard/revision041-status.json'
data = json.loads(path.read_text(encoding='utf-8-sig'))
task = next(row for row in data['tasks'] if row['id'] == args.task)
now = datetime.now().astimezone().isoformat()
if args.state:
    task['status'] = args.state
if args.completed is not None:
    if not 0 <= args.completed <= task['steps']:
        raise ValueError('Completed checks exceed the defined stages')
    task['completed'] = args.completed
if args.detail:
    task['detail'] = args.detail
if args.event:
    task.setdefault('log', []).append({'time': now, 'event': args.event, 'result': args.result or ''})
if args.active:
    data['activeTask'] = task['id']
if task['status'] == 'done' and task['completed'] != task['steps']:
    raise ValueError('All defined checks must pass before marking done')
data['updated'] = now
temporary = path.with_suffix('.json.tmp')
temporary.write_text(json.dumps(data, ensure_ascii=False, indent=2), encoding='utf-8')
temporary.replace(path)
print(json.dumps({'task': task['id'], 'status': task['status'], 'completed': task['completed'], 'steps': task['steps']}))
