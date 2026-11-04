#!/usr/bin/env python3
"""Generate deterministic tone-free candidates from pinned Unicode Unihan data."""
import argparse
from collections import defaultdict
from pathlib import Path
import unicodedata
import zipfile


def generate(source, target):
    readings = defaultdict(set)
    with zipfile.ZipFile(source) as archive:
        for line in archive.read('Unihan_Readings.txt').decode().splitlines():
            if line.startswith('#') or not line.strip(): continue
            code, field, value = line.split('\t')
            if field not in ('kMandarin', 'kHanyuPinyin'): continue
            if field == 'kHanyuPinyin':
                value = ' '.join(part.split(':', 1)[1].replace(',', ' ') for part in value.split())
            for reading in value.lower().split():
                reading = reading.replace('ü', 'v')
                decomposed = unicodedata.normalize('NFD', reading)
                # Decompose accented ü as u + diaeresis; retain the v convention.
                decomposed = decomposed.replace('u\u0308', 'v')
                key = ''.join(c for c in decomposed if not unicodedata.combining(c))
                if key.isascii() and key.isalpha() and len(key) <= 12:
                    readings[key].add(chr(int(code[2:], 16)))
    # LeonMobile's small common-word supplement is original project data.
    common = {
        'ni':'你 妮 尼 泥', 'hao':'好 号 浩', 'shi':'是 时 事 市 十 使 试',
        'de':'的 得 地', 'wo':'我', 'men':'们 门', 'zhong':'中 钟 种 重',
        'guo':'国 过 果', 'wen':'文 问 温', 'shu':'输 书 数 树', 'ru':'入 如',
        'nihao':'你好', 'zhongguo':'中国', 'zhongwen':'中文', 'shuru':'输入',
        'shurufa':'输入法', 'shijie':'世界', 'xiexie':'谢谢', 'zaijian':'再见',
        'xitong':'系统', 'yingyong':'应用', 'shezhi':'设置', 'wenjian':'文件',
        'wangluo':'网络', 'shijian':'时间', 'huanying':'欢迎', 'beijing':'北京',
        'shanghai':'上海', 'shouji':'手机', 'tongzhi':'通知', 'anquan':'安全',
        'ceshi':'测试', 'kaishi':'开始', 'baocun':'保存',
        'quxiao':'取消', 'queding':'确定', 'dakai':'打开', 'guanbi':'关闭',
    }
    lines = []
    for key in sorted(set(readings) | set(common)):
        if not key.isalpha(): continue
        preferred = common.get(key, '').split()
        # Basic CJK first, then extension characters, all in stable codepoint order.
        rest = sorted(readings[key] - set(preferred), key=lambda c: (not 0x4e00<=ord(c)<=0x9fff, ord(c)))
        lines += [key+'\t'+text+'\n' for text in preferred+rest]
    Path(target).parent.mkdir(parents=True,exist_ok=True)
    Path(target).write_text(''.join(lines),encoding='utf-8')
    print(f'Pinyin: {len(lines)} candidates, {len(readings)} syllables')

if __name__ == '__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('unihan');p.add_argument('output');a=p.parse_args();generate(a.unihan,a.output)
