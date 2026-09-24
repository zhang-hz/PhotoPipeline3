#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""PhotoPipeline — CI 预算校准汇总（M4-W4-T16；docs/v0.3.0-design.md §12.3 job 预算表 / §12.6 20min 口径）

用途: 把**本轮 run 的实测 job 耗时**（`gh api repos/$REPO/actions/runs/$RUN_ID/jobs` 的输出）
      与设计 §12.3 的预算表并排打印成 markdown，直接追加进 $GITHUB_STEP_SUMMARY。
      设计 §12.3 末行「超预算处置：单 job 超 20min → 拆 job 或砍步骤（禁止放宽时限）；整轮超 20min
      → 重排 DAG 关键路径」——本脚本就是把"是否超"变成可读证据的那一步，不是新的门禁
      （门禁 = 各 job 的 timeout-minutes 硬顶 + 拓扑本身）。

用法:
  gh api "repos/$GITHUB_REPOSITORY/actions/runs/$GITHUB_RUN_ID/jobs?per_page=100" > jobs.json
  python3 tools/ci-budget.py jobs.json --budget "lint=1,tidy=12,linux=12" --wall-limit 20 \
      --title "main-full 预算校准" >> "$GITHUB_STEP_SUMMARY"

参数:
  jobs.json          `gh api .../jobs` 的原始输出（含 jobs[].name/status/conclusion/started_at/finished_at）
  --budget K=V,...   预算表（分钟；键 = workflow 里的 job `name:` 前缀或裸 job 名，见下）
  --wall-limit N     整轮 wall 硬顶（分钟，默认 20；设计 §12.6）
  --title TEXT       汇总标题（默认「CI 预算校准」）
  --grace N          宽限（分钟，默认 0.5）：实测 ≤ 预算 + 宽限 即视为「预算内」，避免 12:00.3 这类噪声

匹配口径:
  * 预算键与 job 名的匹配是**前缀**匹配（cap 里 job 名常带后缀，如 `linux（全 ctest+金样）`）；
    留空的键取裸 job id（github.job）。
  * 出现在 run 里但不在预算表里的 job → 预算列打 `—`（**不**判超预算），但仍进 wall 计算。
  * 未跑完（无 finished_at）/被跳过的 job → 实测列 `—`。

退出码: 0 = 汇总完成（无论是否超预算；超预算只是报告事实）；2 = 输入/用法错误。
"""

import argparse
import datetime as dt
import json
import sys

sys.stdout.reconfigure(encoding='utf-8', errors='replace')
sys.stderr.reconfigure(encoding='utf-8', errors='replace')


def parse_ts(text):
    if not text:
        return None
    try:
        return dt.datetime.fromisoformat(text.replace('Z', '+00:00'))
    except ValueError:
        return None


def fmt_min(seconds):
    if seconds is None:
        return '—'
    return '{:.1f}'.format(seconds / 60.0)


def main(argv):
    ap = argparse.ArgumentParser(description='CI 预算校准汇总（M4-W4-T16）')
    ap.add_argument('jobs_json')
    ap.add_argument('--budget', default='')
    ap.add_argument('--wall-limit', type=float, default=20.0)
    ap.add_argument('--title', default='CI 预算校准')
    ap.add_argument('--grace', type=float, default=0.5)
    args = ap.parse_args(argv)

    budget = {}
    for item in args.budget.split(','):
        item = item.strip()
        if not item:
            continue
        key, sep, val = item.partition('=')
        if not sep:
            print('ci-budget: 预算项缺 `=`：{}'.format(item), file=sys.stderr)
            return 2
        budget[key.strip()] = float(val)

    try:
        with open(args.jobs_json, encoding='utf-8') as fp:
            data = json.load(fp)
    except (OSError, ValueError) as exc:
        print('ci-budget: jobs.json 读取失败: {}'.format(exc), file=sys.stderr)
        return 2
    jobs = data.get('jobs') or []

    rows = []
    for job in jobs:
        name = job.get('name') or ''
        start = parse_ts(job.get('started_at'))
        end = parse_ts(job.get('finished_at'))
        dur = (end - start).total_seconds() if (start and end) else None
        key = ''
        for k in budget:
            if name == k or name.startswith(k + '（') or name.startswith(k + ' ') or name.startswith(k + '-'):
                key = k
                break
        rows.append({
            'name': name, 'key': key,
            'budget': budget.get(key) if key else None,
            'dur': dur, 'start': start, 'end': end,
            'conclusion': job.get('conclusion') or job.get('status') or '?',
        })

    starts = [r['start'] for r in rows if r['start']]
    ends = [r['end'] for r in rows if r['end']]
    wall = (max(ends) - min(starts)).total_seconds() if (starts and ends) else None

    print('## {}'.format(args.title))
    print('')
    print('| job | 预算 (min) | 实测 (min) | 结论 | 状态 |')
    print('| --- | --- | --- | --- | --- |')
    over = []
    for r in rows:
        verdict = '—'
        if r['budget'] is not None and r['dur'] is not None:
            if r['dur'] / 60.0 <= r['budget'] + args.grace:
                verdict = '✅ 预算内'
            else:
                verdict = '⚠ 超预算 +{:.1f}'.format(r['dur'] / 60.0 - r['budget'])
                over.append(r['name'])
        elif r['budget'] is None:
            verdict = '—（表外 job）'
        print('| {} | {} | {} | {} | {} |'.format(
            r['name'] or '?',
            '—' if r['budget'] is None else '{:.0f}'.format(r['budget']),
            fmt_min(r['dur']), verdict, r['conclusion']))
    print('')
    if wall is not None:
        state = '✅ ≤ {:.0f}min 硬顶'.format(args.wall_limit) if wall / 60.0 <= args.wall_limit \
            else '⚠ 超 {:.0f}min 硬顶 +{:.1f}min（设计 §12.3：重排 DAG 关键路径 / 拆 job / 砍步骤，禁放宽时限）' \
                 .format(args.wall_limit, wall / 60.0 - args.wall_limit)
        print('**整轮 wall-clock** = {} min（首 job {} → 末 job {}）｜{}'.format(
            fmt_min(wall), min(starts).strftime('%H:%M:%S'), max(ends).strftime('%H:%M:%S'), state))
        slow = max([r for r in rows if r['dur'] is not None], key=lambda r: r['dur'], default=None)
        if slow:
            print('')
            print('**关键路径嫌疑** = `{}`（{} min）——若整轮超顶，优先拆它（设计 §12.3）。'.format(
                slow['name'], fmt_min(slow['dur'])))
    else:
        print('**整轮 wall-clock** = 不可得（jobs.json 缺少起止时间；本 run 可能仍在进行或权限不足）')
    if over:
        print('')
        print('**超预算 job**（{} 个）: {}'.format(len(over), ', '.join('`{}`'.format(n) for n in over)))
    print('')
    print('<sub>预算口径 = docs/v0.3.0-design.md §12.3；整轮硬顶 20min 见 §12.6（唯一豁免 = warm-cache 播种）。'
          '本表由 tools/ci-budget.py 从 GitHub jobs API 生成。</sub>')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
